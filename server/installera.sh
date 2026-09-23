#!/bin/zsh
# Installerar länktjänsten som en launchd-agent för den inloggade användaren.
#
#   server/installera.sh          installera och starta
#   server/installera.sh bort     stoppa och ta bort
#
# Loggen hamnar i ~/Library/Logs/office-buddy.log. Tjänsten startar om sig
# själv om den kraschar och kommer tillbaka efter omstart.
set -e
HAR="${0:A:h}"
ETIKETT="se.critero.office-buddy"
PLIST="$HOME/Library/LaunchAgents/$ETIKETT.plist"
PYTHON=$(command -v python3)

NOTIS_ETIKETT="se.critero.office-buddy-notiser"
NOTIS_PLIST="$HOME/Library/LaunchAgents/$NOTIS_ETIKETT.plist"

if [[ "$1" == "bort" ]]; then
  launchctl unload "$PLIST" 2>/dev/null || true
  launchctl unload "$NOTIS_PLIST" 2>/dev/null || true
  rm -f "$PLIST" "$NOTIS_PLIST"
  echo "Tjänsterna borttagna."
  exit 0
fi

# Bygg den app som launchd faktiskt kör, inte en oanvänd fil bredvid.
NOTIS_APP="$HAR/Notiser.app"
NOTIS_BIN="$NOTIS_APP/Contents/MacOS/notiser"
if [[ ! -x "$NOTIS_BIN" || "$HAR/notiser.swift" -nt "$NOTIS_BIN" ]]; then
  mkdir -p "$NOTIS_APP/Contents/MacOS"
  "$PYTHON" - "$NOTIS_APP/Contents/Info.plist" <<'PLISTPY'
import plistlib, sys
from pathlib import Path
p = Path(sys.argv[1])
if not p.exists():
    p.write_bytes(plistlib.dumps({"CFBundleIdentifier": "se.critero.office-buddy.notiser",
        "CFBundleName": "Office Buddy Notiser", "CFBundleExecutable": "notiser",
        "CFBundlePackageType": "APPL", "CFBundleVersion": "1",
        "CFBundleShortVersionString": "1.0", "LSUIElement": True}))
PLISTPY
  swiftc -O "$HAR/notiser.swift" -o "$NOTIS_BIN"
  codesign --force --sign - --identifier se.critero.office-buddy.notiser "$NOTIS_APP"
  echo "Uppdaterad notisläsare: macOS kan kräva nytt godkännande av Full diskåtkomst."
fi

mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$ETIKETT</string>
  <key>ProgramArguments</key>
  <array>
    <string>$PYTHON</string>
    <string>$HAR/buddylank.py</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>$HOME/Library/Logs/office-buddy.log</string>
  <key>StandardErrorPath</key><string>$HOME/Library/Logs/office-buddy.log</string>
</dict>
</plist>
PLIST
cat > "$NOTIS_PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$NOTIS_ETIKETT</string>
  <key>ProgramArguments</key>
  <array><string>$HAR/Notiser.app/Contents/MacOS/notiser</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>$HOME/Library/Logs/office-buddy-notiser.log</string>
  <key>StandardErrorPath</key><string>$HOME/Library/Logs/office-buddy-notiser.log</string>
</dict>
</plist>
PLIST
launchctl unload "$PLIST" 2>/dev/null || true
launchctl load "$PLIST"
launchctl unload "$NOTIS_PLIST" 2>/dev/null || true
launchctl load "$NOTIS_PLIST"
echo "Tjänsterna installerade och startade. Loggar: ~/Library/Logs/office-buddy.log och office-buddy-notiser.log"
echo "Notisläsaren behöver Full diskåtkomst: lägg till $HAR/Notiser.app i Systeminställningar."
echo "Stoppa innan en flashning:  launchctl unload $PLIST"
