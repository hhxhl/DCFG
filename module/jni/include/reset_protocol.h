#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

extern const uint32_t kDcfgResetMagic;
extern const uint32_t kDcfgResetRespMagic;
extern const uint32_t kDcfgResetVersion;
extern const size_t kDcfgResetMaxProps;
extern const size_t kDcfgResetMaxString;

bool dcfg_reset_write_u32(int fd, uint32_t v);
bool dcfg_reset_write_i32(int fd, int32_t v);
bool dcfg_reset_read_u32(int fd, uint32_t &v);
bool dcfg_reset_read_i32(int fd, int32_t &v);

bool dcfg_reset_write_bool_response(
        int fd,
        bool success,
        uint32_t applied,
        uint32_t requested
);

bool dcfg_reset_read_bool_response(
        int fd,
        bool &success,
        uint32_t &applied,
        uint32_t &requested
);

bool dcfg_reset_write_string(int fd, const std::string &s);
bool dcfg_reset_read_string(int fd, std::string &out);
