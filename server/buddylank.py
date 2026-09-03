#!/usr/bin/env python3
"""Länken mellan Macen och Office Buddy.

Brevlådan: andra program på Macen kan lämna rader till kortet genom att
posta dem till http://127.0.0.1:8739/, en rad per textrad. Claude Codes
krokar gör det när Claude väntar på svar:

    curl -s -X POST --data-binary "sag Claude väntar på dig" http://127.0.0.1:8739/


Kortet sitter i USB-C och får rader enligt delat/protokoll.h: klockan, vad
som ligger och väntar ur pulsservern, och en avbockning när något som
väntade försvann. Allt kortet skriver tillbaka, både svar och logg, hamnar
i den här processens utskrift, så loggfilen blir kortets logg.

    python3 server/buddylank.py                 hittar kortet själv
    python3 server/buddylank.py --port /dev/cu.usbmodem144301
    python3 server/buddylank.py --skicka "uttryck arg"   en rad, sedan klart

Ren standardbibliotek. Porten öppnas utan att röra DTR och RTS, så kortet
startar inte om när länken ansluter.
"""
import argparse, glob, json, os, queue, select, socket, subprocess, sys, termios, threading, time, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HAR = os.path.dirname(os.path.abspath(__file__))

# ---- Inställningar -----------------------------------------------------------

STANDARD = {
    "port": "",
    "brevlada_port": 8739,
    "puls_url": "",
    "backup_status": "",
    "offsite_status": "",
    "backup_koll_efter": 11.5,
    "mejl": True,
    "kontofarger": {},
    "kalender": True,
    "kalender_ignorera": ["shl", "hockey", "holiday", "helgdag", "siri", "birthday", "födelsedag"],
    "mote_varna_min": 10,
    "paminnelser": True,
}


def las_installningar():
    inst = dict(STANDARD)
    fil = os.path.join(HAR, "buddy.json")
    if os.path.exists(fil):
        try:
            with open(fil, encoding="utf-8") as f:
                for k, v in json.load(f).items():
                    if not k.endswith("_kommentar") and k in STANDARD:
                        inst[k] = v
        except Exception as fel:
            print("buddy.json gick inte att läsa:", fel, file=sys.stderr)
    return inst


INST = las_installningar()
PULSSERVER = INST["puls_url"]
BACKUP_STATUS = os.path.expanduser(INST["backup_status"]) if INST["backup_status"] else ""
OFFSITE_STATUS = os.path.expanduser(INST["offsite_status"]) if INST["offsite_status"] else ""
BACKUP_KOLL_EFTER_TIMME = float(INST["backup_koll_efter"])
KALENDER_BIN = os.path.join(HAR, "kalender")
KALENDER_VAR_S = 60
MOTE_VARNA_MIN = int(INST["mote_varna_min"])
MEJL_VAR_S = 120
# Kontonas färger, som de heter i Mail, ur buddy.json. Okänt konto ger ingen färg.
KONTOFARGER = dict(INST["kontofarger"])
KALENDER_IGNORERA = [str(x).lower() for x in INST["kalender_ignorera"]]
PAMINNELSE_VAR_S = 60
PAMINNELSE_FONSTER_MIN = 5
MEJL_TYST_S = 600
BREVLADA_PORT = int(INST["brevlada_port"])
HAMTA_VAR_S = 30
KLOCKA_VAR_S = 600


def logg(text):
    print(time.strftime("%H:%M:%S"), text, flush=True)


def hitta_port():
    portar = sorted(glob.glob("/dev/cu.usbmodem*"))
    return portar[0] if portar else None


