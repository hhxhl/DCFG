#include "config_cache.h"

#include <cstdio>

int main(int argc, char **argv) {
    const char *config_path = argc > 1 ? argv[1] : kDcfgConfigPath;
    const char *cache_path = argc > 2 ? argv[2] : "/data/adb/modules/dcfg/config.cache";
    if (!dcfg_rebuild_config_cache_file(config_path, cache_path)) {
        std::fprintf(stderr, "dcfg-cache: failed to build cache from %s to %s\n", config_path, cache_path);
        return 1;
    }
    return 0;
}
