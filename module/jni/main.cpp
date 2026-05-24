// DCFG runtime lifecycle and dlclose policy.

#include "zygisk.hpp"
#include "config.h"
#include "apply.h"
#include "log.h"
#include "module_dir.h"
#include "reset_companion.h"

#include <string>
#include <utility>
#include <unordered_map>
#include <unistd.h>

static void release_profile_memory(Profile &profile) {
    Profile empty;
    profile = std::move(empty);
}

static void dlclose_module_if_possible(zygisk::Api *api) {
    if (api) {
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }
}

static std::string jstring_to_string(JNIEnv *env, jstring value) {
    if (!env || !value) return {};

    const char *chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) return {};

    std::string out = chars;

    env->ReleaseStringUTFChars(value, chars);

    return out;
}

static std::string basename_from_path(const std::string &path) {
    if (path.empty()) return {};

    size_t end = path.size();

    while (end > 0 && path[end - 1] == '/') {
        --end;
    }

    if (end == 0) return {};

    size_t pos = path.rfind('/', end - 1);

    if (pos == std::string::npos) {
        return path.substr(0, end);
    }

    return path.substr(pos + 1, end - pos - 1);
}

static std::string package_from_process_name(const std::string &process_name) {
    if (process_name.empty()) return {};

    size_t colon = process_name.find(':');

    if (colon == std::string::npos) {
        return process_name;
    }

    return process_name.substr(0, colon);
}

class DcfgModule final : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;

        int module_fd = api ? api->getModuleDir() : -1;
        dcfg_set_module_dir_fd(module_fd);

#ifndef DCFG_NO_LOG
        // Keep onLoad lightweight for every process.
        // Companion logging is connected lazily only after a package rule matches
        // and the reloaded log level requires file logging.
        dcfg_log_init();
#endif
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        package_name_.clear();
        process_name_.clear();
        matched_ = false;
        is_child_process_ = false;
        matched_profile_ = Profile();
        resetprop_scope_ = ResetpropScope::PACKAGE;
        matched_profile_name_.clear();
#ifndef DCFG_NO_LOG
        dcfg_log_set_enabled(false);
#endif

        if (!env_ || !args || !args->nice_name) {
            return;
        }

        process_name_ = jstring_to_string(env_, args->nice_name);

        std::string app_data_dir;

        if (args->app_data_dir) {
            app_data_dir = jstring_to_string(env_, args->app_data_dir);
        }

        package_name_ = basename_from_path(app_data_dir);

        if (package_name_.empty()) {
            package_name_ = package_from_process_name(process_name_);
        }

        if (package_name_.empty()) {
            return;
        }

        is_child_process_ =
                !process_name_.empty()
                && process_name_ != package_name_
                && process_name_.find(package_name_ + ":") == 0;

        auto m = dcfg_load_and_match_config(package_name_, is_child_process_);

        if (!m.matched) {
            dlclose_module_if_possible(api_);
            return;
        }

        matched_ = true;
        matched_profile_ = m.profile;
        matched_profile_name_ = m.profile_name;
        resetprop_scope_ = m.resetprop_scope;

#ifndef DCFG_NO_LOG
        dcfg_log_set_package(process_name_.empty() ? package_name_.c_str() : process_name_.c_str());
        dcfg_log_set_enabled(true);
        dcfg_log_reload_config();

        if (dcfg_log_should_connect_companion()) {
            int companion_fd = api_ ? api_->connectCompanion() : -1;
            dcfg_log_set_companion_fd(companion_fd);
        }
