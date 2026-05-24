#include "config_cache.h"
#include "log.h"
#ifndef DCFG_CACHE_TOOL
#include "module_dir.h"
#endif

#ifndef DCFG_RUNTIME_ONLY
#include "json_lite.h"
#include "props_mapping.h"
#endif

#ifndef DCFG_RUNTIME_ONLY
#include <sys/stat.h>
#endif
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifndef DCFG_RUNTIME_ONLY
#include <set>
#endif
#ifndef DCFG_RUNTIME_ONLY
#include <sstream>
#endif
#include <string>
#include <utility>
#include <vector>

static constexpr const char *kCacheMagic = "DCFG_CACHE_V1";

static bool read_fd_all(int fd, std::string &out) {
    out.clear();
    char buf[4096];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) return true;
        if (errno == EINTR) continue;
        out.clear();
        return false;
    }
}

#ifndef DCFG_RUNTIME_ONLY
static bool read_file_path(const char *path, std::string &out) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    bool ok = read_fd_all(fd, out);
    close(fd);
    return ok;
}
#endif

#ifdef DCFG_RUNTIME_ONLY
static bool read_module_file(const char *name, std::string &out) {
    int fd = dcfg_open_module_file(name, O_RDONLY, 0644);
    if (fd < 0) return false;
    bool ok = read_fd_all(fd, out);
    close(fd);
    return ok;
}
#endif

#ifndef DCFG_RUNTIME_ONLY
bool dcfg_read_config_file(std::string &out) {
    if (!read_file_path(kDcfgConfigPath, out)) {
        DLOGW("read_config_file failed path=%s errno=%d %s", kDcfgConfigPath, errno, strerror(errno));
        return false;
    }
    return true;
}

static bool write_all(int fd, const char *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

static bool write_file_atomic_path(const char *path, const std::string &data) {
    std::string tmp = std::string(path) + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;

    bool ok = write_all(fd, data.data(), data.size());
    if (ok) fsync(fd);
    close(fd);

    if (!ok) {
        unlink(tmp.c_str());
        return false;
    }
    if (rename(tmp.c_str(), path) != 0) {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}

static bool stat_path(const char *path, uint64_t &size, uint64_t &mtime) {
    struct stat st {};
    if (stat(path, &st) != 0) return false;
    size = static_cast<uint64_t>(st.st_size);
    mtime = static_cast<uint64_t>(st.st_mtime);
    return true;
}

static std::string esc(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c == '%' || c == '|' || c == '\n' || c == '\r') {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string unesc(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hex_value(s[i + 1]);
            int lo = hex_value(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

static std::vector<std::string> split_line(const std::string &line) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t pos = line.find('|', start);
        if (pos == std::string::npos) {
            out.push_back(unesc(line.substr(start)));
            break;
        }
        out.push_back(unesc(line.substr(start, pos - start)));
        start = pos + 1;
    }
    return out;
}

static PropsMode parse_props_mode_cache(const std::string &mode) {
    if (mode == "manual") return PropsMode::MANUAL;
    if (mode == "auto") return PropsMode::AUTO;
    if (mode == "auto_manual") return PropsMode::AUTO_MANUAL;
    return PropsMode::NONE;
}

static const char *props_mode_name_cache(PropsMode mode) {
    switch (mode) {
        case PropsMode::MANUAL: return "manual";
        case PropsMode::AUTO: return "auto";
        case PropsMode::AUTO_MANUAL: return "auto_manual";
        case PropsMode::NONE:
        default: return "none";
    }
}

static PropsHookMode parse_props_hook_mode_cache(const std::string &mode) {
    if (mode == "runtime") return PropsHookMode::RUNTIME;
    if (mode == "full") return PropsHookMode::FULL;
    return PropsHookMode::NONE;
}

static const char *props_hook_mode_name_cache(PropsHookMode mode) {
    switch (mode) {
        case PropsHookMode::RUNTIME: return "runtime";
        case PropsHookMode::FULL: return "full";
        case PropsHookMode::NONE:
        default: return "none";
    }
}

static ResetpropScope parse_resetprop_scope_cache(const std::string &scope) {
    if (scope == "main") return ResetpropScope::MAIN;
    return ResetpropScope::PACKAGE;
}

static const char *resetprop_scope_name_cache(ResetpropScope scope) {
    switch (scope) {
        case ResetpropScope::MAIN: return "main";
        case ResetpropScope::PACKAGE:
        default: return "package";
    }
}

static bool props_mode_uses_manual_cache(PropsMode mode) {
    return mode == PropsMode::MANUAL || mode == PropsMode::AUTO_MANUAL;
}

static bool find_rule_objects(const std::string &json, std::vector<std::string> &rules_out) {
    rules_out.clear();
    auto rules = extract_array_after_key(json, "rules");
    if (rules.empty()) return false;

    size_t pos = 0;
    for (;;) {
        auto brace = rules.find('{', pos);
        if (brace == std::string::npos) break;
        auto obj = extract_object_at(rules, brace);
        if (obj.empty()) break;
        rules_out.push_back(std::move(obj));
        pos = brace + rules_out.back().size();
    }
    return !rules_out.empty();
}

static std::string find_profile_object_cache(const std::string &json, const std::string &profile_name) {
    auto profiles_key = json.find("\"profiles\"");
    if (profiles_key == std::string::npos) return {};
    auto colon = json.find(':', profiles_key);
    if (colon == std::string::npos) return {};
    auto profiles_start = json.find('{', colon);
    auto profiles_end = find_matching_json_token(json, profiles_start, '{', '}');
    if (profiles_start == std::string::npos || profiles_end == std::string::npos) return {};

    const std::string needle = "\"" + profile_name + "\"";
    bool in_string = false;
    bool escape = false;
    int depth = 0;

    for (size_t i = profiles_start; i <= profiles_end && i < json.size(); ++i) {
        char c = json[i];
        if (in_string) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') {
            if (depth == 1 && json.compare(i, needle.size(), needle) == 0) {
                auto colon_after_key = json.find(':', i + needle.size());
                if (colon_after_key == std::string::npos || colon_after_key > profiles_end) return {};
                auto object_start = json.find('{', colon_after_key);
                if (object_start == std::string::npos || object_start > profiles_end) return {};
                return extract_object_at(json, object_start);
            }
            in_string = true;
            continue;
        }
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth <= 0) break;
        }
    }
    return {};
}

