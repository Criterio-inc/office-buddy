/*
 * Humöret — se humor.h för tanken. Här bor talen.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ansikte.h"
#include "humor.h"

/* ---- Tillståndet ------------------------------------------------------- */

static humor_t h;
static void (*ljud_krok)(humor_ljud_t);

static void ljud(humor_ljud_t l)
{
    if (ljud_krok != NULL) ljud_krok(l);
}

static float vaken_extra;       /* energi som en händelse gav, klingar av på minuter */
static float gladje_extra;      /* glädje som en händelse gav, klingar av på en minut */
static int   vantande_antal, vantande_roda;
static char  aldst_projekt[64];
static int   aldst_dagar;
static int32_t replik_kvar_ms;               /* tystnad tills nästa replik ur oron */
static claude_lage_t claude_lage = CLAUDE_JOBBAR;
static bool lank_ok = true;
static int32_t varm_kvar_ms;                 /* tillfällig orange, t.ex. saknad backup */
static bool sov_for_lanken;
static int32_t claude_paminn_ms;             /* tills nästa lilla blick mot datorn */

#define CLAUDE_MAX 8
typedef struct { char id[12]; char namn[26]; claude_lage_t lage; } claude_session_t;
static claude_session_t sessioner[CLAUDE_MAX];
static bool  morgon_sagd;                    /* en gång per morgon */
static bool  natt_sagd;

static uttryck_t grund = UTTRYCK_NEUTRAL;     /* det uttryck humöret valt */
static uttryck_t kandidat = UTTRYCK_NEUTRAL;  /* det som skulle väljas om det höll i sig */
static int32_t   kandidat_ms;                 /* hur länge kandidaten hållit */
static int32_t   grund_ms;                    /* hur länge grunduttrycket suttit */
static int32_t   val_kvar_ms;                 /* till nästa omprövning */

static int32_t   episod_kvar_ms;              /* till nästa lilla episod */
static int32_t   oro_kvar_ms;                 /* till nästa oroliga stund */
static int32_t   gasp_kvar_ms;                /* till nästa gäspning */
static bool      vaknade_nyss;

/* ---- Dygnet ------------------------------------------------------------ */

/* Energin över dygnet: pigg på förmiddagen, seg vid tretiden, sover på natten. */
static const struct { float timme, energi; } DYGN[] = {
    {  0.0f, 0.05f }, {  5.5f, 0.05f }, {  6.5f, 0.30f }, {  7.5f, 0.75f },
    {  9.0f, 0.92f }, { 11.0f, 0.85f }, { 12.5f, 0.62f }, { 13.5f, 0.36f },
    { 14.5f, 0.30f }, { 15.5f, 0.52f }, { 17.0f, 0.64f }, { 19.0f, 0.55f },
    { 20.5f, 0.42f }, { 21.5f, 0.30f }, { 22.2f, 0.18f }, { 23.0f, 0.08f }, { 24.0f, 0.05f },
};

static float energi_for(float timme)
{
    timme = fmodf(timme, 24.0f);
    if (timme < 0) timme += 24;
    size_t n = sizeof(DYGN) / sizeof(DYGN[0]);
    for (size_t i = 0; i + 1 < n; i++) {
        if (timme >= DYGN[i].timme && timme <= DYGN[i + 1].timme) {
            float t = (timme - DYGN[i].timme) / (DYGN[i + 1].timme - DYGN[i].timme);
            t = t * t * (3 - 2 * t);
            return DYGN[i].energi + (DYGN[i + 1].energi - DYGN[i].energi) * t;
        }
    }
    return DYGN[n - 1].energi;
}

