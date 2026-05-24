#!/system/bin/sh

MODDIR=${0%/*}
CFGDIR=/data/adb/dcfg
CONFIG="$CFGDIR/config.json"
CACHE="$MODDIR/config.cache"
TOOL="$MODDIR/bin/dcfg-cache"
LOGFILE="$CFGDIR/dcfg.log"

mkdir -p "$CFGDIR"
: > "$LOGFILE"
chmod 0644 "$LOGFILE" 2>/dev/null
chmod 0644 "$CONFIG" 2>/dev/null
chcon u:object_r:system_file:s0 "$CFGDIR" 2>/dev/null
chcon u:object_r:system_file:s0 "$CONFIG" 2>/dev/null

if [ -x "$TOOL" ] && [ -f "$CONFIG" ]; then
  if "$TOOL" "$CONFIG" "$CACHE" >> "$LOGFILE" 2>&1; then
    chmod 0644 "$CACHE" 2>/dev/null
    echo "[service] rebuilt config cache: $CACHE" >> "$LOGFILE"
  else
    echo "[service] failed to rebuild config cache" >> "$LOGFILE"
  fi
fi