static bool starts_with_ro(const std::string &key) {
    return key.size() >= 3 && key[0] == 'r' && key[1] == 'o' && key[2] == '.';
}

static bool is_ignore_value(const std::string &value) {
    return value == "__IGNORE__";
}

static std::string prop_value_to_cache(const PropValue &v) {
    if (v.kind == PropValueKind::EMPTY) return "__EMPTY__";
    if (v.kind == PropValueKind::NULL_VALUE) return "__NULL__";
    return v.value;
}

static PropValue prop_value_from_cache(const std::string &value) {
    PropValue out;
    if (value == "__EMPTY__") {
        out.kind = PropValueKind::EMPTY;
        out.value.clear();
    } else if (value == "__NULL__") {
        out.kind = PropValueKind::NULL_VALUE;
        out.value.clear();
    } else {
        out.kind = PropValueKind::SET;
        out.value = value;
    }
    return out;
}

bool dcfg_rebuild_config_cache_file(const char *config_path, const char *cache_path) {
    if (!config_path || !cache_path) return false;

    uint64_t source_size = 0;
    uint64_t source_mtime = 0;
    if (!stat_path(config_path, source_size, source_mtime)) return false;

    std::string json;
    if (!read_file_path(config_path, json) || json.empty()) return false;

    auto global_obj = extract_object_after_key(json, "global");
    bool hook_children = !global_obj.empty() && find_bool(global_obj, "hook_children", false);
    ResetpropScope resetprop_scope = ResetpropScope::PACKAGE;
    if (!global_obj.empty()) {
        resetprop_scope = parse_resetprop_scope_cache(find_string(global_obj, "resetprop_scope"));
    }
    std::vector<std::string> rule_objs;
    find_rule_objects(json, rule_objs);

    std::ostringstream out;
    std::set<std::string> spoof_keys;
    out << kCacheMagic << "\n";
    out << "meta|source_size|" << source_size << "\n";
    out << "meta|source_mtime|" << source_mtime << "\n";
    out << "global|hook_children|" << (hook_children ? "1" : "0") << "\n";
    out << "global|resetprop_scope|" << resetprop_scope_name_cache(resetprop_scope) << "\n";

    size_t entry_count = 0;
    for (const auto &rule_obj : rule_objs) {
        std::string package_name = find_string(rule_obj, "package");
        std::string profile_name = find_string(rule_obj, "profile");
        if (package_name.empty() || profile_name.empty()) continue;

        std::string po = find_profile_object_cache(json, profile_name);
        if (po.empty()) continue;

        PropsMode source_mode = parse_props_mode_cache(find_string(rule_obj, "mode"));
        PropsHookMode hook_mode = parse_props_hook_mode_cache(find_string(rule_obj, "prop_hook"));
        bool resetprop_enabled = find_bool(rule_obj, "resetprop", false);

        Profile source;
        source.props_mode = source_mode;
        source.props_hook_mode = hook_mode;
        source.resetprop_enabled = resetprop_enabled;
        parse_scalar_object(extract_object_after_key(po, "build"), source.build);
        parse_scalar_object(extract_object_after_key(po, "version_info"), source.version_info);
        if (props_mode_uses_manual_cache(source_mode)) {
            parse_string_object(extract_object_after_key(po, "props"), source.props);
        }

        // This is the only auto_props/effective-props expansion point in the cache path.
        PropMap effective = dcfg_make_final_props(source);

        out << "entry|" << esc(package_name) << "|" << esc(profile_name) << "|"
            << props_mode_name_cache(source_mode) << "|"
            << props_hook_mode_name_cache(hook_mode) << "|"
            << (resetprop_enabled ? "1" : "0") << "\n";

        for (const auto &it : source.build) {
            if (!it.first.empty() && !it.second.empty() && !is_ignore_value(it.second)) {
                out << "build|" << esc(it.first) << "|" << esc(it.second) << "\n";
            }
        }
        for (const auto &it : source.version_info) {
            if (!it.first.empty() && !it.second.empty() && !is_ignore_value(it.second)) {
                out << "version|" << esc(it.first) << "|" << esc(it.second) << "\n";
            }
        }
        for (const auto &it : effective) {
            if (it.first.empty()) continue;
            std::string value = prop_value_to_cache(it.second);
            if (value.empty() || is_ignore_value(value)) continue;
            out << "prop|" << esc(it.first) << "|" << esc(value) << "\n";
            if (starts_with_ro(it.first)) spoof_keys.insert(it.first);
        }
        out << "endentry\n";
        ++entry_count;
    }

    for (const auto &key : spoof_keys) out << "spoof|" << esc(key) << "\n";

    bool ok = write_file_atomic_path(cache_path, out.str());
    if (ok) DLOGI("config cache rebuilt path=%s entries=%zu spoof_keys=%zu", cache_path, entry_count, spoof_keys.size());
    return ok;
}

