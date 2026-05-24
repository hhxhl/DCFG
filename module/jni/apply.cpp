// Runtime Build field application and conditional hook installation.

#include "apply.h"
#include "system_props.h"
#include "log.h"

#include <mutex>
#include <cerrno>
#include <cstdlib>

static std::mutex g_lock;

static void clear(JNIEnv *env) {
    if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

static bool parse_int_value(const std::string &value, jint &out) {
    if (value.empty()) {
        return false;
    }

    char *end = nullptr;
    errno = 0;
    long v = std::strtol(value.c_str(), &end, 10);

    if (errno != 0 || !end || *end != '\0') {
        return false;
    }

    out = static_cast<jint>(v);
    return true;
}

static bool parse_long_value(const std::string &value, jlong &out) {
    if (value.empty()) {
        return false;
    }

    char *end = nullptr;
    errno = 0;
    long long v = std::strtoll(value.c_str(), &end, 10);

    if (errno != 0 || !end || *end != '\0') {
        return false;
    }

    out = static_cast<jlong>(v);
    return true;
}

static bool parse_bool_value(const std::string &value, jboolean &out) {
    if (value == "true" || value == "1") {
        out = JNI_TRUE;
        return true;
    }

    if (value == "false" || value == "0") {
        out = JNI_FALSE;
        return true;
    }

    return false;
}

static bool set_static_string_field(
        JNIEnv *env,
        jclass cls,
        const char *class_name,
        const char *field,
        const std::string &value
) {
    if (!env || !cls || !field || value.empty()) {
        return false;
    }

    jfieldID fid = env->GetStaticFieldID(cls, field, "Ljava/lang/String;");

    if (!fid) {
        clear(env);
        return false;
    }

    jstring v = env->NewStringUTF(value.c_str());

    if (!v) {
        DLOGW("NewStringUTF failed for %s.%s", class_name, field);
        clear(env);
        return false;
    }

    env->SetStaticObjectField(cls, fid, v);

    if (env->ExceptionCheck()) {
        DLOGW("SetStaticObjectField exception: %s.%s", class_name, field);
        clear(env);
        env->DeleteLocalRef(v);
        return false;
    }

    DLOGD("%s.%s set as String", class_name, field);

    env->DeleteLocalRef(v);
    return true;
}

static bool set_static_int_field(
        JNIEnv *env,
        jclass cls,
        const char *class_name,
        const char *field,
        const std::string &value
) {
    jint parsed = 0;

    if (!parse_int_value(value, parsed)) {
        return false;
    }

    jfieldID fid = env->GetStaticFieldID(cls, field, "I");

    if (!fid) {
        clear(env);
        return false;
    }

    env->SetStaticIntField(cls, fid, parsed);

    if (env->ExceptionCheck()) {
        DLOGW("SetStaticIntField exception: %s.%s", class_name, field);
        clear(env);
        return false;
    }

    DLOGD(
            "%s.%s set as int value=%d",
            class_name,
            field,
            static_cast<int>(parsed)
    );

    return true;
}

static bool set_static_long_field(
        JNIEnv *env,
        jclass cls,
        const char *class_name,
        const char *field,
        const std::string &value
) {
    jlong parsed = 0;

    if (!parse_long_value(value, parsed)) {
        return false;
    }

    jfieldID fid = env->GetStaticFieldID(cls, field, "J");

    if (!fid) {
        clear(env);
        return false;
    }

    env->SetStaticLongField(cls, fid, parsed);

    if (env->ExceptionCheck()) {
        DLOGW("SetStaticLongField exception: %s.%s", class_name, field);
        clear(env);
        return false;
    }

    DLOGD("%s.%s set as long", class_name, field);

    return true;
}

static bool set_static_bool_field(
        JNIEnv *env,
        jclass cls,
        const char *class_name,
        const char *field,
        const std::string &value
) {
    jboolean parsed = JNI_FALSE;

    if (!parse_bool_value(value, parsed)) {
        return false;
    }

    jfieldID fid = env->GetStaticFieldID(cls, field, "Z");

    if (!fid) {
        clear(env);
        return false;
    }

    env->SetStaticBooleanField(cls, fid, parsed);

    if (env->ExceptionCheck()) {
        DLOGW("SetStaticBooleanField exception: %s.%s", class_name, field);
        clear(env);
        return false;
    }

    DLOGD("%s.%s set as boolean", class_name, field);

    return true;
}

static bool is_known_int_static_field(const std::string &field) {
    return field == "SDK_INT"
           || field == "SDK_INT_FULL"
           || field == "PREVIEW_SDK_INT"
           || field == "DEVICE_INITIAL_SDK_INT"
           || field == "MEDIA_PERFORMANCE_CLASS";
}

static void set_static_field_auto(
        JNIEnv *env,
        jclass cls,
        const char *class_name,
        const std::string &field,
        const std::string &value
) {
    if (!env || !cls || field.empty() || value.empty()) {
        return;
    }

    if (is_known_int_static_field(field)) {
        if (set_static_int_field(env, cls, class_name, field.c_str(), value)) {
            return;
        }

        DLOGW(
                "known int static field apply failed: %s.%s value=%s",
                class_name,
                field.c_str(),
                value.c_str()
        );

        return;
    }

    if (set_static_string_field(env, cls, class_name, field.c_str(), value)) {
        return;
    }

    if (set_static_int_field(env, cls, class_name, field.c_str(), value)) {
        return;
    }

    if (set_static_long_field(env, cls, class_name, field.c_str(), value)) {
        return;
    }

    if (set_static_bool_field(env, cls, class_name, field.c_str(), value)) {
        return;
    }

    DLOGW(
            "static field not found or unsupported type: %s.%s",
            class_name,
            field.c_str()
    );
}


static void apply_build(JNIEnv *env, const Profile &profile) {
    if (!env || profile.build.empty()) {
        return;
    }

    jclass build_cls = env->FindClass("android/os/Build");

    if (!build_cls) {
        DLOGW("FindClass failed: android/os/Build");
        clear(env);
        return;
    }

    for (const auto &it : profile.build) {
        set_static_field_auto(env, build_cls, "android.os.Build", it.first, it.second);
    }

    env->DeleteLocalRef(build_cls);
}

static void apply_build_version(JNIEnv *env, const Profile &profile) {
    if (!env || profile.version_info.empty()) {
        return;
    }

    jclass version_cls = env->FindClass("android/os/Build$VERSION");

    if (!version_cls) {
        DLOGW("FindClass failed: android/os/Build$VERSION");
        clear(env);
        return;
    }

    for (const auto &it : profile.version_info) {
        set_static_field_auto(env, version_cls, "android.os.Build.VERSION", it.first, it.second);
    }

    env->DeleteLocalRef(version_cls);
}

bool dcfg_apply_profile(
        zygisk::Api *api,
        JNIEnv *env,
        const Profile &profile,
        PropMap *final_props_out
) {
    std::lock_guard<std::mutex> guard(g_lock);

    apply_build(env, profile);
    apply_build_version(env, profile);

    auto props = dcfg_make_final_props(profile);

    if (final_props_out) {
        *final_props_out = props;
    }

    dcfg_set_current_props(props);

    const bool hook_props =
            !props.empty()
            && (profile.props_hook_mode == PropsHookMode::RUNTIME
                || profile.props_hook_mode == PropsHookMode::FULL);

    if (hook_props) {
        dcfg_install_system_property_hooks(api);
    }

    DLOGD(
            "applied build=%zu version=%zu final_props=%zu props_mode=%s props_hook=%s hook_props=%d",
            profile.build.size(),
            profile.version_info.size(),
            props.size(),
            dcfg_props_mode_name(profile.props_mode),
            dcfg_props_hook_mode_name(profile.props_hook_mode),
            hook_props ? 1 : 0
    );

    return hook_props;
}