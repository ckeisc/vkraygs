#include "vkgs/engine/splat_hole_fill.h"

#include <cmath>
#include <cstdio>

#include "load-spz.h"

namespace vkgs {

size_t ApplyHoleFill(spz::GaussianCloud& cloud, const HoleFillParams& params) {
  if (!params.enabled()) return 0;

  // Enlarge 3D volumes (log-space: sigma *= inflate).
  if (params.inflate != 1.0f) {
    const float log_inflate = std::log(params.inflate);
    for (size_t k = 0; k < cloud.scales.size(); ++k) cloud.scales[k] += log_inflate;
    fprintf(stderr, "[hole-fill] inflate: sigma x %.3f\n", params.inflate);
  }
  return 0;
}

}  // namespace vkgs
