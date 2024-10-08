#pragma once

#include "taskflow.hpp"
#include "json.hpp"
#include <future>
#include <optional>
#include <filesystem>

#ifdef EXPERIMENTAL_FILESYSTEM
namespace fs = std::experimental::filesystem;
#else
namespace fs = std::filesystem;
#endif

namespace yakka {

struct blueprint {
  struct dependency {
    enum dependency_type { DEFAULT_DEPENDENCY, DATA_DEPENDENCY, DEPENDENCY_FILE_DEPENDENCY } type;
    std::string name;
  };
  std::string target;
  std::optional<std::string> regex;
  std::vector<std::string> requirements;
  std::vector<dependency> dependencies; // Unprocessed dependencies. Raw values as found in the YAML.
  nlohmann::json process;
  std::string parent_path;
  std::string task_group;

  blueprint(const std::string &target, const nlohmann::json &blueprint, const std::string &parent_path);
};
} // namespace yakka
