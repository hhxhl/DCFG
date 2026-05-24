#pragma once

#include <string>
#include <unordered_map>



enum class PropValueKind {
    SET = 0,
    EMPTY = 1,
    NULL_VALUE = 2,
};

struct PropValue {
    PropValueKind kind = PropValueKind::SET;
    std::string value;
};

using PropMap = std::unordered_map<std::string, PropValue>;

enum class PropsMode {
    NONE = 0,
    MANUAL = 1,
    AUTO = 2,
    AUTO_MANUAL = 3,
};

enum class PropsHookMode {
    NONE = 0,
    RUNTIME = 1,
    FULL = 2,
};

enum class ResetpropScope {
    PACKAGE = 0,
    MAIN = 1,
};

struct Profile {
    PropsMode props_mode = PropsMode::NONE;
    PropsHookMode props_hook_mode = PropsHookMode::NONE;
    bool resetprop_enabled = false;

    std::unordered_map<std::string, std::string> build;
    std::unordered_map<std::string, std::string> version_info;
    std::unordered_map<std::string, std::string> props;
};

struct MatchResult {
    bool matched = false;
    bool hook_children = false;
    ResetpropScope resetprop_scope = ResetpropScope::PACKAGE;
    std::string package_name;
    std::string profile_name;
    Profile profile;
};

bool dcfg_hook_children_enabled();

MatchResult dcfg_match_profile(const std::string &package_name);

MatchResult dcfg_load_and_match_config(
        const std::string &package_name,
        bool is_child_process
);

const char *dcfg_props_mode_name(PropsMode mode);
const char *dcfg_props_hook_mode_name(PropsHookMode mode);
const char *dcfg_resetprop_scope_name(ResetpropScope scope);

PropMap
dcfg_make_final_props(const Profile &profile);


