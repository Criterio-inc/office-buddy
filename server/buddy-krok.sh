#!/bin/zsh
# Claude Code-krok: lämnar en rad i Office Buddys brevlåda. Tyst och snabb, hindrar aldrig Claude.
#   buddy.sh claude vantar      (kroken får sessionens JSON på stdin)
# Sessionens id och projektmapp läggs till så att buddyn kan skilja sessioner åt.
EXTRA=$(python3 -c '
import json,sys,os
try:
    d=json.load(sys.stdin)
    sid=str(d.get("session_id",""))[:8] or "0"
    cwd=d.get("cwd","") or ""
    namn=os.path.basename(cwd.rstrip("/")) or "claude"
    print(sid, namn[:24])
except Exception:
    print("0 claude")
' 2>/dev/null)
[[ -z "$EXTRA" ]] && EXTRA="0 claude"
curl -s -m 1 -X POST --data-binary "$* $EXTRA" http://127.0.0.1:8739/ >/dev/null 2>&1
exit 0
