#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ansikte.h"
#include "humor.h"
#include "protokoll.h"
#include "vyer.h"

static protokoll_krokar_t krokar;

void protokoll_satt_krokar(const protokoll_krokar_t *k)
{
    krokar = k != NULL ? *k : (protokoll_krokar_t){ 0 };
}

/* Tar bort radslut och blanksteg i båda ändar. */
static void putsa(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i > 0) memmove(s, s + i, n - i + 1);
}

static bool borjar_med(const char *rad, const char *ord, const char **rest)
{
    size_t n = strlen(ord);
    if (strncmp(rad, ord, n) != 0) return false;
    if (rad[n] != '\0' && rad[n] != ' ') return false;
    *rest = rad + n;
    while (**rest == ' ') (*rest)++;
    return true;
}

static uttryck_t uttryck_fran_namn(const char *namn, bool *hittat)
{
    for (int u = 0; u < UTTRYCK_ANTAL; u++) {
        if (strcmp(uttryck_namn((uttryck_t)u), namn) == 0) { *hittat = true; return (uttryck_t)u; }
    }
    *hittat = false;
    return UTTRYCK_NEUTRAL;
}

bool protokoll_rad(const char *inrad, char *svar, size_t storlek)
{
    char rad[256];
    strncpy(rad, inrad, sizeof(rad) - 1);
    rad[sizeof(rad) - 1] = '\0';
    putsa(rad);
    const char *rest = "";
    const humor_t *h = humor_las();

    if (rad[0] == '\0') { snprintf(svar, storlek, "ob fel tom rad"); return false; }

    if (borjar_med(rad, "hej", &rest)) {
        snprintf(svar, storlek, "ob hej office-buddy %s", uttryck_namn(ansikte_uttryck()));
        return true;
    }
    if (borjar_med(rad, "status", &rest)) {
        snprintf(svar, storlek, "ob status energi %.2f glädje %.2f oro %.2f %s %s",
                 h->energi, h->gladje, h->oro, humor_ord(), uttryck_namn(ansikte_uttryck()));
        return true;
    }
    if (borjar_med(rad, "tid", &rest)) {
        long epok = 0, forskjutning = LONG_MIN;
        int n = sscanf(rest, "%ld %ld", &epok, &forskjutning);
        if (n < 1 || epok < 1600000000L) { snprintf(svar, storlek, "ob fel tid: väntar unix-tid i UTC"); return false; }
        if (n < 2) forskjutning = LONG_MIN;
        if (krokar.tid != NULL) krokar.tid(epok, forskjutning);
        snprintf(svar, storlek, "ob ok tid %ld", epok);
        return true;
    }
    if (borjar_med(rad, "vantande", &rest)) {
        int antal = 0, roda = 0;
        if (sscanf(rest, "%d %d", &antal, &roda) < 1 || antal < 0 || roda < 0) {
            snprintf(svar, storlek, "ob fel vantande: väntar <antal> <röda>");
            return false;
        }
        humor_satt_vantande(antal, roda);
        snprintf(svar, storlek, "ob ok vantande %d %d", antal, roda);
        return true;
    }
    if (borjar_med(rad, "aldst", &rest)) {
        int dagar = 0; char namn[64] = "";
        if (sscanf(rest, "%d %63[^\n]", &dagar, namn) < 2) { snprintf(svar, storlek, "ob fel aldst: väntar <dagar> <projekt>"); return false; }
        humor_satt_aldst(namn, dagar);
        snprintf(svar, storlek, "ob ok aldst %d %s", dagar, namn);
        return true;
    }
    if (borjar_med(rad, "sag", &rest)) {
        if (rest[0] == '\0') { snprintf(svar, storlek, "ob fel sag: tom"); return false; }
        ansikte_sag(rest, 7000);
        snprintf(svar, storlek, "ob ok sag");
        return true;
    }
    if (borjar_med(rad, "avbockat", &rest)) { humor_handelse(HANDELSE_AVBOCKAT); snprintf(svar, storlek, "ob ok avbockat"); return true; }
    if (borjar_med(rad, "peta", &rest))     { humor_handelse(HANDELSE_PETAD);    snprintf(svar, storlek, "ob ok peta");     return true; }
    if (borjar_med(rad, "knack", &rest))    { humor_handelse(HANDELSE_KNACK);    snprintf(svar, storlek, "ob ok knack");    return true; }
    if (borjar_med(rad, "lyft", &rest))     { humor_handelse(HANDELSE_LYFT);     snprintf(svar, storlek, "ob ok lyft");     return true; }
    if (borjar_med(rad, "uttryck", &rest)) {
        /* Namnet kan innehålla blanksteg ("väldigt glad"); ett avslutande tal är tiden i ms. */
        char namn[64];
        strncpy(namn, rest, sizeof(namn) - 1);
        namn[sizeof(namn) - 1] = '\0';
        int ms = 4000;
        char *sista = strrchr(namn, ' ');
        if (sista != NULL && sista[1] >= '0' && sista[1] <= '9') { ms = atoi(sista + 1); *sista = '\0'; }
        bool hittat;
        uttryck_t u = uttryck_fran_namn(namn, &hittat);
        if (!hittat) { snprintf(svar, storlek, "ob fel okänt uttryck '%s'", namn); return false; }
        if (ms <= 0) ansikte_satt_uttryck(u); else ansikte_tillfalligt(u, ms);
        snprintf(svar, storlek, "ob ok uttryck %s %d", uttryck_namn(u), ms);
        return true;
    }
    if (borjar_med(rad, "ljus", &rest)) {
        int procent = atoi(rest);
        if (procent < 0 || procent > 100) { snprintf(svar, storlek, "ob fel ljus: 0-100"); return false; }
        if (krokar.ljus != NULL) krokar.ljus(procent);
        snprintf(svar, storlek, "ob ok ljus %d", procent);
        return true;
    }
    if (borjar_med(rad, "mote", &rest)) {
        int min = 0; char rubrik[100] = "";
        if (sscanf(rest, "%d %99[^\n]", &min, rubrik) < 1) { snprintf(svar, storlek, "ob fel mote: väntar <minuter> <rubrik>"); return false; }
        humor_mote(min, rubrik);
        snprintf(svar, storlek, "ob ok mote %d", min);
        return true;
    }
    if (borjar_med(rad, "paminnelse", &rest)) {
        humor_paminnelse(rest);
        snprintf(svar, storlek, "ob ok paminnelse");
        return true;
    }
    if (borjar_med(rad, "mejl", &rest)) {
        uint32_t hex = 0;
        if (rest[0] == '#') {
            hex = (uint32_t)strtoul(rest + 1, NULL, 16);
            const char *m = strchr(rest, ' ');
            rest = m != NULL ? m + 1 : "";
        }
        humor_mejl(hex, rest);
        snprintf(svar, storlek, "ob ok mejl");
        return true;
    }
    if (borjar_med(rad, "backup", &rest)) {
        const char *text = "";
        bool ok = borjar_med(rest, "ok", &text);
        if (!ok && !borjar_med(rest, "saknas", &text)) { snprintf(svar, storlek, "ob fel backup: ok eller saknas"); return false; }
        humor_satt_backup(ok, text);
        snprintf(svar, storlek, "ob ok backup %s", ok ? "ok" : "saknas");
        return true;
    }
    if (borjar_med(rad, "vy", &rest)) {
        vy_t vy;
        if      (strcmp(rest, "ansikte") == 0) vy = VY_ANSIKTE;
        else if (strcmp(rest, "klocka") == 0)  vy = VY_KLOCKA;
        else if (strcmp(rest, "timer") == 0)   vy = VY_TIMER;
        else { snprintf(svar, storlek, "ob fel vy: ansikte, klocka eller timer"); return false; }
        vyer_visa(vy);
        snprintf(svar, storlek, "ob ok vy %s", rest);
        return true;
    }
    if (borjar_med(rad, "timer", &rest)) {
        int min = atoi(rest);
        vyer_timer_satt(min);
        if (min > 0) { vyer_timer_starta(true); vyer_visa(VY_TIMER); }
        snprintf(svar, storlek, "ob ok timer %d", min);
        return true;
    }
    if (borjar_med(rad, "claude", &rest)) {
        char ord[16] = "", id[12] = "", namn[26] = "";
        sscanf(rest, "%15s %11s %25[^\n]", ord, id, namn);
        claude_lage_t lage;
        if      (strcmp(ord, "vantar") == 0) lage = CLAUDE_VANTAR;
        else if (strcmp(ord, "klar") == 0)   lage = CLAUDE_KLAR;
        else if (strcmp(ord, "jobbar") == 0) lage = CLAUDE_JOBBAR;
        else { snprintf(svar, storlek, "ob fel claude: vantar, klar eller jobbar"); return false; }
        humor_satt_claude(lage, id, namn);
        snprintf(svar, storlek, "ob ok claude %s %s, %d väntar", ord, id[0] ? id : "0", humor_claude_vantande());
        return true;
    }
    if (borjar_med(rad, "spela", &rest)) {
        static const char *const LJUD[LJUD_ANTAL] = { "blipp", "lyft", "glad", "trudelutt" };
        for (int i = 0; i < LJUD_ANTAL; i++) {
            if (strcmp(rest, LJUD[i]) == 0) {
                humor_ljud((humor_ljud_t)i);
                snprintf(svar, storlek, "ob ok spela %s", LJUD[i]);
                return true;
            }
        }
        snprintf(svar, storlek, "ob fel spela: blipp, lyft, glad eller trudelutt");
        return false;
    }
    if (borjar_med(rad, "ljud", &rest)) {
        bool pa = atoi(rest) != 0;
        if (krokar.ljud != NULL) krokar.ljud(pa);
        snprintf(svar, storlek, "ob ok ljud %d", pa ? 1 : 0);
        return true;
    }
    snprintf(svar, storlek, "ob fel okänt kommando '%s'", rad);
    return false;
}
