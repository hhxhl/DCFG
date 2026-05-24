#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
JNI_DIR="$ROOT/module/jni"
OUT_DIR="$ROOT/module/zygisk"
BIN_DIR="$ROOT/module/bin"
PKG_OUT="$ROOT/release"
ZIP_NAME="DCFG-classic-release.zip"
FINAL_SO="$OUT_DIR/arm64-v8a.so"

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    echo "[!] ANDROID_NDK_HOME is not set"
    exit 1
fi

NDK_BUILD="$ANDROID_NDK_HOME/ndk-build"
if [ ! -x "$NDK_BUILD" ]; then
    echo "[!] ndk-build not found or not executable: $NDK_BUILD"
    exit 1
fi

case "$(uname -s)" in
    Linux*)  HOST_TAG="linux-x86_64" ;;
    Darwin*) HOST_TAG="darwin-x86_64" ;;
    *)
        echo "[!] Unsupported host OS: $(uname -s)"
        exit 1
        ;;
esac

STRIP="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG/bin/llvm-strip"

if ! command -v zip >/dev/null 2>&1; then
    echo "[!] zip is not available"
    exit 1
fi

if ! command -v unzip >/dev/null 2>&1; then
    echo "[!] unzip is not available"
    exit 1
fi

echo "[*] Building DCFG classic release native library with classic external resetprop backend..."

cd "$JNI_DIR"

"$NDK_BUILD" clean
"$NDK_BUILD" DCFG_DEBUG_BUILD=0 DCFG_RESET_BACKEND_RUST=0

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
if [ -x "${STRIP:-}" ]; then
    echo "[*] Stripping cache compiler..."
    "$STRIP" --strip-unneeded "$BIN_DIR/dcfg-cache" || true
fi
echo "[*] Cache compiler ready:"
ls -l "$BIN_DIR/dcfg-cache"

echo "[*] Packaging DCFG classic release Magisk/Zygisk module zip..."
mkdir -p "$PKG_OUT"
rm -f "$PKG_OUT/$ZIP_NAME"

cd "$ROOT/module"

zip -9 "$PKG_OUT/$ZIP_NAME" \
    module.prop \
    customize.sh \
    service.sh \
    action.sh \
    uninstall.sh \
    config.example.json \
    bin/dcfg-cache \
    zygisk/arm64-v8a.so

echo "[*] Done:"
echo "    $PKG_OUT/$ZIP_NAME"

echo "[*] Zip contents:"
unzip -l "$PKG_OUT/$ZIP_NAME"
