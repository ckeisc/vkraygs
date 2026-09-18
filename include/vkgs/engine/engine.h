#ifndef VKGS_ENGINE_ENGINE_H
#define VKGS_ENGINE_ENGINE_H

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace vkgs {

class Splats;

class Engine {
 public:
  Engine();
  ~Engine();

  void LoadSplats(const std::string& ply_filepath);
  void LoadSplatsAsync(const std::string& ply_filepath);

  // World up-axis for the interactive orbit camera (default Y-up).
  void SetZUp(bool z_up);

  // Batch (headless) rendering: render each pose to a PNG and return.
  // Views are column-major 4x4 view matrices; eyes are camera positions.
  void SetKernelRayGS(bool raygs);
  void SetBatchViews(const std::vector<std::array<float, 16>>& views,
                     const std::vector<std::array<float, 3>>& eyes);
  void SetBatchOutput(const std::string& dir, const std::string& prefix);

  // Visibility-cluster occlusion culling (Hyperscape od_cluster_masks.bin).
  // masks_path: per-splat uint64 visibility bitmasks; view_index: 0-63 viewpoint.
  // Static CPU mode: filters splats at load time (for batch rendering).
  void SetCullMasks(const std::string& masks_path, int view_index);

  // Dynamic GPU mode: also provide the cluster centroids JSON
  // (od_cluster_centroids, 64 viewpoint positions). The viewer then culls in the
  // projection shader using the union of the 3 nearest viewpoints per frame.
  // Requires SetCullMasks to have been called with the masks path.
  void SetCullCentroids(const std::string& centroids_path);

  // Debug: ignore SH view-dependent terms, use DC only.
  void SetDcOnly(bool dc_only);

  // Hole-filling: GPU-side 3D sigma multiplier applied in parse_ply.
  // Real-time adjustable via PageUp/PageDown. Initial value from --inflate flag.
  void SetInflate(float factor);

  // Experimental alpha correction (see docs/hyperscape-opacity-trace.md).
  // Applies alpha_out = clamp(alpha * scale + bias, 0, 1) GPU-side.
  void SetAlphaCorrection(float scale, float bias);

  void Run();

  void Close();

 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace vkgs

#endif  // VKGS_ENGINE_ENGINE_H
