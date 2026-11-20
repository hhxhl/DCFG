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

Each rule uses a `packages` array (one or more package names) as the last
field. The old `package` field is not supported. All listed apps share the
rule's profile, mode, property hook, and resetprop options; app processes still
match independently.

Example:

```json
{
  "version": 1,
  "global": {
    "hook_children": true,
    "resetprop_scope": "package"
  },
  "rules": [
    {
      "profile": "xiaomi_m2105k81c",
      "mode": "auto_manual",
      "prop_hook": "none",
      "resetprop": true,
      "packages": [
        "com.eltavine.duckdetector",
        "com.example.app2"
      ]
    }
  ],
  "profiles": {
    "xiaomi_m2105k81c": {
      "build": {
        "BRAND": "Xiaomi",
        "MODEL": "M2105K81C"
      },
      "version_info": {},
      "props": {}
    }
  }
}
```

- `packages` must be a non-empty array of non-empty strings. The cache builder
  rejects missing, legacy `package`, or malformed `packages` values.
- One or more packages can share a rule. The cache compiler creates a separate
  entry for each package without changing the per-app runtime matcher.
- Existing `/data/adb/dcfg/config.json` files must be migrated before using a
  build with this change; after editing, rebuild `config.cache` using module
  Action or restart the device.
- `mode=auto_manual` combines automatic Build-to-prop mapping with explicit
  `profiles.<name>.props` overrides; the latter take precedence.
- `global.hook_children` controls child-process matching.
- `global.resetprop_scope` remains either `main` or `package`.

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

