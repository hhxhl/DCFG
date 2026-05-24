// Runtime configuration entry points.
//
// In the compiled-cache architecture, libdcfg.so must not parse config.json.
// JSON parsing, auto props mapping, and effective props generation are owned by
// the dcfg-cache build stage. Runtime only reads module/config.cache.

#include "config.h"
#include "config_cache.h"
#include "log.h"

static PropValue prop_value_from_cached_string(const std::string &value) {
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

const char *dcfg_props_mode_name(PropsMode mode) {
    switch (mode) {
        case PropsMode::MANUAL: return "manual";
        case PropsMode::AUTO: return "auto";
        case PropsMode::AUTO_MANUAL: return "auto_manual";
        case PropsMode::NONE:
        default: return "none";
    }
}

const char *dcfg_props_hook_mode_name(PropsHookMode mode) {
    switch (mode) {
        case PropsHookMode::RUNTIME: return "runtime";
        case PropsHookMode::FULL: return "full";
        case PropsHookMode::NONE:
        default: return "none";
    }
}

const char *dcfg_resetprop_scope_name(ResetpropScope scope) {
    switch (scope) {
        case ResetpropScope::MAIN: return "main";
        case ResetpropScope::PACKAGE:
        default: return "package";
    }
}


bool dcfg_hook_children_enabled() {
    // No runtime JSON fallback. The cached matcher carries global.hook_children.
    return false;
}

MatchResult dcfg_match_profile(const std::string &package_name) {
    return dcfg_load_and_match_config(package_name, false);
}

PropMap dcfg_make_final_props(const Profile &profile) {
    // Cache entries already contain final/effective props. Runtime must not
    // auto-map or merge again; only decode explicit cache sentinels.
    PropMap out;
    out.reserve(profile.props.size());
    for (const auto &it : profile.props) {
        if (!it.first.empty()) {
            out[it.first] = prop_value_from_cached_string(it.second);
        }
    }
    return out;
}

MatchResult dcfg_load_and_match_config(
        const std::string &package_name,
        bool is_child_process
) {
    MatchResult cached;
    if (dcfg_load_and_match_config_cache(package_name, is_child_process, cached)) {
        if (is_child_process && !cached.matched && !cached.hook_children) {
            DLOGD("child process skipped by cached global.hook_children=0 package=%s", package_name.c_str());
        }
        return cached;
    }

    MatchResult r;
    r.package_name = package_name;
    DLOGW("compiled config.cache missing/invalid; no runtime JSON fallback package=%s", package_name.c_str());
    return r;
}
