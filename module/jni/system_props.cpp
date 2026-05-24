// Native property interception layer.

#include "system_props.h"
#include "log.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <tuple>
#include <utility>

#include <sys/sysmacros.h>
#include <sys/system_properties.h>
#include <sys/types.h>

static std::mutex g_mutex;
static PropMap g_props;
static std::unordered_map<const prop_info *, std::string> g_fake_pi_to_key;
static std::unordered_map<std::string, prop_info *> g_key_to_fake_pi;
static std::set<std::tuple<dev_t, ino_t, std::string>> g_hooked_symbol_targets;

using prop_get_t = int (*)(const char *, char *);
using prop_find_t = const prop_info *(*)(const char *);
using prop_read_t = int (*)(const prop_info *, char *, char *);
using prop_read_callback_t = void (*)(
        const prop_info *,
        void (*)(void *, const char *, const char *, uint32_t),
        void *
);

static prop_get_t orig_get = nullptr;
static prop_find_t orig_find = nullptr;
static prop_read_t orig_read = nullptr;
static prop_read_callback_t orig_read_callback = nullptr;

static void clear_fake_tokens_locked() {
    for (const auto &it : g_key_to_fake_pi) {
        delete[] reinterpret_cast<char *>(it.second);
    }

    g_key_to_fake_pi.clear();
    g_fake_pi_to_key.clear();
}

void dcfg_set_current_props(const PropMap &props) {
    std::lock_guard<std::mutex> l(g_mutex);
    clear_fake_tokens_locked();
    g_props = props;
}

bool dcfg_get_fake_prop(const char *key, PropValue &value) {
    if (!key) return false;

    std::lock_guard<std::mutex> l(g_mutex);

    auto it = g_props.find(key);

    if (it == g_props.end()) return false;

    value = it->second;
    return true;
}

static int copy_prop_value(char *out, const std::string &value) {
    if (!out) return static_cast<int>(value.size());

    std::strncpy(out, value.c_str(), PROP_VALUE_MAX - 1);
    out[PROP_VALUE_MAX - 1] = '\0';

    return static_cast<int>(std::strlen(out));
}

static int hooked_get(const char *name, char *value) {
    PropValue fake;

    if (dcfg_get_fake_prop(name, fake)) {
        if (fake.kind == PropValueKind::NULL_VALUE || fake.kind == PropValueKind::EMPTY) {
            if (value) value[0] = '\0';

            DLOGD(
                    "sysprop hit get key=%s special=%s",
                    name ? name : "-",
                    fake.kind == PropValueKind::NULL_VALUE ? "__NULL__" : "__EMPTY__"
            );

            return 0;
        }

        int len = copy_prop_value(value, fake.value);

        DLOGD(
                "sysprop hit get key=%s value=%s",
                name ? name : "-",
                value ? value : fake.value.c_str()
        );

        return len;
    }

    if (orig_get) return orig_get(name, value);

    if (value) value[0] = '\0';

    return 0;
}

static const prop_info *make_fake_prop_info_locked(const std::string &key) {
    auto it = g_key_to_fake_pi.find(key);

    if (it != g_key_to_fake_pi.end()) return it->second;

    auto *token = reinterpret_cast<prop_info *>(new char[1]);

    g_key_to_fake_pi[key] = token;
    g_fake_pi_to_key[token] = key;

    return token;
}

static const prop_info *hooked_find(const char *name) {
    if (!name) return nullptr;

    PropValue fake;

    if (dcfg_get_fake_prop(name, fake)) {
        DLOGD(
                "sysprop hit find key=%s kind=%d value=%s",
                name,
                static_cast<int>(fake.kind),
                fake.value.c_str()
        );

        if (fake.kind == PropValueKind::NULL_VALUE) {
            return nullptr;
        }

        const prop_info *real = nullptr;

        if (orig_find) real = orig_find(name);

        if (real) {
            std::lock_guard<std::mutex> l(g_mutex);
            g_fake_pi_to_key[real] = name;
            return real;
        }

        std::lock_guard<std::mutex> l(g_mutex);
        return make_fake_prop_info_locked(name);
    }

    return orig_find ? orig_find(name) : nullptr;
}