def oppna(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0                                             # iflag: rå
    a[1] = 0                                             # oflag: rå
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
    a[3] = 0                                             # lflag: rå
    a[4] = a[5] = termios.B115200
    a[6][termios.VMIN] = 0
    a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd


class Lank:
    def __init__(self, port):
        self.port = port
        self.fd = None
        self.rest = b""

    def anslut(self):
        port = self.port or hitta_port()
        if port is None:
            return False
        try:
            self.fd = oppna(port)
        except OSError as fel:
            logg(f"kunde inte öppna {port}: {fel}")
            return False
        logg(f"ansluten till {port}")
        return True

    def stang(self):
        if self.fd is not None:
            try:
                os.close(self.fd)
            except OSError:
                pass
        self.fd = None

    def skicka(self, rad):
        if self.fd is None:
            return False
        try:
            os.write(self.fd, (rad + "\n").encode("utf-8"))
            logg(f"→ {rad}")
            return True
        except OSError as fel:
            logg(f"skrivfel: {fel}")
            self.stang()
            return False

    def las(self, sekunder):
        """Läser i högst så många sekunder och skriver ut varje rad från kortet."""
        slut = time.time() + sekunder
        while self.fd is not None:
            kvar = slut - time.time()
            if kvar <= 0:
                break
            r, _, _ = select.select([self.fd], [], [], min(kvar, 0.5))
            if not r:
                continue
            try:
                d = os.read(self.fd, 4096)
            except OSError as fel:
                logg(f"läsfel: {fel}")
                self.stang()
                break
            if not d:
                continue
            self.rest += d
            while b"\n" in self.rest:
                rad, self.rest = self.rest.split(b"\n", 1)
                text = rad.decode("utf-8", "replace").rstrip("\r")
                if text:
                    logg(f"← {text}")


# ---- Brevlådan ----------------------------------------------------------

brev = queue.Queue()


class Brevhanterare(BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        text = self.rfile.read(n).decode("utf-8", "replace")
        antal = 0
        for rad in text.splitlines():
            rad = rad.strip()
            if rad:
                brev.put(rad)
                antal += 1
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        self.wfile.write(f"{antal} rader till kortet\n".encode("utf-8"))

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        self.wfile.write("Office Buddys brevlåda. POST rader enligt delat/protokoll.h.\n".encode("utf-8"))

    def log_message(self, *_):
        pass


class Brevlada(ThreadingHTTPServer):
    """Hoppar över den omvända DNS-uppslagningen i server_bind, som hänger på
    hemnät utan reverse-DNS. Samma fälla som pulsservern hade."""
    daemon_threads = True

    def server_bind(self):
        socket.socket.bind(self.socket, self.server_address)
        self.server_name = "127.0.0.1"
        self.server_port = self.server_address[1]


def starta_brevlada():
    try:
        server = Brevlada(("127.0.0.1", BREVLADA_PORT), Brevhanterare)
    except OSError as fel:
        logg(f"brevlådan kunde inte öppnas på {BREVLADA_PORT}: {fel}")
        return
    threading.Thread(target=server.serve_forever, daemon=True).start()
    logg(f"brevlådan lyssnar på http://127.0.0.1:{BREVLADA_PORT}/")


# ---- Backupvakten --------------------------------------------------------

def _timmar_sedan(iso):
    """Timmar sedan en ISO-tidsstämpel i UTC, eller None om den saknas."""
    if not iso:
        return None
    try:
        t = time.strptime(iso[:19], "%Y-%m-%dT%H:%M:%S")
    except ValueError:
        return None
    import calendar
    return (time.time() - calendar.timegm(t)) / 3600.0


def backup_status():
    """Returnerar (ok, text). Läser statusfilerna som är angivna, aldrig loggarna."""
    fel = []
    if BACKUP_STATUS:
        try:
            disk = json.load(open(BACKUP_STATUS))
            h = _timmar_sedan(disk.get("senasteLyckade"))
            if h is None or h > 26:
                fel.append("disken")
        except Exception:
            fel.append("disken")
    if OFFSITE_STATUS:
        try:
            moln = json.load(open(OFFSITE_STATUS))
            h = _timmar_sedan(moln.get("senasteLyckade"))
            if moln.get("senasteFel") or h is None or h > 26:
                fel.append("molnet")
        except Exception:
            fel.append("molnet")
    if not fel:
        return True, ""
    return False, "backup saknas: " + " och ".join(fel)


# ---- Kalendern ---------------------------------------------------------------

def kalender_handelser():
    """Kommande händelser som lista av dict (id, titel, minuter). Tom lista vid fel."""
    if not os.path.exists(KALENDER_BIN):
        kalla = os.path.join(HAR, "kalender.swift")
        try:
            subprocess.run(["swiftc", "-O", kalla, "-o", KALENDER_BIN,
                            "-Xlinker", "-sectcreate", "-Xlinker", "__TEXT", "-Xlinker", "__info_plist",
                            "-Xlinker", os.path.join(HAR, "Info.plist")], check=True, timeout=300,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception as fel:
            logg(f"kunde inte bygga kalenderhjälpen: {fel}")
            return []
    try:
        r = subprocess.run([KALENDER_BIN, "60"], capture_output=True, text=True, timeout=20)
    except Exception as fel:
        logg(f"kalendern svarar inte: {fel}")
        return []
    if r.returncode != 0:
        if not getattr(kalender_handelser, "klagat", False):
            logg("kalendern: " + (r.stderr.strip() or f"fel {r.returncode}"))
            kalender_handelser.klagat = True
        return []
    ut = []
    for rad in r.stdout.splitlines():
        try:
            ut.append(json.loads(rad))
        except ValueError:
            pass
    return ut


class Kalendervakt:
    """Säger till en gång vid tio minuter kvar och en gång när mötet börjar."""
    def __init__(self):
        self.sagt = {}   # id -> set av "10"/"0"

    def kolla(self, lank):
        for h in kalender_handelser():
            typ = str(h.get("typ", "")).lower()
            namn = (str(h.get("kalender", "")) + " " + str(h.get("konto", ""))).lower()
            if typ in ("subscription", "birthday") or any(o in namn for o in KALENDER_IGNORERA):
                continue
            hid = h.get("id") or h.get("titel")
            minuter = int(h.get("minuter", 999))
            titel = (h.get("titel") or "möte")[:60]
            gjort = self.sagt.setdefault(hid, set())
            if 0 < minuter <= MOTE_VARNA_MIN and "10" not in gjort:
                gjort.add("10")
                lank.skicka(f"mote {minuter} {titel}")
            elif minuter <= 0 and minuter > -3 and "0" not in gjort:
                gjort.add("0")
                lank.skicka(f"mote 0 {titel}")
        if len(self.sagt) > 200:
            self.sagt.clear()


# ---- Påminnelserna -------------------------------------------------------------

class Paminnelsevakt:
    """Säger till en gång per påminnelse när den förfaller."""
    def __init__(self):
        self.sagt = set()

    def kolla(self, lank):
        if not os.path.exists(KALENDER_BIN):
            return
        try:
            r = subprocess.run([KALENDER_BIN, "paminnelser", str(PAMINNELSE_FONSTER_MIN)],
                               capture_output=True, text=True, timeout=20)
        except Exception as fel:
            logg(f"påminnelserna svarar inte: {fel}")
            return
        if r.returncode != 0:
            if not getattr(self, "klagat", False):
                logg("påminnelserna: " + (r.stderr.strip() or f"fel {r.returncode}"))
                self.klagat = True
            return
        for rad in r.stdout.splitlines():
            try:
                p = json.loads(rad)
            except ValueError:
                continue
            pid = p.get("id") or p.get("titel")
            if int(p.get("minuter", 99)) <= 0 and pid not in self.sagt:
                self.sagt.add(pid)
                lank.skicka(f"paminnelse {(p.get('titel') or 'påminnelse')[:70]}")
        if len(self.sagt) > 500:
            self.sagt.clear()


# ---- Mejlen ------------------------------------------------------------------

MEJL_SKRIPT = """
tell application "Mail"
    set n to unread count of inbox
    if n is 0 then return "0"
    set m to message 1 of inbox
    set k to ""
    try
        set k to name of account of mailbox of m
    end try
    return (n as text) & "|" & (sender of m) & "|" & (subject of m) & "|" & k
end tell
"""


def mejl_lage():
    """(antal olästa, senaste avsändare, ämne) eller None om Mail inte svarar."""
    try:
        r = subprocess.run(["osascript", "-e", MEJL_SKRIPT], capture_output=True, text=True, timeout=20)
    except Exception:
        return None
    if r.returncode != 0:
        if not getattr(mejl_lage, "klagat", False):
            logg("mejlen: " + r.stderr.strip()[:160])
            mejl_lage.klagat = True
        return None
    delar = r.stdout.strip().split("|", 3)
    try:
        antal = int(delar[0])
    except (ValueError, IndexError):
        return None
    avs = delar[1] if len(delar) > 1 else ""
    amne = delar[2] if len(delar) > 2 else ""
    konto = delar[3] if len(delar) > 3 else ""
    # "Namn <adress>" -> Namn
    if "<" in avs:
        avs = avs.split("<")[0].strip().strip('"')
    return antal, avs, amne, konto


class Mejlvakt:
    """En blick när olästa blir fler, högst en gång per tio minuter."""
    def __init__(self):
        self.forra = None
        self.tyst_till = 0

    def kolla(self, lank):
        lage = mejl_lage()
        if lage is None:
            return
        antal, avs, amne, konto = lage
        if self.forra is not None and antal > self.forra and time.time() >= self.tyst_till:
            text = f"{avs}" if avs else f"{antal} olästa"
            if amne:
                text += f", {amne[:40]}"
            farg = KONTOFARGER.get(konto, "")
            lank.skicka(f"mejl {farg + ' ' if farg else ''}{text}")
            self.tyst_till = time.time() + MEJL_TYST_S
        self.forra = antal


def las_puls():
    try:
        with urllib.request.urlopen(PULSSERVER, timeout=5) as svar:
            return json.load(svar)
    except Exception as fel:
        logg(f"pulsservern svarar inte: {fel}")
        return None


def main():
    arg = argparse.ArgumentParser()
    arg.add_argument("--port", help="t.ex. /dev/cu.usbmodem144301, annars hittas kortet själv")
    arg.add_argument("--skicka", help="skicka en rad, vänta på svar, avsluta")
    val = arg.parse_args()

    lank = Lank(val.port or INST["port"] or None)

    if val.skicka:
        if not lank.anslut():
            sys.exit("hittar inget kort")
        lank.skicka(val.skicka)
        lank.las(2)
        return

    starta_brevlada()
    # En rad vid start om vad vakterna ser, så att loggen visar om tillstånden finns.
    logg("inställningar: " + ", ".join(k for k in ("kalender", "mejl", "paminnelser") if INST[k])
         + (", puls" if PULSSERVER else "") + (", backup" if (BACKUP_STATUS or OFFSITE_STATUS) else ""))
    try:
        r = subprocess.run([KALENDER_BIN, "1440"], capture_output=True, text=True, timeout=20) if os.path.exists(KALENDER_BIN) else None
        if r is None:
            logg("kalendern: hjälpen byggs vid första koll")
        elif r.returncode == 0:
            logg(f"kalendern: {len(r.stdout.splitlines())} händelser närmaste dygnet")
        else:
            logg("kalendern: " + (r.stderr.strip() or f"fel {r.returncode}"))
    except Exception as fel:
        logg(f"kalendern: {fel}")
    lage = mejl_lage() if INST["mejl"] else None
    if INST["mejl"]:
        logg("mejlen: " + (f"{lage[0]} olästa i inkorgen" if lage else "Mail svarar inte eller saknar tillstånd"))
    try:
        r = subprocess.run([KALENDER_BIN, "paminnelser", "1440"], capture_output=True, text=True, timeout=20)
        logg(f"påminnelserna: {len(r.stdout.splitlines())} förfaller inom ett dygn" if r.returncode == 0
             else "påminnelserna: " + (r.stderr.strip() or f"fel {r.returncode}"))
    except Exception as fel:
        logg(f"påminnelserna: {fel}")
    forra_antal = None
    lagre_i_rad = 0
    nasta_hamtning = 0
    nasta_klocka = 0
    backup_sagd_dag = None
    kalender = Kalendervakt()
    mejl = Mejlvakt()
    paminnelser = Paminnelsevakt()
    nasta_kalender = 0
    nasta_mejl = 0
    nasta_paminnelse = 0
    while True:
        if lank.fd is None:
            if not lank.anslut():
                time.sleep(5)
                continue
            lank.skicka("hej")
            nasta_klocka = 0
            nasta_hamtning = 0
        nu = time.time()
        if nu >= nasta_klocka:
            forskjutning = -time.altzone if time.localtime(nu).tm_isdst > 0 else -time.timezone
            lank.skicka(f"tid {int(nu)} {int(forskjutning)}")
            nasta_klocka = nu + KLOCKA_VAR_S
        if nu >= nasta_hamtning and PULSSERVER:
            puls = las_puls()
            if puls is not None:
                antal = int(puls.get("antal", 0))
                roda = sum(1 for p in puls.get("poster", []) if p.get("bradska") == "rod")
                lank.skicka(f"vantande {antal} {roda}")
                # Avbockat först när minskningen står sig två hämtningar i rad;
                # pulsservern kan flimra mellan två värden och det är ingen seger.
                if forra_antal is not None and antal < forra_antal:
                    lagre_i_rad += 1
                else:
                    lagre_i_rad = 0
                poster = puls.get("poster", [])
                if poster:
                    aldst = max(poster, key=lambda p: int(p.get("dagar", 0)))
                    lank.skicka(f"aldst {int(aldst.get('dagar', 0))} {aldst.get('projekt', '')[:60]}")
                if lagre_i_rad >= 2:
                    lank.skicka("avbockat")
                    lagre_i_rad = 0
                    forra_antal = antal
                elif lagre_i_rad == 0:
                    forra_antal = antal
            nasta_hamtning = nu + HAMTA_VAR_S
        if nu >= nasta_kalender and INST["kalender"]:
            kalender.kolla(lank)
            nasta_kalender = nu + KALENDER_VAR_S
        if nu >= nasta_mejl and INST["mejl"]:
            mejl.kolla(lank)
            nasta_mejl = nu + MEJL_VAR_S
        if nu >= nasta_paminnelse and INST["paminnelser"]:
            paminnelser.kolla(lank)
            nasta_paminnelse = nu + PAMINNELSE_VAR_S
        # Backupvakten: en gång per dag, efter att båda körningarna hunnit gå.
        lt = time.localtime(nu)
        dag = (lt.tm_year, lt.tm_yday)
        if (BACKUP_STATUS or OFFSITE_STATUS) and backup_sagd_dag != dag and lt.tm_hour + lt.tm_min / 60.0 >= BACKUP_KOLL_EFTER_TIMME:
            ok, text = backup_status()
            lank.skicka("backup ok" if ok else f"backup saknas {text}")
            backup_sagd_dag = dag
        while lank.fd is not None:
            try:
                rad = brev.get_nowait()
            except queue.Empty:
                break
            lank.skicka(rad)
        lank.las(1)


if __name__ == "__main__":
    main()
