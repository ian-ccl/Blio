#include <span>
#include <string>
#include <string_view>
#include <iostream>
#include <print>
#include <fstream>
#include <sstream>
#include <vector>
#include <set>
#include <map>
#include <optional>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <nlohmann/json.hpp>

using namespace std::literals;
namespace fs = std::filesystem;
using json = nlohmann::json;

// ===================== Tokens =====================

struct Token {
    enum class Kind {
        Modifier, Property, Val, Separator, Lang, Tab, ErrorToken
    } kind;
    std::string content;
    size_t line = 0;
};

// ===================== Utilidades =====================

inline std::string read_file(const std::ifstream& f) {
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

// ===================== Lexer =====================

std::vector<Token> lex(std::string_view code) {
    std::vector<Token> res;
    size_t i = 0;
    size_t line = 1;

    auto continue_with = [&code, &i](std::string_view s) {
        if (i + s.size() <= code.size() && code.substr(i, s.size()) == s) {
            i += s.size();
            return true;
        }
        return false;
    };

    for (; i < code.size(); i++) {
        if (code[i] == '[') {
            size_t start_line = line;
            std::string modifier;
            i++;
            for (; i < code.size() && code[i] != ']'; i++) {
                if (code[i] == '\n') line++;
                modifier += code[i];
            }
            if (i >= code.size()) {
                std::print(std::cerr, "line {}: unclosed modifier '['\n", start_line);
                return {{Token::Kind::ErrorToken, "", start_line}};
            }
            res.push_back({Token::Kind::Modifier, std::move(modifier), start_line});
            continue;
        }
        if (continue_with("compiler:")) { res.push_back({Token::Kind::Property, "compiler", line}); continue; }
        if (continue_with("lib:"))      { res.push_back({Token::Kind::Property, "lib",      line}); continue; }
        if (continue_with("out:"))      { res.push_back({Token::Kind::Property, "out",      line}); continue; }
        if (continue_with("src:"))      { res.push_back({Token::Kind::Property, "src",      line}); continue; }
        if (continue_with("opts:"))     { res.push_back({Token::Kind::Property, "opts",     line}); continue; }
        if (continue_with("linkopts:")) { res.push_back({Token::Kind::Property, "linkopts", line}); continue; }
        if (continue_with("run:"))      { res.push_back({Token::Kind::Property, "run",      line}); continue; }
        if (continue_with("lang")) {
            size_t start_line = line;
            std::string lang;
            while (i < code.size() && isspace(static_cast<unsigned char>(code[i])) && code[i] != '\n') i++;
            while (i < code.size() && !isspace(static_cast<unsigned char>(code[i])))
                lang += code[i++];
            res.push_back({Token::Kind::Lang, std::move(lang), start_line});
            continue;
        }
        if (code[i] == '\n' || (i + 1 < code.size() && code[i] == '\r' && code[i + 1] == '\n')) {
            if (code[i] == '\r') i++;
            res.push_back({Token::Kind::Separator, "", line});
            line++;
            continue;
        }
        if (code[i] == ',') { res.push_back({Token::Kind::Separator, ",", line}); continue; }
        if (code[i] == '\t') { res.push_back({Token::Kind::Tab, "", line}); continue; }
        if (code[i] == '"') {
            size_t start_line = line;
            std::string val;
            i++;
            for (; i < code.size() && code[i] != '"'; i++) {
                if (code[i] == '\n') {
                    std::print(std::cerr, "line {}: unclosed string\n", start_line);
                    return {{Token::Kind::ErrorToken, "", start_line}};
                }
                val += code[i];
            }
            if (i >= code.size()) {
                std::print(std::cerr, "line {}: unclosed string at EOF\n", start_line);
                return {{Token::Kind::ErrorToken, "", start_line}};
            }
            res.push_back({Token::Kind::Val, std::move(val), start_line});
            continue;
        }
        if (continue_with("//")) {
            while (i < code.size() && code[i] != '\n') i++;
            if (i < code.size()) i--;
        }
    }
    return res;
}

// ===================== Modelo =====================

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
};

