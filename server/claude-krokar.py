#!/usr/bin/env python3
"""Lägger in Office Buddys krokar i Claude Codes ~/.claude/settings.json.

    python3 server/claude-krokar.py          lägg till
    python3 server/claude-krokar.py bort     ta bort

En säkerhetskopia av settings.json skrivs bredvid. Krokarna anropar
server/buddy-krok.sh i det här repot, som postar till brevlådan.
"""
import json, os, shutil, sys, time

HAR = os.path.dirname(os.path.abspath(__file__))
KROK = os.path.join(HAR, "buddy-krok.sh")
FIL = os.path.expanduser("~/.claude/settings.json")
KROKAR = {
    "Notification": "vantar",
    "Stop": "klar",
    "UserPromptSubmit": "jobbar",
    "SessionEnd": "jobbar",
}


def main():
    bort = len(sys.argv) > 1 and sys.argv[1] == "bort"
    d = {}
    if os.path.exists(FIL):
        with open(FIL, encoding="utf-8") as f:
            d = json.load(f)
        shutil.copy(FIL, FIL + ".bak-" + time.strftime("%Y%m%d-%H%M%S"))
    hooks = d.setdefault("hooks", {})
    for event, arg in KROKAR.items():
        lista = hooks.setdefault(event, [])
        lista[:] = [g for g in lista if not any("buddy-krok.sh" in h.get("command", "") or "buddy.sh" in h.get("command", "")
                                                for h in g.get("hooks", []))]
        if not bort:
            lista.append({"hooks": [{"type": "command", "command": f"{KROK} claude {arg}", "timeout": 3}]})
        if not lista:
            del hooks[event]
    os.makedirs(os.path.dirname(FIL), exist_ok=True)
    with open(FIL, "w", encoding="utf-8") as f:
        json.dump(d, f, indent=2, ensure_ascii=False)
    print("Krokarna borttagna." if bort else "Krokarna inlagda: Notification, Stop, UserPromptSubmit, SessionEnd.")


if __name__ == "__main__":
    main()
