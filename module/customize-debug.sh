#!/system/bin/sh

SKIPUNZIP=1

ui_print "*******************************"
ui_print "        DCFG Installer        "
ui_print "*******************************"

unzip -o "$ZIPFILE" -d "$MODPATH" >&2

CFGDIR="/data/adb/dcfg"
CONFIG="$CFGDIR/config.json"
EXAMPLE="$MODPATH/config.example.json"

mkdir -p "$CFGDIR"

if [ ! -f "$CONFIG" ] && [ -f "$EXAMPLE" ]; then
  cp -f "$EXAMPLE" "$CONFIG"
  ui_print "- Created default config.json"
fi

chmod 0644 "$CONFIG" 2>/dev/null
chcon u:object_r:system_file:s0 "$CFGDIR" 2>/dev/null
chcon u:object_r:system_file:s0 "$CONFIG" 2>/dev/null

CACHE="$MODPATH/config.cache"
TOOL="$MODPATH/bin/dcfg-cache"
if [ -x "$TOOL" ] && [ -f "$CONFIG" ]; then
  "$TOOL" "$CONFIG" "$CACHE" >/dev/null 2>&1 || true
  chmod 0644 "$CACHE" 2>/dev/null
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644

LOGFILE="$CFGDIR/dcfg.log"
touch "$LOGFILE"
chmod 0644 "$LOGFILE"
chcon u:object_r:system_file:s0 "$LOGFILE" 2>/dev/null
