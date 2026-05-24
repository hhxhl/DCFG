#include "props_mapping.h"

#include <string>

struct AutoPropMap {
    const char *source;
    const char *target;
};

static constexpr AutoPropMap kBuildAutoMaps[] = {
        // Product identity props.
        {"BRAND", "ro.product.brand"},
        {"MANUFACTURER", "ro.product.manufacturer"},
        {"DEVICE", "ro.product.device"},
        {"PRODUCT", "ro.product.name"},
        {"MODEL", "ro.product.model"},

        // System partition product identity props.
        {"BRAND", "ro.product.system.brand"},
        {"MANUFACTURER", "ro.product.system.manufacturer"},
        {"DEVICE", "ro.product.system.device"},
        {"PRODUCT", "ro.product.system.name"},
        {"MODEL", "ro.product.system.model"},

        // Vendor partition product identity props.
        {"BRAND", "ro.product.vendor.brand"},
        {"MANUFACTURER", "ro.product.vendor.manufacturer"},
        {"DEVICE", "ro.product.vendor.device"},
        {"PRODUCT", "ro.product.vendor.name"},
        {"MODEL", "ro.product.vendor.model"},

        // ODM partition product identity props.
        {"BRAND", "ro.product.odm.brand"},
        {"MANUFACTURER", "ro.product.odm.manufacturer"},
        {"DEVICE", "ro.product.odm.device"},
        {"PRODUCT", "ro.product.odm.name"},
        {"MODEL", "ro.product.odm.model"},

        // Product partition product identity props.
        {"BRAND", "ro.product.product.brand"},
        {"MANUFACTURER", "ro.product.product.manufacturer"},
        {"DEVICE", "ro.product.product.device"},
        {"PRODUCT", "ro.product.product.name"},
        {"MODEL", "ro.product.product.model"},

        // Bootimage product identity props.
        {"BRAND", "ro.product.bootimage.brand"},
        {"MANUFACTURER", "ro.product.bootimage.manufacturer"},
        {"DEVICE", "ro.product.bootimage.device"},
        {"PRODUCT", "ro.product.bootimage.name"},
        {"MODEL", "ro.product.bootimage.model"},

        {"BOARD", "ro.product.board"},

        // Build identity props.
        {"FINGERPRINT", "ro.build.fingerprint"},
        {"FINGERPRINT", "ro.system.build.fingerprint"},
        {"FINGERPRINT", "ro.vendor.build.fingerprint"},
        {"ID", "ro.build.id"},
        {"DISPLAY", "ro.build.display.id"},
        {"TYPE", "ro.build.type"},
        {"TAGS", "ro.build.tags"},
        {"HOST", "ro.build.host"},
        {"USER", "ro.build.user"},
        {"BOOTLOADER", "ro.bootloader"},
        {"HARDWARE", "ro.hardware"},
        {"SOC_MANUFACTURER", "ro.soc.manufacturer"},
        {"SOC_MODEL", "ro.soc.model"},
};

static constexpr AutoPropMap kVersionAutoMaps[] = {
        // Main build version props.
        {"INCREMENTAL", "ro.build.version.incremental"},
        {"RELEASE", "ro.build.version.release"},
        {"RELEASE", "ro.build.version.release_or_codename"},
        {"RELEASE", "ro.build.version.release_or_preview_display"},
        {"SDK_INT", "ro.build.version.sdk"},
        {"SDK_FULL", "ro.build.version.sdk_full"},
        {"CODENAME", "ro.build.version.codename"},
        {"SECURITY_PATCH", "ro.build.version.security_patch"},
        {"BASE_OS", "ro.build.version.base_os"},
        {"PREVIEW_SDK_INT", "ro.build.version.preview_sdk"},

        // Partition build version props.
        {"INCREMENTAL", "ro.system.build.version.incremental"},
        {"INCREMENTAL", "ro.vendor.build.version.incremental"},
        {"INCREMENTAL", "ro.product.build.version.incremental"},
        {"INCREMENTAL", "ro.odm.build.version.incremental"},

        {"RELEASE", "ro.system.build.version.release"},
        {"RELEASE", "ro.vendor.build.version.release"},
        {"RELEASE", "ro.product.build.version.release"},
        {"RELEASE", "ro.odm.build.version.release"},

        {"RELEASE", "ro.system.build.version.release_or_codename"},
        {"RELEASE", "ro.vendor.build.version.release_or_codename"},
        {"RELEASE", "ro.product.build.version.release_or_codename"},
        {"RELEASE", "ro.odm.build.version.release_or_codename"},

        {"SDK_INT", "ro.system.build.version.sdk"},
        {"SDK_INT", "ro.vendor.build.version.sdk"},
        {"SDK_INT", "ro.product.build.version.sdk"},
        {"SDK_INT", "ro.odm.build.version.sdk"},

        {"SDK_FULL", "ro.system.build.version.sdk_full"},
        {"SDK_FULL", "ro.vendor.build.version.sdk_full"},
        {"SDK_FULL", "ro.product.build.version.sdk_full"},
        {"SDK_FULL", "ro.odm.build.version.sdk_full"},

        {"SECURITY_PATCH", "ro.system.build.version.security_patch"},
        {"SECURITY_PATCH", "ro.vendor.build.version.security_patch"},
        {"SECURITY_PATCH", "ro.product.build.version.security_patch"},
        {"SECURITY_PATCH", "ro.odm.build.version.security_patch"},
};

static bool props_mode_uses_auto(PropsMode mode) {
    return mode == PropsMode::AUTO || mode == PropsMode::AUTO_MANUAL;
}

static bool props_mode_uses_manual(PropsMode mode) {
    return mode == PropsMode::MANUAL || mode == PropsMode::AUTO_MANUAL;
}

static PropValue make_prop_value(const std::string &value) {
    PropValue out;

    if (value == "__EMPTY__") {
        out.kind = PropValueKind::EMPTY;
        out.value.clear();
        return out;
    }

    if (value == "__NULL__") {
        out.kind = PropValueKind::NULL_VALUE;
        out.value.clear();
        return out;
    }

    out.kind = PropValueKind::SET;
    out.value = value;
    return out;
}

PropMap
dcfg_make_final_props(const Profile &profile) {
    PropMap out;

    auto put_effective =
            [&](const std::string &key,
                const std::string &value) {
                if (key.empty() || value.empty()) {
                    return;
                }

                out[key] = make_prop_value(value);
            };

    if (props_mode_uses_auto(profile.props_mode)) {
        auto copy_build =
                [&](const AutoPropMap &map) {
                    auto it = profile.build.find(map.source);

                    if (it == profile.build.end()) {
                        return;
                    }

                    put_effective(map.target, it->second);
                };

        for (const auto &map : kBuildAutoMaps) {
            copy_build(map);
        }

        auto copy_version =
                [&](const AutoPropMap &map) {
                    auto it = profile.version_info.find(map.source);

                    if (it == profile.version_info.end()) {
                        return;
                    }

                    put_effective(map.target, it->second);
                };

        for (const auto &map : kVersionAutoMaps) {
            copy_version(map);
        }
    }

    if (props_mode_uses_manual(profile.props_mode)) {
        for (const auto &it : profile.props) {
            put_effective(it.first, it.second);
        }
    }

    return out;
}