#endif

#ifdef DCFG_RUNTIME_ONLY
static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string unesc(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hex_value(s[i + 1]);
            int lo = hex_value(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

static std::vector<std::string> split_line(const std::string &line) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t pos = line.find('|', start);
        if (pos == std::string::npos) {
            out.push_back(unesc(line.substr(start)));
            break;
        }
        out.push_back(unesc(line.substr(start, pos - start)));
        start = pos + 1;
    }
    return out;
}

static PropsHookMode parse_props_hook_mode_cache(const std::string &mode) {
    if (mode == "runtime") return PropsHookMode::RUNTIME;
    if (mode == "full") return PropsHookMode::FULL;
    return PropsHookMode::NONE;
}

static ResetpropScope parse_resetprop_scope_runtime(const std::string &scope) {
    if (scope == "main") return ResetpropScope::MAIN;
    return ResetpropScope::PACKAGE;
}

static PropValue prop_value_from_cache(const std::string &value) {
    PropValue out;
    if (value == "__EMPTY__") {
        out.kind = PropValueKind::EMPTY;
        return out;
    }
    if (value == "__NULL__") {
        out.kind = PropValueKind::NULL_VALUE;
        return out;
    }
    out.kind = PropValueKind::SET;
    out.value = value;
    return out;
}
#endif

