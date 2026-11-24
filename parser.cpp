#include "parser.hpp"

#include <iostream>
#include <print>

using namespace std::literals;

namespace {

std::optional<std::string> parse_modifier_tag(std::string_view modifier) {
    constexpr auto prefix = "only in "sv;
    if (!modifier.starts_with(prefix)) return std::nullopt;
    auto rest = modifier.substr(prefix.size());
    constexpr auto tag_prefix = "$Tag:"sv;
    if (rest.starts_with(tag_prefix)) rest = rest.substr(tag_prefix.size());
    return std::string(rest);
}

}

ParseResult parse(const std::vector<Token>& tokens,
                  const std::set<std::string>& active_tags) {
    ParseResult result;
    Project& project = result.project;
    size_t first = 0;
    while (first < tokens.size() &&
           (tokens[first].kind == Token::Kind::Separator || tokens[first].kind == Token::Kind::Tab)) ++first;
    if (first >= tokens.size() || tokens[first].kind != Token::Kind::Lang) {
        std::print(std::cerr, "line {}: file must start with 'lang <name>'\n",
                   first < tokens.size() ? tokens[first].line : 0);
        return result;
    }
    if (tokens[first].content.empty()) {
        std::print(std::cerr, "line {}: 'lang' requires a name\n", tokens[first].line);
        return result;
    }

    std::optional<std::string> pending_tag;
    std::string current_property;
    std::vector<std::string>* target = nullptr;
    bool next_src_is_include = false;
    bool pending_set_as_default = false;
    bool last_was_value = false;
    bool at_line_start = true;
    bool seen_out = false, seen_compiler = false, seen_lang = false;
    bool property_has_value = false, property_skipped = false;

    auto is_active = [&](const std::string& tag) { return active_tags.contains(tag); };
    auto select_target = [&]() -> std::vector<std::string>* {
        if (current_property == "src") return &project.src_files;
        if (current_property == "lib") return &project.libs;
        if (current_property == "opts") return &project.opts;
        if (current_property == "linkopts") return &project.linkopts;
        if (current_property == "run") return &project.run_cmds;
        return nullptr;
    };
    auto property_needs_values = [](const std::string& property) {
        return property == "src" || property == "lib" || property == "opts" ||
               property == "linkopts" || property == "run";
    };
    auto flush_property = [&]() {
        if (current_property.empty()) return true;
        if (property_needs_values(current_property) && !property_has_value && !property_skipped) {
            std::print(std::cerr, "property '{}' has no values\n", current_property);
            return false;
        }
        return true;
    };

    for (size_t index = first; index < tokens.size(); ++index) {
        const auto& token = tokens[index];
        if (token.kind == Token::Kind::ErrorToken) return result;
        if (token.kind == Token::Kind::Tab) {
            std::print(std::cerr, "line {}: unexpected tab\n", token.line);
            return result;
        }
        if (token.kind == Token::Kind::Separator) {
            last_was_value = false;
            if (token.content.empty()) at_line_start = true;
            continue;
        }
        if (token.kind == Token::Kind::Modifier) {
            if (token.content == "include dir") {
                if (current_property != "src") {
                    std::print(std::cerr, "line {}: [include dir] only valid inside 'src' and it's '{}'\n",
                               token.line, current_property.empty() ? "(none)" : current_property);
                    return result;
                }
                if (property_skipped) { at_line_start = false; continue; }
                if (next_src_is_include) {
                    std::print(std::cerr, "line {}: duplicate [include dir]\n", token.line);
                    return result;
                }
                if (last_was_value) {
                    if (project.src_files.empty()) {
                        std::print(std::cerr, "line {}: [include dir] has no previous source value\n", token.line);
                        return result;
                    }
                    project.include_dirs.push_back(std::move(project.src_files.back()));
                    project.src_files.pop_back();
                } else next_src_is_include = true;
                at_line_start = false;
                continue;
            }
            if (token.content == "set as default") {
                pending_set_as_default = true;
                at_line_start = false;
                continue;
            }
            if (!at_line_start && !current_property.empty()) {
                std::print(std::cerr, "line {}: modifier after property '{}' on the same line\n",
                           token.line, current_property);
                return result;
            }
            auto tag = parse_modifier_tag(token.content);
            if (!tag) {
                std::print(std::cerr, "line {}: unknown modifier '{}'\n", token.line, token.content);
                return result;
            }
            pending_tag = std::move(tag);
            at_line_start = false;
            continue;
        }
        if (token.kind == Token::Kind::Lang) {
            if (seen_lang) {
                std::print(std::cerr, "line {}: duplicate 'lang'\n", token.line);
                return result;
            }
            seen_lang = true;
            project.lang = token.content;
            at_line_start = false;
            continue;
        }
        if (token.kind == Token::Kind::Property) {
            if (!flush_property()) return result;
            current_property = token.content;
            target = select_target();
            if (current_property == "out" || current_property == "compiler") target = nullptr;
            property_has_value = false;
            property_skipped = pending_tag && !is_active(*pending_tag);
            pending_tag.reset();
            if (pending_set_as_default && current_property == "compiler") project.compiler_is_default = true;
            pending_set_as_default = false;
            next_src_is_include = false;
            last_was_value = false;
            at_line_start = false;
            continue;
        }
        if (token.kind != Token::Kind::Val) continue;
        if (property_skipped) {
            last_was_value = false;
            at_line_start = false;
            next_src_is_include = false;
            continue;
        }
        if (pending_tag && !is_active(*pending_tag)) {
            last_was_value = true;
            at_line_start = false;
            next_src_is_include = false;
            continue;
        }
        if (last_was_value) {
            std::print(std::cerr, "line {}: value '{}' without separator after previous value\n",
                       token.line, token.content);
            return result;
        }
        if (next_src_is_include) {
            project.include_dirs.push_back(token.content);
            next_src_is_include = false;
            property_has_value = true;
            last_was_value = true;
            at_line_start = false;
            continue;
        }
        if (current_property.empty()) {
            std::print(std::cerr, "line {}: value '{}' without property\n", token.line, token.content);
            return result;
        }
        if (current_property == "out") {
            if (seen_out) { std::print(std::cerr, "line {}: duplicate 'out'\n", token.line); return result; }
            seen_out = true;
            project.out = token.content;
        } else if (current_property == "compiler") {
            if (seen_compiler) { std::print(std::cerr, "line {}: duplicate 'compiler'\n", token.line); return result; }
            seen_compiler = true;
            project.compiler = token.content;
        } else if (target) {
            target->push_back(token.content);
        } else {
            std::print(std::cerr, "line {}: property '{}' has no value slot\n", token.line, current_property);
            return result;
        }
        property_has_value = true;
        last_was_value = true;
        at_line_start = false;
    }

    if (!flush_property()) return result;
    if (next_src_is_include) {
        std::print(std::cerr, "[include dir] without following value\n");
        return result;
    }
    if (!seen_out) { std::print(std::cerr, "missing 'out:'\n"); return result; }
    if (project.src_files.empty()) { std::print(std::cerr, "no source files\n"); return result; }
    result.ok = true;
    return result;
}
