#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
JNI_DIR="$ROOT/module/jni"
OUT_DIR="$ROOT/module/zygisk"
BIN_DIR="$ROOT/module/bin"
PKG_OUT="$ROOT/debug"
ZIP_NAME="DCFG-classic-debug.zip"
TMP="$ROOT/.pkg-classic-debug-tmp"
FINAL_SO="$OUT_DIR/arm64-v8a.so"

cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    echo "[!] ANDROID_NDK_HOME is not set"
    exit 1
fi

NDK_BUILD="$ANDROID_NDK_HOME/ndk-build"
if [ ! -x "$NDK_BUILD" ]; then
    echo "[!] ndk-build not found or not executable: $NDK_BUILD"
    exit 1
fi

if ! command -v zip >/dev/null 2>&1; then
    echo "[!] zip is not available"
    exit 1
fi

if ! command -v unzip >/dev/null 2>&1; then
    echo "[!] unzip is not available"
    exit 1
fi

echo "[*] Building DCFG classic debug native library with classic external resetprop backend..."

cd "$JNI_DIR"

"$NDK_BUILD" clean
"$NDK_BUILD" DCFG_DEBUG_BUILD=1 DCFG_RESET_BACKEND_RUST=0

echo "[*] Locating built library..."

SO_PATH=$(find "$ROOT" -type f -path "*/libs/arm64-v8a/libdcfg.so" | head -n 1)

if [ -z "$SO_PATH" ]; then
    echo "[!] libdcfg.so not found"
    echo "[*] Existing .so files:"
    find "$ROOT" -type f -name "*.so" -print
    exit 1
fi

echo "[*] Found:"
echo "    $SO_PATH"

echo "[*] Preparing zygisk output directory..."
mkdir -p "$OUT_DIR"

cp -f "$SO_PATH" "$FINAL_SO"

echo "[*] Verifying output..."
ls -l "$FINAL_SO"

TOOL_PATH=$(find "$ROOT" -type f -path "*/libs/arm64-v8a/dcfg-cache" | head -n 1)
if [ -z "$TOOL_PATH" ]; then
    echo "[!] dcfg-cache tool not found"
    find "$ROOT" -type f -name "dcfg-cache" -print
    exit 1
fi
mkdir -p "$BIN_DIR"
cp -f "$TOOL_PATH" "$BIN_DIR/dcfg-cache"
chmod 0755 "$BIN_DIR/dcfg-cache"
echo "[*] Cache compiler ready:"
ls -l "$BIN_DIR/dcfg-cache"

echo "[*] Packaging DCFG classic debug Magisk/Zygisk module zip..."
mkdir -p "$PKG_OUT"
rm -f "$PKG_OUT/$ZIP_NAME"
rm -rf "$TMP"
mkdir -p "$TMP"

cp -f "$ROOT/module/module.prop" "$TMP/module.prop"
cp -f "$ROOT/module/customize-debug.sh" "$TMP/customize.sh"
cp -f "$ROOT/module/service-debug.sh" "$TMP/service.sh"
cp -f "$ROOT/module/uninstall.sh" "$TMP/uninstall.sh"
cp -f "$ROOT/module/config.example.json" "$TMP/config.example.json"

mkdir -p "$TMP/zygisk"
cp -f "$FINAL_SO" "$TMP/zygisk/arm64-v8a.so"
mkdir -p "$TMP/bin"
cp -f "$BIN_DIR/dcfg-cache" "$TMP/bin/dcfg-cache"
chmod 0755 "$TMP/bin/dcfg-cache"

cd "$TMP"

zip -9 "$PKG_OUT/$ZIP_NAME" \
    module.prop \
    customize.sh \
    service.sh \
    uninstall.sh \
    config.example.json \
    bin/dcfg-cache \
    zygisk/arm64-v8a.so

cd "$ROOT"
rm -rf "$TMP"
trap - EXIT INT TERM

echo "[*] Done:"
echo "    $PKG_OUT/$ZIP_NAME"

echo "[*] Zip contents:"
unzip -l "$PKG_OUT/$ZIP_NAME"
