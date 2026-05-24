#include "reset_protocol.h"

#ifndef DCFG_NO_LOG
#include <android/log.h>
#endif

#include <cerrno>
#include <unistd.h>

#ifndef DCFG_NO_LOG
#define RLOGW(...) __android_log_print(ANDROID_LOG_WARN, "dcfg", __VA_ARGS__)
#else
#define RLOGW(...) do {} while (0)
#endif

const uint32_t kDcfgResetMagic = 0x44525354u; // DRST
const uint32_t kDcfgResetRespMagic = 0x44525352u; // DRSR
const uint32_t kDcfgResetVersion = 4;
const size_t kDcfgResetMaxProps = 512;
const size_t kDcfgResetMaxString = 4096;

static bool write_all(int fd, const void *buf, size_t len) {
    const auto *p = static_cast<const char *>(buf);
    size_t off = 0;

    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);

        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
}

static bool read_all(int fd, void *buf, size_t len) {
    auto *p = static_cast<char *>(buf);
    size_t off = 0;

    while (off < len) {
        ssize_t n = read(fd, p + off, len - off);

        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
}

bool dcfg_reset_write_u32(int fd, uint32_t v) {
    return write_all(fd, &v, sizeof(v));
}

bool dcfg_reset_write_i32(int fd, int32_t v) {
    return write_all(fd, &v, sizeof(v));
}

bool dcfg_reset_read_u32(int fd, uint32_t &v) {
    return read_all(fd, &v, sizeof(v));
}

bool dcfg_reset_read_i32(int fd, int32_t &v) {
    return read_all(fd, &v, sizeof(v));
}

bool dcfg_reset_write_bool_response(int fd, bool success, uint32_t applied, uint32_t requested) {
    bool ok = true;
    ok = ok && dcfg_reset_write_u32(fd, kDcfgResetRespMagic);
    ok = ok && dcfg_reset_write_u32(fd, kDcfgResetVersion);
    ok = ok && dcfg_reset_write_u32(fd, success ? 1u : 0u);
    ok = ok && dcfg_reset_write_u32(fd, applied);
    ok = ok && dcfg_reset_write_u32(fd, requested);
    return ok;
}

bool dcfg_reset_read_bool_response(int fd, bool &success, uint32_t &applied, uint32_t &requested) {
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t status = 0;

    if (!dcfg_reset_read_u32(fd, magic) || !dcfg_reset_read_u32(fd, version) ||
        !dcfg_reset_read_u32(fd, status) || !dcfg_reset_read_u32(fd, applied) || !dcfg_reset_read_u32(fd, requested)) {
        return false;
    }

    if (magic != kDcfgResetRespMagic || version != kDcfgResetVersion) {
        RLOGW("reset companion invalid response magic=0x%x version=%u", magic, version);
        return false;
    }

    success = status != 0;
    return true;
}

bool dcfg_reset_write_string(int fd, const std::string &s) {
    if (s.size() > kDcfgResetMaxString) {
        return false;
    }

    uint32_t len = static_cast<uint32_t>(s.size());

    return dcfg_reset_write_u32(fd, len)
           && (len == 0 || write_all(fd, s.data(), len));
}

bool dcfg_reset_read_string(int fd, std::string &out) {
    uint32_t len = 0;

    if (!dcfg_reset_read_u32(fd, len) || len > kDcfgResetMaxString) {
        return false;
    }

    out.clear();

    if (len == 0) {
        return true;
    }

    out.resize(len);
    return read_all(fd, &out[0], len);
}
