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
  parser.add_argument("--kernel")
      .default_value(std::string("raygs"))
      .help("splat kernel for batch mode: gs (EWA) or raygs");
  parser.add_argument("--views")
      .default_value(std::string(""))
      .help("batch mode: text file with one camera pose per line "
            "(16 floats view matrix column-major + 3 floats eye)");
  parser.add_argument("--outdir").default_value(std::string(".")).help("batch mode: output directory for PNGs");
  parser.add_argument("--prefix").default_value(std::string("shot")).help("batch mode: output filename prefix");
  try {
    parser.parse_args(argc, argv);
  } catch (const std::exception& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << parser;
    return 1;
  }

  try {
    vkgs::Engine engine;

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
