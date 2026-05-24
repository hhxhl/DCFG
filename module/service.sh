#!/system/bin/sh

MODDIR=${0%/*}
CFGDIR=/data/adb/dcfg
CONFIG="$CFGDIR/config.json"
CACHE="$MODDIR/config.cache"
TOOL="$MODDIR/bin/dcfg-cache"

mkdir -p "$CFGDIR"

chmod 0644 "$CONFIG" 2>/dev/null
chcon u:object_r:system_file:s0 "$CFGDIR" 2>/dev/null
chcon u:object_r:system_file:s0 "$CONFIG" 2>/dev/null

chmod 0755 "$TOOL" 2>/dev/null

if [ -x "$TOOL" ] && [ -f "$CONFIG" ]; then
  TMP="$CACHE.tmp"
  rm -f "$TMP"
  if "$TOOL" "$CONFIG" "$TMP" >/dev/null 2>&1; then
    mv -f "$TMP" "$CACHE"
    chmod 0644 "$CACHE" 2>/dev/null
  else
    rm -f "$TMP"
  fi
fi