// ===================== Defaults =====================

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

// Devuelve el JSON parseado de .blio.defaults, o nullopt si:
//   - no hay archivo en ninguna de las ubicaciones candidatas, o
//   - el archivo existe pero el JSON es inválido (en ese caso imprime el error).
// Un .blio.defaults inválido se trata como "sin defaults"; el build va a fallar
// después si el lang no tiene compilador. Esto es deliberado: no queremos que un
// defaults roto impida usar un .blio que especifica compiler: explícitamente.
std::optional<json> load_defaults_json(const fs::path& mymake_path) {
    fs::path candidates[] = {
        mymake_path.parent_path() / ".blio.defaults",
        fs::path(".blio.defaults"),
    };
    for (const auto& p : candidates) {
        std::ifstream f(p);
        if (!f) continue;
        try {
            return json::parse(f);
        } catch (const json::parse_error& e) {
            std::print(std::cerr, "error parsing {}: {}\n", p.string(), e.what());
            return std::nullopt;
        }
    }
    return std::nullopt;
}

Defaults parse_defaults(const json& root) {
    Defaults d;
    if (root.contains("compilers")) {
        for (const auto& [name, cval] : root["compilers"].items()) {
            CompilerSpec spec;
            if (cval.contains("flags")) {
                for (const auto& [prop, fval] : cval["flags"].items()) {
                    FlagSpec fspec;
                    fspec.prefix    = std::string(trim(fval.value("prefix", "")));
                    fspec.separator = std::string(trim(fval.value("separator", "")));
                    spec.flags[prop] = std::move(fspec);
                }
            }
            d.compilers[name] = std::move(spec);
        }
    }
    if (root.contains("defaults")) {
        for (const auto& [lang, cval] : root["defaults"].items()) {
            if (cval.is_string())
                d.lang_to_compiler[lang] = cval.get<std::string>();
        }
    }
    return d;
}

CompilerSpec spec_for(const Defaults& d, const std::string& compiler) {
    if (auto it = d.compilers.find(compiler); it != d.compilers.end()) return it->second;
    if (auto it = d.compilers.find("gcc"); it != d.compilers.end())   return it->second;
    if (auto it = d.compilers.find("clang"); it != d.compilers.end()) return it->second;
    CompilerSpec s;
    s.flags["src"]         = {"", " "};
    s.flags["include_dir"] = {"-I", ""};
    s.flags["lib"]         = {"-l", ""};
    s.flags["out"]         = {"-o", " "};
    s.flags["opts"]        = {"", " "};
    s.flags["linkopts"]    = {"", " "};
    return s;
}

// ===================== Parser =====================

std::optional<std::string> parse_modifier_tag(std::string_view m) {
    constexpr auto prefix = "only in "sv;
    if (!m.starts_with(prefix)) return std::nullopt;
    auto rest = m.substr(prefix.size());
    constexpr auto tag_prefix = "$Tag:"sv;
    if (rest.starts_with(tag_prefix)) rest = rest.substr(tag_prefix.size());
    return std::string(rest);
}

struct ParseResult {
    bool ok = false;
    Project project;
};

