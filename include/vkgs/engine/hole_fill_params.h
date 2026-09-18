#ifndef VKGS_ENGINE_HOLE_FILL_PARAMS_H
#define VKGS_ENGINE_HOLE_FILL_PARAMS_H

namespace vkgs {

// Post-load processing of a decoded SPZ cloud, applied in memory after SPZ
// decode (and optional visibility-cluster culling), before the GPU vertex
// buffer is built. Goal: reduce visible holes so renders look closer to the
// Hyperscape reference (see docs/hole-filling.md).
struct HoleFillParams {
  // Idea 1: enlarge each Gaussian's 3D volume. Multiplies sigma by this
  // factor (applied as += log(factor) on the SPZ log-scales). 1.0 = off.
  float inflate = 1.0f;

  // Idea 2: dynamically interpolate new Gaussians between captured ones.
  // A new splat is inserted at the midpoint of a pair whose gap exceeds
  // densify_gap * (r_i + r_j), where r is the 1-sigma radius.
  bool densify = false;
  float densify_gap = 3.0f;

  // Idea 3: ramp opacities up. o' = o^(1/gamma); gamma > 1 pushes mid
  // opacities toward 1 (real-world surfaces are rarely semi-transparent
  // in small patches). 1.0 = off.
  float opacity_gamma = 1.0f;

  bool enabled() const { return inflate != 1.0f || densify || opacity_gamma != 1.0f; }
};

}  // namespace vkgs

#endif  // VKGS_ENGINE_HOLE_FILL_PARAMS_H
