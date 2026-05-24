// App-top resetprop companion implementation.
//
// resetprop=true is handled as centralized companion-owned runtime state:
// - app processes only submit acquire requests
// - companion owns prop backup/apply/restore
// - props are reference-counted by key
// - main scope keeps one resetprop lifecycle driven by the main app process
// - package scope keeps one resetprop lifecycle per matched package
// - the last session release restores the original prop value

#include "reset_companion.h"
#include "log.h"
#include "reset_engine.h"
#include "reset_protocol.h"

#ifndef DCFG_NO_LOG
#include <android/log.h>
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

#ifndef DCFG_NO_LOG
#define RLOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "dcfg", __VA_ARGS__)
#define RLOGI(...) __android_log_print(ANDROID_LOG_INFO, "dcfg", __VA_ARGS__)
#define RLOGW(...) __android_log_print(ANDROID_LOG_WARN, "dcfg", __VA_ARGS__)
#define RLOGE(...) __android_log_print(ANDROID_LOG_ERROR, "dcfg", __VA_ARGS__)
#else
#define RLOGD(...) do {} while (0)
#define RLOGI(...) do {} while (0)
#define RLOGW(...) do {} while (0)
#define RLOGE(...) do {} while (0)
#endif

static constexpr int kPollMs = 250;
static constexpr int64_t kInactiveRestoreDelayMs = 2000;

struct PropState {
    ResetPropItem item;
    uint32_t refcount = 0;
    bool applied = false;
};

struct Session {
    pid_t pid = -1;
    std::string package_name;
    std::string profile_name;
    std::vector<std::string> prop_keys;
    int64_t inactive_since_ms = 0;
};

struct PackageSession {
    std::string package_name;
    std::string profile_name;
    pid_t driver_pid = -1;
    std::unordered_map<pid_t, bool> active_pids;
    std::vector<std::string> prop_keys;
    int64_t inactive_since_ms = 0;
};

struct RuntimeState {
    std::mutex lock;
    std::unordered_map<std::string, PropState> props;
    std::unordered_map<pid_t, Session> sessions;
    std::unordered_map<std::string, PackageSession> package_sessions;
    bool monitor_started = false;
};

static RuntimeState g_runtime;

static bool pid_alive(pid_t pid) {
    if (pid <= 0) {
        return false;
    }

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d", pid);

    return access(path, F_OK) == 0;
}

static bool pid_is_top_app(pid_t pid) {
    if (pid <= 0) {
        return false;
    }

    char path[96];
    snprintf(path, sizeof(path), "/proc/%d/cgroup", pid);

    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        return false;
    }

    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0) {
        return false;
    }

    buf[n] = '\0';

    bool top = std::strstr(buf, "top-app") != nullptr;
    RLOGD("resetprop cgroup pid=%d top=%d data=%s", pid, top ? 1 : 0, buf);
    return top;
}

static bool package_session_is_top_app(PackageSession &session) {
    bool top = false;

    for (auto it = session.active_pids.begin(); it != session.active_pids.end();) {
        pid_t pid = it->first;
        if (!pid_alive(pid)) {
            it = session.active_pids.erase(it);
            continue;
        }

        if (pid_is_top_app(pid)) {
            top = true;
        }
        ++it;
    }

    return top;
}

static int64_t monotonic_ms() {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return static_cast<int64_t>(ts.tv_sec) * 1000LL
           + static_cast<int64_t>(ts.tv_nsec / 1000000LL);
}

static bool fake_value_equal(const PropValue &a, const PropValue &b) {
    return a.kind == b.kind && a.value == b.value;
}

static bool apply_single_prop_locked(PropState &state) {
    std::vector<ResetPropItem> one;
    one.push_back(state.item);

    dcfg_reset_backup_originals(one);
    uint32_t applied = dcfg_reset_apply_fake(one);

    if (applied != 1) {
        RLOGW("reset runtime apply failed key=%s", state.item.key.c_str());
        return false;
    }

    state.item = one[0];
    state.applied = true;
    return true;
}

static void restore_single_prop_locked(PropState &state) {
    if (!state.applied) {
        return;
    }

    std::vector<ResetPropItem> one;
    one.push_back(state.item);
    dcfg_reset_restore_originals(one);
    state.applied = false;
}