ParseResult parse(const std::vector<Token>& tokens,
                  const std::set<std::string>& active_tags) {
    ParseResult result;
    Project& out = result.project;

    size_t first = 0;
    while (first < tokens.size() &&
           (tokens[first].kind == Token::Kind::Separator || tokens[first].kind == Token::Kind::Tab))
        first++;
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
    bool last_was_val = false;
    bool at_line_start = true;
    bool seen_out = false, seen_compiler = false, seen_lang = false;
    bool current_property_has_value = false;
    bool current_property_skipped = false;

    auto is_active = [&](const std::string& tag) { return active_tags.count(tag) > 0; };
    auto select_target = [&]() -> std::vector<std::string>* {
        if (current_property == "src")      return &out.src_files;
        if (current_property == "lib")      return &out.libs;
        if (current_property == "opts")     return &out.opts;
        if (current_property == "linkopts") return &out.linkopts;
        if (current_property == "run")      return &out.run_cmds;
        return nullptr;
    };
    auto property_needs_values = [](const std::string& p) {
        return p == "src" || p == "lib" || p == "opts" ||
               p == "linkopts" || p == "run";
    };
    auto flush_property = [&]() -> bool {
        if (current_property.empty()) return true;
        if (property_needs_values(current_property)
            && !current_property_has_value
            && !current_property_skipped) {
            std::print(std::cerr, "property '{}' has no values\n", current_property);
            return false;
        }
        return true;
    };

    for (size_t k = first; k < tokens.size(); k++) {
        const auto& t = tokens[k];
        if (t.kind == Token::Kind::ErrorToken) return result;

        if (t.kind == Token::Kind::Tab) {
            std::print(std::cerr, "line {}: unexpected tab\n", t.line);
            return result;
        }

        if (t.kind == Token::Kind::Separator) {
            last_was_val = false;
            if (t.content.empty()) at_line_start = true;  // solo newline, no coma
            continue;
        }

        if (t.kind == Token::Kind::Modifier) {
            // [include dir] aplica al Val anterior o al siguiente dentro de src.
            if (t.content == "include dir") {
                if (current_property != "src") {
                    std::print(std::cerr, "line {}: [include dir] only valid inside 'src' and it's '{}'\n",
                               t.line, current_property.empty() ? "(none)" : current_property);
                    return result;
                }
                if (current_property_skipped) {
                    at_line_start = false;
                    continue;
                }
                if (next_src_is_include) {
                    std::print(std::cerr, "line {}: duplicate [include dir]\n", t.line);
                    return result;
                }
                if (last_was_val) {
                    if (out.src_files.empty()) {
                        std::print(std::cerr, "line {}: [include dir] has no previous source value\n", t.line);
                        return result;
                    }
                    out.include_dirs.push_back(std::move(out.src_files.back()));
                    out.src_files.pop_back();
                    at_line_start = false;
                    continue;
                }
                next_src_is_include = true;
                at_line_start = false;
                continue;
            }
            // [set as default]: marcador semántico sin efecto en el MVP
            if (t.content == "set as default") {
                at_line_start = false;
                continue;
            }
            // modifier de tag: solo válido al principio de línea
            if (!at_line_start && !current_property.empty()) {
                std::print(std::cerr,
                    "line {}: modifier after property '{}' on the same line\n",
                    t.line, current_property);
                return result;
            }
            auto tag = parse_modifier_tag(t.content);
            if (!tag) {
                std::print(std::cerr, "line {}: unknown modifier '{}'\n", t.line, t.content);
                return result;
            }
            pending_tag = std::move(tag);
            at_line_start = false;
            continue;
        }

        if (t.kind == Token::Kind::Lang) {
            if (seen_lang) {
                std::print(std::cerr, "line {}: duplicate 'lang'\n", t.line);
                return result;
            }
            seen_lang = true;
            out.lang = t.content;
            at_line_start = false;
            continue;
        }

        if (t.kind == Token::Kind::Property) {
            if (!flush_property()) return result;
            current_property = t.content;
            target = select_target();
            if (current_property == "out" || current_property == "compiler")
                target = nullptr;
            current_property_has_value = false;
            current_property_skipped = (pending_tag && !is_active(*pending_tag));
            pending_tag.reset();
            next_src_is_include = false;
            last_was_val = false;
            at_line_start = false;
            continue;
        }

        if (t.kind == Token::Kind::Val) {
            if (current_property_skipped) {
                last_was_val = false;
                at_line_start = false;
                next_src_is_include = false;
                continue;
            }
            if (pending_tag && !is_active(*pending_tag)) {
                last_was_val = true;
                at_line_start = false;
                next_src_is_include = false;
                continue;
            }
            if (last_was_val) {
                std::print(std::cerr,
                    "line {}: value '{}' without separator after previous value\n",
                    t.line, t.content);
                return result;
            }
            if (next_src_is_include) {
                out.include_dirs.push_back(t.content);
                next_src_is_include = false;
                current_property_has_value = true;
                last_was_val = true;
                at_line_start = false;
                continue;
            }
            if (current_property.empty()) {
                std::print(std::cerr, "line {}: value '{}' without property\n",
                           t.line, t.content);
                return result;
            }
            if (current_property == "out") {
                if (seen_out) {
                    std::print(std::cerr, "line {}: duplicate 'out'\n", t.line);
                    return result;
                }
                seen_out = true;
                out.out = t.content;
            } else if (current_property == "compiler") {
                if (seen_compiler) {
                    std::print(std::cerr, "line {}: duplicate 'compiler'\n", t.line);
                    return result;
                }
                seen_compiler = true;
                out.compiler = t.content;
            } else if (target) {
                target->push_back(t.content);
            } else {
                std::print(std::cerr, "line {}: property '{}' has no value slot\n",
                           t.line, current_property);
                return result;
            }
            current_property_has_value = true;
            last_was_val = true;
            at_line_start = false;
            continue;
        }
    }

    if (!flush_property()) return result;
    if (next_src_is_include) {
        std::print(std::cerr, "[include dir] without following value\n");
        return result;
    }
    if (!seen_out) {
        std::print(std::cerr, "missing 'out:'\n");
        return result;
    }
    if (out.src_files.empty()) {
        std::print(std::cerr, "no source files\n");
        return result;
    }
    result.ok = true;
    return result;
}

