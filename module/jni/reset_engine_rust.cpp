#include "reset_engine.h"

#ifndef DCFG_NO_LOG
#include <android/log.h>
#endif

#include <sys/system_properties.h>

#include <cstdint>
#include <cstring>
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

extern "C" bool dcfg_rp_set(const char *key, const char *value);
extern "C" bool dcfg_rp_delete(const char *key);
extern "C" int dcfg_rp_get(const char *key, char *buf, size_t cap);

static bool resetprop_set(const std::string &key, const std::string &value) {
    return dcfg_rp_set(key.c_str(), value.c_str());
}

static bool resetprop_delete(const std::string &key) {
    return dcfg_rp_delete(key.c_str());
}

void dcfg_reset_backup_originals(std::vector<ResetPropItem> &items) {
    char value[PROP_VALUE_MAX];

    for (auto &item : items) {
        value[0] = '\0';
        int r = dcfg_rp_get(item.key.c_str(), value, sizeof(value));

        if (r <= 0) {
            item.existed = false;
            item.original_value.clear();
            RLOGD("rust resetprop backup key=%s existed=0", item.key.c_str());
            continue;
        }

        item.existed = true;
        item.original_value = value;
        RLOGD("rust resetprop backup key=%s existed=1 len=%zu", item.key.c_str(), item.original_value.size());
    }
}

uint32_t dcfg_reset_apply_fake(const std::vector<ResetPropItem> &items) {
    RLOGI("rust resetprop apply_fake count=%zu", items.size());
    uint32_t applied = 0;

    for (const auto &item : items) {
        if (item.key.empty()) {
            continue;
        }

        bool ok = false;

        if (item.fake_value.kind == PropValueKind::NULL_VALUE) {
            if (!item.existed) {
                ok = true;
                RLOGD("rust resetprop null/delete key=%s existed=0 ok=1", item.key.c_str());
            } else {
                ok = resetprop_delete(item.key);
                RLOGD("rust resetprop null/delete key=%s existed=1 ok=%d", item.key.c_str(), ok ? 1 : 0);
            }
        } else if (item.fake_value.kind == PropValueKind::EMPTY) {
            ok = resetprop_set(item.key, "");
            RLOGD("rust resetprop empty key=%s ok=%d", item.key.c_str(), ok ? 1 : 0);
        } else if (!item.fake_value.value.empty()) {
            ok = resetprop_set(item.key, item.fake_value.value);
            RLOGD("rust resetprop set key=%s ok=%d", item.key.c_str(), ok ? 1 : 0);
        }

        if (ok) {
            ++applied;
        }
    }

    RLOGI("rust resetprop apply_fake done applied=%u requested=%zu", applied, items.size());
    return applied;
}

void dcfg_reset_restore_originals(const std::vector<ResetPropItem> &items) {
    RLOGI("rust resetprop restore count=%zu", items.size());

    for (const auto &item : items) {
        if (item.key.empty()) {
            continue;
        }

        bool ok = false;
        if (item.existed) {
            ok = resetprop_set(item.key, item.original_value);
            RLOGD("rust resetprop restore key=%s existed=1 ok=%d", item.key.c_str(), ok ? 1 : 0);
        } else {
            ok = resetprop_delete(item.key);
            RLOGD("rust resetprop delete key=%s existed=0 ok=%d", item.key.c_str(), ok ? 1 : 0);
        }
    }
}
