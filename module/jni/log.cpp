// Debug-only companion logging implementation.
// Release builds do not compile this file.

#ifndef DCFG_NO_LOG

#include "log.h"

#include <android/log.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

static constexpr const char *kTag = "dcfg";
static constexpr const char *kRootLogDir = "/data/adb/dcfg";
static constexpr const char *kRootLogPath = "/data/adb/dcfg/dcfg.log";

static std::mutex g_log_mutex;
static int g_companion_fd = -1;
static std::string g_package = "-";
static bool g_log_enabled_for_process = false;

static long current_tid() {
#ifdef __NR_gettid
    return static_cast<long>(syscall(__NR_gettid));
#else
    return static_cast<long>(getpid());
#endif
}

static const char *level_name(DcfgLogLevel level) {
    switch (level) {
        case DcfgLogLevel::ERROR:
            return "ERROR";
        case DcfgLogLevel::WARN:
            return "WARN";
        case DcfgLogLevel::INFO:
            return "INFO";
        case DcfgLogLevel::DEBUG:
            return "DEBUG";
        case DcfgLogLevel::NONE:
        default:
            return "NONE";
    }
}

static int android_level(DcfgLogLevel level) {
    switch (level) {
        case DcfgLogLevel::ERROR:
            return ANDROID_LOG_ERROR;
        case DcfgLogLevel::WARN:
            return ANDROID_LOG_WARN;
        case DcfgLogLevel::INFO:
            return ANDROID_LOG_INFO;
        case DcfgLogLevel::DEBUG:
            return ANDROID_LOG_DEBUG;
        case DcfgLogLevel::NONE:
        default:
            return ANDROID_LOG_DEFAULT;
    }
}

static void write_all_fd(int fd, const char *buf, size_t len) {
    size_t off = 0;

    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);

        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        break;
    }
}

void dcfg_log_set_companion_fd(int fd) {
    std::lock_guard<std::mutex> lock(g_log_mutex);

    if (g_companion_fd >= 0 && g_companion_fd != fd) {
        close(g_companion_fd);
    }

    g_companion_fd = fd;
}

bool dcfg_log_should_connect_companion() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    return g_log_enabled_for_process;
}

void dcfg_log_init() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_package = "-";
    g_log_enabled_for_process = false;
}

void dcfg_log_reload_config() {
    // Debug builds always use DEBUG logging.
    // Release builds compile logging out entirely.
}

void dcfg_log_set_package(const char *package_name) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_package = package_name && package_name[0] ? package_name : "-";
}

void dcfg_log_set_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_enabled_for_process = enabled;
}

void dcfg_log_write(DcfgLogLevel level, const char *fmt, ...) {
    if (level == DcfgLogLevel::NONE) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_log_mutex);

        if (!g_log_enabled_for_process) {
            return;
        }
    }

    char msg[1536];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    __android_log_print(android_level(level), kTag, "%s", msg);

    std::lock_guard<std::mutex> lock(g_log_mutex);

    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tmv{};
    localtime_r(&ts.tv_sec, &tmv);

    char timebuf[64];

    snprintf(
            timebuf,
            sizeof(timebuf),
            "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
            tmv.tm_year + 1900,
            tmv.tm_mon + 1,
            tmv.tm_mday,
            tmv.tm_hour,
            tmv.tm_min,
            tmv.tm_sec,
            ts.tv_nsec / 1000000L
    );

    char line[2048];

    int n = snprintf(
            line,
            sizeof(line),
            "[%s][pid=%d][tid=%ld][pkg=%s][%s] %s\n",
            timebuf,
            getpid(),
            current_tid(),
            g_package.c_str(),
            level_name(level),
            msg
    );

    if (n <= 0) {
        return;
    }

    if (n > static_cast<int>(sizeof(line))) {
        n = static_cast<int>(sizeof(line));
    }

    if (g_companion_fd >= 0) {
        ssize_t written = write(g_companion_fd, line, static_cast<size_t>(n));

        if (written < 0 && errno != EINTR) {
            close(g_companion_fd);
            g_companion_fd = -1;
        }
    }
}

void dcfg_log_companion(int client_fd) {
    mkdir(kRootLogDir, 0755);

    int out = open(
            kRootLogPath,
            O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC,
            0666
    );

    if (out < 0) {
        close(client_fd);
        return;
    }

    fchmod(out, 0666);

    char buf[4096];

    for (;;) {
        ssize_t n = read(client_fd, buf, sizeof(buf));

        if (n > 0) {
            write_all_fd(out, buf, static_cast<size_t>(n));
            fsync(out);
            continue;
        }

        if (n == 0) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        break;
    }

    close(out);
    close(client_fd);
}

#endif