// ===================== Generador =====================

std::string emit_flag(const FlagSpec& spec, const std::string& value) {
    return spec.prefix + spec.separator + value;
}

std::string build_command(const Project& p, const CompilerSpec& spec) {
    auto flag = [&](const std::string& prop) -> FlagSpec {
        auto it = spec.flags.find(prop);
        if (it != spec.flags.end()) return it->second;
        return {"", " "};
    };

    std::ostringstream cmd;
    cmd << p.compiler;
    for (const auto& s : p.src_files)    cmd << ' ' << emit_flag(flag("src"), s);
    for (const auto& d : p.include_dirs) cmd << ' ' << emit_flag(flag("include_dir"), d);
    for (const auto& l : p.libs)         cmd << ' ' << emit_flag(flag("lib"), l);
    for (const auto& o : p.opts)         cmd << ' ' << emit_flag(flag("opts"), o);
    for (const auto& o : p.linkopts)     cmd << ' ' << emit_flag(flag("linkopts"), o);
    if (!p.out.empty())                  cmd << ' ' << emit_flag(flag("out"), p.out);
    return cmd.str();
}

// ===================== Ejecución =====================

struct RunResult { int exit_code = -1; };

RunResult run_command(const std::string& cmd) {
    RunResult r;
    r.exit_code = std::system(cmd.c_str());
#if !defined(_WIN32)
    if (r.exit_code != -1) r.exit_code = WEXITSTATUS(r.exit_code);
#endif
    return r;
}

// ===================== main =====================

