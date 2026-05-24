#pragma once
#include <jni.h>
#include <string>
#include <unordered_map>
#include "zygisk.hpp"
#include "config.h"
bool dcfg_apply_profile(
        zygisk::Api *api,
        JNIEnv *env,
        const Profile &profile,
        PropMap *final_props_out = nullptr
);
