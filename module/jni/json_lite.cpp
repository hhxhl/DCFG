#include "json_lite.h"

#include <cctype>
#include <string>

void skip_ws(const std::string &s, size_t &pos) {
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
        ++pos;
    }
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void append_utf8(std::string &out, unsigned codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

bool read_json_string(
        const std::string &s,
        size_t &pos,
        std::string &out
) {
    out.clear();

    if (pos >= s.size() || s[pos] != '"') {
        return false;
    }

    ++pos;

    while (pos < s.size()) {
        char c = s[pos++];

        if (c == '"') {
            return true;
        }

        if (c != '\\') {
            out.push_back(c);
            continue;
        }

        if (pos >= s.size()) {
            return false;
        }

        char e = s[pos++];

        switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;

            case 'u': {
                if (pos + 4 > s.size()) {
                    return false;
                }

                unsigned codepoint = 0;

                for (int i = 0; i < 4; ++i) {
                    int v = hex_value(s[pos++]);

                    if (v < 0) {
                        return false;
                    }

                    codepoint = (codepoint << 4) | static_cast<unsigned>(v);
                }

                append_utf8(out, codepoint);
                break;
            }

            default:
                return false;
        }
    }

    return false;
}

static bool skip_json_value(const std::string &s, size_t &pos);

static bool skip_json_container(
        const std::string &s,
        size_t &pos,
        char open_token,
        char close_token
) {
    if (pos >= s.size() || s[pos] != open_token) {
        return false;
    }

    ++pos;

    for (;;) {
        skip_ws(s, pos);

        if (pos >= s.size()) {
            return false;
        }

        if (s[pos] == close_token) {
            ++pos;
            return true;
        }

        if (!skip_json_value(s, pos)) {
            return false;
        }

        skip_ws(s, pos);

        if (pos < s.size() && s[pos] == ':') {
            ++pos;
            skip_ws(s, pos);

            if (!skip_json_value(s, pos)) {
                return false;
            }

            skip_ws(s, pos);
        }

        if (pos < s.size() && s[pos] == ',') {
            ++pos;
            continue;
        }

        if (pos < s.size() && s[pos] == close_token) {
            ++pos;
            return true;
        }

        return false;
    }
}

static bool skip_json_value(const std::string &s, size_t &pos) {
    skip_ws(s, pos);

    if (pos >= s.size()) {
        return false;
    }

    if (s[pos] == '"') {
        std::string ignored;
        return read_json_string(s, pos, ignored);
    }

    if (s[pos] == '{') {
        return skip_json_container(s, pos, '{', '}');
    }

    if (s[pos] == '[') {
        return skip_json_container(s, pos, '[', ']');
    }

    while (pos < s.size()) {
        char c = s[pos];

        if (c == ',' || c == '}' || c == ']' || std::isspace(static_cast<unsigned char>(c))) {
            break;
        }

        ++pos;
    }

    return true;
}

static bool find_member_value_start(
        const std::string &obj,
        const std::string &key,
        size_t &value_pos
) {
    size_t pos = 0;

    while (pos < obj.size()) {
        skip_ws(obj, pos);

        if (pos >= obj.size()) {
            return false;
        }

        if (obj[pos] == '{' || obj[pos] == ',' || obj[pos] == '[') {
            ++pos;
            continue;
        }

        if (obj[pos] == '}' || obj[pos] == ']') {
            return false;
        }

        if (obj[pos] != '"') {
            ++pos;
            continue;
        }

        std::string current_key;
        size_t key_pos = pos;

        if (!read_json_string(obj, pos, current_key)) {
            return false;
        }

        skip_ws(obj, pos);

        if (pos >= obj.size() || obj[pos] != ':') {
            pos = key_pos + 1;
            continue;
        }

        ++pos;
        skip_ws(obj, pos);

        if (current_key == key) {
            value_pos = pos;
            return true;
        }

        if (!skip_json_value(obj, pos)) {
            return false;
        }
    }

    return false;
}

std::string find_string(const std::string &obj, const std::string &key) {
    size_t value_pos = 0;

    if (!find_member_value_start(obj, key, value_pos)) {
        return {};
    }

    std::string out;
    return read_json_string(obj, value_pos, out) ? out : std::string{};
}

bool find_bool(const std::string &obj, const std::string &key, bool def) {
    size_t value_pos = 0;

    if (!find_member_value_start(obj, key, value_pos)) {
        return def;
    }

    if (obj.compare(value_pos, 4, "true") == 0) {
        return true;
    }

    if (obj.compare(value_pos, 5, "false") == 0) {
        return false;
    }

    return def;
}

