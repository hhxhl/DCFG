#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
JNI_DIR="$ROOT/module/jni"
RUST_DIR="$ROOT/module/rust"
OUT_DIR="$ROOT/module/zygisk"
BIN_DIR="$ROOT/module/bin"
PKG_OUT="$ROOT/release"
ZIP_NAME="DCFG-release.zip"
TARGET="aarch64-linux-android"
API_LEVEL="23"
RUST_PROFILE="release"
RUST_LIB="$RUST_DIR/target/$TARGET/$RUST_PROFILE/libdcfg_resetprop_rust.a"
FINAL_SO="$OUT_DIR/arm64-v8a.so"

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    echo "ANDROID_NDK_HOME is not set"
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

LLVM_BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG/bin"
CLANG="$LLVM_BIN/${TARGET}${API_LEVEL}-clang"
AR="$LLVM_BIN/llvm-ar"
RANLIB="$LLVM_BIN/llvm-ranlib"
STRIP="$LLVM_BIN/llvm-strip"

if [ ! -x "$CLANG" ]; then
    echo "[!] Android clang not found: $CLANG"
    exit 1
fi

if [ ! -x "$AR" ]; then
    echo "[!] llvm-ar not found: $AR"
    exit 1
fi

if ! command -v cargo >/dev/null 2>&1; then
    echo "[!] cargo is not available"
    exit 1
fi

if ! command -v rustup >/dev/null 2>&1; then
    echo "[!] rustup is not available"
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

if ! rustup target list --installed 2>/dev/null | grep -qx "$TARGET"; then
    echo "[!] Rust target $TARGET is not installed"
    echo "    Run: rustup target add $TARGET"
    exit 1
fi

echo "[*] Building DCFG resetprop static library (size-oriented release)..."

cd "$RUST_DIR"

export CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER="$CLANG"
export CARGO_TARGET_AARCH64_LINUX_ANDROID_AR="$AR"
export AR_aarch64_linux_android="$AR"
export CC_aarch64_linux_android="$CLANG"

if [ -x "$RANLIB" ]; then
    export RANLIB_aarch64_linux_android="$RANLIB"
fi

# Cargo release profile handles LTO/panic/strip/opt-level.
# Keep section GC explicit so the final native link can discard unused code.
export RUSTFLAGS="${RUSTFLAGS:-} -C link-arg=-Wl,--gc-sections"

cargo build --release --target "$TARGET"

if [ ! -f "$RUST_LIB" ]; then
    echo "[!] Rust static library not found: $RUST_LIB"
    exit 1
fi

echo "[*] Rust static library ready:"
echo "    $RUST_LIB"
ls -l "$RUST_LIB"

echo "[*] Building DCFG release native library with Rust resetprop backend..."

cd "$JNI_DIR"

"$NDK_BUILD" clean
"$NDK_BUILD" DCFG_DEBUG_BUILD=0 DCFG_RESET_BACKEND_RUST=1 DCFG_RUST_PROFILE="$RUST_PROFILE"

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

echo "[*] Copying built library..."
cp -f "$SO_PATH" "$FINAL_SO"

if [ -x "$STRIP" ]; then
    echo "[*] Stripping final Zygisk library..."
    "$STRIP" --strip-unneeded "$FINAL_SO" || true
else
    echo "[!] llvm-strip not found; final library was not stripped: $STRIP"
fi

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

if command -v llvm-readelf >/dev/null 2>&1; then
    if llvm-readelf -S "$FINAL_SO" | grep -Eq "symtab|debug"; then
        echo "[!] Warning: symtab/debug sections are still present"
    else
        echo "[*] No symtab/debug sections found"
    fi
fi

echo "[*] Packaging DCFG release Magisk/Zygisk module zip..."
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
