#include "vkgs/engine/splat_load_thread.h"

#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <cstring>
#include <vector>
#include <functional>

#include "load-spz.h"

namespace {

constexpr int kSpzStrideFloats = 59;

// Loads an .spz file and converts it to the raw vertex layout that
// parse_ply.comp consumes: 59 floats/vertex (pos, log-scale, quat,
// sh, opacity-logit) plus a 60-entry offsets table.
//
// SPZ unpacking already yields log-scales, xyzw quaternions, logit
// opacity and SH DC coefficients, matching the shader's expectations
// (exp/sigmoid activations happen on the GPU), so the fields map
// 1:1 onto the PLY loader's vertex record:
//
//   slots  0-2  = x,y,z            (positions)
//   slots  3-5  = scale_x..z       (log scale)
//   slots  6-9  = rot_1,rot_2,rot_3,rot_0 = qx,qy,qz,qw
//   slots 10-25 = f_dc_0,f_rest_0..14
//   slots 26-41 = f_dc_1,f_rest_15..29
//   slots 42-57 = f_dc_2,f_rest_30..44
//   slot  58    = opacity          (logit)
//
bool LoadSpzAsPly(const std::string& path, std::vector<uint32_t>& ply_offsets, std::vector<char>& buffer_,
                  size_t* point_count, int* stride_bytes, const std::function<bool()>& cancelled,
                  const std::function<void(uint32_t)>& progress,
                  const std::string& cull_masks_path = "", int cull_view_index = -1,
                  float opacity_bias = 0.0f) {
  spz::GaussianCloud cloud = spz::loadSpz(path, spz::UnpackOptions());

  // Optional visibility-cluster culling (Hyperscape od_cluster_masks).
  if (!cull_masks_path.empty() && cull_view_index >= 0) {
    const int view_idx = cull_view_index;
    const size_t n0 = static_cast<size_t>(cloud.numPoints);
    std::vector<uint64_t> masks(n0);
    std::ifstream mf(cull_masks_path, std::ios::binary);
    mf.read(reinterpret_cast<char*>(masks.data()), n0 * sizeof(uint64_t));
    if (mf) {
      const uint64_t bit = 1ULL << view_idx;
      const size_t sh_per = cloud.sh.empty() ? 0 : cloud.sh.size() / n0;
      spz::GaussianCloud culled;
      culled.shDegree = cloud.shDegree;
      culled.antialiased = cloud.antialiased;
      size_t kept = 0;
      for (size_t i = 0; i < n0; ++i) {
        if (!(masks[i] & bit)) continue;
        culled.positions.insert(culled.positions.end(), cloud.positions.begin() + i * 3, cloud.positions.begin() + i * 3 + 3);
        culled.scales.insert(culled.scales.end(), cloud.scales.begin() + i * 3, cloud.scales.begin() + i * 3 + 3);
        culled.rotations.insert(culled.rotations.end(), cloud.rotations.begin() + i * 4, cloud.rotations.begin() + i * 4 + 4);
        culled.alphas.push_back(cloud.alphas[i]);
        culled.colors.insert(culled.colors.end(), cloud.colors.begin() + i * 3, cloud.colors.begin() + i * 3 + 3);
        if (sh_per) culled.sh.insert(culled.sh.end(), cloud.sh.begin() + i * sh_per, cloud.sh.begin() + i * sh_per + sh_per);
        ++kept;
      }
      culled.numPoints = static_cast<int32_t>(kept);
      fprintf(stderr, "[spz] cluster culling: view %d, %zu -> %zu splats\n", view_idx, n0, kept);
      cloud = std::move(culled);
    } else {
      fprintf(stderr, "[spz] warning: could not read cull masks %s\n", cull_masks_path.c_str());
    }
  }

  if (cloud.numPoints <= 0) {
    fprintf(stderr, "[spz] failed to load %s\n", path.c_str());
    return false;
  }
  const size_t n = static_cast<size_t>(cloud.numPoints);
  const int sh_rest = cloud.sh.empty() ? 0 : static_cast<int>(cloud.sh.size() / (n * 3));  // 0,3,8,15,24
  if (sh_rest > 15) {
    fprintf(stderr, "[spz] SH degree > 3 (%d rest coeffs); truncating to 15\n", sh_rest);
  }
  const int sh_copy = std::min(sh_rest, 15);

  for (int k = 0; k < kSpzStrideFloats; ++k) ply_offsets[k] = static_cast<uint32_t>(k);
  ply_offsets[59] = static_cast<uint32_t>(kSpzStrideFloats);

  buffer_.resize(n * kSpzStrideFloats * sizeof(float));
  float* dst = reinterpret_cast<float*>(buffer_.data());

  constexpr uint32_t chunk = 65536;
  for (uint32_t start = 0; start < n; start += chunk) {
    if (cancelled()) return false;
    const uint32_t count = std::min<uint32_t>(chunk, static_cast<uint32_t>(n - start));
    for (uint32_t j = 0; j < count; ++j) {
      const size_t i = start + j;
      float* v = dst + i * kSpzStrideFloats;
      v[0] = cloud.positions[3 * i + 0];
      v[1] = cloud.positions[3 * i + 1];
      v[2] = cloud.positions[3 * i + 2];
      v[3] = cloud.scales[3 * i + 0];
      v[4] = cloud.scales[3 * i + 1];
      v[5] = cloud.scales[3 * i + 2];
      v[6] = cloud.rotations[4 * i + 0];  // qx -> rot_1
      v[7] = cloud.rotations[4 * i + 1];  // qy -> rot_2
      v[8] = cloud.rotations[4 * i + 2];  // qz -> rot_3
      v[9] = cloud.rotations[4 * i + 3];  // qw -> rot_0
      v[10] = cloud.colors[3 * i + 0];    // f_dc_0
      v[26] = cloud.colors[3 * i + 1];    // f_dc_1
      v[42] = cloud.colors[3 * i + 2];    // f_dc_2
      for (int c = 0; c < 15; ++c) {
        float r = 0.f, g = 0.f, b = 0.f;
        if (c < sh_copy) {
          const size_t s = (i * static_cast<size_t>(sh_rest) + static_cast<size_t>(c)) * 3;
          r = cloud.sh[s + 0];
          g = cloud.sh[s + 1];
          b = cloud.sh[s + 2];
        }
        v[11 + c] = r;
        v[27 + c] = g;
        v[43 + c] = b;
      }
      v[58] = cloud.alphas[i] + opacity_bias;  // logit; sigmoid applied on GPU
    }
    progress(start + count);
  }

  *point_count = n;
  *stride_bytes = kSpzStrideFloats * static_cast<int>(sizeof(float));
  
  return true;
}

}  // namespace

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

