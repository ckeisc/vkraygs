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
  void SetCullMasks(const std::string& masks_path, int view_index);

  // Logit-space opacity bias (added to SPZ alpha logits before sigmoid).
  // Positive values boost opacity to reduce background bleed-through.
  // Debug: ignore SH view-dependent terms, use DC only.
  void SetDcOnly(bool dc_only);

  void Run();

  void Close();

 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace vkgs

#endif  // VKGS_ENGINE_ENGINE_H
