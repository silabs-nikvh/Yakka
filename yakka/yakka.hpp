#pragma once

#include "utilities.hpp"
#include "yaml-cpp/yaml.h"
#include <set>
#include <string>
#include <functional>
#include <unordered_set>
#include <future>

namespace yakka {
const std::string yakka_component_extension     = ".yakka";
const std::string yakka_component_old_extension = ".bob";
const std::string slcc_component_extension      = ".slcc";
const std::string slce_component_extension      = ".slce";
const std::string slsdk_component_extension     = ".slsdk";
const std::string slcp_component_extension      = ".slcp";
const std::string slcs_extension                = ".slcs";
const std::string slsdk_extensions_directory    = "extension";
const char data_dependency_identifier           = ':';
const char data_wildcard_identifier             = '*';
const std::string database_filename             = "yakka-components.yaml";

#if defined(_WIN64) || defined(_WIN32) || defined(__CYGWIN__)
const std::string host_os_string         = "windows";
const std::string executable_extension   = ".exe";
const std::string host_os_path_seperator = ";";
const auto async_launch_option           = std::launch::async | std::launch::deferred;
#elif defined(__APPLE__)
const std::string host_os_string         = "macos";
const std::string executable_extension   = "";
const std::string host_os_path_seperator = ":";
const auto async_launch_option           = std::launch::async | std::launch::deferred; // Unsure
#elif defined(__linux__)
const std::string host_os_string         = "linux";
const std::string executable_extension   = "";
const std::string host_os_path_seperator = ":";
const auto async_launch_option           = std::launch::deferred;
#endif

struct process_return {
  std::string result;
  int retcode;
};

struct project_description {
  std::vector<std::string> components;
  std::vector<std::string> features;
};

enum yakka_status {
  SUCCESS = 0,
  FAIL,
};

} // namespace yakka
