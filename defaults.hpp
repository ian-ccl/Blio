#pragma once

#include "blio.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

struct FlagSpec {
    std::string prefix;
    std::string separator;
};

struct CompilerSpec {
    std::map<std::string, FlagSpec> flags;
};

struct Defaults {
    std::map<std::string, CompilerSpec> compilers;
    std::map<std::string, std::string> lang_to_compiler;
};

std::optional<json> load_defaults_json(const fs::path& mymake_path);
fs::path defaults_path_for(const fs::path& mymake_path);
std::optional<json> create_defaults_json(const fs::path& mymake_path,
                                         const Project& project);
Defaults parse_defaults(const json& root);
CompilerSpec spec_for(const Defaults& defaults, const std::string& compiler);
