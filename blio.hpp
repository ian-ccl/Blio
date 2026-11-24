#pragma once

#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

struct Token {
    enum class Kind { Modifier, Property, Val, Separator, Lang, Tab, ErrorToken } kind;
    std::string content;
    size_t line = 0;
};

struct Project {
    std::string lang;
    std::string out;
    std::string compiler;
    std::vector<std::string> src_files;
    std::vector<std::string> include_dirs;
    std::vector<std::string> libs;
    std::vector<std::string> opts;
    std::vector<std::string> linkopts;
    std::vector<std::string> run_cmds;
    bool compiler_is_default = false;
};

inline std::string read_file(const std::ifstream& f) {
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string_view trim(std::string_view s);
