#pragma once

#include "config.h"

#include <cstdint>
#include <string>
#include <vector>

struct ResetPropItem {
    std::string key;
    PropValue fake_value;
    bool existed = false;
    std::string original_value;
};

void dcfg_reset_backup_originals(std::vector<ResetPropItem> &items);

uint32_t dcfg_reset_apply_fake(const std::vector<ResetPropItem> &items);

void dcfg_reset_restore_originals(const std::vector<ResetPropItem> &items);