static void release_prop_keys_locked(
        const std::vector<std::string> &prop_keys,
        const char *reason
) {
    for (const std::string &key : prop_keys) {
        auto prop_it = g_runtime.props.find(key);
        if (prop_it == g_runtime.props.end()) {
            continue;
        }

        PropState &state = prop_it->second;
        if (state.refcount > 0) {
            --state.refcount;
        }

        RLOGD("reset runtime release key=%s refcount=%u reason=%s", key.c_str(), state.refcount, reason ? reason : "-");

        if (state.refcount == 0) {
            RLOGI("reset runtime restore key=%s reason=refcount_zero", key.c_str());
            restore_single_prop_locked(state);
            g_runtime.props.erase(prop_it);
        }
    }
}

static void release_session_locked(pid_t pid, const char *reason) {
    auto session_it = g_runtime.sessions.find(pid);
    if (session_it == g_runtime.sessions.end()) {
        return;
    }

    Session session = std::move(session_it->second);
    g_runtime.sessions.erase(session_it);

    RLOGI("reset runtime release scope=main pid=%d package=%s profile=%s reason=%s props=%zu",
          pid, session.package_name.c_str(), session.profile_name.c_str(), reason ? reason : "-", session.prop_keys.size());

    release_prop_keys_locked(session.prop_keys, reason);
}

static void release_session_locked(pid_t pid) {
    release_session_locked(pid, "replace");
}

static void release_package_session_locked(const std::string &package_name, const char *reason) {
    auto session_it = g_runtime.package_sessions.find(package_name);
    if (session_it == g_runtime.package_sessions.end()) {
        return;
    }

    PackageSession session = std::move(session_it->second);
    g_runtime.package_sessions.erase(session_it);

    RLOGI("reset runtime release scope=package package=%s driver_pid=%d profile=%s reason=%s props=%zu active_pids=%zu",
          session.package_name.c_str(),
          session.driver_pid,
          session.profile_name.c_str(),
          reason ? reason : "-",
          session.prop_keys.size(),
          session.active_pids.size());

    release_prop_keys_locked(session.prop_keys, reason);
}

static void rollback_acquire_locked(pid_t pid, const std::vector<std::string> &keys) {
    Session session;
    session.pid = pid;
    session.prop_keys = keys;
    g_runtime.sessions[pid] = std::move(session);
    release_session_locked(pid, "rollback");
}

static void rollback_package_acquire_locked(const std::string &package_name, const std::vector<std::string> &keys) {
    PackageSession session;
    session.package_name = package_name;
    session.prop_keys = keys;
    g_runtime.package_sessions[package_name] = std::move(session);
    release_package_session_locked(package_name, "rollback");
}

static bool props_conflict_locked(const std::vector<ResetPropItem> &items) {
    for (const auto &item : items) {
        auto prop_it = g_runtime.props.find(item.key);
        if (prop_it != g_runtime.props.end()
            && prop_it->second.refcount > 0
            && !fake_value_equal(prop_it->second.item.fake_value, item.fake_value)) {
            RLOGW("reset runtime conflict key=%s old_kind=%u new_kind=%u",
                  item.key.c_str(),
                  static_cast<uint32_t>(prop_it->second.item.fake_value.kind),
                  static_cast<uint32_t>(item.fake_value.kind));
            return true;
        }
    }
    return false;
}

