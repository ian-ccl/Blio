#include "lexer.hpp"

#include <cctype>
#include <iostream>
#include <print>

std::string_view trim(std::string_view s) {
    while (!s.empty() && isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::vector<Token> lex(std::string_view code) {
    std::vector<Token> result;
    size_t i = 0;
    size_t line = 1;
    auto consume = [&code, &i](std::string_view text) {
        if (i + text.size() <= code.size() && code.substr(i, text.size()) == text) {
            i += text.size();
            return true;
        }
        return false;
    };

    for (; i < code.size(); ++i) {
        if (code[i] == '[') {
            const size_t start_line = line;
            std::string modifier;
            ++i;
            for (; i < code.size() && code[i] != ']'; ++i) {
                if (code[i] == '\n') ++line;
                modifier += code[i];
            }
            if (i >= code.size()) {
                std::print(std::cerr, "line {}: unclosed modifier '['\n", start_line);
                return {{Token::Kind::ErrorToken, "", start_line}};
            }
            result.push_back({Token::Kind::Modifier, std::move(modifier), start_line});
            continue;
        }
        if (consume("compiler:")) { result.push_back({Token::Kind::Property, "compiler", line}); continue; }
        if (consume("lib:"))      { result.push_back({Token::Kind::Property, "lib", line}); continue; }
        if (consume("out:"))      { result.push_back({Token::Kind::Property, "out", line}); continue; }
        if (consume("src:"))      { result.push_back({Token::Kind::Property, "src", line}); continue; }
        if (consume("opts:"))     { result.push_back({Token::Kind::Property, "opts", line}); continue; }
        if (consume("linkopts:")) { result.push_back({Token::Kind::Property, "linkopts", line}); continue; }
        if (consume("run:"))      { result.push_back({Token::Kind::Property, "run", line}); continue; }
        if (consume("lang")) {
            const size_t start_line = line;
            std::string language;
            while (i < code.size() && isspace(static_cast<unsigned char>(code[i])) && code[i] != '\n') ++i;
            while (i < code.size() && !isspace(static_cast<unsigned char>(code[i]))) language += code[i++];
            result.push_back({Token::Kind::Lang, std::move(language), start_line});
            continue;
        }
        if (code[i] == '\n' || (i + 1 < code.size() && code[i] == '\r' && code[i + 1] == '\n')) {
            if (code[i] == '\r') ++i;
            result.push_back({Token::Kind::Separator, "", line++});
            continue;
        }
        if (code[i] == ',') { result.push_back({Token::Kind::Separator, ",", line}); continue; }
        if (code[i] == '\t') { result.push_back({Token::Kind::Tab, "", line}); continue; }
        if (code[i] == '"') {
            const size_t start_line = line;
            std::string value;
            ++i;
            for (; i < code.size() && code[i] != '"'; ++i) {
                if (code[i] == '\n') {
                    std::print(std::cerr, "line {}: unclosed string\n", start_line);
                    return {{Token::Kind::ErrorToken, "", start_line}};
                }
                value += code[i];
            }
            if (i >= code.size()) {
                std::print(std::cerr, "line {}: unclosed string at EOF\n", start_line);
                return {{Token::Kind::ErrorToken, "", start_line}};
            }
            result.push_back({Token::Kind::Val, std::move(value), start_line});
            continue;
        }
        if (consume("//")) {
            while (i < code.size() && code[i] != '\n') ++i;
            if (i < code.size()) --i;
        }
    }
    return result;
}
