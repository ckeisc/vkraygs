#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include <vector>

#include <argparse/argparse.hpp>

#include <vkgs/engine/engine.h>

namespace {

// Batch views file: one camera pose per line,
//   16 floats view matrix (column-major) + 3 floats eye position.
// Lines starting with '#' and blank lines are ignored.
bool LoadViewsFile(const std::string& path, std::vector<std::array<float, 16>>* views,
                   std::vector<std::array<float, 3>>* eyes) {
  std::ifstream in(path);
  if (!in.good()) return false;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream iss(line);
    std::array<float, 16> v;
    std::array<float, 3> e;
    for (int i = 0; i < 16; ++i) {
      if (!(iss >> v[i])) return false;
    }
    for (int i = 0; i < 3; ++i) {
      if (!(iss >> e[i])) return false;
    }
    views->push_back(v);
    eyes->push_back(e);
  }
  return !views->empty();
}

}  // namespace

int main(int argc, char** argv) {
  argparse::ArgumentParser parser("vkgs");
  parser.add_argument("-i", "--input").help("input ply/spz file.");
  parser.add_argument("--z-up")
      .default_value(true)
      .implicit_value(true)
      .help("Z-up world for the orbit camera (default: true; Hyperscape SPZ captures are Z-up).");
  parser.add_argument("--y-up")
      .default_value(false)
      .implicit_value(true)
      .help("Y-up world for the orbit camera (for standard Y-up files).");
  parser.add_argument("--kernel")
      .default_value(std::string("raygs"))
      .help("splat kernel for batch mode: gs (EWA) or raygs");
  parser.add_argument("--views")
      .default_value(std::string(""))
      .help("batch mode: text file with one camera pose per line "
            "(16 floats view matrix column-major + 3 floats eye)");
  parser.add_argument("--outdir").default_value(std::string(".")).help("batch mode: output directory for PNGs");
  parser.add_argument("--prefix").default_value(std::string("shot")).help("batch mode: output filename prefix");
  parser.add_argument("--cull-masks")
      .default_value(std::string(""))
      .help("visibility-cluster masks file (Hyperscape od_cluster_masks.bin) for occlusion culling");
  parser.add_argument("--cull-view")
      .default_value(-1)
      .scan<'i', int>()
      .help("viewpoint index (0-63) for --cull-masks; static CPU culling (batch mode)");
  parser.add_argument("--cull-centroids")
      .default_value(std::string(""))
      .help("cluster centroids JSON (Hyperscape od_cluster_centroids) for dynamic GPU culling: "
            "union of 3 nearest viewpoints per frame (interactive mode)");
  parser.add_argument("--dc-only")
      .default_value(true)
      .implicit_value(true)
      .help("ignore SH view-dependent terms, use DC color only (default: true; Hyperscape SPZ uses non-standard SH convention)");
  parser.add_argument("--full-sh")
      .default_value(false)
      .implicit_value(true)
      .help("enable full SH evaluation (for standard 3DGS SPZ files)");
  parser.add_argument("--opacity-bias")
      .default_value(0.0f)
      .scan<'g', float>()
      .help("logit-space opacity boost for Hyperscape SPZ (e.g. 1.0); compensates for rasterizer vs ray-marcher accumulation");
  try {
    parser.parse_args(argc, argv);
  } catch (const std::exception& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << parser;
    return 1;
  }

  try {
    vkgs::Engine engine;

    // Z-up is the default (Hyperscape SPZ captures are Z-up).
    // --y-up opts back into Y-up orbit camera for standard files.
    bool z_up = parser.get<bool>("z-up") && !parser.get<bool>("y-up");
    if (z_up) {
      engine.SetZUp(true);
    }

    const std::string views_path = parser.get<std::string>("views");
    if (!views_path.empty()) {
      const std::string kernel = parser.get<std::string>("kernel");
      if (kernel != "gs" && kernel != "raygs") {
        std::cerr << "unknown kernel: " << kernel << " (expected gs or raygs)" << std::endl;
        return 1;
      }
      std::vector<std::array<float, 16>> views;
      std::vector<std::array<float, 3>> eyes;
      if (!LoadViewsFile(views_path, &views, &eyes)) {
        std::cerr << "failed to load views file: " << views_path << std::endl;
        return 1;
      }
      engine.SetKernelRayGS(kernel == "raygs");
      engine.SetBatchViews(views, eyes);
      engine.SetBatchOutput(parser.get<std::string>("outdir"), parser.get<std::string>("prefix"));
      std::cout << "batch mode: " << views.size() << " poses, kernel=" << kernel << std::endl;
    }

    const std::string cull_masks = parser.get<std::string>("cull-masks");
    const std::string cull_centroids = parser.get<std::string>("cull-centroids");
    const int cull_view = parser.get<int>("cull-view");
    if (!cull_masks.empty() && !cull_centroids.empty()) {
      // Dynamic GPU culling: no CPU filtering (view_index=-1), masks go to GPU.
      engine.SetCullMasks(cull_masks, -1);
      engine.SetCullCentroids(cull_centroids);
      std::cout << "culling: dynamic GPU (union of 3 nearest), masks=" << cull_masks
                << " centroids=" << cull_centroids << std::endl;
    } else if (!cull_masks.empty() && cull_view >= 0) {
      engine.SetCullMasks(cull_masks, cull_view);
      std::cout << "culling: static CPU, masks=" << cull_masks << " view=" << cull_view << std::endl;
    }

    // DC-only is the default (Hyperscape SPZ uses non-standard SH convention).
    // --full-sh opts back into standard 3DGS SH evaluation.
    bool dc_only = parser.get<bool>("dc-only") && !parser.get<bool>("full-sh");
    if (dc_only) {
      engine.SetDcOnly(true);
      std::cout << "dc-only: ignoring SH view-dependent terms (Hyperscape mode)" << std::endl;
    }

    const float opacity_bias = parser.get<float>("opacity-bias");
    if (opacity_bias != 0.0f) {
      engine.SetOpacityBias(opacity_bias);
      std::cout << "opacity bias: " << opacity_bias << " (logit space)" << std::endl;
    }

    if (parser.is_used("input")) {
      auto ply_filepath = parser.get<std::string>("input");
      engine.LoadSplats(ply_filepath);
    }

    engine.Run();
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
  }

  return 0;
}
