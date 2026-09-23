"""Läsande brygga från VibePulse v2 till buddyn. Skickar aldrig agentsvar."""
import threading
import functools
import hashlib
import math
import time
import urllib.request
import json
from collections import OrderedDict


def last(f):
    @functools.wraps(f)
    def skyddad(self, *a, **kw):
        with self.las:
            return f(self, *a, **kw)
    return skyddad


def radtext(text, maxbyte=96):
    """En enda protokollrad, kapad vid hel UTF-8-kodpunkt."""
    text = " ".join(str(text or "").split())
    return text.encode("utf-8")[:maxbyte].decode("utf-8", "ignore")


def identitet(aktor, id):
    return "vp-" + hashlib.sha256(f"{aktor}:{id}".encode()).hexdigest()[:24]


class Agentvakt:
    def __init__(self, skicka, aktorer=("codex",), klocka=time.monotonic):
        self.las = threading.RLock()
        self.skicka, self.aktorer, self.klocka = skicka, set(aktorer), klocka
        self.fragor = {}
        self.klara = {}
        self.sedda = OrderedDict()
        self.forsta = True
        self.senast_ok = None
        self.forra = {}

    def _rad(self, aktor, lage, id, projekt=""):
        self.skicka(f"agent {aktor} {lage} {id} {radtext(projekt)}".rstrip())

    @last
    def uppdatera(self, data):
        if not isinstance(data, dict) or data.get("v") != 2 or not isinstance(data.get("agents"), dict):
            raise ValueError("Väntar VibePulse agentstatus v2")
        nu = self.klocka()
        fragor, nuvarande, klara = {}, {}, []
        for aktor in ("claude", "codex"):
            if aktor not in self.aktorer:
                continue
            post = data["agents"].get(aktor)
            if not isinstance(post, dict) or not isinstance(post.get("jobs"), list):
                raise ValueError("Ofullständig agentstatus")
            for jobb in post["jobs"][:16]:
                if not isinstance(jobb, dict):
                    continue
                jid, eid = jobb.get("task_id"), jobb.get("event_id")
                if not isinstance(jid, str) or not jid:
                    continue
                id = identitet(aktor, jid)
                projekt = radtext(jobb.get("project") or aktor)
                state = jobb.get("state")
                nyckel = (aktor, id)
                nuvarande[nyckel] = state
                # Ett vanligt end_turn/waiting är ingen fråga.
                if state == "waiting" and jobb.get("activity") in ("waiting_input", "waiting_approval"):
                    fragor[nyckel] = projekt
                if isinstance(eid, str) and eid:
                    event = (aktor, eid)
                    if state == "done" and event not in self.sedda and not self.forsta:
                        age = jobb.get("updated_ms")
                        if isinstance(age, (int, float)) and math.isfinite(age) and 0 <= age < 30000:
                            klara.append((aktor, id, projekt))
                    self.sedda[event] = None
                    self.sedda.move_to_end(event)
                    while len(self.sedda) > 256:
                        self.sedda.popitem(last=False)
        # Den hållna frågan är explicit även när Codex logg saknar waiting.
        p = data.get("pending")
        if isinstance(p, dict):
            aktor = p.get("provider", "claude")
            rid, liv = p.get("request_id"), p.get("expires_in_ms")
            if (aktor in self.aktorer and isinstance(rid, str) and rid and
                    isinstance(liv, (int, float)) and math.isfinite(liv) and liv > 0):
                fragor[(aktor, identitet(aktor, "fraga:" + rid))] = radtext(p.get("project") or aktor)
        # VibePulse kan använda turn-id: nästa användarsvar får då ett nytt id.
        # Nytt arbete hos aktören kvitterar äldre klarsignaler, aldrig frågor.
        fortsatt = {aktor for (aktor, id), state in nuvarande.items()
                    if state == "working" and self.forra.get((aktor, id)) != "working"}
        # Ett snabbt nytt svar kan bli klart mellan två pollningar.
        # Det ersätter äldre klarsignaler även om working aldrig hann synas.
        fortsatt.update(aktor for aktor, _, _ in klara)
        for (aktor, id), state in nuvarande.items():
            if state == "working" and self.forra.get((aktor, id)) != "working":
                # Rensar även klarsignaler som kortet minns efter bryggans omstart.
                self._rad(aktor, "jobbar", id)
        for aktor, id in self.fragor:
            if (aktor, id) not in fragor:
                # Försvunnen/förfallen fråga betyder inte att användaren godkänt.
                self._rad(aktor, "borta", id)
        for key, projekt in fragor.items():
            fore = self.fragor.get(key)
            if fore is None or nu - fore[1] >= 25 or fore[0] != projekt:
                self._rad(key[0], "vantar", key[1], projekt)
                fragor[key] = (projekt, nu)
            else:
                fragor[key] = fore
        # Klart skickas en gång; kortet avslutar scenen efter 20 sekunder.
        for key in list(self.klara):
            if key[0] in fortsatt or nuvarande.get(key) not in ("done", "waiting") or key in fragor:
                if key not in fragor:
                    self._rad(key[0], "borta", key[1])
                del self.klara[key]
        for aktor, id, projekt in klara:
            self.klara[(aktor, id)] = (projekt, -float("inf"))
        for key, (projekt, senast) in list(self.klara.items()):
            if senast == -float("inf"):
                self._rad(key[0], "klar", key[1], projekt)
                self.klara[key] = (projekt, nu)
        self.fragor = fragor
        self.forra = nuvarande
        self.forsta = False
        self.senast_ok = nu

    @last
    def fel(self):
        if self.senast_ok is not None and self.klocka() - self.senast_ok >= 30:
            for aktor, id in self.fragor:
                self._rad(aktor, "borta", id)
            self.fragor.clear()
            for aktor, id in self.klara:
                self._rad(aktor, "borta", id)
            self.klara.clear()
            self.forsta = True  # gamla klarmarkeringar spelas inte efter avbrott

    @last
    def ateranslut(self):
        # Kortet kan ha startat om. Återställ bara verkliga aktuella frågor.
        for (aktor, id), (projekt, _) in self.fragor.items():
            self._rad(aktor, "vantar", id, projekt)


def bevaka(url, vakt, stopp, logg):
    klagat = False
    while not stopp.is_set():
        try:
            with urllib.request.urlopen(url, timeout=2) as svar:
                raw = svar.read(65537)
                if len(raw) > 65536:
                    raise ValueError("För stort statussvar")
                data = json.loads(raw)
            vakt.uppdatera(data)
            klagat = False
        except Exception as fel:
            vakt.fel()
            if not klagat:
                logg(f"agentstatus: {fel}")
                klagat = True
        stopp.wait(2)
