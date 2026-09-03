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

if [[ "$1" == "bort" ]]; then
  launchctl unload "$PLIST" 2>/dev/null || true
  rm -f "$PLIST"
  echo "Tjänsten borttagen."
  exit 0
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
launchctl unload "$PLIST" 2>/dev/null || true
launchctl load "$PLIST"
echo "Tjänsten installerad och startad. Logg: ~/Library/Logs/office-buddy.log"
echo "Stoppa innan en flashning:  launchctl unload $PLIST"