static bool find_key_by_pi(const prop_info *pi, std::string &key) {
    if (!pi) return false;

    std::lock_guard<std::mutex> l(g_mutex);

    auto it = g_fake_pi_to_key.find(pi);

    if (it == g_fake_pi_to_key.end()) return false;

    key = it->second;
    return true;
}

static int hooked_read(const prop_info *pi, char *name, char *value) {
    if (!pi) {
        return orig_read ? orig_read(pi, name, value) : 0;
    }

    std::string key;

    if (find_key_by_pi(pi, key)) {
        PropValue fake;

        if (dcfg_get_fake_prop(key.c_str(), fake)) {
            if (name) {
                std::strncpy(name, key.c_str(), PROP_NAME_MAX - 1);
                name[PROP_NAME_MAX - 1] = '\0';
            }

            if (fake.kind == PropValueKind::NULL_VALUE || fake.kind == PropValueKind::EMPTY) {
                if (value) value[0] = '\0';

                DLOGD(
                        "sysprop hit read key=%s special=%s",
                        key.c_str(),
                        fake.kind == PropValueKind::NULL_VALUE ? "__NULL__" : "__EMPTY__"
                );

                return 0;
            }

            int len = copy_prop_value(value, fake.value);

            DLOGD(
                    "sysprop hit read key=%s value=%s",
                    key.c_str(),
                    value ? value : fake.value.c_str()
            );

            return len;
        }
    }

    return orig_read ? orig_read(pi, name, value) : 0;
}

static void hooked_read_callback(
        const prop_info *pi,
        void (*callback)(void *, const char *, const char *, uint32_t),
        void *cookie
) {
    if (!pi || !callback) {
        if (orig_read_callback) orig_read_callback(pi, callback, cookie);
        return;
    }

    std::string key;

    if (find_key_by_pi(pi, key)) {
        PropValue fake;

        if (dcfg_get_fake_prop(key.c_str(), fake)) {
            if (fake.kind == PropValueKind::NULL_VALUE) {
                DLOGD("sysprop hit read_callback key=%s special=__NULL__", key.c_str());
                return;
            }

            const char *value = fake.kind == PropValueKind::EMPTY ? "" : fake.value.c_str();

            DLOGD(
                    "sysprop hit read_callback key=%s value=%s",
                    key.c_str(),
                    value
            );

            callback(cookie, key.c_str(), value, 0);
            return;
        }
    }

    if (orig_read_callback) {
        orig_read_callback(pi, callback, cookie);
    }
}

static bool parse_unsigned_long_long(
        const std::string &text,
        int base,
        unsigned long long &out
) {
    if (text.empty()) return false;

    errno = 0;
    char *end = nullptr;
    const char *begin = text.c_str();
    unsigned long long value = strtoull(begin, &end, base);

    if (errno != 0) return false;
    if (end == begin || *end != '\0') return false;

    out = value;
    return true;
}

static bool parse_dev_inode(
        const std::string &dev,
        const std::string &inode_s,
        dev_t &out_dev,
        ino_t &out_inode
) {
    auto colon = dev.find(':');

    if (colon == std::string::npos) return false;

    unsigned long long major = 0;
    unsigned long long minor = 0;
    unsigned long long inode_value = 0;

    if (!parse_unsigned_long_long(dev.substr(0, colon), 16, major)) return false;
    if (!parse_unsigned_long_long(dev.substr(colon + 1), 16, minor)) return false;
    if (!parse_unsigned_long_long(inode_s, 10, inode_value)) return false;
    if (inode_value == 0) return false;

    out_dev = makedev(static_cast<unsigned int>(major), static_cast<unsigned int>(minor));
    out_inode = static_cast<ino_t>(inode_value);
    return true;
}

static bool path_is_hook_target(const std::string &path) {
    if (path.empty()) return false;

    // Do not target libc itself: PLT hooks need to be installed on callers.
    if (path.find("/libc.so") != std::string::npos) return false;

    // Java android.os.SystemProperties native methods live here on Android.
    if (path.find("/libandroid_runtime.so") != std::string::npos) return true;

    // libbase property helpers may be used by native code.
    if (path.find("/libbase.so") != std::string::npos) return true;
// 暂不考虑app自身native hook
    // Native app libraries already loaded at hook time.
    // if (path.find("/data/app/") != std::string::npos && path.find(".so") != std::string::npos) {
        // return true;
    // }

    // APK-embedded native libraries shown by linker as zip paths.
    // if (path.find(".apk") != std::string::npos && path.find(".so") != std::string::npos) {
        // return true;
    // }
// app自身native hook 结束
    return false;
}

