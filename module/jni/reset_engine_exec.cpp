#include "reset_engine.h"

#ifndef DCFG_NO_LOG
#include <android/log.h>
#endif

#include <sys/system_properties.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <string>
#include <vector>

#ifndef DCFG_NO_LOG
#define RLOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "dcfg", __VA_ARGS__)
#define RLOGI(...) __android_log_print(ANDROID_LOG_INFO, "dcfg", __VA_ARGS__)
#define RLOGW(...) __android_log_print(ANDROID_LOG_WARN, "dcfg", __VA_ARGS__)
#else
#define RLOGD(...) do {} while (0)
#define RLOGI(...) do {} while (0)
#define RLOGW(...) do {} while (0)
#endif

static bool file_exists(const char *path) {
    return path && access(path, X_OK) == 0;
}

static const char *find_resetprop() {
    static const char *candidates[] = {
            "/data/adb/ksu/bin/resetprop",
            "/data/adb/magisk/resetprop",
            "/debug_ramdisk/magisk/resetprop",
            "/system/bin/resetprop",
            nullptr
    };

    for (const char **p = candidates; *p; ++p) {
        if (file_exists(*p)) {
            RLOGD("resetprop path found=%s", *p);
            return *p;
        }
    }

    RLOGW("resetprop path not found in known locations; fallback to PATH");
    return nullptr;
}

static bool run_resetprop_argv(const std::vector<std::string> &args) {
    if (args.empty()) {
        RLOGW("resetprop argv empty");
        return false;
    }

    RLOGD("resetprop exec cmd=%s key=%s", args.size() > 0 ? args[0].c_str() : "-", args.size() > 1 ? args[1].c_str() : "-");

    pid_t child = fork();

    if (child < 0) {
        RLOGW("resetprop fork failed errno=%d", errno);
        return false;
    }

    if (child == 0) {
        std::vector<char *> argv;
        argv.reserve(args.size() + 1);

        for (const auto &s : args) {
            argv.push_back(const_cast<char *>(s.c_str()));
        }

        argv.push_back(nullptr);

        const char *resetprop = find_resetprop();

        if (resetprop) {
            execv(resetprop, argv.data());
        }

        execvp("resetprop", argv.data());
        _exit(127);
    }

    int status = 0;

    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }

        RLOGW("resetprop waitpid failed errno=%d", errno);
        return false;
    }

    bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) {
        RLOGW("resetprop command failed status=%d exited=%d code=%d", status, WIFEXITED(status) ? 1 : 0, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    } else {
        RLOGD("resetprop command ok");
    }
    return ok;
}

static bool resetprop_set(const std::string &key, const std::string &value) {
    return run_resetprop_argv({"resetprop", key, value});
}

static bool resetprop_delete(const std::string &key) {
    return run_resetprop_argv({"resetprop", "--delete", key});
}

void dcfg_reset_backup_originals(std::vector<ResetPropItem> &items) {
    char value[PROP_VALUE_MAX];

    for (auto &item : items) {
        const prop_info *pi = __system_property_find(item.key.c_str());

        if (!pi) {
            item.existed = false;
            item.original_value.clear();
            RLOGD("resetprop backup key=%s existed=0", item.key.c_str());
            continue;
        }

        value[0] = '\0';
        int n = __system_property_get(item.key.c_str(), value);

        item.existed = n >= 0;
        item.original_value = value;
        RLOGD("resetprop backup key=%s existed=%d len=%d", item.key.c_str(), item.existed ? 1 : 0, n);
    }
}

uint32_t dcfg_reset_apply_fake(const std::vector<ResetPropItem> &items) {
    RLOGI("resetprop apply_fake count=%zu", items.size());
    uint32_t applied = 0;
    for (const auto &item : items) {
        if (item.key.empty()) {
            continue;
        }

        bool ok = false;

        if (item.fake_value.kind == PropValueKind::NULL_VALUE) {
            if (!item.existed) {
                ok = true;
                RLOGD("resetprop null/delete key=%s existed=0 ok=1", item.key.c_str());
            } else {
                ok = resetprop_delete(item.key);
                RLOGD("resetprop null/delete key=%s existed=1 ok=%d", item.key.c_str(), ok ? 1 : 0);
            }
        } else if (item.fake_value.kind == PropValueKind::EMPTY) {
            ok = resetprop_set(item.key, "");
            RLOGD("resetprop empty key=%s ok=%d", item.key.c_str(), ok ? 1 : 0);
        } else if (!item.fake_value.value.empty()) {
            ok = resetprop_set(item.key, item.fake_value.value);
            RLOGD("resetprop set key=%s value=%s ok=%d", item.key.c_str(), item.fake_value.value.c_str(), ok ? 1 : 0);
        }

        if (ok) {
            ++applied;
        }
    }
    RLOGI("resetprop apply_fake done applied=%u requested=%zu", applied, items.size());
    return applied;
}

void dcfg_reset_restore_originals(const std::vector<ResetPropItem> &items) {
    RLOGI("resetprop restore count=%zu", items.size());
    for (const auto &item : items) {
        if (item.key.empty()) {
            continue;
        }

        bool ok = false;
        if (item.existed) {
            ok = resetprop_set(item.key, item.original_value);
            RLOGD("resetprop restore key=%s existed=1 ok=%d", item.key.c_str(), ok ? 1 : 0);
        } else {
            ok = resetprop_delete(item.key);
            RLOGD("resetprop delete key=%s existed=0 ok=%d", item.key.c_str(), ok ? 1 : 0);
        }
    }
}

