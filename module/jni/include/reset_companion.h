#pragma once

#include "zygisk.hpp"
#include "config.h"

#include <string>
#include <unordered_map>

bool dcfg_reset_companion_send(
        zygisk::Api *api,
        int pid,
        const std::string &package_name,
        const std::string &profile_name,
        ResetpropScope scope,
        const PropMap &props
);

void dcfg_reset_companion_entry(int client_fd);

void dcfg_companion_entry(int client_fd);
