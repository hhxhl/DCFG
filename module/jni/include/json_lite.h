#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

void skip_ws(const std::string &s, size_t &pos);

bool read_json_string(
        const std::string &s,
        size_t &pos,
        std::string &out
);

std::string find_string(const std::string &obj, const std::string &key);

bool find_bool(const std::string &obj, const std::string &key, bool def);

void parse_string_object(
        const std::string &obj,
        std::unordered_map<std::string, std::string> &out
);

void parse_scalar_object(
        const std::string &obj,
        std::unordered_map<std::string, std::string> &out
);

std::string extract_object_at(const std::string &json, size_t brace);

std::string extract_array_at(const std::string &json, size_t bracket);

std::string extract_object_after_key(const std::string &json, const std::string &key);

std::string extract_array_after_key(const std::string &json, const std::string &key);

size_t find_matching_json_token(
        const std::string &json,
        size_t start,
        char open_token,
        char close_token
);
