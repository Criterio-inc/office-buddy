/* Prioriterade scener, samma kod i simulatorn och kortet. */
#include <stdio.h>
#include <string.h>
#include "ansikte.h"
#include "humor.h"
#include "signaler.h"
#include "vyer.h"

#define ANTAL_AGENTER 16
#define ANTAL_SCENER 8

typedef struct {
    char aktor[8], id[65], projekt[64];
    uint32_t senast;
    bool anvand, visad, tyst;
} agent_t;
typedef struct {
    signal_typ_t typ;
    int prioritet, ms;
    uint32_t skapad, farg;
    char text[160];
    bool anvand;
} scen_t;
static agent_t agenter[ANTAL_AGENTER];
static scen_t ko[ANTAL_SCENER], aktiv;
static int visad_agent = -1;
static uint32_t start, senaste_mejlljud, agent_visad_sedan;
static bool mejlljud_spelat, vilar;
static lv_timer_t *timer;

static void kopiera(char *ut, size_t n, const char *in)
{
    snprintf(ut, n, "%s", in ? in : "");
    /* Kapa vid en hel UTF-8-kodpunkt. */
    size_t slut = strlen(ut), i = slut;
    while (i && ((unsigned char)ut[i - 1] & 0xc0) == 0x80) i--;
    if (i) {
        unsigned char c = (unsigned char)ut[i - 1];
        size_t behov = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
        if (slut - (i - 1) < behov) ut[i - 1] = 0;
    }
}

