#pragma once

#include "config.h"

#include <string>
#include <vector>

#ifndef DCFG_RUNTIME_ONLY
static constexpr const char *kDcfgConfigPath = "/data/adb/dcfg/config.json";
#endif
static constexpr const char *kDcfgConfigCacheName = "config.cache";

bool dcfg_load_and_match_config_cache(
        const std::string &package_name,
        bool is_child_process,
        MatchResult &out
);

#ifndef DCFG_RUNTIME_ONLY
bool dcfg_rebuild_config_cache_file(const char *config_path, const char *cache_path);

bool dcfg_read_config_file(std::string &out);
#endif
bool dcfg_read_cache_spoof_keys(std::vector<std::string> &out);
