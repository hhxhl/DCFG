LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := dcfg

LOCAL_SRC_FILES := main.cpp config.cpp config_cache.cpp apply.cpp system_props.cpp module_dir.cpp reset_companion.cpp reset_protocol.cpp
LOCAL_CPPFLAGS += -Oz -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fvisibility=hidden
LOCAL_LDFLAGS += -Wl,--gc-sections -Wl,--icf=safe

ifeq ($(DCFG_RESET_BACKEND_RUST),1)
LOCAL_SRC_FILES += reset_engine_rust.cpp
LOCAL_STATIC_LIBRARIES += dcfg_resetprop_rust
else
LOCAL_SRC_FILES += reset_engine_exec.cpp
endif

ifeq ($(DCFG_DEBUG_BUILD),1)
LOCAL_SRC_FILES += log.cpp
LOCAL_CFLAGS += -DDCFG_DEBUG_LOG -DDCFG_RUNTIME_ONLY
LOCAL_LDLIBS := -llog
else
LOCAL_CFLAGS += -DDCFG_NO_LOG -DDCFG_RUNTIME_ONLY
LOCAL_LDLIBS :=
endif

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := dcfg-cache
LOCAL_SRC_FILES := cache_tool.cpp config_cache.cpp json_lite.cpp props_mapping.cpp
LOCAL_CPPFLAGS += -Oz -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fvisibility=hidden
LOCAL_LDFLAGS += -Wl,--gc-sections -Wl,--icf=safe
LOCAL_CFLAGS += -DDCFG_NO_LOG -DDCFG_CACHE_TOOL
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)
include $(BUILD_EXECUTABLE)

ifeq ($(DCFG_RESET_BACKEND_RUST),1)
DCFG_RUST_PROFILE ?= release
include $(CLEAR_VARS)
LOCAL_MODULE := dcfg_resetprop_rust
LOCAL_SRC_FILES := ../rust/target/aarch64-linux-android/$(DCFG_RUST_PROFILE)/libdcfg_resetprop_rust.a
include $(PREBUILT_STATIC_LIBRARY)
endif