static std::set<std::pair<dev_t, ino_t>> find_property_hook_targets() {
    std::set<std::pair<dev_t, ino_t>> out;
    std::ifstream maps("/proc/self/maps");
    std::string line;

    while (std::getline(maps, line)) {
        std::istringstream iss(line);
        std::string addr;
        std::string perms;
        std::string offset;
        std::string dev;
        std::string inode_s;
        std::string path;

        if (!(iss >> addr >> perms >> offset >> dev >> inode_s)) continue;
        std::getline(iss, path);

        auto first = path.find_first_not_of(' ');
        if (first != std::string::npos) path = path.substr(first);

        if (perms.find('x') == std::string::npos) continue;
        if (!path_is_hook_target(path)) continue;

        dev_t parsed_dev = 0;
        ino_t parsed_inode = 0;

        if (!parse_dev_inode(dev, inode_s, parsed_dev, parsed_inode)) continue;

        DLOGD(
                "sysprop target path=%s dev=%llu ino=%llu",
                path.c_str(),
                static_cast<unsigned long long>(parsed_dev),
                static_cast<unsigned long long>(parsed_inode)
        );

        out.insert({parsed_dev, parsed_inode});
    }

    return out;
}

static bool install_one_hook(
        zygisk::Api *api,
        dev_t dev,
        ino_t inode,
        const char *symbol,
        void *new_func,
        void **old_func
) {
    if (!api || !symbol || !new_func || !old_func) return false;

    const auto key = std::make_tuple(dev, inode, std::string(symbol));

    {
        std::lock_guard<std::mutex> l(g_mutex);
        if (g_hooked_symbol_targets.find(key) != g_hooked_symbol_targets.end()) {
            return false;
        }
    }

    api->pltHookRegister(dev, inode, symbol, new_func, old_func);

    if (!api->pltHookCommit()) {
        DLOGW(
                "sysprop hook failed symbol=%s dev=%llu ino=%llu",
                symbol,
                static_cast<unsigned long long>(dev),
                static_cast<unsigned long long>(inode)
        );
        return false;
    }

    {
        std::lock_guard<std::mutex> l(g_mutex);
        g_hooked_symbol_targets.insert(key);
    }

    DLOGD(
            "sysprop hook installed symbol=%s dev=%llu ino=%llu",
            symbol,
            static_cast<unsigned long long>(dev),
            static_cast<unsigned long long>(inode)
    );

    return true;
}

void dcfg_install_system_property_hooks(zygisk::Api *api) {
    if (!api) return;

    auto targets = find_property_hook_targets();

    if (targets.empty()) {
        DLOGD("sysprop-plus hook skipped: no eligible caller mapping found");
        return;
    }

    size_t installed = 0;

    for (const auto &target : targets) {
        installed += install_one_hook(
                api,
                target.first,
                target.second,
                "__system_property_get",
                reinterpret_cast<void *>(hooked_get),
                reinterpret_cast<void **>(&orig_get)
        ) ? 1 : 0;

        installed += install_one_hook(
                api,
                target.first,
                target.second,
                "__system_property_find",
                reinterpret_cast<void *>(hooked_find),
                reinterpret_cast<void **>(&orig_find)
        ) ? 1 : 0;

        installed += install_one_hook(
                api,
                target.first,
                target.second,
                "__system_property_read",
                reinterpret_cast<void *>(hooked_read),
                reinterpret_cast<void **>(&orig_read)
        ) ? 1 : 0;

        installed += install_one_hook(
                api,
                target.first,
                target.second,
                "__system_property_read_callback",
                reinterpret_cast<void *>(hooked_read_callback),
                reinterpret_cast<void **>(&orig_read_callback)
        ) ? 1 : 0;
    }

    if (installed == 0) {
        DLOGD("sysprop-plus hook skipped: no new symbol hooks installed");
        return;
    }

    DLOGD(
            "sysprop-plus hooks installed new_symbols=%zu total_symbols=%zu",
            installed,
            g_hooked_symbol_targets.size()
    );
}
