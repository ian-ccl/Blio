#include "build.hpp"

#include <cstdlib>
#include <sstream>
#include <sys/wait.h>

namespace {
std::string emit_flag(const FlagSpec& spec, const std::string& value) {
    return spec.prefix + spec.separator + value;
}
}

std::string build_command(const Project& project, const CompilerSpec& spec) {
    auto flag = [&](const std::string& property) -> FlagSpec {
        if (auto it = spec.flags.find(property); it != spec.flags.end()) return it->second;
        return {"", " "};
    };
    std::ostringstream command;
    command << project.compiler;
    for (const auto& source : project.src_files) command << ' ' << emit_flag(flag("src"), source);
    for (const auto& include : project.include_dirs) command << ' ' << emit_flag(flag("include_dir"), include);
    for (const auto& library : project.libs) command << ' ' << emit_flag(flag("lib"), library);
    for (const auto& option : project.opts) command << ' ' << emit_flag(flag("opts"), option);
    for (const auto& option : project.linkopts) command << ' ' << emit_flag(flag("linkopts"), option);
    if (!project.out.empty()) command << ' ' << emit_flag(flag("out"), project.out);
    return command.str();
}

RunResult run_command(const std::string& command) {
    RunResult result;
    result.exit_code = std::system(command.c_str());
#if !defined(_WIN32)
    if (result.exit_code != -1) result.exit_code = WEXITSTATUS(result.exit_code);
#endif
    return result;
}