namespace vkgs {

class SplatLoadThread::Impl {
 public:
  Impl() = delete;

  Impl(vk::Context context) : context_(context) {
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(context_.device(), &fence_info, NULL, &fence_);

    thread_ = std::thread([this] {
      // thread-local command buffer
      VkCommandPoolCreateInfo command_pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
      command_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
      command_pool_info.queueFamilyIndex = context_.transfer_queue_family_index();
      VkCommandPool command_pool = VK_NULL_HANDLE;
      vkCreateCommandPool(context_.device(), &command_pool_info, NULL, &command_pool);

      VkCommandBufferAllocateInfo command_buffer_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
      command_buffer_info.commandPool = command_pool;
      command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      command_buffer_info.commandBufferCount = 1;
      VkCommandBuffer cb = VK_NULL_HANDLE;
      vkAllocateCommandBuffers(context_.device(), &command_buffer_info, &cb);

      while (true) {
        std::string ply_filepath;
        {
          std::unique_lock<std::mutex> guard{mutex_};
          cv_.wait(guard, [this] { return terminate_ || !ply_filepath_.empty(); });
          if (terminate_) break;
          ply_filepath = std::move(ply_filepath_);
        }

        cancel_ = false;

        const bool is_spz = ply_filepath.size() >= 4 &&
                            ply_filepath.compare(ply_filepath.size() - 4, 4, ".spz") == 0;

        std::vector<uint32_t> ply_offsets(60);
        int offset = 0;  // vertex stride in bytes
        size_t point_count = 0;
        std::unordered_map<std::string, int> offsets;

        if (is_spz) {
          // native .spz: decode and synthesize the PLY vertex layout
          if (!LoadSpzAsPly(
                  ply_filepath, ply_offsets, buffer_, &point_count, &offset,
                  [this] { return terminate_ || cancel_; },
                  [this](uint32_t loaded) {
                    std::unique_lock<std::mutex> guard{mutex_};
                    loaded_point_count_ = loaded;
                  },
                  cull_masks_path_, cull_view_index_, opacity_bias_)) {
            continue;
          }
          {
            std::unique_lock<std::mutex> guard{mutex_};
            total_point_count_ = point_count;
          }
        } else {
          std::ifstream in(ply_filepath, std::ios::binary);

          // parse header
          std::string line;
          while (std::getline(in, line)) {
            if (line == "end_header") break;

            std::istringstream iss(line);
            std::string word;
            iss >> word;
            if (word == "property") {
              int size = 0;
              std::string type, property;
              iss >> type >> property;
              if (type == "float") {
                size = 4;
              }
              offsets[property] = offset;
              offset += size;
            } else if (word == "element") {
              std::string type;
              size_t count;
              iss >> type >> count;
              if (type == "vertex") {
                point_count = count;
              }
            }
          }

          // update total point count
          {
            std::unique_lock<std::mutex> guard{mutex_};
            total_point_count_ = point_count;
          }

          // ply offsets
          ply_offsets[0] = offsets["x"] / 4;
          ply_offsets[1] = offsets["y"] / 4;
          ply_offsets[2] = offsets["z"] / 4;
          ply_offsets[3] = offsets["scale_0"] / 4;
          ply_offsets[4] = offsets["scale_1"] / 4;
          ply_offsets[5] = offsets["scale_2"] / 4;
          ply_offsets[6] = offsets["rot_1"] / 4;  // qx
          ply_offsets[7] = offsets["rot_2"] / 4;  // qy
          ply_offsets[8] = offsets["rot_3"] / 4;  // qz
          ply_offsets[9] = offsets["rot_0"] / 4;  // qw
          ply_offsets[10 + 0] = offsets["f_dc_0"] / 4;
          ply_offsets[10 + 16] = offsets["f_dc_1"] / 4;
          ply_offsets[10 + 32] = offsets["f_dc_2"] / 4;
          for (int i = 0; i < 15; ++i) {
            ply_offsets[10 + 1 + i] = offsets["f_rest_" + std::to_string(i)] / 4;
            ply_offsets[10 + 17 + i] = offsets["f_rest_" + std::to_string(15 + i)] / 4;
            ply_offsets[10 + 33 + i] = offsets["f_rest_" + std::to_string(30 + i)] / 4;
          }
          ply_offsets[58] = offsets["opacity"] / 4;
          ply_offsets[59] = offset / 4;

          // read all binary data
          buffer_.resize(offset * point_count);

          constexpr uint32_t chunk_size = 65536;
          for (uint32_t start = 0; start < point_count; start += chunk_size) {
            if (terminate_ || cancel_) break;

            auto chunk_point_count = std::min<uint32_t>(chunk_size, point_count - start);
            in.read(buffer_.data() + offset * start, offset * chunk_point_count);

            {
              std::unique_lock<std::mutex> guard{mutex_};
              loaded_point_count_ = start + chunk_point_count;
            }
          }

          if (terminate_) break;
        }

        // assuming all properties are float
        VkDeviceSize size = 60 * sizeof(uint32_t) + static_cast<VkDeviceSize>(offset) * point_count;
        if (staging_size_ < size) {
          if (staging_) vmaDestroyBuffer(context_.allocator(), staging_, allocation_);

          VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
          buffer_info.size = size;
          buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
          VmaAllocationCreateInfo allocation_create_info = {};
          allocation_create_info.flags =
              VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
          allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;
          VmaAllocationInfo allocation_info;
          vmaCreateBuffer(context_.allocator(), &buffer_info, &allocation_create_info, &staging_, &allocation_,
                          &allocation_info);
          staging_map_ = reinterpret_cast<uint8_t*>(allocation_info.pMappedData);
          staging_size_ = size;
        }

        // TODO: make GPU buffer persist. Buffer creation is expensive.
        auto ply_buffer =
            vk::Buffer(context_, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);


        // copy to staging buffer
        {
          std::memcpy(staging_map_, ply_offsets.data(), ply_offsets.size() * sizeof(uint32_t));
          std::memcpy(staging_map_ + 60 * sizeof(uint32_t), buffer_.data(), buffer_.size() * sizeof(char));
        }

        // transfer command
        VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &begin_info);

        VkBufferCopy region = {};
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = size;
        vkCmdCopyBuffer(cb, staging_, ply_buffer, 1, &region);

        // transfer ownership
        std::vector<VkBufferMemoryBarrier> buffer_barriers(1);
        buffer_barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        buffer_barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        buffer_barriers[0].srcQueueFamilyIndex = context_.transfer_queue_family_index();
        buffer_barriers[0].dstQueueFamilyIndex = context_.graphics_queue_family_index();
        buffer_barriers[0].buffer = ply_buffer;
        buffer_barriers[0].offset = 0;
        buffer_barriers[0].size = size;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL,
                             buffer_barriers.size(), buffer_barriers.data(), 0, NULL);

        vkEndCommandBuffer(cb);

        // submit (serialized: transfer may share the physical queue with graphics)
        {
          
          VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
          submit_info.commandBufferCount = 1;
          submit_info.pCommandBuffers = &cb;
          {
            std::unique_lock<std::mutex> guard{context_.queue_mutex()};
            vkQueueSubmit(context_.transfer_queue(), 1, &submit_info, fence_);
          }

          vkWaitForFences(context_.device(), 1, &fence_, VK_TRUE, UINT64_MAX);
          vkResetFences(context_.device(), 1, &fence_);
          
        }

        // update loaded point count
        {
          std::unique_lock<std::mutex> guard{mutex_};
          buffer_barriers_.insert(buffer_barriers_.end(), buffer_barriers.begin(), buffer_barriers.end());
          buffer_barriers.clear();
          ply_buffer_ = ply_buffer;
        }
      }

      vkDestroyCommandPool(context_.device(), command_pool, NULL);
    });
  }

  ~Impl() {
    if (thread_.joinable()) {
      cancel_ = true;
      terminate_ = true;
      cv_.notify_one();
      thread_.join();
    }

    vkDestroyFence(context_.device(), fence_, NULL);
    if (staging_) vmaDestroyBuffer(context_.allocator(), staging_, allocation_);
  }

  void Start(const std::string& ply_filepath) {
    {
      std::unique_lock<std::mutex> guard{mutex_};
      total_point_count_ = 0;
      loaded_point_count_ = 0;
      ply_filepath_ = ply_filepath;
    }
    cv_.notify_one();
  }

  void SetCullMasks(const std::string& masks_path, int view_index) {
    std::unique_lock<std::mutex> guard{mutex_};
    cull_masks_path_ = masks_path;
    cull_view_index_ = view_index;
  }

  void SetOpacityBias(float bias) {
    std::unique_lock<std::mutex> guard{mutex_};
    opacity_bias_ = bias;
  }

  Progress GetProgress() {
    Progress result;
    std::unique_lock<std::mutex> guard{mutex_};
    result.total_point_count = total_point_count_;
    result.loaded_point_count = loaded_point_count_;
    result.ply_buffer = ply_buffer_;
    result.buffer_barriers = std::move(buffer_barriers_);

    ply_buffer_ = {};

    return result;
  }

  void Cancel() { cancel_ = true; }

 private:
  vk::Context context_;

  std::thread thread_;
  std::mutex mutex_;
  std::atomic_bool terminate_ = false;
  std::atomic_bool cancel_ = false;
  std::condition_variable cv_;

  std::string ply_filepath_;
  std::string cull_masks_path_;
  int cull_view_index_ = -1;
  float opacity_bias_ = 0.0f;

  uint32_t total_point_count_ = 0;
  uint32_t loaded_point_count_ = 0;
  std::vector<VkBufferMemoryBarrier> buffer_barriers_;

  VkFence fence_ = VK_NULL_HANDLE;

  // position: (N, 3), cov3d: (N, 6), sh: (N, 48), opacity: (N).
  // staging: (N, 58)
  std::vector<char> buffer_;
  VkDeviceSize staging_size_ = 0;
  VkBuffer staging_ = VK_NULL_HANDLE;
  VmaAllocation allocation_ = VK_NULL_HANDLE;
  uint8_t* staging_map_ = nullptr;

  vk::Buffer ply_buffer_;
};

SplatLoadThread::SplatLoadThread() = default;

SplatLoadThread::SplatLoadThread(vk::Context context) : impl_(std::make_shared<Impl>(context)) {}

SplatLoadThread::~SplatLoadThread() = default;

void SplatLoadThread::Start(const std::string& ply_filepath) { impl_->Start(ply_filepath); }

void SplatLoadThread::SetCullMasks(const std::string& masks_path, int view_index) {
  impl_->SetCullMasks(masks_path, view_index);
}

void SplatLoadThread::SetOpacityBias(float bias) {
  impl_->SetOpacityBias(bias);
}

SplatLoadThread::Progress SplatLoadThread::GetProgress() { return impl_->GetProgress(); }

void SplatLoadThread::Cancel() { impl_->Cancel(); }

}  // namespace vkgs