static bool acquire_session_locked(
        pid_t pid,
        const std::string &package_name,
        const std::string &profile_name,
        const std::vector<ResetPropItem> &items,
        uint32_t &applied
) {
    applied = 0;

    if (pid <= 0 || items.empty()) {
        return false;
    }

    // A duplicate acquire for the same pid means the previous session is stale.
    // Release it first to avoid leaking main-process references.
    release_session_locked(pid, "replace");

    if (props_conflict_locked(items)) {
        return false;
    }

    Session session;
    session.pid = pid;
    session.package_name = package_name;
    session.profile_name = profile_name;
    session.prop_keys.reserve(items.size());

    std::vector<std::string> acquired_keys;
    acquired_keys.reserve(items.size());

    for (const auto &item : items) {
        auto prop_it = g_runtime.props.find(item.key);

        if (prop_it == g_runtime.props.end()) {
            PropState state;
            state.item = item;
            auto inserted = g_runtime.props.emplace(item.key, std::move(state));
            prop_it = inserted.first;
        }

        PropState &state = prop_it->second;

        if (state.refcount == 0) {
            if (!apply_single_prop_locked(state)) {
                g_runtime.props.erase(item.key);
                rollback_acquire_locked(pid, acquired_keys);
                return false;
            }
        }

        ++state.refcount;
        ++applied;
        acquired_keys.push_back(item.key);
        session.prop_keys.push_back(item.key);

        RLOGD("reset runtime acquire scope=main key=%s refcount=%u", item.key.c_str(), state.refcount);
    }

    g_runtime.sessions[pid] = std::move(session);

    RLOGI("reset runtime acquire scope=main pid=%d package=%s profile=%s props=%zu active_main_sessions=%zu active_package_sessions=%zu active_props=%zu",
          pid,
          package_name.c_str(),
          profile_name.c_str(),
          items.size(),
          g_runtime.sessions.size(),
          g_runtime.package_sessions.size(),
          g_runtime.props.size());
    return true;
}

static bool acquire_package_session_locked(
        pid_t pid,
        const std::string &package_name,
        const std::string &profile_name,
        const std::vector<ResetPropItem> &items,
        uint32_t &applied
) {
    applied = 0;

    if (pid <= 0 || package_name.empty() || items.empty()) {
        return false;
    }

    auto existing = g_runtime.package_sessions.find(package_name);
    if (existing != g_runtime.package_sessions.end()) {
        if (props_conflict_locked(items)) {
            return false;
        }

        PackageSession &session = existing->second;
        session.driver_pid = pid;
        session.profile_name = profile_name;
        session.active_pids[pid] = true;
        session.inactive_since_ms = 0;
        applied = static_cast<uint32_t>(session.prop_keys.size());

        RLOGI("reset runtime acquire scope=package package=%s driver_pid=%d profile=%s props=%zu active_pids=%zu active_package_sessions=%zu active_props=%zu reused=1",
              package_name.c_str(),
              pid,
              profile_name.c_str(),
              items.size(),
              session.active_pids.size(),
              g_runtime.package_sessions.size(),
              g_runtime.props.size());
        return true;
    }

    if (props_conflict_locked(items)) {
        return false;
    }

    PackageSession session;
    session.package_name = package_name;
    session.profile_name = profile_name;
    session.driver_pid = pid;
    session.active_pids[pid] = true;
    session.prop_keys.reserve(items.size());

    std::vector<std::string> acquired_keys;
    acquired_keys.reserve(items.size());

    for (const auto &item : items) {
        auto prop_it = g_runtime.props.find(item.key);

        if (prop_it == g_runtime.props.end()) {
            PropState state;
            state.item = item;
            auto inserted = g_runtime.props.emplace(item.key, std::move(state));
            prop_it = inserted.first;
        }

        PropState &state = prop_it->second;

        if (state.refcount == 0) {
            if (!apply_single_prop_locked(state)) {
                g_runtime.props.erase(item.key);
                rollback_package_acquire_locked(package_name, acquired_keys);
                return false;
            }
        }

        ++state.refcount;
        ++applied;
        acquired_keys.push_back(item.key);
        session.prop_keys.push_back(item.key);

        RLOGD("reset runtime acquire scope=package key=%s refcount=%u", item.key.c_str(), state.refcount);
    }

    g_runtime.package_sessions[package_name] = std::move(session);

    RLOGI("reset runtime acquire scope=package package=%s driver_pid=%d profile=%s props=%zu active_main_sessions=%zu active_package_sessions=%zu active_props=%zu",
          package_name.c_str(),
          pid,
          profile_name.c_str(),
          items.size(),
          g_runtime.sessions.size(),
          g_runtime.package_sessions.size(),
          g_runtime.props.size());
    return true;
}