#endif

        if (is_child_process_) {
            DLOGI(
                    "matched package=%s process=%s profile=%s child=1 build=%zu props=%zu version=%zu props_mode=%s props_hook=%s resetprop=%d",
                    package_name_.c_str(),
                    process_name_.c_str(),
                    matched_profile_name_.c_str(),
                    matched_profile_.build.size(),
                    matched_profile_.props.size(),
                    matched_profile_.version_info.size(),
                    dcfg_props_mode_name(matched_profile_.props_mode),
                    dcfg_props_hook_mode_name(matched_profile_.props_hook_mode),
                    matched_profile_.resetprop_enabled ? 1 : 0
            );

            DLOGD(
                    "applying package=%s process=%s profile=%s child=1 stage=preAppSpecialize",
                    package_name_.c_str(),
                    process_name_.c_str(),
                    matched_profile_name_.c_str()
            );
        } else {
            DLOGI(
                    "matched package=%s profile=%s build=%zu props=%zu version=%zu props_mode=%s props_hook=%s resetprop=%d",
                    package_name_.c_str(),
                    matched_profile_name_.c_str(),
                    matched_profile_.build.size(),
                    matched_profile_.props.size(),
                    matched_profile_.version_info.size(),
                    dcfg_props_mode_name(matched_profile_.props_mode),
                    dcfg_props_hook_mode_name(matched_profile_.props_hook_mode),
                    matched_profile_.resetprop_enabled ? 1 : 0
            );

            DLOGD(
                    "applying package=%s profile=%s stage=preAppSpecialize",
                    package_name_.c_str(),
                    matched_profile_name_.c_str()
            );
        }

        PropMap final_props;

        const bool hook_props = dcfg_apply_profile(
                api_,
                env_,
                matched_profile_,
                &final_props
        );

        DLOGD(
                "resetprop gate child=%d enabled=%d final_props=%zu package=%s profile=%s scope=%s",
                is_child_process_ ? 1 : 0,
                matched_profile_.resetprop_enabled ? 1 : 0,
                final_props.size(),
                package_name_.c_str(),
                matched_profile_name_.c_str(),
                dcfg_resetprop_scope_name(resetprop_scope_)
        );

        bool resetprop_needed = matched_profile_.resetprop_enabled
                && !final_props.empty()
                && (resetprop_scope_ == ResetpropScope::PACKAGE || !is_child_process_);
        bool resetprop_ok = true;

        if (resetprop_needed) {
            DLOGI(
                    "resetprop send package=%s profile=%s pid=%d props=%zu scope=%s",
                    package_name_.c_str(),
                    matched_profile_name_.c_str(),
                    getpid(),
                    final_props.size(),
                    dcfg_resetprop_scope_name(resetprop_scope_)
            );

            resetprop_ok = dcfg_reset_companion_send(
                    api_,
                    getpid(),
                    package_name_,
                    matched_profile_name_,
                    resetprop_scope_,
                    final_props
            );

            DLOGI(
                    "resetprop result package=%s profile=%s ok=%d",
                    package_name_.c_str(),
                    matched_profile_name_.c_str(),
                    resetprop_ok ? 1 : 0
            );
        } else {
            DLOGD(
                    "resetprop skipped child=%d enabled=%d final_props=%zu scope=%s",
                    is_child_process_ ? 1 : 0,
                    matched_profile_.resetprop_enabled ? 1 : 0,
                    final_props.size(),
                    dcfg_resetprop_scope_name(resetprop_scope_)
            );
        }

        release_profile_memory(matched_profile_);

        if (!hook_props && (!resetprop_needed || resetprop_ok)) {
            DLOGD(
                    "dlclose package=%s profile=%s reason=lite resetprop_needed=%d resetprop_ok=%d stage=preAppSpecialize",
                    package_name_.c_str(),
                    matched_profile_name_.c_str(),
                    resetprop_needed ? 1 : 0,
                    resetprop_ok ? 1 : 0
            );

            dlclose_module_if_possible(api_);
        } else if (!hook_props) {
            DLOGW(
                    "dlclose skipped package=%s profile=%s reason=resetprop_failed stage=preAppSpecialize",
                    package_name_.c_str(),
                    matched_profile_name_.c_str()
            );
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        (void) args;
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *) override {}

    void postServerSpecialize(const zygisk::ServerSpecializeArgs *) override {}

private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;

    std::string package_name_;
    std::string process_name_;

    bool matched_ = false;
    bool is_child_process_ = false;
    Profile matched_profile_;
    ResetpropScope resetprop_scope_ = ResetpropScope::PACKAGE;

    std::string matched_profile_name_;
};

REGISTER_ZYGISK_MODULE(DcfgModule)
REGISTER_ZYGISK_COMPANION(dcfg_companion_entry)