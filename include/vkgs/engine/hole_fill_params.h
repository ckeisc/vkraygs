#ifndef VKGS_ENGINE_HOLE_FILL_PARAMS_H
#define VKGS_ENGINE_HOLE_FILL_PARAMS_H

namespace vkgs {

// Post-load processing of a decoded SPZ cloud, applied in memory after SPZ
// decode (and optional visibility-cluster culling), before the GPU vertex
// buffer is built. Goal: reduce visible holes so renders look closer to the
// Hyperscape reference (see docs/hole-filling.md).
struct HoleFillParams {
  // Enlarge each Gaussian's 3D volume. Multiplies sigma by this factor
  // (applied as += log(factor) on the SPZ log-scales). 1.0 = off.
  float inflate = 1.0f;

  bool enabled() const { return inflate != 1.0f; }
};

}  // namespace vkgs

#endif  // VKGS_ENGINE_HOLE_FILL_PARAMS_H
