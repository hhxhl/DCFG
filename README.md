# DCFG

DCFG is a native-only per-app Zygisk runtime for Build and system property virtualization.

The project focuses on:

* Strict per-app scope
* Runtime-aware resetprop virtualization
* Minimal persistent footprint
* Package-aware top-app lifecycle management
* Official Zygisk API compatibility
* NeoZygisk / KernelSU compatibility

DCFG does not use LSPosed, Java hook frameworks, or a WebUI.

---

# Architecture Overview

DCFG consists of:

```text
Zygisk runtime
├── Config loader
├── Build field virtualization
├── Runtime property virtualization
├── Companion resetprop runtime
├── Top-app lifecycle monitor
├── Config cache runtime
└── Rust / external resetprop backend
```

The module applies virtualization only to matched target apps.

Unmatched processes are allowed to dlclose early.

---

# Runtime Design

## Build Virtualization

DCFG modifies Build and Build.VERSION fields inside matched app processes.

Examples:

```text
Build.MODEL
Build.DEVICE
Build.PRODUCT
Build.FINGERPRINT
Build.VERSION.SDK_INT
```

The modification is per-process and does not globally change the system.

---

## Runtime resetprop Virtualization

DCFG uses a companion-managed resetprop runtime.

App processes do not directly own resetprop lifecycle.

Instead:

```text
app process
    ↓
companion acquire request
    ↓
centralized runtime state
    ↓
runtime-aware restore
```

The companion manages:

* prop backup
* fake prop apply
* refcount state
* lifecycle tracking
* delayed restore
* top-app monitoring

---

# resetprop Scope

DCFG currently supports:

```json
"resetprop_scope": "main"
```

and:

```json
"resetprop_scope": "package"
```

---

## main Scope

```text
Only the main app process participates in resetprop lifecycle.
```

Behavior:

```text
- child processes do not acquire resetprop session
- only main process drives lifecycle
- main process leaving top-app restores props
- main process exit restores props
```

This mode is lightweight and aggressive.

---

## package Scope

```text
All package processes participate in top-app lifecycle tracking.
```

Behavior:

```text
- package shares one resetprop session
- props apply once per package session
- child processes participate in active_pids tracking
- any top-app pid keeps session alive
- entire package leaving top-app restores props
```

This mode is more stable for:

* WebView apps
* Chromium renderer processes
* remote UI processes
* multi-process apps

---

# Top-App Runtime Model

DCFG restore logic is top-app driven.

The runtime checks:

```text
/proc/<pid>/cgroup
```

and detects:

```text
top-app
```

Restore occurs when:

```text
- process exits
or
- process/package remains non-top-app for delay window
```

Current delayed restore:

```text
2000 ms
```

This avoids immediate restore during:

* app switching
* Activity transitions
* renderer teardown
* recent-task animations

---

# Config Cache Runtime

DCFG supports a compiled runtime cache.

The runtime primarily reads:

```text
config.cache
```

instead of reparsing JSON on every app launch.

Benefits:

* faster startup
* lower runtime parse overhead
* reduced allocation pressure
* lower Zygisk process overhead

Important:

```text
Editing config.json alone does not refresh cache.
```

You must rebuild cache after config changes.

---

# Rust Backend

The current mainline uses the Rust resetprop backend.

Architecture:

```text
C++ runtime
    ↓
Rust reset engine
```

Advantages:

* lower runtime overhead
* avoids external resetprop fork/exec cost
* faster app launch
* better runtime synchronization

---

# Classic Backend

DCFG also provides a classic external resetprop backend.

Architecture:

```text
C++ runtime
    ↓
external resetprop process
```

Characteristics:

* smaller implementation complexity
* slower runtime performance
* higher process overhead
* lighter dependency chain

---

# Build Variants

## Mainline

Rust backend runtime:

```text
build.sh
build-debug.sh
```

Outputs:

```text
release/DCFG-release.zip
```

---

## Classic

External resetprop backend:

```text
build-classic.sh
build-classic-debug.sh
```

Outputs:

```text
release/DCFG-classic-release.zip
```

---

# Project Structure

```text
DCFG/
├── build.sh
├── build-debug.sh
├── build-classic.sh
├── build-classic-debug.sh
├── README.md
├── module/
│   ├── module.prop
│   ├── service.sh
│   ├── customize.sh
│   ├── config.example.json
│   └── jni/
│       ├── main.cpp
│       ├── config.cpp
│       ├── apply.cpp
│       ├── system_props.cpp
│       ├── reset_companion.cpp
│       ├── reset_engine_exec.cpp
│       ├── reset_engine_rust.cpp
│       └── ...
└── release/
```

---

# Configuration

Example:

```json
{
  "version": 1,
  "log": {
    "level": "none"
  },
  "global": {
    "apply_to_children": true,
    "resetprop_policy": "top_app"
  },
  "rules": [
    {
      "package": "com.example.app",
      "profile": "pixel9"
    }
  ],
  "profiles": {
    "pixel9": {
      "enabled": true,
      "auto_props": true,
      "resetprop": true,
      "resetprop_scope": "main",
      "build": {
        "BRAND": "google",
        "MANUFACTURER": "Google",
        "MODEL": "Pixel 9",
        "DEVICE": "tokay",
        "PRODUCT": "tokay"
      },
      "props": {
        "ro.product.brand": "google",
        "ro.product.manufacturer": "Google"
      }
    }
  }
}
```

---

# Configuration Semantics

## auto_props

```json
"auto_props": true
```

Automatically maps Build fields into property overrides.

---

## props

```json
"props": {
  "ro.product.brand": "google"
}
```

Explicit property overrides.

Overrides take precedence over auto-generated mappings.

---

## apply_to_children

```json
"apply_to_children": true
```

Controls whether child app processes inherit matched runtime behavior.

---


# Logging

Release builds:

```text
DCFG_NO_LOG enabled
```

Characteristics:

```text
- no log.cpp
- no -llog link
- no runtime log file
- no logcat output
```

Debug builds retain runtime logging.

---

# Compatibility

Supported:

* Magisk Zygisk
* NeoZygisk
* KernelSU
* Android 12+

Primary target:

```text
arm64-v8a
```

---

# Goals

DCFG prioritizes:

* minimal runtime footprint
* strict per-app isolation
* runtime-aware virtualization
* reduced detection surface
* stable top-app lifecycle handling
* lightweight native-only architecture

---

# Non-Goals

DCFG intentionally does not provide:

* WebUI
* LSPosed dependency
* Java framework hooks
* global device spoofing
* persistent system-wide resetprop

