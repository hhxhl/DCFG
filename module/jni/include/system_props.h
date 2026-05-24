#pragma once
#include <string>
#include <unordered_map>
#include "zygisk.hpp"
#include "config.h"

void dcfg_set_current_props(const PropMap &props);
bool dcfg_get_fake_prop(const char *key, PropValue &value);
void dcfg_install_system_property_hooks(zygisk::Api *api);
