#ifndef VKGS_ENGINE_SPLAT_HOLE_FILL_H
#define VKGS_ENGINE_SPLAT_HOLE_FILL_H

#include <cstddef>

#include "vkgs/engine/hole_fill_params.h"

namespace spz {
struct GaussianCloud;
}

namespace vkgs {

// Applies the enabled HoleFillParams steps to the cloud in place.
// Returns the number of splats added by densification (0 if disabled).
size_t ApplyHoleFill(spz::GaussianCloud& cloud, const HoleFillParams& params);

}  // namespace vkgs

#endif  // VKGS_ENGINE_SPLAT_HOLE_FILL_H