static void *monitor_thread_main(void *) {
    RLOGI("reset runtime monitor start mode=top-app");

    for (;;) {
        std::vector<pid_t> release_pids;
        std::vector<std::string> release_packages;
        int64_t now_ms = monotonic_ms();

        {
            std::lock_guard<std::mutex> guard(g_runtime.lock);

            for (auto &entry : g_runtime.sessions) {
                pid_t pid = entry.first;
                Session &session = entry.second;

                if (!pid_alive(pid)) {
                    RLOGI("reset runtime pid exit pid=%d package=%s", pid, session.package_name.c_str());
                    release_pids.push_back(pid);
                    continue;
                }

                bool active = pid_is_top_app(pid);

                if (active) {
                    session.inactive_since_ms = 0;
                    continue;
                }

                if (session.inactive_since_ms == 0) {
                    session.inactive_since_ms = now_ms;
                    continue;
                }

                if (now_ms - session.inactive_since_ms >= kInactiveRestoreDelayMs) {
                    RLOGI("reset runtime non-top release scope=main pid=%d package=%s delay_ms=%lld",
                          pid,
                          session.package_name.c_str(),
                          static_cast<long long>(now_ms - session.inactive_since_ms));
                    release_pids.push_back(pid);
                }
            }

            for (auto &entry : g_runtime.package_sessions) {
                PackageSession &session = entry.second;
                bool active = package_session_is_top_app(session);

                if (session.active_pids.empty()) {
                    RLOGI("reset runtime package all-pids-exit package=%s", session.package_name.c_str());
                    release_packages.push_back(entry.first);
                    continue;
                }

                if (active) {
                    session.inactive_since_ms = 0;
                    continue;
                }

                if (session.inactive_since_ms == 0) {
                    session.inactive_since_ms = now_ms;
                    continue;
                }

                if (now_ms - session.inactive_since_ms >= kInactiveRestoreDelayMs) {
                    RLOGI("reset runtime non-top release scope=package package=%s driver_pid=%d delay_ms=%lld active_pids=%zu",
                          session.package_name.c_str(),
                          session.driver_pid,
                          static_cast<long long>(now_ms - session.inactive_since_ms),
                          session.active_pids.size());
                    release_packages.push_back(entry.first);
                }
            }

            for (pid_t pid : release_pids) {
                release_session_locked(pid, "exit_or_top_lost");
            }
            for (const std::string &package_name : release_packages) {
                release_package_session_locked(package_name, "exit_or_top_lost");
            }
        }

        usleep(kPollMs * 1000);
    }

    return nullptr;
}

static bool start_monitor_locked() {
    if (g_runtime.monitor_started) {
        return true;
    }

    pthread_t thread {};
    int rc = pthread_create(&thread, nullptr, monitor_thread_main, nullptr);
    if (rc != 0) {
        RLOGW("reset runtime monitor pthread_create failed rc=%d", rc);
        return false;
    }

    pthread_detach(thread);
    g_runtime.monitor_started = true;
    return true;
}