void print_usage(const char* prog) {
    std::print(std::cerr,
        "usage: {} [options] file.blio\n"
        "\n"
        "options:\n"
        "  --os-windows       target Windows (filters [only in Windows])\n"
        "  --os-linux         target Linux (filters [only in Linux])\n"
        "  --add-tag TAG      activate a user tag (for [only in $Tag:TAG])\n"
        "  --dry-run          print the command, don't execute it\n"
        "  --verbose          print the command before executing\n"
        "  --list-defaults    print parsed .blio.defaults and exit\n"
        "  --print-version    print version and exit\n"
        "  --                 end of options\n",
        prog);
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

    std::set<std::string> active_tags;
    active_tags.insert(real_os);

    std::string file;
    bool flags_done = false;
    bool forced_windows = false, forced_linux = false;
    bool dry_run = false, verbose = false;

    for (size_t i = 1; i < args.size(); i++) {
        std::string_view a = args[i];
        if (!flags_done && a == "--") { flags_done = true; continue; }
        if (!flags_done && a == "--os-windows") {
            if (forced_linux) { std::print(std::cerr, "can't force both OS\n"); return 1; }
            forced_windows = true;
            active_tags.erase("Linux"); active_tags.erase("Unknown");
            active_tags.insert("Windows");
            continue;
        }
        if (!flags_done && a == "--os-linux") {
            if (forced_windows) { std::print(std::cerr, "can't force both OS\n"); return 1; }
            forced_linux = true;
            active_tags.erase("Windows"); active_tags.erase("Unknown");
            active_tags.insert("Linux");
            continue;
        }
        if (!flags_done && a == "--add-tag") {
            if (i + 1 >= args.size()) { std::print(std::cerr, "--add-tag needs a value\n"); return 1; }
            active_tags.insert(args[++i]);
            continue;
        }
        if (!flags_done && a == "--dry-run")  { dry_run = true;  continue; }
        if (!flags_done && a == "--verbose")  { verbose = true;  continue; }
        if (!flags_done && a == "--print-version") {
            std::print("blio 0.1.0\n");
            return 0;
        }
        if (!flags_done && a == "--list-defaults") {
            auto root = load_defaults_json(fs::path(file.empty() ? "." : file));
            if (!root) { std::print("(no .blio.defaults found)\n"); return 0; }
            auto d = parse_defaults(*root);
            std::print("compilers:\n");
            for (const auto& [name, spec] : d.compilers) {
                std::print("  {}:\n", name);
                for (const auto& [prop, fspec] : spec.flags)
                    std::print("    {} -> prefix='{}' sep='{}'\n", prop, fspec.prefix, fspec.separator);
            }
            std::print("defaults:\n");
            for (const auto& [lang, comp] : d.lang_to_compiler)
                std::print("  {} -> {}\n", lang, comp);
            return 0;
        }
        if (!file.empty()) { std::print(std::cerr, "multiple input files\n"); return 1; }
        file = a;
    }

    if (file.empty()) { std::print(std::cerr, "no input file\n"); return 1; }

    std::ifstream f(file);
    if (!f) { std::print(std::cerr, "can't open '{}'\n", file); return 1; }
    std::string code = read_file(f);

    auto tokens = lex(code);
    auto result = parse(tokens, active_tags);
    if (!result.ok) return 1;
    Project& p = result.project;

    Defaults defaults;
    if (auto root = load_defaults_json(fs::path(file)))
        defaults = parse_defaults(*root);

    if (p.compiler.empty()) {
        if (auto it = defaults.lang_to_compiler.find(p.lang); it != defaults.lang_to_compiler.end())
            p.compiler = it->second;
        else {
            std::print(std::cerr, "no 'compiler:' and '{}' not in .blio.defaults\n", p.lang);
            return 1;
        }
    }

    auto spec = spec_for(defaults, p.compiler);
    std::string cmd = build_command(p, spec);

    if (dry_run) {
        std::print("{}\n", cmd);
        for (const auto& r : p.run_cmds) std::print("run: {}\n", r);
        return 0;
    }
    if (verbose) std::print("$ {}\n", cmd);

    auto start = std::chrono::steady_clock::now();
    auto r = run_command(cmd);
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    if (r.exit_code != 0) {
        std::print(std::cerr, "build failed with exit code {} ({} ms)\n", r.exit_code, ms);
        return 1;
    }
    std::print("build ok ({} ms)\n", ms);

    for (const auto& rc : p.run_cmds) {
        if (verbose) std::print("$ {}\n", rc);
        auto rr = run_command(rc);
        if (rr.exit_code != 0) {
            std::print(std::cerr, "run failed ({}): exit code {}\n", rc, rr.exit_code);
            return 1;
        }
    }
    return 0;
}