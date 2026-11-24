#pragma once

#include "blio.hpp"

#include <optional>

struct ParseResult {
    bool ok = false;
    Project project;
};

ParseResult parse(const std::vector<Token>& tokens,
                  const std::set<std::string>& active_tags);