bool dcfg_reset_companion_send(
        zygisk::Api *api,
        int pid,
        const std::string &package_name,
        const std::string &profile_name,
        ResetpropScope scope,
        const PropMap &props
) {
    RLOGI("reset companion send enter pid=%d package=%s profile=%s props=%zu mode=top-app scope=%s api=%d", pid, package_name.c_str(), profile_name.c_str(), props.size(), dcfg_resetprop_scope_name(scope), api ? 1 : 0);

    if (!api || pid <= 0 || props.empty()) {
        RLOGW("reset companion send invalid api=%d pid=%d props=%zu", api ? 1 : 0, pid, props.size());
        return false;
    }

    errno = 0;
    RLOGI("reset companion before connectCompanion pid=%d props=%zu", pid, props.size());
    int fd = api->connectCompanion();
    int saved_errno = errno;
    RLOGI("reset companion after connectCompanion fd=%d errno=%d", fd, saved_errno);

    if (fd < 0) {
        DLOGW("reset companion connect failed");
        RLOGW("reset companion connect failed fd=%d errno=%d", fd, saved_errno);
        return false;
    }

    RLOGI("reset companion connected fd=%d", fd);

    bool ok = true;

    ok = ok && dcfg_reset_write_u32(fd, kDcfgResetMagic);
    RLOGD("reset companion write magic ok=%d", ok ? 1 : 0);
    ok = ok && dcfg_reset_write_u32(fd, kDcfgResetVersion);
    RLOGD("reset companion write version ok=%d", ok ? 1 : 0);
    ok = ok && dcfg_reset_write_i32(fd, static_cast<int32_t>(pid));
    RLOGD("reset companion write pid ok=%d", ok ? 1 : 0);
    ok = ok && dcfg_reset_write_string(fd, package_name);
    RLOGD("reset companion write package ok=%d", ok ? 1 : 0);
    ok = ok && dcfg_reset_write_string(fd, profile_name);
    RLOGD("reset companion write profile ok=%d", ok ? 1 : 0);
    ok = ok && dcfg_reset_write_string(fd, dcfg_resetprop_scope_name(scope));
    RLOGD("reset companion write scope ok=%d", ok ? 1 : 0);
    uint32_t count = 0;

    for (const auto &it : props) {
        if (!it.first.empty()
            && (it.second.kind == PropValueKind::EMPTY
                || it.second.kind == PropValueKind::NULL_VALUE
                || !it.second.value.empty())) {
            ++count;
        }
    }

    if (count > kDcfgResetMaxProps) {
        count = kDcfgResetMaxProps;
    }

    RLOGI("reset companion payload count=%u filtered_from=%zu", count, props.size());
    ok = ok && dcfg_reset_write_u32(fd, count);
    RLOGD("reset companion write count ok=%d", ok ? 1 : 0);

    uint32_t written = 0;

    if (ok) {
        for (const auto &it : props) {
            if (it.first.empty()
                || !(it.second.kind == PropValueKind::EMPTY
                     || it.second.kind == PropValueKind::NULL_VALUE
                     || !it.second.value.empty())) {
                continue;
            }

            if (written >= count) {
                break;
            }

            ok = ok && dcfg_reset_write_string(fd, it.first);
            ok = ok && dcfg_reset_write_u32(fd, static_cast<uint32_t>(it.second.kind));
            ok = ok && dcfg_reset_write_string(fd, it.second.value);
            ++written;
            if (written <= 8 || written == count) {
                RLOGD("reset companion write item index=%u/%u key=%s kind=%u ok=%d",
                      written, count, it.first.c_str(), static_cast<uint32_t>(it.second.kind), ok ? 1 : 0);
            }

            if (!ok) {
                RLOGW("reset companion write item failed index=%u key=%s errno=%d", written, it.first.c_str(), errno);
                break;
            }
        }
    }

    bool response_success = false;
    bool response_read = false;
    uint32_t response_applied = 0;
    uint32_t response_requested = 0;

    if (!ok) {
        DLOGW("reset companion send failed");
        RLOGW("reset companion send failed written=%u count=%u", written, count);
    } else {
        RLOGI("reset companion send ok written=%u count=%u", written, count);

        RLOGI("reset companion waiting response fd=%d", fd);
        response_read = dcfg_reset_read_bool_response(fd, response_success, response_applied, response_requested);
        if (response_read) {
            RLOGI("reset companion response success=%d applied=%u requested=%u",
                  response_success ? 1 : 0, response_applied, response_requested);
        } else {
            RLOGW("reset companion response read failed errno=%d", errno);
        }
    }

    RLOGI("reset companion closing fd=%d ok=%d written=%u count=%u response_read=%d response_success=%d",
          fd, ok ? 1 : 0, written, count, response_read ? 1 : 0, response_success ? 1 : 0);
    close(fd);

    return ok && response_read && response_success;
}

