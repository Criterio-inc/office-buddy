#!/bin/zsh
#
# Skapar firmware/main/secrets.h med wifi-uppgifterna inför bygget.
# Lösenordet hämtas ur macOS-nyckelringen och hamnar aldrig i git.
#
# Lägg in lösenordet en gång (frågar interaktivt, hamnar inte i historiken):
#   security add-generic-password -a "$USER" -s office-buddy-wifi -w
#
# Bygg sedan med:
#   OFFICE_BUDDY_SSID="ditt 2,4 GHz-nät" verktyg/generera-secrets.sh
#
# Utan secrets.h byggs firmwaren utan wifi och buddyn går enbart på USB.
set -e
ROT="${0:A:h}/.."
UT="$ROT/firmware/main/secrets.h"

if [[ -z "$OFFICE_BUDDY_SSID" ]]; then
  echo "Sätt OFFICE_BUDDY_SSID till namnet på ditt 2,4 GHz-nät:"
  echo "  OFFICE_BUDDY_SSID=\"Mitt nät\" $0"
  exit 1
fi

LOSEN=$(security find-generic-password -a "$USER" -s office-buddy-wifi -w 2>/dev/null) \
  || LOSEN=$(security find-generic-password -a "$USER" -s projektpulsen-wifi -w 2>/dev/null) \
  || { echo "Inget lösenord i nyckelringen. Lägg in det med:"; echo "  security add-generic-password -a \"\$USER\" -s office-buddy-wifi -w"; exit 1; }

escapa() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'; }
cat > "$UT" <<H
/* GENERERAD av verktyg/generera-secrets.sh. Redigera inte, lägg aldrig i git. */
#pragma once
#define WIFI_SSID     "$(escapa "$OFFICE_BUDDY_SSID")"
#define WIFI_LOSENORD "$(escapa "$LOSEN")"
H
chmod 600 "$UT"
echo "Skrev firmware/main/secrets.h för nätet $OFFICE_BUDDY_SSID (lösenord $(printf '%s' "$LOSEN" | wc -c | tr -d ' ') tecken, ur nyckelringen)"