static float begransa(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float slump(float lo, float hi)
{
    return lo + (hi - lo) * (float)lv_rand(0, 10000) / 10000.0f;
}

/* ---- Valet av uttryck -------------------------------------------------- */

static uttryck_t valj(void)
{
    if (h.sover)          return UTTRYCK_SOVER;
    if (h.oro > 0.9f)     return UTTRYCK_STRESSAD;
    if (h.gladje < -0.5f) return UTTRYCK_LEDSEN;
    if (h.gladje < -0.2f) return UTTRYCK_BESVIKEN;
    if (h.energi < 0.2f)  return UTTRYCK_SOMNIG;
    if (h.energi < 0.34f) return UTTRYCK_TROTT;
    if (h.gladje > 0.7f)  return UTTRYCK_NOJD;
    if (h.gladje > 0.4f)  return UTTRYCK_GLAD;
    /* En pigg förmiddag syns, om inte oron drar ned. */
    if (h.energi > 0.8f && h.gladje >= 0) return UTTRYCK_GLAD;
    return UTTRYCK_NEUTRAL;
}

/*
 * En liten episod ovanpå grunduttrycket. Vilken som väljs beror på läget,
 * och ingenting väljs alltid: tystnad är också ett svar.
 */
static void episod(void)
{
    float t = slump(0, 1);
    if (h.energi < 0.45f && t < 0.35f) {
        ansikte_tillfalligt(UTTRYCK_TROTT, 2500);
        ansikte_titta(0, 0.4f);
    } else if (t < 0.45f) {
        ansikte_tillfalligt(UTTRYCK_FUNDERSAM, (int32_t)slump(2200, 3800));
    } else if (t < 0.6f) {
        ansikte_tillfalligt(UTTRYCK_NYFIKEN, (int32_t)slump(1500, 2600));
        ansikte_titta(slump(-0.8f, 0.8f), slump(-0.5f, 0.2f));
    } else if (t < 0.7f) {
        ansikte_titta(slump(-0.9f, 0.9f), slump(-0.4f, 0.4f));
    } else if (t < 0.78f) {
        ansikte_blinka();
    }
    /* resten av gångerna: ingenting alls */
}

/*
 * Repliker. Få, och alltid ur något verkligt. En buddy som pratar för mycket
 * blir avstängd, så oron får ord högst var tionde minut, och morgonen och
 * natten en gång var.
 */
static void replik_oro(void)
{
    if (aldst_projekt[0] == '\0' || replik_kvar_ms > 0) return;
    char rad[128];
    if (aldst_dagar >= 60)
        snprintf(rad, sizeof(rad), "%s har väntat i %d dagar nu", aldst_projekt, aldst_dagar);
    else if (aldst_dagar >= 14)
        snprintf(rad, sizeof(rad), "%s väntar sedan %d dagar", aldst_projekt, aldst_dagar);
    else
        snprintf(rad, sizeof(rad), "%d saker väntar på dig", vantande_antal);
    ansikte_sag(rad, 7000);
    replik_kvar_ms = 600000;
}

/* ---- Utåt -------------------------------------------------------------- */

void humor_starta(float timme)
{
    vaken_extra = gladje_extra = 0;
    h.energi = energi_for(timme);
    h.sover  = h.energi < 0.12f;
    h.oro    = 0;
    h.gladje = 0.15f;
    grund = kandidat = valj();
    kandidat_ms = grund_ms = 0;
    val_kvar_ms = 1500;
    episod_kvar_ms = (int32_t)slump(20000, 60000);
    oro_kvar_ms    = (int32_t)slump(60000, 180000);
    gasp_kvar_ms   = (int32_t)slump(30000, 90000);
    vaknade_nyss = false;
    ansikte_sover(h.sover);
    ansikte_satt_uttryck(grund);
}

void humor_tick(float timme, int32_t dt_ms)
{
    if (dt_ms <= 0) return;
    float dts = (float)dt_ms / 1000.0f;

    /* Det som händelser gav klingar av: glädjen på en minut, vakenheten på tio. */
    gladje_extra *= expf(-dts / 60.0f);
    vaken_extra  *= expf(-dts / 600.0f);

    /*
     * Kalibrerad mot ett verkligt läge, 14 väntande varav 9 röda: det ska ge
     * oroliga stunder (oro kring 0,6), inte en stressad min hela dagen. Med
     * 0,12 per röd post stod buddyn stressad från första hämtningen.
     */
    h.oro    = begransa(vantande_roda * 0.05f + vantande_antal * 0.01f, 0, 1);
    h.energi = begransa(energi_for(timme) + vaken_extra, 0, 1);
    /* Oron drar bara lite i glädjen; den visar sig i stunder i stället. */
    h.gladje = begransa(0.15f + gladje_extra - h.oro * 0.15f, -1, 1);

    /* Somnar när energin är slut eller datorn sover; vaknar av morgonen, av ett lyft eller av datorn. */
    bool sov_forr = h.sover;
    if (!h.sover && h.energi < 0.10f) h.sover = true;
    if (h.sover && h.energi > 0.22f && lank_ok)  h.sover = false;
    if (!lank_ok) h.sover = true;
    if (sov_forr != h.sover) {
        ansikte_sover(h.sover);
        if (!h.sover) {
            vaknade_nyss = true;
            grund = UTTRYCK_SOMNIG;
            ansikte_satt_uttryck(grund);
            ansikte_gaspa();
            gasp_kvar_ms = 6000;
        }
    }

    /* Grunduttrycket byts först när ett annat val hållit i sig ett tag. */
    val_kvar_ms -= dt_ms;
    grund_ms += dt_ms;
    if (val_kvar_ms <= 0) {
        val_kvar_ms = 1500;
        uttryck_t v = valj();
        if (v == kandidat) kandidat_ms += 1500;
        else { kandidat = v; kandidat_ms = 0; }
        bool brådskande = (v == UTTRYCK_SOVER) != (grund == UTTRYCK_SOVER);
        if (kandidat != grund && (kandidat_ms >= 3000 || brådskande) && (grund_ms >= 6000 || brådskande)) {
            grund = kandidat;
            grund_ms = 0;
            ansikte_satt_uttryck(grund);
        }
    }

    if (replik_kvar_ms > 0) replik_kvar_ms -= dt_ms;
    if (varm_kvar_ms > 0) {
        varm_kvar_ms -= dt_ms;
        if (varm_kvar_ms <= 0 && claude_lage == CLAUDE_JOBBAR) ansikte_varm(false);
    }

    /* Claude väntar: en liten blick mot datorn då och då, utan ljud. */
    if (claude_lage != CLAUDE_JOBBAR && !h.sover) {
        claude_paminn_ms -= dt_ms;
        if (claude_paminn_ms <= 0) {
            claude_paminn_ms = (int32_t)slump(120000, 240000);
            ansikte_tillfalligt(UTTRYCK_NYFIKEN, 2000);
            ansikte_titta(0, 0.8f);
        }
    }

    /* Morgon och natt får ett ord var. */
    if (h.sover) {
        morgon_sagd = false;
        return;
    }
    natt_sagd = false;
    if (!morgon_sagd && h.energi > 0.55f && timme >= 6 && timme < 11) {
        morgon_sagd = true;
        ansikte_sag(vantande_antal > 0 ? "god morgon. det ligger saker och väntar." : "god morgon", 6000);
        ljud(LJUD_TRUDELUTT);
    }
    if (!natt_sagd && h.energi < 0.2f && timme >= 21) {
        natt_sagd = true;
        ansikte_sag("jag sover snart", 5000);
    }

    /* Gäspningar: täta när energin är låg, sällsynta annars. */
    gasp_kvar_ms -= dt_ms;
    if (gasp_kvar_ms <= 0) {
        if (h.energi < 0.45f || slump(0, 1) < 0.15f) ansikte_gaspa();
        gasp_kvar_ms = (int32_t)(h.energi < 0.35f ? slump(35000, 90000) : slump(120000, 420000));
    }

    /* Oron visar sig i stunder, inte som en min hela dagen. */
    oro_kvar_ms -= dt_ms;
    if (oro_kvar_ms <= 0) {
        if (h.oro > 0.4f) {
            ansikte_tillfalligt(UTTRYCK_OROLIG, (int32_t)slump(2500, 4500));
            replik_oro();
        }
        oro_kvar_ms = (int32_t)slump(90000, 300000);
    }

    /* Små episoder. */
    episod_kvar_ms -= dt_ms;
    if (episod_kvar_ms <= 0) {
        episod();
        episod_kvar_ms = (int32_t)slump(18000, 70000);
    }
}

/*
 * Kvittot. Orange är en fråga, och ett knack eller ett tryck är svaret:
 * färgen går tillbaka, raden försvinner, och buddyn nickar nöjt utan ljud.
 * Det gäller oavsett vad som frågade: Claude, ett möte, en påminnelse,
 * backupen eller timern.
 */
static void kvittera(void)
{
    ansikte_varm(false);
    varm_kvar_ms = 0;
    ansikte_sag("", 0);
    claude_lage = CLAUDE_JOBBAR;
    memset(sessioner, 0, sizeof(sessioner));
    ansikte_tillfalligt(UTTRYCK_NOJD, 1800);
    ansikte_blinka();
}

void humor_handelse(humor_handelse_t e)
{
    switch (e) {
    case HANDELSE_PETAD:
    case HANDELSE_KNACK:
        if (ansikte_ar_varm()) { kvittera(); break; }
        if (h.sover) {
            /* Vaknar till, men somnar om ifall det är natt. */
            vaken_extra += 0.15f;
            ansikte_sover(false);
            ansikte_tillfalligt(UTTRYCK_SOMNIG, 2500);
        } else {
            ansikte_petad();
        }
        gladje_extra += 0.25f;
        vaken_extra  += 0.06f;
        ljud(LJUD_BLIPP);
        break;
    case HANDELSE_LYFT:
        vaken_extra += 0.35f;
        if (h.sover) { h.sover = false; ansikte_sover(false); }
        ansikte_tillfalligt(UTTRYCK_FORVANAD, 1500);
        ljud(LJUD_LYFT);
        break;
    case HANDELSE_AVBOCKAT:
        gladje_extra += 0.6f;
        ansikte_tillfalligt(UTTRYCK_VALDIGT_GLAD, 3000);
        ljud(LJUD_GLAD);
        ansikte_sag(slump(0, 1) < 0.5f ? "en sak mindre!" : "det där blev gjort", 4000);
        break;
    }
}

void humor_satt_vantande(int antal, int roda)
{
    vantande_antal = antal;
    vantande_roda  = roda;
}

/* Skriver raden för det som återstår: väntande först, sedan klara. */
static void claude_visa(void)
{
    int vantar = 0, klara = 0;
    const claude_session_t *forsta_v = NULL, *forsta_k = NULL;
    for (int i = 0; i < CLAUDE_MAX; i++) {
        if (sessioner[i].id[0] == '\0') continue;
        if (sessioner[i].lage == CLAUDE_VANTAR) { vantar++; if (!forsta_v) forsta_v = &sessioner[i]; }
        if (sessioner[i].lage == CLAUDE_KLAR)   { klara++;  if (!forsta_k) forsta_k = &sessioner[i]; }
    }
    char rad[128];
    if (vantar > 0) {
        claude_lage = CLAUDE_VANTAR;
        if (vantar == 1) snprintf(rad, sizeof(rad), "Claude väntar: %s", forsta_v->namn);
        else snprintf(rad, sizeof(rad), "Claude väntar: %s och %d till", forsta_v->namn, vantar - 1);
        ansikte_varm(true);
        ansikte_sag(rad, 30 * 60 * 1000);
    } else if (klara > 0) {
        claude_lage = CLAUDE_KLAR;
        if (klara == 1) snprintf(rad, sizeof(rad), "Claude klar: %s", forsta_k->namn);
        else snprintf(rad, sizeof(rad), "Claude klar: %s och %d till", forsta_k->namn, klara - 1);
        ansikte_varm(true);
        ansikte_sag(rad, 30 * 60 * 1000);
    } else {
        claude_lage = CLAUDE_JOBBAR;
        ansikte_varm(false);
        ansikte_sag("", 0);
    }
}

static claude_session_t *claude_hitta(const char *id, bool skapa)
{
    for (int i = 0; i < CLAUDE_MAX; i++) {
        if (strcmp(sessioner[i].id, id) == 0) return &sessioner[i];
    }
    if (!skapa) return NULL;
    for (int i = 0; i < CLAUDE_MAX; i++) {
        if (sessioner[i].id[0] == '\0') {
            strncpy(sessioner[i].id, id, sizeof(sessioner[i].id) - 1);
            sessioner[i].lage = CLAUDE_JOBBAR;
            return &sessioner[i];
        }
    }
    /* Fullt: ta den första platsen, det är ändå den äldsta. */
    memset(&sessioner[0], 0, sizeof(sessioner[0]));
    strncpy(sessioner[0].id, id, sizeof(sessioner[0].id) - 1);
    return &sessioner[0];
}

void humor_satt_claude(claude_lage_t lage, const char *id, const char *namn)
{
    if (id == NULL || id[0] == '\0') id = "0";
    if (lage == CLAUDE_JOBBAR) {
        claude_session_t *s = claude_hitta(id, false);
        if (s == NULL) return;
        memset(s, 0, sizeof(*s));
        claude_visa();
        return;
    }
    claude_session_t *s = claude_hitta(id, true);
    if (namn != NULL && namn[0] != '\0') {
        strncpy(s->namn, namn, sizeof(s->namn) - 1);
        s->namn[sizeof(s->namn) - 1] = '\0';
    } else if (s->namn[0] == '\0') {
        strncpy(s->namn, "claude", sizeof(s->namn) - 1);
    }
    bool nytt = s->lage != lage;
    s->lage = lage;
    claude_visa();
    if (!nytt) return;
    /* Varje ny signal från en session hörs och syns, även om en annan redan väntar. */
    if (lage == CLAUDE_VANTAR) {
        ansikte_tillfalligt(UTTRYCK_NYFIKEN, 3000);
        ansikte_titta(0, 0.8f);
        ljud(LJUD_LYFT);
    } else {
        ansikte_tillfalligt(UTTRYCK_GLAD, 2500);
        ansikte_titta(0, 0.8f);
        ljud(LJUD_BLIPP);
    }
    claude_paminn_ms = (int32_t)slump(120000, 240000);
}

int humor_claude_vantande(void)
{
    int n = 0;
    for (int i = 0; i < CLAUDE_MAX; i++) if (sessioner[i].id[0] && sessioner[i].lage == CLAUDE_VANTAR) n++;
    return n;
}

void humor_satt_lank(bool ok)
{
    if (ok == lank_ok) return;
    lank_ok = ok;
    if (!ok) {
        sov_for_lanken = !h.sover;
        h.sover = true;
        ansikte_sover(true);
        ansikte_sag("", 0);
        ansikte_varm(false);
        claude_lage = CLAUDE_JOBBAR;
        memset(sessioner, 0, sizeof(sessioner));
    } else if (sov_for_lanken && h.energi > 0.22f) {
        sov_for_lanken = false;
        h.sover = false;
        ansikte_sover(false);
        grund = UTTRYCK_SOMNIG;
        ansikte_satt_uttryck(grund);
        ansikte_gaspa();
        ljud(LJUD_TRUDELUTT);
    }
}

void humor_mote(int minuter, const char *rubrik)
{
    char rad[128];
    if (minuter <= 1) {
        snprintf(rad, sizeof(rad), "möte nu: %s", rubrik ? rubrik : "");
        ansikte_varm(true);
        varm_kvar_ms = 90000;
        ansikte_tillfalligt(UTTRYCK_FORVANAD, 2500);
        ansikte_sag(rad, 90000);
        ljud(LJUD_TRUDELUTT);
    } else {
        snprintf(rad, sizeof(rad), "möte om %d min: %s", minuter, rubrik ? rubrik : "");
        ansikte_varm(true);
        varm_kvar_ms = 30000;
        ansikte_tillfalligt(UTTRYCK_NYFIKEN, 2500);
        ansikte_titta(0, 0.8f);
        ansikte_sag(rad, 30000);
        ljud(LJUD_LYFT);
    }
}

void humor_paminnelse(const char *text)
{
    char rad[128];
    snprintf(rad, sizeof(rad), "påminnelse: %s", text ? text : "");
    ansikte_varm(true);
    varm_kvar_ms = 30000;
    ansikte_tillfalligt(UTTRYCK_NYFIKEN, 2500);
    ansikte_titta(0, 0.8f);
    ansikte_sag(rad, 30000);
    ljud(LJUD_BLIPP);
}

void humor_mejl(uint32_t hex, const char *text)
{
    char rad[128];
    snprintf(rad, sizeof(rad), "nytt mejl: %s", text ? text : "");
    if (hex != 0) ansikte_ton(hex, 8000);
    ansikte_tillfalligt(UTTRYCK_NYFIKEN, 2000);
    ansikte_titta(0.7f, -0.3f);
    ansikte_sag(rad, 8000);
}

void humor_satt_backup(bool ok, const char *text)
{
    if (ok) return;   /* det som fungerar behöver inte sägas */
    ansikte_varm(true);
    varm_kvar_ms = 60000;
    ansikte_tillfalligt(UTTRYCK_OROLIG, 4000);
    ansikte_sag(text != NULL && text[0] ? text : "nattens backup saknas", 20000);
    ljud(LJUD_LYFT);
}

void humor_satt_aldst(const char *projekt, int dagar)
{
    if (projekt == NULL) { aldst_projekt[0] = '\0'; return; }
    strncpy(aldst_projekt, projekt, sizeof(aldst_projekt) - 1);
    aldst_projekt[sizeof(aldst_projekt) - 1] = '\0';
    aldst_dagar = dagar;
}

const humor_t *humor_las(void)
{
    return &h;
}

void humor_vid_ljud(void (*krok)(humor_ljud_t))
{
    ljud_krok = krok;
}

void humor_ljud(humor_ljud_t l)
{
    ljud(l);
}

const char *humor_ord(void)
{
    if (h.sover)          return "sover";
    if (h.oro > 0.9f)     return "stressad";
    if (h.gladje < -0.2f) return "nere";
    if (h.energi < 0.2f)  return "sömnig";
    if (h.energi < 0.34f) return "seg";
    if (h.gladje > 0.4f)  return "glad";
    if (h.energi > 0.75f) return "pigg";
    return "lugn";
}
