#!/system/bin/sh

MODDIR=${0%/*}
CFGDIR=/data/adb/dcfg
CONFIG="$CFGDIR/config.json"
CACHE="$MODDIR/config.cache"
TOOL="$MODDIR/bin/dcfg-cache"
TMP="$CACHE.tmp"

ui_print() {
  echo "$@"
}

open_config() {
  ui_print "[*] Open config.json"
  ui_print "[*] After editing and saving,"
  ui_print "[*] press Action again to rebuild cache."
  ui_print "[*] $CONFIG"

  sleep 1

  am start \
    -a android.intent.action.VIEW \
    -d "file://$CONFIG" \
    -t "application/json" >/dev/null 2>&1

  exit 0
}

rebuild_cache() {
  ui_print "[*] DCFG rebuild config.cache"

  if [ ! -x "$TOOL" ]; then
    chmod 0755 "$TOOL" 2>/dev/null
  fi

  if [ ! -x "$TOOL" ]; then
    ui_print "[!] dcfg-cache is not executable: $TOOL"
    exit 1
  fi

  if [ ! -f "$CONFIG" ]; then
    ui_print "[!] config.json not found: $CONFIG"
    exit 1
  fi

  rm -f "$TMP"
  if "$TOOL" "$CONFIG" "$TMP"; then
    mv -f "$TMP" "$CACHE"
    chmod 0644 "$CACHE" 2>/dev/null
    ui_print "[*] config.cache rebuilt: $CACHE"
    exit 0
  fi

  rm -f "$TMP"
  ui_print "[!] rebuild failed; old config.cache kept"
  exit 1
}

if [ ! -f "$CONFIG" ]; then
  ui_print "[!] config.json not found: $CONFIG"
  exit 1
fi

if [ ! -f "$CACHE" ] || [ "$CONFIG" -nt "$CACHE" ]; then
  rebuild_cache
fi

open_config