#ifdef DCFG_RUNTIME_ONLY
static bool next_cache_line(const std::string &text, size_t &pos, std::string &line) {
    if (pos >= text.size()) return false;
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) {
        line.assign(text, pos, std::string::npos);
        pos = text.size();
    } else {
        line.assign(text, pos, end - pos);
        pos = end + 1;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

static bool cache_has_valid_magic(const std::string &cache_text) {
    size_t pos = 0;
    std::string line;
    while (next_cache_line(cache_text, pos, line)) {
        if (line.empty()) continue;
        return line == kCacheMagic;
    }
    return false;
}

static bool read_module_cache(std::string &cache_text) {
    if (!read_module_file(kDcfgConfigCacheName, cache_text)) return false;
    return cache_has_valid_magic(cache_text);
}

bool dcfg_load_and_match_config_cache(
        const std::string &package_name,
        bool is_child_process,
        MatchResult &out
) {
    out = MatchResult();
    out.package_name = package_name;

    std::string cache;
    if (!read_module_cache(cache)) return false;

    size_t pos = 0;
    std::string line;
    bool in_target_entry = false;
    bool found = false;
    bool global_hook_children = false;
    ResetpropScope global_resetprop_scope = ResetpropScope::PACKAGE;
    MatchResult result;
    result.package_name = package_name;

    while (next_cache_line(cache, pos, line)) {
        if (line.empty() || line == kCacheMagic) continue;
        auto parts = split_line(line);
        if (parts.empty()) continue;

        if (parts[0] == "global" && parts.size() == 3) {
            if (parts[1] == "hook_children") global_hook_children = parts[2] == "1" || parts[2] == "true";
            else if (parts[1] == "resetprop_scope") global_resetprop_scope = parse_resetprop_scope_runtime(parts[2]);
            continue;
        }

        if (parts[0] == "entry") {
            in_target_entry = false;
            if (parts.size() >= 6 && parts[1] == package_name) {
                found = true;
                in_target_entry = true;
                result.matched = true;
                result.package_name = package_name;
                result.profile_name = parts[2];
                result.hook_children = global_hook_children;
                result.resetprop_scope = global_resetprop_scope;
                // Cached profiles are already effective. Runtime must not auto-map again.
                result.profile.props_mode = PropsMode::MANUAL;
                result.profile.props_hook_mode = parse_props_hook_mode_cache(parts[4]);
                result.profile.resetprop_enabled = parts[5] == "1" || parts[5] == "true";
            }
            continue;
        }

        if (parts[0] == "endentry") {
            if (in_target_entry) break;
            in_target_entry = false;
            continue;
        }
        if (!in_target_entry) continue;

        if (parts.size() >= 3 && parts[0] == "build") {
            result.profile.build[parts[1]] = parts[2];
        } else if (parts.size() >= 3 && parts[0] == "version") {
            result.profile.version_info[parts[1]] = parts[2];
        } else if (parts.size() >= 3 && parts[0] == "prop") {
            PropValue v = prop_value_from_cache(parts[2]);
            // Profile::props stores string source values; dcfg_make_final_props will convert markers.
            if (v.kind == PropValueKind::EMPTY) result.profile.props[parts[1]] = "__EMPTY__";
            else if (v.kind == PropValueKind::NULL_VALUE) result.profile.props[parts[1]] = "__NULL__";
            else result.profile.props[parts[1]] = v.value;
        }
    }

    if (!found) {
        out.hook_children = global_hook_children;
        out.resetprop_scope = global_resetprop_scope;
        return true;
    }

    if (is_child_process && !global_hook_children) {
        out = MatchResult();
        out.package_name = package_name;
        out.hook_children = global_hook_children;
        out.resetprop_scope = global_resetprop_scope;
        return true;
    }

    out = std::move(result);
    return true;
}

bool dcfg_read_cache_spoof_keys(std::vector<std::string> &out) {
    out.clear();
    std::string cache;
    if (!read_module_cache(cache)) return false;
    size_t pos = 0;
    std::string line;
    while (next_cache_line(cache, pos, line)) {
        auto parts = split_line(line);
        if (parts.size() == 2 && parts[0] == "spoof") out.push_back(parts[1]);
    }
    return true;
}

#endif // DCFG_RUNTIME_ONLY
