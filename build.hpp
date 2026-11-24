#pragma once

#include "blio.hpp"
#include "defaults.hpp"

std::string build_command(const Project& project, const CompilerSpec& spec);

struct RunResult { int exit_code = -1; };
RunResult run_command(const std::string& command);