static void visa(signal_typ_t typ, const char *text, int ms, uint32_t farg, bool hors)
{
    vyer_visa(VY_ANSIKTE);
    if (typ >= SIGNAL_SMS && typ <= SIGNAL_BACKUP) {
        ansikte_signal(typ, ms);
        ansikte_ton(farg, farg ? ms : 0);
        ansikte_varm(typ == SIGNAL_PAMINNELSE || typ == SIGNAL_BACKUP);
        /* Symbolen i ögat bär budskapet; bara backupen byter uttryck. */
        if (typ == SIGNAL_BACKUP) { ansikte_tillfalligt(UTTRYCK_OROLIG, ms); ansikte_titta(0, .8f); }
        if (typ == SIGNAL_SMS || typ == SIGNAL_TEAMS) ansikte_ikon(typ == SIGNAL_SMS ? 2 : 3, farg, ms);
        ansikte_sag(text, ms);
        if (hors) humor_ljud(typ == SIGNAL_BACKUP ? LJUD_LYFT : LJUD_BLIPP);
        return;
    }
    ansikte_ton(farg, farg ? ms : 0);
    ansikte_varm(typ == SIGNAL_CLAUDE || typ == SIGNAL_CODEX || typ == SIGNAL_MOTE);
    bool klar = typ == SIGNAL_CLAUDE_KLAR || typ == SIGNAL_CODEX_KLAR;
    ansikte_signal(klar ? SIGNAL_KLAR : typ, ms);
    if (klar) ansikte_agentlogga(typ == SIGNAL_CLAUDE_KLAR ? 2 : 3);
    ansikte_sag(text, ms);
    if (!hors) return;
    humor_ljud(typ == SIGNAL_MEJL ? LJUD_MEJL : typ == SIGNAL_CLAUDE ? LJUD_CLAUDE
               : typ == SIGNAL_CODEX ? LJUD_CODEX : typ == SIGNAL_MOTE ? LJUD_MOTE : LJUD_GLAD);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    uint32_t nu = lv_tick_get();
    /* Bryggan förnyar bevakade frågor. Gamla direktkrokar får två timmar. */
    for (int i = 0; i < ANTAL_AGENTER; i++) {
        uint32_t liv = strncmp(agenter[i].id, "vp-", 3) == 0 ? 90000 : 7200000;
        if (agenter[i].anvand && nu - agenter[i].senast >= liv) agenter[i].anvand = false;
    }
    for (int i = 0; i < ANTAL_SCENER; i++)
        if (ko[i].anvand && nu - ko[i].skapad > 60000) ko[i].anvand = false;
    if (vilar) return;
    bool slut = aktiv.anvand && nu - start >= (uint32_t)aktiv.ms;
    if (slut) aktiv.anvand = false;
    int a = -1;
    for (int i = 0; i < ANTAL_AGENTER; i++)
        if (agenter[i].anvand && !agenter[i].tyst && (a < 0 || (!agenter[i].visad && agenter[a].visad))) a = i;
    if (visad_agent >= 0 && agenter[visad_agent].anvand && !agenter[visad_agent].tyst) {
        a = visad_agent;
        if (nu - agent_visad_sedan >= 5000) {
            for (int steg = 1; steg < ANTAL_AGENTER; steg++) {
                int n = (visad_agent + steg) % ANTAL_AGENTER;
                if (agenter[n].anvand && !agenter[n].tyst) { a = n; break; }
            }
        }
    }
    int q = -1;
    for (int i = 0; i < ANTAL_SCENER; i++)
        if (ko[i].anvand && (q < 0 || ko[i].prioritet > ko[q].prioritet)) q = i;
    int prio = aktiv.anvand ? aktiv.prioritet : 0;
    if (a >= 0 && prio < 3) {
        if (q < 0 || ko[q].prioritet < 3) {
            aktiv.anvand = false;
            if (visad_agent != a || nu - agent_visad_sedan >= 60000) {
                agent_t *p = &agenter[a];
                char text[160];
                snprintf(text, sizeof(text), "%s %s: %s", strcmp(p->aktor, "codex") == 0 ? "Codex" : "Claude", "väntar", p->projekt);
                visa(strcmp(p->aktor, "codex") == 0 ? SIGNAL_CODEX : SIGNAL_CLAUDE,
                     text, 7200000, 0, !p->visad);
                p->visad = true;
                visad_agent = a;
                agent_visad_sedan = nu;
            }
            return;
        }
    }
    if (q >= 0 && (!aktiv.anvand || ko[q].prioritet > prio)) {
        aktiv = ko[q]; ko[q].anvand = false; start = nu; visad_agent = -1;
        bool hors = true;
        if (aktiv.typ == SIGNAL_MEJL) {
            hors = !mejlljud_spelat || nu - senaste_mejlljud >= 60000;
            if (hors) { senaste_mejlljud = nu; mejlljud_spelat = true; }
        }
        visa(aktiv.typ, aktiv.text, aktiv.ms, aktiv.farg, hors);
    } else if (!aktiv.anvand && (slut || visad_agent >= 0 || ansikte_signal_typ() != 0)) {
        visad_agent = -1;
        ansikte_signal(0, 0); ansikte_varm(false); ansikte_sag("", 0);
    }
}

static void starta(void)
{
    if (!timer) timer = lv_timer_create(tick, 100, NULL);
}
static void lagg(signal_typ_t typ, int prio, int ms, uint32_t farg, const char *text)
{
    starta();
    int plats = -1;
    for (int i = 0; i < ANTAL_SCENER; i++) {
        if (ko[i].anvand && ko[i].typ == typ) { if (ko[i].prioritet > prio) return; plats = i; break; }
        if (!ko[i].anvand) plats = i;
    }
    if (plats < 0) {
        for (int i = 0; i < ANTAL_SCENER; i++)
            if (ko[i].prioritet < prio) { plats = i; break; }
    }
    if (plats < 0) return;
    ko[plats] = (scen_t){ .typ = typ, .prioritet = prio, .ms = ms,
        .skapad = lv_tick_get(), .farg = farg, .anvand = true };
    kopiera(ko[plats].text, sizeof(ko[plats].text), text);
    tick(NULL);
}

