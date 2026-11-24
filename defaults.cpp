#include "defaults.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <print>

std::optional<json> load_defaults_json(const fs::path& mymake_path) {
    const fs::path candidates[] = {
        mymake_path.parent_path() / ".blio.defaults",
        fs::path(".blio.defaults"),
    };
    for (const auto& path : candidates) {
        std::ifstream file(path);
        if (!file) continue;
        try {
            return json::parse(file);
        } catch (const json::parse_error& error) {
            std::print(std::cerr, "error parsing {}: {}\n", path.string(), error.what());
            return std::nullopt;
        }
    }
    return std::nullopt;
}

fs::path defaults_path_for(const fs::path& mymake_path) {
    return mymake_path.parent_path() / ".blio.defaults";
}

std::optional<json> create_defaults_json(const fs::path& mymake_path,
                                         const Project& project) {
    const auto path = defaults_path_for(mymake_path);
    json root = {{"compilers", json::object()}, {"defaults", json::object()}};
    if (project.compiler_is_default && !project.compiler.empty())
        root["defaults"][project.lang] = project.compiler;

    std::ofstream file(path);
    if (!file) {
        std::print(std::cerr, "can't create '{}': {}\n", path.string(), std::strerror(errno));
        return std::nullopt;
    }
    file << root.dump(2) << '\n';
    if (!file) {
        std::print(std::cerr, "can't write '{}': {}\n", path.string(), std::strerror(errno));
        return std::nullopt;
    }
    return root;
}

Defaults parse_defaults(const json& root) {
    Defaults defaults;
    if (root.contains("compilers")) {
        for (const auto& [name, compiler] : root["compilers"].items()) {
            CompilerSpec spec;
            if (compiler.contains("flags")) {
                for (const auto& [property, flag] : compiler["flags"].items()) {
                    spec.flags[property] = {
                        std::string(trim(flag.value("prefix", ""))),
                        std::string(trim(flag.value("separator", "")))
                    };
                }
            }
            defaults.compilers[name] = std::move(spec);
        }
    }
    if (root.contains("defaults")) {
        for (const auto& [language, compiler] : root["defaults"].items())
            if (compiler.is_string()) defaults.lang_to_compiler[language] = compiler.get<std::string>();
    }
    return defaults;
}

CompilerSpec spec_for(const Defaults& defaults, const std::string& compiler) {
    if (auto it = defaults.compilers.find(compiler); it != defaults.compilers.end()) return it->second;
    if (auto it = defaults.compilers.find("gcc"); it != defaults.compilers.end()) return it->second;
    if (auto it = defaults.compilers.find("clang"); it != defaults.compilers.end()) return it->second;
    CompilerSpec spec;
    spec.flags["src"] = {"", " "};
    spec.flags["include_dir"] = {"-I", ""};
    spec.flags["lib"] = {"-l", ""};
    spec.flags["out"] = {"-o", " "};
    spec.flags["opts"] = {"", " "};
    spec.flags["linkopts"] = {"", " "};
    return spec;
}
