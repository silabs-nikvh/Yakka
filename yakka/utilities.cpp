#include "yakka_project.hpp"
#include "utilities.hpp"
#include "subprocess.hpp"
#include "spdlog/spdlog.h"
#include "glob/glob.h"
#include <concepts>
#include <string_view>
#include <expected>
#include <fstream>
#include <string>
#include <vector>
#include <ranges>
#include <algorithm>
#include <iomanip>
#include <filesystem>

namespace yakka {

/*
// Execute with admin
if(0 == CreateProcess(argv[2], params, NULL, NULL, false, 0, NULL, NULL, &si, &pi)) {
        //runas word is a hack to require UAC elevation
        ShellExecute(NULL, "runas", argv[2], params, NULL, SW_SHOWNORMAL);
}
*/
std::pair<std::string, int> exec(const std::string &command_text, const std::string &arg_text)
{
  spdlog::info("{} {}", command_text, arg_text);
  try {
    std::string command = command_text;
    if (!arg_text.empty())
      command += " " + arg_text;
#if defined(__USING_WINDOWS__)
    auto p = subprocess::Popen(command, subprocess::output{ subprocess::PIPE }, subprocess::error{ subprocess::STDOUT });
#else
    auto p      = subprocess::Popen(command, subprocess::shell{ true }, subprocess::output{ subprocess::PIPE }, subprocess::error{ subprocess::STDOUT });
#endif
#if defined(__USING_WINDOWS__)
    auto output  = p.communicate().first;
    auto retcode = p.wait();
    retcode      = p.poll();
#else
    auto output = p.communicate().first;
    p.wait();
    auto retcode = p.retcode();
#endif
    std::string output_text = output.buf.data();
    return { output_text, retcode };
  } catch (std::exception &e) {
    spdlog::error("Exception while executing: {}\n{}", command_text, e.what());
    return { "", -1 };
  }
}

int exec(const std::string &command_text, const std::string &arg_text, std::function<void(std::string &)> function)
{
  spdlog::info("{} {}", command_text, arg_text);
  try {
    std::string command = command_text;
    if (!arg_text.empty())
      command += " " + arg_text;
#if defined(__USING_WINDOWS__)
    auto p = subprocess::Popen(command, subprocess::output{ subprocess::PIPE }, subprocess::error{ subprocess::STDOUT });
#else
    auto p       = subprocess::Popen(command, subprocess::shell{ true }, subprocess::output{ subprocess::PIPE }, subprocess::error{ subprocess::STDOUT });
#endif
    auto output = p.output();
    std::array<char, 512> buffer;
    size_t count = 0;
    buffer.fill('\0');
    if (output != nullptr) {
      while (1) {
        buffer[count] = fgetc(output);
        if (feof(output))
          break;

        if (count == buffer.size() - 1 || buffer[count] == '\r' || buffer[count] == '\n') {
          std::string temp(buffer.data());
          try {
            function(temp);
          } catch (std::exception &e) {
            spdlog::debug("exec() data processing threw exception '{}'for the following data:\n{}", e.what(), temp);
          }
          buffer.fill('\0');
          count = 0;
        } else
          ++count;
      };
    }
    auto retcode = p.wait();
#if defined(__USING_WINDOWS__)
    retcode = p.poll();
#endif
    return retcode;
  } catch (std::exception &e) {
    spdlog::error("Exception while executing: {}\n{}", command_text, e.what());
  }
  return -1;
}

bool yaml_diff(const YAML::Node &node1, const YAML::Node &node2)
{
  std::vector<std::pair<const YAML::Node &, const YAML::Node &>> compare_list;
  compare_list.push_back({ node1, node2 });
  for (size_t i = 0; i < compare_list.size(); ++i) {
    const YAML::Node &left  = compare_list[i].first;
    const YAML::Node &right = compare_list[i].second;

    if (left.Type() != right.Type())
      return true;

    switch (left.Type()) {
      case YAML::NodeType::Scalar:
        if (left.Scalar() != right.Scalar())
          return true;
        break;

      case YAML::NodeType::Sequence:
        // Verify the sequences have the same length
        if (left.size() != right.size())
          return true;
        for (size_t a = 0; a < left.size(); ++a)
          compare_list.push_back({ left[a], right[a] });
        break;

      case YAML::NodeType::Map:
        // Verify the maps have the same length
        if (left.size() != right.size())
          return true;
        for (const auto &a: left) {
          auto &key  = a.first.Scalar();
          auto &node = a.second;
          if (!right[key])
            return true;
          compare_list.push_back({ node, right[key] });
        }
        break;
      default:
        break;
    }
  }

  return false;
}

YAML::Node yaml_path(const YAML::Node &node, std::string path)
{
  YAML::Node temp = node;
  std::stringstream ss(path);
  std::string s;
  while (std::getline(ss, s, '.'))
    temp.reset(temp[s]);
  return temp;
}

nlohmann::json::json_pointer json_pointer(std::string path)
{
  if (path.front() != '/') {
    path = "/" + path;
    std::replace(path.begin(), path.end(), '.', '/');
  }
  return nlohmann::json::json_pointer{ path };
}

nlohmann::json json_path(const nlohmann::json &node, std::string path)
{
  nlohmann::json::json_pointer temp(path);
  return node[temp];

  // return node[nlohmann::json_pointer(path)];
  // auto temp = node;
  // std::stringstream ss(path);
  // std::string s;
  // while (std::getline(ss, s, '.'))
  //     temp = temp[s];//.reset(temp[s]);
  // return temp;
}

std::tuple<component_list_t, feature_list_t, command_list_t> parse_arguments(const std::vector<std::string> &argument_string)
{
  component_list_t components;
  feature_list_t features;
  command_list_t commands;

  //    for (auto s = argument_string.begin(); s != argument_string.end(); ++s)
  for (auto s: argument_string) {
    // Identify features, commands, and components
    if (s.front() == '+')
      features.insert(s.substr(1));
    else if (s.back() == '!')
      commands.insert(s.substr(0, s.size() - 1));
    else
      components.insert(s);
  }

  return { std::move(components), std::move(features), std::move(commands) };
}

std::string generate_project_name(const component_list_t &components, const feature_list_t &features)
{
  std::string project_name = "";

  // Generate the project name from the project string
  for (const auto &i: components)
    project_name += i + "-";

  if (!components.empty())
    project_name.pop_back();

  for (const auto &i: features)
    project_name += "-" + i;

  if (project_name.empty())
    project_name = "none";

  return project_name;
}

/**
 * @brief Parses dependency files as output by GCC or Clang generating a vector of filenames as found in the named file
 *
 * @param filename  Name of the dependency file. Typically ending in '.d'
 * @return std::vector<std::string>  Vector of files specified as dependencies
 */
std::vector<std::string> parse_gcc_dependency_file(const std::string &filename)
{
  std::vector<std::string> dependencies;
  std::ifstream infile(filename);

  if (!infile.is_open())
    return {};

  std::string line;

  // Find and ignore the first line with the target. Typically "<target>: \"
  do {
    std::getline(infile, line);
  } while (line.length() > 0 && line.find(':') == std::string::npos);

  while (std::getline(infile, line, ' ')) {
    if (line.empty() || line.compare("\\\n") == 0)
      continue;
    if (line.back() == '\n')
      line.pop_back();
    if (line.back() == '\r')
      line.pop_back();
    dependencies.push_back(line.starts_with("./") ? line.substr(line.find_first_not_of("/", 2)) : line);
  }

  return dependencies;
}

void json_node_merge(nlohmann::json &merge_target, const nlohmann::json &node)
{
  switch (node.type()) {
    case nlohmann::detail::value_t::object:
      if (merge_target.type() != nlohmann::detail::value_t::object) {
        spdlog::error("Currently not supported");
        return;
      }
      // Iterate through child nodes
      for (auto it = node.begin(); it != node.end(); ++it) {
        // Check if the key is already in merge_target
        auto it2 = merge_target.find(it.key());
        if (it2 != merge_target.end()) {
          json_node_merge(it2.value(), it.value());
          continue;
        } else {
          merge_target[it.key()] = it.value();
        }
      }
      break;

    case nlohmann::detail::value_t::array:
      switch (merge_target.type()) {
        case nlohmann::detail::value_t::object:
          spdlog::error("Cannot merge array into an object");
          break;
        default:
          // Convert scalar into an array
          merge_target = nlohmann::json::array({ merge_target });
          [[fallthrough]];
        case nlohmann::detail::value_t::array:
        case nlohmann::detail::value_t::null:
          for (auto &i: node)
            merge_target.push_back(i);
          break;
      }
      break;
    default:
      switch (merge_target.type()) {
        case nlohmann::detail::value_t::object:
          spdlog::error("Cannot merge scalar into an object");
          break;
        default:
          // Convert scalar into an array
          merge_target = nlohmann::json::array({ merge_target });
          [[fallthrough]];
        case nlohmann::detail::value_t::array:
          merge_target.push_back(node);
          break;
      }
      break;
  }
}

std::string component_dotname_to_id(const std::string dotname)
{
  return dotname.find_last_of(".") != std::string::npos ? dotname.substr(dotname.find_last_of(".") + 1) : dotname;
}

std::string try_render(inja::Environment &env, const std::string &input, const nlohmann::json &data)
{
  try {
    return env.render(input, data);
  } catch (std::exception &e) {
    spdlog::error("Template error: {}\n{}", input, e.what());
    return "";
  }
}

std::string try_render_file(inja::Environment &env, const std::string &filename, const nlohmann::json &data)
{
  try {
    return env.render_file(filename, data);
  } catch (std::exception &e) {
    spdlog::error("Template error: {}\n{}", filename, e.what());
    return "";
  }
}

void add_common_template_commands(inja::Environment &inja_env)
{
  inja_env.add_callback("dir", 1, [](inja::Arguments &args) {
    auto path = std::filesystem::path{ args.at(0)->get<std::string>() };
    return path.has_filename() ? path.parent_path().string() : path.string();
  });
  inja_env.add_callback("not_dir", 1, [](inja::Arguments &args) {
    return std::filesystem::path{ args.at(0)->get<std::string>() }.filename().string();
  });
  inja_env.add_callback("parent_path", 1, [](inja::Arguments &args) {
    return std::filesystem::path{ args.at(0)->get<std::string>() }.parent_path().string();
  });
  inja_env.add_callback("glob", [](inja::Arguments &args) {
    nlohmann::json aggregate = nlohmann::json::array();
    std::vector<std::string> string_args;
    for (const auto &i: args)
      string_args.push_back(i->get<std::string>());
    for (auto &p: glob::rglob(string_args))
      aggregate.push_back(p.generic_string());
    return aggregate;
  });
  inja_env.add_callback("absolute_dir", 1, [](inja::Arguments &args) {
    const auto path = std::filesystem::path{ args.at(0)->get<std::string>() };
    return std::filesystem::absolute(path).generic_string();
  });
  inja_env.add_callback("absolute_path", 1, [](inja::Arguments &args) {
    const auto path = std::filesystem::path{ args.at(0)->get<std::string>() };
    return std::filesystem::absolute(path).generic_string();
  });
  inja_env.add_callback("relative_path", 1, [](inja::Arguments &args) {
    auto path          = std::filesystem::path{ args.at(0)->get<std::string>() };
    const auto current = std::filesystem::current_path();
    auto new_path      = std::filesystem::relative(path, current);
    return new_path.generic_string();
  });
  inja_env.add_callback("relative_path", 2, [](inja::Arguments &args) {
    const auto path1 = args.at(0)->get<std::string>();
    const auto path2 = std::filesystem::absolute(args.at(1)->get<std::string>());
    return std::filesystem::relative(path1, path2).generic_string();
  });
  inja_env.add_callback("extension", 1, [](inja::Arguments &args) {
    return std::filesystem::path{ args.at(0)->get<std::string>() }.extension().string().substr(1);
  });
  inja_env.add_callback("filesize", 1, [](const inja::Arguments &args) {
    return fs::file_size(args[0]->get<std::string>());
  });
  inja_env.add_callback("file_exists", 1, [](const inja::Arguments &args) {
    return fs::exists(args[0]->get<std::string>());
  });
  inja_env.add_callback("hex2dec", 1, [](const inja::Arguments &args) {
    return std::stoul(args[0]->get<std::string>(), nullptr, 16);
  });
  inja_env.add_callback("read_file", 1, [](const inja::Arguments &args) {
    auto file = std::ifstream(args[0]->get<std::string>());
    return std::string{ std::istreambuf_iterator<char>{ file }, {} };
  });
  inja_env.add_callback("load_yaml", 1, [](const inja::Arguments &args) {
    const auto file_path = args[0]->get<std::string>();
    if (std::filesystem::exists(file_path)) {
      auto yaml_data = YAML::LoadFile(file_path);
      return yaml_data.as<nlohmann::json>();
    } else {
      return nlohmann::json();
    }
  });
  inja_env.add_callback("load_json", 1, [](const inja::Arguments &args) {
    std::ifstream file_stream(args[0]->get<std::string>());
    return nlohmann::json::parse(file_stream);
  });
  inja_env.add_callback("quote", 1, [](const inja::Arguments &args) {
    std::stringstream ss;
    if (args[0]->is_string())
      ss << std::quoted(args[0]->get<std::string>());
    else if (args[0]->is_number_integer())
      ss << std::quoted(std::to_string(args[0]->get<int>()));
    else if (args[0]->is_number_float())
      ss << std::quoted(std::to_string(args[0]->get<float>()));
    return ss.str();
  });
  inja_env.add_callback("replace", 3, [](const inja::Arguments &args) {
    auto input  = args[0]->get<std::string>();
    auto target = std::regex(args[1]->get<std::string>());
    auto match  = args[2]->get<std::string>();
    return std::regex_replace(input, target, match);
  });
  inja_env.add_callback("regex_escape", 1, [](const inja::Arguments &args) {
    auto input = args[0]->get<std::string>();
    const std::regex metacharacters(R"([\.\^\$\+\(\)\[\]\{\}\|\?])");
    return std::regex_replace(input, metacharacters, "\\$&");
  });
  inja_env.add_callback("split", 2, [](const inja::Arguments &args) {
    auto input = args[0]->get<std::string>();
    auto delim = args[1]->get<std::string>();
    nlohmann::json output;
    for (auto word: std::views::split(input, delim))
      output.push_back(std::string_view(word));
    return output;
  });
  inja_env.add_callback("starts_with", 2, [](const inja::Arguments &args) {
    auto input = args[0]->get<std::string>();
    auto start = args[1]->get<std::string>();
    return input.starts_with(start);
  });
  inja_env.add_callback("substring", 2, [](const inja::Arguments &args) {
    auto input = args[0]->get<std::string>();
    auto index = args[1]->get<int>();
    return input.substr(index);
  });
  inja_env.add_callback("trim", 1, [](const inja::Arguments &args) {
    auto input = args[0]->get<std::string>();
    input.erase(input.begin(), std::find_if(input.begin(), input.end(), [](unsigned char ch) {
                  return !std::isspace(ch);
                }));
    input.erase(std::find_if(input.rbegin(),
                             input.rend(),
                             [](unsigned char ch) {
                               return !std::isspace(ch);
                             })
                  .base(),
                input.end());
    return input;
  });
}

std::pair<std::string, int> run_command(const std::string target, construction_task *task, project *project)
{
  std::string captured_output = "";
  inja::Environment inja_env  = inja::Environment();
  auto &blueprint             = task->match;
  std::string curdir_path     = blueprint->blueprint->parent_path;
  nlohmann::json data_store;

  add_common_template_commands(inja_env);

  inja_env.add_callback("store", 3, [&](const inja::Arguments &args) {
    if (args[0] && args[1]) {
      nlohmann::json::json_pointer ptr{ args[0]->get<std::string>() };
      auto key             = args[1]->get<std::string>();
      data_store[ptr][key] = *args[2];
    }
    return nlohmann::json{};
  });
  inja_env.add_callback("store", 2, [&](const inja::Arguments &args) {
    nlohmann::json::json_pointer ptr{ args[0]->get<std::string>() };
    data_store[ptr] = *args[1];
    return nlohmann::json{};
  });
  inja_env.add_callback("push_back", 2, [&](const inja::Arguments &args) {
    nlohmann::json::json_pointer ptr{ args[0]->get<std::string>() };
    if (!data_store.contains(ptr)) {
      data_store[ptr] = nlohmann::json::array();
    }
    data_store[ptr].push_back(*args[1]);
    return nlohmann::json{};
  });
  inja_env.add_callback("unique", 1, [&](const inja::Arguments &args) {
    nlohmann::json filtered;
    std::copy_if(args[0]->cbegin(), args[0]->cend(), std::back_inserter(filtered), [&](const nlohmann::json &item) {
      return std::find(filtered.begin(), filtered.end(), item.get<std::string>()) == filtered.end();
    });
    return filtered;
  });

  inja_env.add_callback("fetch", 2, [&](const inja::Arguments &args) {
    nlohmann::json::json_pointer ptr{ args[0]->get<std::string>() };
    auto key = args[1]->get<std::string>();
    return data_store[ptr][key];
  });
  inja_env.add_callback("fetch", 1, [&](const inja::Arguments &args) {
    nlohmann::json::json_pointer ptr{ args[0]->get<std::string>() };
    return data_store[ptr];
  });
  inja_env.add_callback("erase", 1, [&](const inja::Arguments &args) {
    nlohmann::json::json_pointer ptr{ args[0]->get<std::string>() };
    data_store[ptr].clear();
    return nlohmann::json{};
  });
  inja_env.add_callback("$", 1, [&blueprint](const inja::Arguments &args) {
    return blueprint->regex_matches[args[0]->get<int>()];
  });
  inja_env.add_callback("curdir", 0, [&](const inja::Arguments &args) {
    return curdir_path;
  });
  inja_env.add_callback("render", 1, [&](const inja::Arguments &args) {
    return try_render(inja_env, args[0]->get<std::string>(), project->project_summary);
  });
  inja_env.add_callback("render", 2, [&curdir_path, &inja_env, &project](const inja::Arguments &args) {
    auto backup               = curdir_path;
    curdir_path               = args[1]->get<std::string>();
    std::string render_output = try_render(inja_env, args[0]->get<std::string>(), project->project_summary);
    curdir_path               = backup;
    return render_output;
  });

  inja_env.add_callback("aggregate", 1, [&](const inja::Arguments &args) {
    nlohmann::json aggregate;
    auto path = json_pointer(args[0]->get<std::string>());
    // Loop through components, check if object path exists, if so add it to the aggregate
    for (const auto &[c_key, c_value]: project->project_summary["components"].items()) {
      // auto v = json_path(c.value(), path);
      if (!c_value.contains(path) || c_value[path].is_null())
        continue;

      auto v = c_value[path];
      if (v.is_object())
        for (const auto &[i_key, i_value]: v.items()) {
          aggregate[i_key] = i_value; //try_render(inja_env, i.second.as<std::string>(), project->project_summary, log);
        }
      else if (v.is_array())
        for (const auto &[i_key, i_value]: v.items())
          if (i_value.is_object())
            aggregate.push_back(i_value);
          else
            aggregate.push_back(try_render(inja_env, i_value.get<std::string>(), project->project_summary));
      else if (!v.is_null())
        aggregate.push_back(try_render(inja_env, v.get<std::string>(), project->project_summary));
    }

    // Check project data
    if (project->project_summary["data"].contains(path)) {
      auto v = project->project_summary["data"][path];
      if (v.is_object())
        for (const auto &[i_key, i_value]: v.items())
          aggregate[i_key] = i_value;
      else if (v.is_array())
        for (const auto &i: v)
          aggregate.push_back(inja_env.render(i.get<std::string>(), project->project_summary));
      else
        aggregate.push_back(inja_env.render(v.get<std::string>(), project->project_summary));
    }
    return aggregate;
  });
  inja_env.add_callback("load_component", 1, [&](const inja::Arguments &args) {
    const auto component_name     = args[0]->get<std::string>();
    const auto component_location = project->workspace.find_component(component_name);
    if (!component_location.has_value()) {
      return nlohmann::json{};
    }
    auto [component_path, package_path] = component_location.value();
    yakka::component new_component;
    if (new_component.parse_file(component_path, package_path) == yakka::yakka_status::SUCCESS) {
      return new_component.json;
    } else {
      return nlohmann::json{};
    }
  });

  std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

  // Note: A blueprint process is a sequence of maps
  for (const auto &command_entry: blueprint->blueprint->process) {
    assert(command_entry.is_object());

    if (command_entry.size() != 1) {
      spdlog::error("Command '{}' for target '{}' is malformed", command_entry.begin().key(), target);
      return { "", -1 };
    }

    // Take the first entry in the map as the command
    auto command                   = command_entry.begin();
    const std::string command_name = command.key();
    int retcode                    = 0;

    try {
      if (project->project_summary["tools"].contains(command_name)) {
        auto tool                = project->project_summary["tools"][command_name];
        std::string command_text = "";

        command_text.append(tool);

        std::string arg_text = command.value().get<std::string>();

        // Apply template engine
        arg_text = try_render(inja_env, arg_text, project->project_summary);

        auto [temp_output, temp_retcode] = exec(command_text, arg_text);
        retcode                          = temp_retcode;

        if (retcode != 0)
          spdlog::error("Returned {}\n{}", retcode, temp_output);
        if (retcode < 0)
          return { temp_output, retcode };

        captured_output = temp_output;
        // Echo the output of the command
        // TODO: Note this should be done by the main thread to ensure the outputs from multiple run_command instances don't overlap
        spdlog::info(captured_output);
      }
      // Else check if it is a built-in command
      else if (project->blueprint_commands.contains(command_name)) {
        yakka::process_return test_result = project->blueprint_commands.at(command_name)(target, command.value(), captured_output, project->project_summary, inja_env);
        captured_output                   = test_result.result;
        retcode                           = test_result.retcode;
      } else {
        spdlog::error("{} tool doesn't exist", command_name);
      }

      if (retcode < 0)
        return { captured_output, retcode };
    } catch (std::exception &e) {
      spdlog::error("Failed to run command: '{}' as part of {}", command_name, target);
      spdlog::error("{}", e.what());
      throw e;
    }
  }

  std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
  auto duration                                     = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
  spdlog::info("{}: {} milliseconds", target, duration);
  return { captured_output, 0 };
}

std::pair<std::string, int> download_resource(const std::string url, fs::path destination)
{
  fs::path filename = destination / url.substr(url.find_last_not_of('/'));
#if defined(_WIN64) || defined(_WIN32) || defined(__CYGWIN__)
  return exec("powershell", "Invoke-WebRequest " + url + " -OutFile " + filename.generic_string());
#else
  return exec("curl", url + " -o " + filename.generic_string());
#endif
}

nlohmann::json::json_pointer create_condition_pointer(const nlohmann::json condition)
{
  nlohmann::json::json_pointer pointer;

  for (const auto &item: condition) {
    pointer /= "/supports/features"_json_pointer;
    pointer /= item.get<std::string>();
  }

  return pointer;
}

// Helper struct to hold component comparison results
struct ComparisonResult {
    bool changed{false};
    std::string error_message;
};

[[nodiscard]]
std::expected<bool, std::string> has_data_dependency_changed(
    std::string_view data_path,
    const nlohmann::json& left,
    const nlohmann::json& right) noexcept
{
    if (data_path.empty() || data_path[0] != data_dependency_identifier) {
        return false;
    }
    
    if (data_path[1] != '/') {
        return std::unexpected{"Invalid path format: missing root separator"};
    }

    // Early return if left data is missing
    if (left.is_null() || left["components"].is_null()) {
        return true;
    }

    try {
        const auto process_component = [&](std::string_view component_name, 
                                        const nlohmann::json::json_pointer& pointer) -> ComparisonResult {
            if (!left["components"].contains(component_name) || !right["components"].contains(component_name)) {
                return {true, ""};
            }

            const auto& left_comp = left["components"][std::string{component_name}];
            const auto& right_comp = right["components"][std::string{component_name}];

            auto get_value = [](const auto& json, const auto& ptr) {
                return json.contains(ptr) ? json[ptr] : nlohmann::json{};
            };

            auto left_value = get_value(left_comp, pointer);
            auto right_value = get_value(right_comp, pointer);

            return {left_value != right_value, ""};
        };

        if (data_path[2] == data_wildcard_identifier) {
            if (data_path[3] != '/') {
                return std::unexpected{"Data dependency malformed: " + std::string{data_path}};
            }

            auto path_view = std::string_view{data_path}.substr(3);
            nlohmann::json::json_pointer pointer{std::string{path_view}};

            // Using C++20 ranges to process components
            for (const auto& [component_name, _] : right["components"].items()) {
                auto result = process_component(component_name, pointer);
                if (!result.error_message.empty()) {
                    return std::unexpected{std::move(result.error_message)};
                }
                if (result.changed) {
                    return true;
                }
            }
        } else {
            auto path_view = std::string_view{data_path}.substr(2);
            auto separator_pos = path_view.find_first_of('/');
            if (separator_pos == std::string_view::npos) {
                return std::unexpected{"Invalid path format: missing component separator"};
            }

            auto component_name = path_view.substr(0, separator_pos);
            auto remaining_path = path_view.substr(separator_pos);
            nlohmann::json::json_pointer pointer{std::string{remaining_path}};

            auto result = process_component(component_name, pointer);
            if (!result.error_message.empty()) {
                return std::unexpected{std::move(result.error_message)};
            }
            return result.changed;
        }

        return false;
    } catch (const std::exception& e) {
        return std::unexpected{std::string{"Failed to determine data dependency: "} + e.what()};
    }
}


} // namespace yakka