void signaler_agent(const char *aktor, const char *lage, const char *id, const char *projekt)
{
    starta();
    if (!id || !*id) id = "0";
    /* Nya arbeten ersätter korta klarnotiser, aldrig andra frågor. */
    signal_typ_t klar_typ = strcmp(aktor, "codex") == 0 ? SIGNAL_CODEX_KLAR : SIGNAL_CLAUDE_KLAR;
    if (strcmp(lage, "jobbar") == 0 || strcmp(lage, "klar") == 0) {
        if (aktiv.anvand && aktiv.typ == klar_typ) aktiv.anvand = false;
        for (int i = 0; i < ANTAL_SCENER; i++)
            if (ko[i].anvand && ko[i].typ == klar_typ) ko[i].anvand = false;
    }
    int plats = -1, ledig = -1;
    for (int i = 0; i < ANTAL_AGENTER; i++) {
        if (!agenter[i].anvand) { if (ledig < 0) ledig = i; continue; }
        if (strcmp(agenter[i].aktor, aktor) == 0 && strcmp(agenter[i].id, id) == 0) plats = i;
    }
    if (strcmp(lage, "vantar") == 0) {
        if (plats < 0) {
            plats = ledig;
            if (plats < 0) return; /* släck aldrig en annan fråga för att tabellen är full */
            agenter[plats] = (agent_t){ .anvand = true };
            kopiera(agenter[plats].aktor, sizeof(agenter[plats].aktor), aktor);
            kopiera(agenter[plats].id, sizeof(agenter[plats].id), id);
        }
        agenter[plats].senast = lv_tick_get();
        kopiera(agenter[plats].projekt, sizeof(agenter[plats].projekt), projekt && *projekt ? projekt : aktor);
    } else {
        if (plats >= 0) agenter[plats].anvand = false;
        if (strcmp(lage, "klar") == 0) {
            char text[160];
            snprintf(text, sizeof(text), "%s klar: %s", strcmp(aktor, "codex") == 0 ? "Codex" : "Claude", projekt ? projekt : "");
            lagg(klar_typ, 1, 20000, 0, text);
        }
    }
    tick(NULL);
}
void signaler_mejl(uint32_t farg, const char *text)
{
    char rad[160]; snprintf(rad, sizeof(rad), "Nytt mejl: %s", text ? text : "");
    lagg(SIGNAL_MEJL, 1, 5000, farg, rad);
}
void signaler_mote(int minuter, const char *text)
{
    char rad[160];
    if (minuter <= 0) snprintf(rad, sizeof(rad), "Möte nu: %s", text ? text : "");
    else snprintf(rad, sizeof(rad), "Möte om %d min: %s", minuter, text ? text : "");
    lagg(SIGNAL_MOTE, minuter <= 0 ? 4 : 3, minuter <= 0 ? 12000 : 8000, 0, rad);
}
void signaler_timer(void)
{
    lagg(SIGNAL_MOTE, 3, 12000, 0, "Tiden är ute");
}
void signaler_kvittera(void)
{
    for (int i = 0; i < ANTAL_AGENTER; i++) agenter[i].tyst = true;
    memset(ko, 0, sizeof(ko)); aktiv.anvand = false; visad_agent = -1;
    ansikte_signal(0, 0); ansikte_varm(false); ansikte_sag("", 0);
}
void signaler_vila(bool vila)
{
    if (vilar == vila) return;
    vilar = vila;
    if (vila) {
        memset(ko, 0, sizeof(ko)); aktiv.anvand = false; visad_agent = -1;
        ansikte_signal(0, 0); ansikte_varm(false); ansikte_sag("", 0);
    } else tick(NULL);
}
bool signaler_upptagen(void) { return !vilar && (aktiv.anvand || visad_agent >= 0); }
int signaler_vantande(void)
{
    int n = 0;
    for (int i = 0; i < ANTAL_AGENTER; i++) if (agenter[i].anvand) n++;
    return n;
}

void signaler_notis(signal_typ_t typ, uint32_t farg, const char *text)
{
    lagg(typ, 1, typ == SIGNAL_PAMINNELSE ? 12000 : 8000, farg, text);
}
