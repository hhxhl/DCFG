#pragma once

#ifndef DCFG_NO_LOG

#include <cstdarg>

enum class DcfgLogLevel {
    NONE = 0,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4,
};

void dcfg_log_init();
void dcfg_log_reload_config();
void dcfg_log_set_package(const char *package_name);
void dcfg_log_set_enabled(bool enabled);
void dcfg_log_set_companion_fd(int fd);
bool dcfg_log_should_connect_companion();
void dcfg_log_write(DcfgLogLevel level, const char *fmt, ...);
void dcfg_log_companion(int client_fd);

#define DLOGD(...) dcfg_log_write(DcfgLogLevel::DEBUG, __VA_ARGS__)
#define DLOGI(...) dcfg_log_write(DcfgLogLevel::INFO, __VA_ARGS__)
#define DLOGW(...) dcfg_log_write(DcfgLogLevel::WARN, __VA_ARGS__)
#define DLOGE(...) dcfg_log_write(DcfgLogLevel::ERROR, __VA_ARGS__)

#else

#define DLOGD(...) do {} while (0)
#define DLOGI(...) do {} while (0)
#define DLOGW(...) do {} while (0)
#define DLOGE(...) do {} while (0)

#endif
