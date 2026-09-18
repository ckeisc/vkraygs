#include "vkgs/engine/splat_hole_fill.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "load-spz.h"

namespace vkgs {
namespace {

inline float Sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

inline float Logit(float o) {
  o = std::min(std::max(o, 1e-6f), 1.0f - 1e-6f);
  return std::log(o / (1.0f - o));
}

// 1-sigma radius of splat i from SPZ log-scales.
inline float SplatRadius(const spz::GaussianCloud& cloud, size_t i) {
  const float sx = cloud.scales[3 * i + 0];
  const float sy = cloud.scales[3 * i + 1];
  const float sz = cloud.scales[3 * i + 2];
  return std::exp(std::max(sx, std::max(sy, sz)));
}

struct CellKey {
  int64_t x, y, z;
  bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct CellKeyHash {
  size_t operator()(const CellKey& k) const {
    size_t h = std::hash<int64_t>{}(k.x);
    h ^= std::hash<int64_t>{}(k.y + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
    h ^= std::hash<int64_t>{}(k.z + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
    return h;
  }
};

// Idea 2: insert midpoint splats where the gap between nearest neighbors
// is large relative to their sizes.
size_t Densify(spz::GaussianCloud& cloud, float gap_factor) {
  const size_t n = static_cast<size_t>(cloud.numPoints);
  if (n == 0) return 0;

  std::vector<float> radii(n);
  for (size_t i = 0; i < n; ++i) radii[i] = SplatRadius(cloud, i);

  // Cell size from the median radius: large enough that a gap worth
  // filling is found within the 27 neighboring cells.
  std::vector<float> sorted = radii;
  std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
  const float median_r = std::max(sorted[n / 2], 1e-6f);
  const float cell = std::max(gap_factor * 4.0f * median_r, 1e-6f);

  std::unordered_map<CellKey, std::vector<size_t>, CellKeyHash> grid;
  grid.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    const float* p = &cloud.positions[3 * i];
    CellKey k{static_cast<int64_t>(std::floor(p[0] / cell)),
              static_cast<int64_t>(std::floor(p[1] / cell)),
              static_cast<int64_t>(std::floor(p[2] / cell))};
    grid[k].push_back(i);
  }

  const size_t sh_per = cloud.sh.empty() ? 0 : cloud.sh.size() / n;

  // Accumulate new splats separately; seeds are original splats only.
  std::vector<float> npos, nscale, nrot, nalpha, ncolor, nsh;
  npos.reserve(n * 3);
  nscale.reserve(n * 3);
  nrot.reserve(n * 4);
  nalpha.reserve(n);
  ncolor.reserve(n * 3);
  if (sh_per) nsh.reserve(n * sh_per);

  size_t added = 0;
  for (size_t i = 0; i < n; ++i) {
    if (added >= n) break;  // cap: at most double the splat count
    const float* pi = &cloud.positions[3 * i];
    const int64_t cx = static_cast<int64_t>(std::floor(pi[0] / cell));
    const int64_t cy = static_cast<int64_t>(std::floor(pi[1] / cell));
    const int64_t cz = static_cast<int64_t>(std::floor(pi[2] / cell));

    // Nearest neighbor over the 27 neighboring cells.
    size_t best_j = n;
    float best_d2 = 1e30f;
    for (int64_t dx = -1; dx <= 1; ++dx)
      for (int64_t dy = -1; dy <= 1; ++dy)
        for (int64_t dz = -1; dz <= 1; ++dz) {
          auto it = grid.find(CellKey{cx + dx, cy + dy, cz + dz});
          if (it == grid.end()) continue;
          for (size_t j : it->second) {
            if (j == i) continue;
            const float* pj = &cloud.positions[3 * j];
            const float ddx = pi[0] - pj[0], ddy = pi[1] - pj[1], ddz = pi[2] - pj[2];
            const float d2 = ddx * ddx + ddy * ddy + ddz * ddz;
            if (d2 < best_d2) {
              best_d2 = d2;
              best_j = j;
            }
          }
        }
    if (best_j >= n) continue;

    const float gap = std::sqrt(best_d2);
    if (gap <= gap_factor * (radii[i] + radii[best_j])) continue;

    // Interpolate a new Gaussian at the midpoint.
    const float* pj = &cloud.positions[3 * best_j];
    npos.push_back(0.5f * (pi[0] + pj[0]));
    npos.push_back(0.5f * (pi[1] + pj[1]));
    npos.push_back(0.5f * (pi[2] + pj[2]));
    for (int k = 0; k < 3; ++k)
      nscale.push_back(0.5f * (cloud.scales[3 * i + k] + cloud.scales[3 * best_j + k]));
    // Normalized lerp of quaternions (flip for antipodal).
    const float* qi = &cloud.rotations[4 * i];
    const float* qj = &cloud.rotations[4 * best_j];
    float dot = qi[0] * qj[0] + qi[1] * qj[1] + qi[2] * qj[2] + qi[3] * qj[3];
    const float s = dot < 0 ? -0.5f : 0.5f;
    float qx = 0.5f * qi[0] + s * qj[0], qy = 0.5f * qi[1] + s * qj[1],
          qz = 0.5f * qi[2] + s * qj[2], qw = 0.5f * qi[3] + s * qj[3];
    const float qlen = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw) + 1e-12f;
    nrot.push_back(qx / qlen);
    nrot.push_back(qy / qlen);
    nrot.push_back(qz / qlen);
    nrot.push_back(qw / qlen);
    nalpha.push_back(0.5f * (cloud.alphas[i] + cloud.alphas[best_j]));
    for (int k = 0; k < 3; ++k)
      ncolor.push_back(0.5f * (cloud.colors[3 * i + k] + cloud.colors[3 * best_j + k]));
    for (size_t k = 0; k < sh_per; ++k)
      nsh.push_back(0.5f * (cloud.sh[i * sh_per + k] + cloud.sh[best_j * sh_per + k]));
    ++added;
  }

  if (added > 0) {
    cloud.positions.insert(cloud.positions.end(), npos.begin(), npos.end());
    cloud.scales.insert(cloud.scales.end(), nscale.begin(), nscale.end());
    cloud.rotations.insert(cloud.rotations.end(), nrot.begin(), nrot.end());
    cloud.alphas.insert(cloud.alphas.end(), nalpha.begin(), nalpha.end());
    cloud.colors.insert(cloud.colors.end(), ncolor.begin(), ncolor.end());
    if (sh_per) cloud.sh.insert(cloud.sh.end(), nsh.begin(), nsh.end());
    cloud.numPoints = static_cast<int32_t>(n + added);
  }
  return added;
}

}  // namespace

size_t ApplyHoleFill(spz::GaussianCloud& cloud, const HoleFillParams& params) {
  if (!params.enabled()) return 0;
  const size_t n0 = static_cast<size_t>(cloud.numPoints);

  // Idea 1: enlarge 3D volumes (log-space: sigma *= inflate).
  if (params.inflate != 1.0f) {
    const float log_inflate = std::log(params.inflate);
    for (size_t k = 0; k < cloud.scales.size(); ++k) cloud.scales[k] += log_inflate;
    fprintf(stderr, "[hole-fill] inflate: sigma x %.3f\n", params.inflate);
  }

  // Idea 3: ramp opacities up (logit -> opacity -> gamma -> logit).
  if (params.opacity_gamma != 1.0f && params.opacity_gamma > 0.0f) {
    const float inv_gamma = 1.0f / params.opacity_gamma;
    for (size_t k = 0; k < cloud.alphas.size(); ++k) {
      const float o = Sigmoid(cloud.alphas[k]);
      cloud.alphas[k] = Logit(std::pow(o, inv_gamma));
    }
    fprintf(stderr, "[hole-fill] opacity ramp: gamma %.3f\n", params.opacity_gamma);
  }

  // Idea 2: interpolate new Gaussians into gaps (after inflate so radii match).
  size_t added = 0;
  if (params.densify) {
    added = Densify(cloud, params.densify_gap);
    fprintf(stderr, "[hole-fill] densify: %zu -> %zu splats (gap %.2f)\n", n0, n0 + added,
            params.densify_gap);
  }
  return added;
}

}  // namespace vkgs