void dcfg_reset_companion_entry(int client_fd) {
    RLOGI("reset companion entry fd=%d", client_fd);
    uint32_t magic = 0;
    uint32_t version = 0;
    int32_t pid = -1;
    std::string package_name;
    std::string profile_name;
    std::string scope_name;
    uint32_t count = 0;

    if (!dcfg_reset_read_u32(client_fd, magic)
        || !dcfg_reset_read_u32(client_fd, version)
        || magic != kDcfgResetMagic
        || version != kDcfgResetVersion
        || !dcfg_reset_read_i32(client_fd, pid)
        || !dcfg_reset_read_string(client_fd, package_name)
        || !dcfg_reset_read_string(client_fd, profile_name)
        || !dcfg_reset_read_string(client_fd, scope_name)
        || !dcfg_reset_read_u32(client_fd, count)
        || count > kDcfgResetMaxProps) {
        RLOGW("reset companion invalid header magic=0x%x version=%u pid=%d count=%u", magic, version, pid, count);
        close(client_fd);
        return;
    }

    ResetpropScope scope = scope_name == "main" ? ResetpropScope::MAIN : ResetpropScope::PACKAGE;

    RLOGI("reset companion request pid=%d package=%s profile=%s mode=top-app scope=%s count=%u", pid, package_name.c_str(), profile_name.c_str(), dcfg_resetprop_scope_name(scope), count);

    std::vector<ResetPropItem> items;
    items.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        ResetPropItem item;

        uint32_t kind = 0;

        if (!dcfg_reset_read_string(client_fd, item.key)
            || !dcfg_reset_read_u32(client_fd, kind)
            || !dcfg_reset_read_string(client_fd, item.fake_value.value)
            || kind > static_cast<uint32_t>(PropValueKind::NULL_VALUE)) {
            RLOGW("reset companion read item failed index=%u", i);
            close(client_fd);
            return;
        }

        item.fake_value.kind = static_cast<PropValueKind>(kind);

        if (!item.key.empty()
            && (item.fake_value.kind == PropValueKind::EMPTY
                || item.fake_value.kind == PropValueKind::NULL_VALUE
                || !item.fake_value.value.empty())) {
            if (i < 8 || i + 1 == count) {
                RLOGD("reset companion read item index=%u/%u key=%s kind=%u value=%s",
                      i + 1, count, item.key.c_str(), kind, item.fake_value.value.c_str());
            }
            items.push_back(std::move(item));
        }
    }

    if (pid <= 0 || items.empty()) {
        RLOGW("reset companion empty request pid=%d items=%zu", pid, items.size());
        bool resp_ok = dcfg_reset_write_bool_response(client_fd, false, 0, count);
        RLOGI("reset companion response sent success=0 ok=%d", resp_ok ? 1 : 0);
        close(client_fd);
        return;
    }

    uint32_t applied = 0;
    bool success = false;

    {
        std::lock_guard<std::mutex> guard(g_runtime.lock);
        bool monitor_ok = start_monitor_locked();
        if (scope == ResetpropScope::MAIN) {
            success = monitor_ok && acquire_session_locked(
                    static_cast<pid_t>(pid),
                    package_name,
                    profile_name,
                    items,
                    applied
            );
        } else {
            success = monitor_ok && acquire_package_session_locked(
                    static_cast<pid_t>(pid),
                    package_name,
                    profile_name,
                    items,
                    applied
            );
        }
    }

    size_t active_props = 0;
    size_t active_sessions = 0;
    size_t active_package_sessions = 0;
    {
        std::lock_guard<std::mutex> guard(g_runtime.lock);
        active_props = g_runtime.props.size();
        active_sessions = g_runtime.sessions.size();
        active_package_sessions = g_runtime.package_sessions.size();
    }

    bool resp_ok = dcfg_reset_write_bool_response(client_fd, success, applied, static_cast<uint32_t>(items.size()));
    RLOGI("reset companion response sent success=%d applied=%u requested=%zu active_props=%zu active_main_sessions=%zu active_package_sessions=%zu ok=%d",
          success ? 1 : 0,
          applied,
          items.size(),
          active_props,
          active_sessions,
          active_package_sessions,
          resp_ok ? 1 : 0);
    close(client_fd);
}

void dcfg_companion_entry(int client_fd) {
    uint32_t magic = 0;

    errno = 0;
    RLOGI("companion entry dispatch fd=%d", client_fd);
    ssize_t n = recv(client_fd, &magic, sizeof(magic), MSG_PEEK);
    int saved_errno = errno;
    RLOGI("companion entry peek fd=%d n=%zd errno=%d magic=0x%x", client_fd, n, saved_errno, magic);

    if (n == static_cast<ssize_t>(sizeof(magic)) && magic == kDcfgResetMagic) {
        RLOGI("companion dispatch reset fd=%d", client_fd);
        dcfg_reset_companion_entry(client_fd);
        return;
    }

    RLOGD("companion dispatch log fd=%d peek_n=%zd magic=0x%x", client_fd, n, magic);

#ifndef DCFG_NO_LOG
    dcfg_log_companion(client_fd);
#else
    close(client_fd);
#endif
}
