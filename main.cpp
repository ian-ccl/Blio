#include "build.hpp"
#include "defaults.hpp"
#include "lexer.hpp"
#include "parser.hpp"

#include <chrono>
#include <iostream>
#include <print>
#include <span>
#include <filesystem>

static const char* version = "0.4.0";

namespace fs = std::filesystem;

void print_usage(const char* program) {
    std::print(std::cerr,
        "usage: {} [options] file.blio\n\n"
        "options:\n"
        "  --os-windows       target Windows (filters [only in Windows])\n"
        "  --os-linux         target Linux (filters [only in Linux])\n"
        "  --add-tag TAG      activate a user tag (for [only in $Tag:TAG])\n"
        "  --dry-run          print the command, don't execute it\n"
        "  --verbose          print the command before executing\n"
        "  --list-defaults    print parsed .blio.defaults and exit\n"
        "  --print-version    print version and exit\n"
        "  --                 end of options\n", program);
}

int main(int argc, char** argv) {
    std::span args = std::span<char*>{argv, static_cast<size_t>(argc)};
    if (args.size() < 2) { print_usage(args[0]); return 1; }

    std::string real_os =
#if defined(_WIN32)
        "Windows";
#elif defined(__linux__)
        "Linux";
#else
        "Unknown";
#endif
    std::set<std::string> active_tags{real_os};
    std::string file;
    bool flags_done = false;
    bool forced_windows = false, forced_linux = false;
    bool dry_run = false, verbose = false;

    for (size_t i = 1; i < args.size(); ++i) {
        std::string_view argument = args[i];
        if (!flags_done && argument == "--") { flags_done = true; continue; }
        if (!flags_done && argument == "--os-windows") {
            if (forced_linux) { std::print(std::cerr, "can't force both OS\n"); return 1; }
            forced_windows = true;
            active_tags.erase("Linux"); active_tags.erase("Unknown"); active_tags.insert("Windows");
            continue;
        }
        if (!flags_done && argument == "--os-linux") {
            if (forced_windows) { std::print(std::cerr, "can't force both OS\n"); return 1; }
            forced_linux = true;
            active_tags.erase("Windows"); active_tags.erase("Unknown"); active_tags.insert("Linux");
            continue;
        }
        if (!flags_done && argument == "--add-tag") {
            if (i + 1 >= args.size()) { std::print(std::cerr, "--add-tag needs a value\n"); return 1; }
            active_tags.insert(args[++i]);
            continue;
        }
        if (!flags_done && argument == "--dry-run") { dry_run = true; continue; }
        if (!flags_done && argument == "--verbose") { verbose = true; continue; }
        if (!flags_done && argument == "--print-version" || argument == "--version") {
            std::print("blio {}\n", version);
            return 0;
        }
        if (!flags_done && argument == "--list-defaults") {
            auto root = load_defaults_json(fs::path(file.empty() ? "." : file));
            if (!root) { std::print("(no .blio.defaults found)\n"); return 0; }
            auto defaults = parse_defaults(*root);
            std::print("compilers:\n");
            for (const auto& [name, spec] : defaults.compilers) {
                std::print("  {}:\n", name);
                for (const auto& [property, flag] : spec.flags)
                    std::print("    {} -> prefix='{}' sep='{}'\n", property, flag.prefix, flag.separator);
            }
            std::print("defaults:\n");
            for (const auto& [language, compiler] : defaults.lang_to_compiler)
                std::print("  {} -> {}\n", language, compiler);
            return 0;
        }
        if (!file.empty()) { std::print(std::cerr, "multiple input files\n"); return 1; }
        file = argument;
    }
    if (file.empty()) { std::print(std::cerr, "no input file\n"); return 1; }

    std::ifstream input(file);
    if (!input) { std::print(std::cerr, "can't open '{}'\n", file); return 1; }
    auto result = parse(lex(read_file(input)), active_tags);
    if (!result.ok) return 1;
    Project project = result.project;

    auto defaults_root = load_defaults_json(fs::path(file));
    if (!defaults_root) {
        const bool defaults_exists = fs::exists(defaults_path_for(fs::path(file))) ||
                                     fs::exists(fs::path(".blio.defaults"));
        if (!defaults_exists) defaults_root = create_defaults_json(fs::path(file), project);
    }
    Defaults defaults;
    if (defaults_root) defaults = parse_defaults(*defaults_root);

    if (project.compiler.empty()) {
        if (auto it = defaults.lang_to_compiler.find(project.lang); it != defaults.lang_to_compiler.end())
            project.compiler = it->second;
        else {
            std::print(std::cerr, "no 'compiler:' and '{}' not in .blio.defaults\n", project.lang);
            return 1;
        }
    }

    auto command = build_command(project, spec_for(defaults, project.compiler));
    if (dry_run) {
        std::print("{}\n", command);
        for (const auto& run : project.run_cmds) std::print("run: {}\n", run);
        return 0;
    }
    if (verbose) std::print("$ {}\n", command);
    const auto start = std::chrono::steady_clock::now();
    auto build_result = run_command(command);
    const auto end = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    if (build_result.exit_code != 0) {
        std::print(std::cerr, "build failed with exit code {} ({} ms)\n", build_result.exit_code, ms);
        return 1;
    }
    std::print("build ok ({} ms)\n", ms);
    for (const auto& run_command_text : project.run_cmds) {
        if (verbose) std::print("$ {}\n", run_command_text);
        auto run_result = run_command(run_command_text);
        if (run_result.exit_code != 0) {
            std::print(std::cerr, "run failed ({}): exit code {}\n", run_command_text, run_result.exit_code);
            return 1;
        }
    }
    return 0;
}