static bool read_json_scalar(
        const std::string &obj,
        size_t &pos,
        std::string &out
) {
    skip_ws(obj, pos);
    out.clear();

    if (pos >= obj.size()) {
        return false;
    }

    if (obj[pos] == '"') {
        return read_json_string(obj, pos, out);
    }

    size_t start = pos;

    while (pos < obj.size()) {
        char c = obj[pos];

        if (c == ',' || c == '}' || c == ']' || std::isspace(static_cast<unsigned char>(c))) {
            break;
        }

        ++pos;
    }

    if (pos == start) {
        return false;
    }

    out = obj.substr(start, pos - start);
    return out == "true"
           || out == "false"
           || out == "null"
           || !out.empty();
}

static void parse_object_scalars(
        const std::string &obj,
        std::unordered_map<std::string, std::string> &out,
        bool strings_only
) {
    if (obj.empty()) {
        return;
    }

    size_t pos = 0;
    skip_ws(obj, pos);

    if (pos < obj.size() && obj[pos] == '{') {
        ++pos;
    }

    for (;;) {
        skip_ws(obj, pos);

        if (pos >= obj.size() || obj[pos] == '}') {
            return;
        }

        if (obj[pos] == ',') {
            ++pos;
            continue;
        }

        if (obj[pos] != '"') {
            ++pos;
            continue;
        }

        std::string key;

        if (!read_json_string(obj, pos, key)) {
            return;
        }

        skip_ws(obj, pos);

        if (pos >= obj.size() || obj[pos] != ':') {
            return;
        }

        ++pos;
        skip_ws(obj, pos);

        if (pos < obj.size() && (obj[pos] == '{' || obj[pos] == '[')) {
            if (!skip_json_value(obj, pos)) {
                return;
            }
        } else {
            bool is_string = pos < obj.size() && obj[pos] == '"';
            std::string value;

            if (!read_json_scalar(obj, pos, value)) {
                return;
            }

            if (!key.empty() && !value.empty() && (!strings_only || is_string)) {
                out[key] = value;
            }
        }

        skip_ws(obj, pos);

        if (pos < obj.size() && obj[pos] == ',') {
            ++pos;
        }
    }
}

void parse_string_object(
        const std::string &obj,
        std::unordered_map<std::string, std::string> &out
) {
    parse_object_scalars(obj, out, true);
}

void parse_scalar_object(
        const std::string &obj,
        std::unordered_map<std::string, std::string> &out
) {
    parse_object_scalars(obj, out, false);
}

size_t find_matching_json_token(
        const std::string &json,
        size_t start,
        char open_token,
        char close_token
) {
    if (start == std::string::npos || start >= json.size() || json[start] != open_token) {
        return std::string::npos;
    }

    int depth = 0;
    bool in_string = false;
    bool escape = false;

    for (size_t i = start; i < json.size(); ++i) {
        char c = json[i];

        if (in_string) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                in_string = false;
            }

            continue;
        }

        if (c == '"') {
            in_string = true;
            continue;
        }

        if (c == open_token) {
            depth++;
            continue;
        }

        if (c == close_token) {
            depth--;

            if (depth == 0) {
                return i;
            }
        }
    }

    return std::string::npos;
}

std::string extract_object_at(const std::string &json, size_t brace) {
    auto end = find_matching_json_token(json, brace, '{', '}');

    if (end == std::string::npos) {
        return {};
    }

    return json.substr(brace, end - brace + 1);
}

std::string extract_array_at(const std::string &json, size_t bracket) {
    auto end = find_matching_json_token(json, bracket, '[', ']');

    if (end == std::string::npos) {
        return {};
    }

    return json.substr(bracket, end - bracket + 1);
}

std::string extract_object_after_key(const std::string &json, const std::string &key) {
    auto p = json.find("\"" + key + "\"");

    if (p == std::string::npos) {
        return {};
    }

    auto colon = json.find(':', p);

    if (colon == std::string::npos) {
        return {};
    }

    return extract_object_at(json, json.find('{', colon));
}

std::string extract_array_after_key(const std::string &json, const std::string &key) {
    auto p = json.find("\"" + key + "\"");

    if (p == std::string::npos) {
        return {};
    }

    auto colon = json.find(':', p);

    if (colon == std::string::npos) {
        return {};
    }

    return extract_array_at(json, json.find('[', colon));
}
