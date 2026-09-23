/*
 * Ansiktet — ritkoden och rörelsen, delad mellan kortet och emulatorn.
 *
 * Svart bakgrund är inte ett stilval utan ett hårdvaruval: på en AMOLED är
 * en släckt bildpunkt verkligen släckt. Ansiktet lyser cyanblått ur svärtan,
 * som i förlagan.
 *
 * Tre lager av rörelse ligger ovanpå varandra:
 *   1. Uttrycket: en uppsättning tal som beskriver ögon och mun. Byte av
 *      uttryck betyder att talen glider mot nya mål.
 *   2. Livet: blinkningar med slumpade mellanrum, en blick som vandrar och
 *      kommer tillbaka, ett nästan osynligt darr, och en andning.
 *   3. Det tillfälliga: en gäspning eller en förvåning som lägger sig
 *      ovanpå en stund och sedan släpper.
 *
 * Ingenting upprepas exakt likadant. Det är skillnaden mellan en animation
 * och en varelse.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ansikte.h"
/* Claudes och Codex loggor är varumärken och ligger inte i repot. Finns
 * delat/agentloggor.h lokalt används de, annars egna vektorsymboler. */
#if __has_include("agentloggor.h")
#include "agentloggor.h"
#define HAR_AGENTLOGGOR 1
#endif

/* ---- Färger ------------------------------------------------------------ */

#define FARG_BAKGRUND lv_color_hex(0x000000)
/*
 * Paletten. Ögonvitan är benvit, irisen bär den gröna, munnen är rosa,
 * brynen ljusgröna. Orange är fortfarande reserverad för "behöver dig":
 * då tonas alla ljusa delar mot den, liksom mot ett mejlkontos färg.
 */
#define FARG_OGA      lv_color_hex(0xF5F1E6)   /* ögonvitan, benvit */
#define FARG_IRIS     lv_color_hex(ANSIKTE_FARG_GRUND)
#define FARG_PUPILL   lv_color_hex(0x141C17)   /* nästan svart, med en aning grönt */
#define FARG_GLIMT    lv_color_hex(0xFFFFFF)   /* ljusglimten i pupillen */
#define FARG_MUN      lv_color_hex(0xF29AA6)   /* rosa */
#define FARG_BRYN     lv_color_hex(0xC9D6BC)   /* ljusgrön */
#define FARG_HJARTA   lv_color_hex(0xFF6B8B)
#define FARG_SPIRAL   lv_color_hex(0xB79CFF)
#define FARG_ORANGE   lv_color_hex(0xFFA042)   /* varm, när något behöver dig */
#define FARG_RODNAD   lv_color_hex(0xF08A8A)
#define FARG_REPLIK_K lv_color_hex(0xB8C2B0)   /* dämpat, orden ska inte tävla med ögonen */

/*
 * Färgen just nu. Målet är cyan, orange när något behöver dig, eller en
 * tillfällig ton; färgen glider dit och ritas därefter. varm_nu används
 * bara för replikens matta variant.
 */
static float      varm_mal;
static lv_color_t ton_farg;
static int32_t    ton_kvar_ms;
static lv_color_t ton_aktiv;     /* färgen allt tonas mot just nu */
static int32_t    varm_auto_ms;  /* orange släpper av sig själv efter en stund */
static float      ton_andel;     /* 0..1, hur långt tonen nått */

static float begransa(float v, float lo, float hi);

/* Blandar två färger med en andel 0..1. */
static lv_color_t blanda(lv_color_t fran, lv_color_t till, float andel)
{
    return lv_color_mix(till, fran, (lv_opa_t)(begransa(andel, 0, 1) * 255));
}

/* En av ansiktets ljusa färger, tonad mot orange eller ett kontos färg. */
static lv_color_t farg(lv_color_t bas)
{
    return ton_andel < 0.01f ? bas : blanda(bas, ton_aktiv, ton_andel);
}

LV_FONT_DECLARE(lv_font_replik);

/* ---- Var saker sitter -------------------------------------------------- */

/*
 * Uttrycken är ritade i en mindre skala och förstoras här, så att talen i
 * tabellen är hanterliga. 1,2 lämnar luft runt det samlade ansiktet.
 */
#define SKALA      1.2f

#define OGA_CX_V   123.0f
#define OGA_CX_H   245.0f
#define OGA_CY     200.0f
#define MUN_CX     184.0f
#define MUN_CY     284.0f

#define TICK_MS    33

/* ---- Modellen ---------------------------------------------------------- */

/*
 * Ett öga. Alla fält är float, och det är inte en tillfällighet: hela
 * strukturen behandlas som en rad tal som glider mot sina mål.
 */
typedef struct {
    float w, h;        /* mått i bildpunkter */
    float r;           /* hörnrundning, 0 = fyrkant, 1 = helt rund */
    float oppen;       /* 0..1, hur öppet ögat är */
    float lock;        /* 0..1, hur långt det övre locket hänger ned */
    float lutning;     /* -1..1, lockets lutning: + = arg (inre hörnet lägre), - = ledsen */
    float glad;        /* 0..1, en båge underifrån som gör ögat till ett leende ^ */
    float botten;      /* 0..1, en rak kant underifrån som gör ögat till ett D */
    float form;        /* 0 = vanligt, 1 = hjärta, 2 = spiral */
    float bryn;        /* 0..1, hur synligt ögonbrynet är */
    float bryn_hojd;   /* 0..1, hur högt över ögat det sitter */
    float bryn_lut;    /* -1..1, + = inre änden lägre (arg), - = inre änden högre (ledsen) */
    float pupill;      /* 0..1, pupillens storlek som andel av ögats bredd; 0 = ingen */
} oga_t;

typedef struct {
    float w, h;        /* h = 0 ger streck eller båge, h > 0 ger öppen mun */
    float kurva;       /* -1..1, sur till glad */
    float vag;         /* 0..1, vågig mun */
    float platt;       /* 0..1, rak överkant på en öppen mun (skratt) */
    float y;           /* förskjutning i höjdled */
} mun_t;

typedef struct {
    oga_t v, h;        /* vänster och höger öga, sett från betraktaren */
    mun_t mun;
    float blick_x, blick_y;   /* vart uttrycket i sig tittar, -1..1 */
    float darr;               /* 0..1, hur oroligt allt darrar */
    float rodnad;             /* 0..1, fläckar under ögonen */
} param_t;

#define ANTAL_TAL (sizeof(param_t) / sizeof(float))

/* ---- Uttrycken --------------------------------------------------------- */

#define OGA_STD .w = 84, .h = 92, .r = 1, .oppen = 1, .pupill = 0.58f
#define OGA(...) { __VA_ARGS__ }
#define BADA(o)  .v = o, .h = o

static const param_t UTTRYCK[UTTRYCK_ANTAL] = {
    [UTTRYCK_NEUTRAL]      = { .v = OGA(OGA_STD),
                               .h = OGA(.w = 82, .h = 88, .r = 1, .oppen = 1, .pupill = 0.58f),
                               .mun = { .w = 32, .kurva = 0.55f } },
    [UTTRYCK_GLAD]         = { BADA(OGA(OGA_STD, .botten = 0.12f)),
                               .mun = { .w = 42, .kurva = 0.75f } },
    [UTTRYCK_VALDIGT_GLAD] = { BADA(OGA(OGA_STD, .glad = 0.65f)),
                               .mun = { .w = 52, .h = 24, .platt = 1 } },
    [UTTRYCK_FORVANAD]     = { BADA(OGA(.w = 86, .h = 96, .r = 1, .oppen = 1, .pupill = 0.50f)),
                               .mun = { .w = 14, .h = 18 }, .blick_y = -0.1f },
    [UTTRYCK_ENTUSIASTISK] = { BADA(OGA(OGA_STD, .glad = 0.55f, .botten = 0.1f)),
                               .mun = { .w = 58, .h = 28, .platt = 1 } },
    [UTTRYCK_NOJD]         = { BADA(OGA(OGA_STD, .glad = 0.92f)),
                               .mun = { .w = 52, .kurva = 0.6f } },
    [UTTRYCK_BLINKNING]    = { .v = OGA(OGA_STD, .glad = 0.95f), .h = OGA(OGA_STD),
                               .mun = { .w = 48, .kurva = 0.7f } },
    [UTTRYCK_LEDSEN]       = { BADA(OGA(OGA_STD, .lock = 0.28f, .lutning = -0.7f, .bryn = 1, .bryn_hojd = 0.3f, .bryn_lut = -0.6f)),
                               .mun = { .w = 46, .kurva = -0.55f, .vag = 0.35f }, .blick_y = 0.25f },
    [UTTRYCK_BESVIKEN]     = { BADA(OGA(OGA_STD, .lock = 0.45f, .lutning = -0.35f, .bryn = 1, .bryn_hojd = 0.15f, .bryn_lut = -0.3f)),
                               .mun = { .w = 42, .kurva = -0.5f } },
    [UTTRYCK_OROLIG]       = { BADA(OGA(.w = 82, .h = 86, .r = 1, .oppen = 1, .lock = 0.18f, .lutning = -0.6f, .bryn = 1, .bryn_hojd = 0.4f, .bryn_lut = -0.7f, .pupill = 0.3f)),
                               .mun = { .w = 30, .h = 14, .platt = 1 }, .darr = 0.3f },
    [UTTRYCK_ARG]          = { BADA(OGA(OGA_STD, .lock = 0.3f, .lutning = 0.85f, .bryn = 1, .bryn_hojd = 0.2f, .bryn_lut = 0.8f, .pupill = 0.26f)),
                               .mun = { .w = 46, .kurva = -0.5f } },
    [UTTRYCK_FUNDERSAM]    = { .v = OGA(OGA_STD, .lock = 0.12f, .bryn = 1, .bryn_hojd = 0.5f, .bryn_lut = 0.15f),
                               .h = OGA(OGA_STD, .lock = 0.3f, .lutning = 0.2f, .bryn = 1, .bryn_hojd = 0.12f, .bryn_lut = 0.1f),
                               .mun = { .w = 34, .kurva = -0.15f }, .blick_x = -0.55f, .blick_y = -0.5f },
    [UTTRYCK_TROTT]        = { BADA(OGA(OGA_STD, .lock = 0.58f, .bryn = 1, .bryn_hojd = 0.08f)),
                               .mun = { .w = 40 } },
    [UTTRYCK_SOMNIG]       = { BADA(OGA(.w = 84, .h = 96, .r = 0.8f, .oppen = 0.18f, .glad = 0.5f)),
                               .mun = { .w = 16, .h = 18 }, .blick_y = 0.2f },
    [UTTRYCK_GASPAR]       = { BADA(OGA(OGA_STD, .glad = 0.95f)),
                               .mun = { .w = 46, .h = 62 } },
    [UTTRYCK_STRESSAD]     = { BADA(OGA(.w = 66, .h = 66, .r = 1, .oppen = 1, .lock = 0.1f, .lutning = -0.3f, .bryn = 1, .bryn_hojd = 0.35f, .bryn_lut = -0.5f, .pupill = 0.22f)),
                               .mun = { .w = 52, .vag = 1 }, .darr = 1 },
    [UTTRYCK_NYFIKEN]      = { .v = OGA(.w = 88, .h = 100, .r = 1, .oppen = 1, .bryn = 0.25f, .bryn_hojd = 0.3f, .pupill = 0.58f),
                               .h = OGA(.w = 80, .h = 88, .r = 1, .oppen = 1, .pupill = 0.58f),
                               .mun = { .w = 30, .kurva = 0.45f }, .blick_x = 0.3f, .blick_y = -0.15f },
    [UTTRYCK_KAR]          = { BADA(OGA(.w = 92, .h = 88, .r = 0.5f, .oppen = 1, .form = 1, .pupill = 0)),
                               .mun = { .w = 56, .kurva = 0.8f } },
    [UTTRYCK_OVERVALDIGAD] = { BADA(OGA(.w = 84, .h = 84, .r = 1, .oppen = 1, .form = 2, .pupill = 0)),
                               .mun = { .w = 50, .vag = 0.8f }, .darr = 0.6f },
    [UTTRYCK_START]        = { BADA(OGA(.w = 62, .h = 46, .r = 0.08f, .oppen = 1, .pupill = 0)),
                               .mun = { .w = 0 } },
    [UTTRYCK_SOVER]        = { BADA(OGA(OGA_STD, .form = 3, .pupill = 0)),
                               .mun = { .w = 22, .kurva = 0.2f }, .blick_y = 0.15f },
};

static const char *const NAMN[UTTRYCK_ANTAL] = {
    [UTTRYCK_NEUTRAL] = "neutral",         [UTTRYCK_GLAD] = "glad",
    [UTTRYCK_VALDIGT_GLAD] = "väldigt glad", [UTTRYCK_FORVANAD] = "förvånad",
    [UTTRYCK_ENTUSIASTISK] = "entusiastisk", [UTTRYCK_NOJD] = "nöjd",
    [UTTRYCK_BLINKNING] = "blinkning",     [UTTRYCK_LEDSEN] = "ledsen",
    [UTTRYCK_BESVIKEN] = "besviken",       [UTTRYCK_OROLIG] = "orolig",
    [UTTRYCK_ARG] = "arg",                 [UTTRYCK_FUNDERSAM] = "fundersam",
    [UTTRYCK_TROTT] = "trött",             [UTTRYCK_SOMNIG] = "sömnig",
    [UTTRYCK_GASPAR] = "gäspar",           [UTTRYCK_STRESSAD] = "stressad",
    [UTTRYCK_NYFIKEN] = "nyfiken",         [UTTRYCK_KAR] = "kär",
    [UTTRYCK_OVERVALDIGAD] = "överväldigad", [UTTRYCK_SOVER] = "sover",
    [UTTRYCK_START] = "start",
};

/* ---- Tillståndet ------------------------------------------------------- */

static lv_obj_t *yta;
static lv_obj_t *replik;          /* raden under ansiktet */
static int32_t   replik_kvar_ms;  /* hur länge den står kvar, 0 = borta */
static float     replik_opa;      /* 0..1, tonar in och ut */
static lv_timer_t *klocka;

static uttryck_t uttryck_nu = UTTRYCK_NEUTRAL;
static param_t nu;              /* det som ritas just nu */
static param_t mal;             /* det som talen glider mot */

/* Tillfälligt uttryck ovanpå grunduttrycket. */
static uttryck_t aterga_till;
static int32_t   tillfalligt_kvar_ms;

/* Blinkningen. */
static enum { BLINK_VILA, BLINK_STANGER, BLINK_OPPNAR } blink_lage;
static int32_t blink_kvar_ms;   /* till nästa blinkning, eller inom fasen */
static bool    blink_dubbel;    /* nästa blinkning kommer tätt inpå */
static float   blink_oppen = 1; /* 1 = öppet, 0 = stängt, läggs ovanpå uttrycket */

/* Blicken. */
static float   blick_x, blick_y;       /* där ögonen är nu */
static float   blick_mal_x, blick_mal_y;
static int32_t blick_kvar_ms;          /* tills blicken vandrar vidare */
static int32_t blick_lasta_ms;         /* hålls kvar efter ansikte_titta() */
static float   darr_x, darr_y;

/* Pupillerna gör små egna hopp, sackader, som ögongloben inte följer. */
static float   sackad_x, sackad_y, sackad_mal_x, sackad_mal_y;
static int32_t sackad_kvar_ms;

/* Kvicka övergångar en kort stund efter ett byte, så reaktioner känns snabba. */
static int32_t snabb_kvar_ms;
static int32_t rodnad_kvar_ms;

/* Startsekvensen. */
static int32_t   start_kvar_ms;
static uttryck_t start_aterga;

/* Drömmen: brus över glaset. */
static int32_t  drom_kvar_ms;
static uint32_t drom_fro;
static int32_t  drom_byt_ms;
static bool     drom_nyss;

/* Ikonen. */
static int32_t  ikon_kvar_ms, ikon_total_ms;
static int      ikon_typ;
static lv_color_t ikon_farg;

/* Flugan. */
static int32_t leka_kvar_ms;
static float   fluga_x, fluga_y, fluga_vx, fluga_vy;

/* Andningen. */
static float   andning_fas;
static float   andning_period_ms = 4300;
static float   andning_djup = 1;       /* 1 vaken, större i sömnen */
static bool    sover;

static uint32_t forra_tick_ms;
static lv_area_t forra_yta;    /* det som ritades förra varvet, ska ritas om */
static bool      forra_hornet; /* något syntes i hörnet förra varvet */

/* ---- Små hjälpare ------------------------------------------------------ */

static float begransa(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float slump(float lo, float hi)
{
    return lo + (hi - lo) * (float)lv_rand(0, 10000) / 10000.0f;
}

static float mjuk(float t)   /* smoothstep, 0..1 → 0..1 */
{
    t = begransa(t, 0, 1);
    return t * t * (3 - 2 * t);
}

static void area_satt(lv_area_t *a, float x1, float y1, float x2, float y2)
{
    a->x1 = (int32_t)floorf(x1);
    a->y1 = (int32_t)floorf(y1);
    a->x2 = (int32_t)ceilf(x2);
    a->y2 = (int32_t)ceilf(y2);
}

/* Snittet av två rutor. Falskt om de inte överlappar. */
static bool area_snitt(lv_area_t *ut, const lv_area_t *a, const lv_area_t *b)
{
    ut->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    ut->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    ut->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    ut->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    return ut->x1 <= ut->x2 && ut->y1 <= ut->y2;
}

static void area_utvidga(lv_area_t *a, const lv_area_t *b)
{
    if (a->x2 < a->x1) { *a = *b; return; }
    if (b->x1 < a->x1) a->x1 = b->x1;
    if (b->y1 < a->y1) a->y1 = b->y1;
    if (b->x2 > a->x2) a->x2 = b->x2;
    if (b->y2 > a->y2) a->y2 = b->y2;
}

/* ---- Ritning ----------------------------------------------------------- */

static void rita_rekt(lv_layer_t *l, lv_area_t *a, lv_color_t farg, lv_opa_t opa, int32_t radie)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = farg;
    d.bg_opa   = opa;
    d.radius   = radie;
    lv_draw_rect(l, &d, a);
}

static void rita_triangel(lv_layer_t *l, float x0, float y0, float x1, float y1, float x2, float y2, lv_color_t farg)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = farg;
    d.opa   = LV_OPA_COVER;
    d.p[0].x = (lv_value_precise_t)lroundf(x0); d.p[0].y = (lv_value_precise_t)lroundf(y0);
    d.p[1].x = (lv_value_precise_t)lroundf(x1); d.p[1].y = (lv_value_precise_t)lroundf(y1);
    d.p[2].x = (lv_value_precise_t)lroundf(x2); d.p[2].y = (lv_value_precise_t)lroundf(y2);
    lv_draw_triangle(l, &d);
}

static void rita_linje_opa(lv_layer_t *l, lv_point_precise_t *p, int32_t antal, int32_t bredd, lv_color_t farg, lv_opa_t opa)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color       = farg;
    d.width       = bredd;
    d.round_start = 1;
    d.round_end   = 1;
    d.opa         = opa;
    if (antal == 2) {
        d.p1 = p[0];
        d.p2 = p[1];
        lv_draw_line(l, &d);
    } else {
        for (int32_t i = 0; i + 1 < antal; i++) {
            d.p1 = p[i];
            d.p2 = p[i + 1];
            lv_draw_line(l, &d);
        }
    }
}

static void rita_linje(lv_layer_t *l, lv_point_precise_t *p, int32_t antal, int32_t bredd, lv_color_t farg)
{
    rita_linje_opa(l, p, antal, bredd, farg, LV_OPA_COVER);
}

static void rita_bage(lv_layer_t *l, float cx, float cy, float radie, float fran, float till, int32_t bredd, lv_color_t farg)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color       = farg;
    d.width       = bredd;
    d.center.x    = (int32_t)lroundf(cx);
    d.center.y    = (int32_t)lroundf(cy);
    d.radius      = (uint16_t)lroundf(radie);
    d.start_angle = (lv_value_precise_t)lroundf(fran);
    d.end_angle   = (lv_value_precise_t)lroundf(till);
    d.rounded     = 1;
    d.opa         = LV_OPA_COVER;
    lv_draw_arc(l, &d);
}

static void rita_hjarta(lv_layer_t *l, float cx, float cy, float w, float h)
{
    float r = w * 0.27f;
    lv_area_t a;
    area_satt(&a, cx - w * 0.5f, cy - h * 0.38f, cx - w * 0.5f + 2 * r, cy - h * 0.38f + 2 * r);
    rita_rekt(l, &a, farg(FARG_HJARTA), LV_OPA_COVER, LV_RADIUS_CIRCLE);
    area_satt(&a, cx + w * 0.5f - 2 * r, cy - h * 0.38f, cx + w * 0.5f, cy - h * 0.38f + 2 * r);
    rita_rekt(l, &a, farg(FARG_HJARTA), LV_OPA_COVER, LV_RADIUS_CIRCLE);
    rita_triangel(l, cx - w * 0.5f, cy - h * 0.38f + r * 1.02f,
                     cx + w * 0.5f, cy - h * 0.38f + r * 1.02f,
                     cx,            cy + h * 0.5f, farg(FARG_HJARTA));
}

static void rita_spiral(lv_layer_t *l, float cx, float cy, float w, float snurr)
{
    float r = w * 0.5f;
    int32_t b = (int32_t)(6 * SKALA);
    rita_bage(l, cx, cy, r,             snurr,        snurr + 290, b, farg(FARG_SPIRAL));
    rita_bage(l, cx, cy, r * 0.66f,     snurr + 120,  snurr + 400, b, farg(FARG_SPIRAL));
    rita_bage(l, cx, cy, r * 0.33f,     snurr + 240,  snurr + 480, b, farg(FARG_SPIRAL));
}

/*
 * Ett öga. hoger säger vilket, eftersom lockets lutning speglas: det inre
 * hörnet sitter till höger på vänster öga och till vänster på höger öga.
 */
static int signal_typ;
static int klar_logga;
static uttryck_t signal_bas;
static int32_t signal_kvar, signal_total;

/* Vektorsymboler som förblir läsbara på håll, oberoende av typsnitt. */
static void symbol(lv_layer_t *l, int typ, float x, float y, float r, lv_color_t f)
{
    if (typ == 1) { /* @: yttre båge, inre ring och krok */
        rita_bage(l, x, y, r, 35, 345, 5, f);
        rita_bage(l, x, y, r * .46f, 0, 360, 5, f);
        lv_point_precise_t p[] = {{x+r*.46f,y-r*.46f},{x+r*.46f,y+r*.36f},
            {x+r*.75f,y+r*.40f},{x+r*.96f,y+r*.12f}};
        rita_linje(l,p,4,5,f);
    } else if (typ == 2) { /* frågetecken */
        rita_bage(l, x, y-r*.35f, r*.56f, 180, 450, 6, f);
        lv_point_precise_t p[]={{x+r*.56f,y-r*.35f},{x,y+r*.22f},{x,y+r*.40f}};
        rita_linje(l,p,3,6,f);
        lv_area_t a; area_satt(&a,x-3,y+r*.75f-3,x+3,y+r*.75f+3);
        rita_rekt(l,&a,f,LV_OPA_COVER,LV_RADIUS_CIRCLE);
    } else if (typ == 3) { /* terminalens >_ */
        lv_point_precise_t p[]={{x-r*.8f,y-r*.5f},{x-r*.25f,y},{x-r*.8f,y+r*.5f}};
        rita_linje(l,p,3,5,f);
        lv_point_precise_t q[]={{x,y+r*.5f},{x+r*.7f,y+r*.5f}};
        rita_linje(l,q,2,5,f);
    } else if (typ == 4) { /* klocka */
        rita_bage(l,x,y,r,0,360,4,f);
        lv_point_precise_t p[]={{x,y-r*.62f},{x,y},{x+r*.46f,y+r*.2f}};
        rita_linje(l,p,3,4,f);
    } else if (typ == 6) { /* sms: mörk pratbubbla med tre ljusa prickar */
        lv_area_t a; area_satt(&a,x-r,y-r*.72f,x+r,y+r*.52f);
        rita_rekt(l,&a,f,LV_OPA_COVER,(int32_t)(r*.5f));
        lv_draw_triangle_dsc_t td; lv_draw_triangle_dsc_init(&td);
        td.color = f; td.opa = LV_OPA_COVER;
        td.p[0].x = x-r*.62f; td.p[0].y = y+r*.40f;
        td.p[1].x = x-r*.12f; td.p[1].y = y+r*.40f;
        td.p[2].x = x-r*.78f; td.p[2].y = y+r*.95f;
        lv_draw_triangle(l,&td);
        float pr = fmaxf(3, r*.13f);
        for (int i = -1; i <= 1; i++) {
            lv_area_t pa; area_satt(&pa,x+i*r*.45f-pr,y-r*.10f-pr,x+i*r*.45f+pr,y-r*.10f+pr);
            rita_rekt(l,&pa,farg(FARG_OGA),LV_OPA_COVER,LV_RADIUS_CIRCLE);
        }
    } else if (typ == 7) { /* Teams: ett kraftigt T */
        lv_point_precise_t p[]={{x-r*.72f,y-r*.62f},{x+r*.72f,y-r*.62f}};
        lv_point_precise_t q[]={{x,y-r*.62f},{x,y+r*.78f}};
        rita_linje(l,p,2,7,f); rita_linje(l,q,2,7,f);
    } else { /* stjärna */
        lv_point_precise_t p[]={{x-r,y},{x+r,y}}, q[]={{x,y-r},{x,y+r}};
        rita_linje(l,p,2,3,f); rita_linje(l,q,2,3,f);
        lv_point_precise_t d[]={{x-r*.55f,y-r*.55f},{x+r*.55f,y+r*.55f}};
        lv_point_precise_t e[]={{x-r*.55f,y+r*.55f},{x+r*.55f,y-r*.55f}};
        rita_linje(l,d,2,2,f); rita_linje(l,e,2,2,f);
    }
}

/* Samma logga i simulator och på kortet; ögonlocken ritas ovanpå. */
static void rita_agentlogga(lv_layer_t *l, int typ, float x, float y, float r)
{
#ifndef HAR_AGENTLOGGOR
    /* Utan loggorna: en stjärna för Claude, terminalens >_ för Codex. */
    symbol(l, typ == 2 ? 5 : 3, x, y, r, FARG_PUPILL);
    return;
#else
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = typ == 2 ? &logga_claude : &logga_codex;
    d.recolor = FARG_PUPILL;
    d.recolor_opa = LV_OPA_COVER;
    (void)r; /* Inbyggd storlek ger skarpa kanter utan omskalning. */
    lv_area_t a;
    a.x1 = (int32_t)lroundf(x) - 32; a.y1 = (int32_t)lroundf(y) - 32;
    a.x2 = a.x1 + 63; a.y2 = a.y1 + 63;
    lv_draw_image(l, &d, &a);
#endif
}

static float pup_x, pup_y;   /* pupillernas riktning, -1..1, sätts i rita() */

static void rita_oga(lv_layer_t *l, const oga_t *o, float cx, float cy, bool hoger, float oppen_extra, float snurr)
{
    float w = o->w;
    float h = o->h * begransa(o->oppen * oppen_extra, 0.05f, 1);

    if (o->form > 2.5f) {
        /* Sovande: en mjuk våg där ögat var, som i förlagan. */
        lv_point_precise_t p[15];
        for (int i = 0; i < 15; i++) {
            float t = (float)i / 14.0f;
            p[i].x = (lv_value_precise_t)lroundf(cx - w * 0.42f + w * 0.84f * t);
            p[i].y = (lv_value_precise_t)lroundf(cy + sinf(t * 2 * (float)M_PI) * o->h * 0.07f);
        }
        rita_linje(l, p, 15, (int32_t)(8 * SKALA), farg(FARG_OGA));
        return;
    }
    if (o->form > 1.5f) { rita_spiral(l, cx, cy, w, snurr); return; }
    if (o->form > 0.5f) { rita_hjarta(l, cx, cy, w, h);     return; }

    lv_area_t a;
    area_satt(&a, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
    int32_t r = (int32_t)(o->r * fminf(w, h) / 2);

    rita_rekt(l, &a, farg(FARG_OGA), LV_OPA_COVER, r);

    /*
     * Pupillen: en mörk cirkel som följer blicken med större utslag än
     * ögat, plus sina egna små hopp. Ritas före locken så att en blinkning
     * täcker den. En liten glimt uppe till vänster ger den liv.
     */
    /* Under en glad båge finns inget öga att se pupillen i, så den krymper bort. */
    float pupill = o->pupill * (1 - begransa((o->glad - 0.2f) / 0.3f, 0, 1));
    /* Signaltyp → symbol i ögat: mejl @, möte och påminnelse klocka, sms bubbla, Teams T. */
    int sym = signal_typ == 1 ? 1 : signal_typ == 4 || signal_typ == 9 ? 4
            : signal_typ == 7 ? 6 : signal_typ == 8 ? 7 : 0;
    bool tecken = signal_kvar > 0 && !sover &&
        (sym || signal_typ == 2 || signal_typ == 3 || (signal_typ == 5 && klar_logga && !hoger));
    if (tecken && h > 30) {
        if (sym) symbol(l, sym, cx, cy, fminf(w*.32f,h*.35f), FARG_PUPILL);
        else rita_agentlogga(l, signal_typ == 5 ? klar_logga : signal_typ, cx, cy, fminf(w*.36f,h*.36f));
    }
    if (!tecken && pupill > 0.02f) {
        float rp = pupill * w * 0.5f;
        float h_full = o->h;   /* pupillen rör sig i det öppna ögat, inte i det blinkande */
        float pr = fminf(rp, h * 0.48f);
        float px = cx + (pup_x) * (w * 0.5f - rp) * 0.9f;
        float py = cy + (pup_y) * (h_full * 0.5f - rp) * 0.8f;
        if (py - pr < cy - h / 2) py = cy - h / 2 + pr;
        if (py + pr > cy + h / 2) py = cy + h / 2 - pr;
        /* Irisen: en grön ring runt pupillen, sedan pupillen själv. */
        float ir = fminf(pr * 1.18f, h * 0.46f);
        px = begransa(px, cx - w/2 + ir + 2, cx + w/2 - ir - 2);
        py = begransa(py, cy - h/2 + ir + 1, cy + h/2 - ir - 1);
        lv_area_t ia;
        area_satt(&ia, px - ir, py - ir, px + ir, py + ir);
        rita_rekt(l, &ia, farg(FARG_IRIS), LV_OPA_COVER, LV_RADIUS_CIRCLE);
        lv_area_t pa;
        area_satt(&pa, px - pr, py - pr, px + pr, py + pr);
        rita_rekt(l, &pa, FARG_PUPILL, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        float gr = fmaxf(2.0f, pr * 0.20f);
        lv_area_t ga;
        area_satt(&ga, px - pr * 0.42f - gr, py - pr * 0.42f - gr, px - pr * 0.42f + gr, py - pr * 0.42f + gr);
        rita_rekt(l, &ga, FARG_GLIMT, LV_OPA_80, LV_RADIUS_CIRCLE);
        float liten = fmaxf(1.0f, pr * 0.08f);
        area_satt(&ga, px + pr*.40f - liten, py + pr*.38f - liten,
                       px + pr*.40f + liten, py + pr*.38f + liten);
        rita_rekt(l, &ga, FARG_GLIMT, LV_OPA_60, LV_RADIUS_CIRCLE);
    }

    /*
     * Det som täcker ögat ritas i bakgrundsfärgen och får bara verka inom
     * ögats egen ruta, annars skulle en glad båge kunna bita i munnen.
     */
    lv_area_t klipp_forr = l->_clip_area;
    lv_area_t ruta = { a.x1 - 8, a.y1 - 8, a.x2 + 8, a.y2 + 8 };
    lv_area_t klipp;
    if (area_snitt(&klipp, &klipp_forr, &ruta)) {
        l->_clip_area = klipp;

        /* Det övre locket: en fyrkant med lutande underkant. */
        if (o->lock > 0.01f || fabsf(o->lutning) > 0.01f) {
            float y_ut = begransa(a.y1 + h * (o->lock - o->lutning * 0.45f), a.y1 - 4, a.y2 + 1);
            float y_in = begransa(a.y1 + h * (o->lock + o->lutning * 0.45f), a.y1 - 4, a.y2 + 1);
            float xl = a.x1 - 6, xr = a.x2 + 6;
            float yl = hoger ? y_in : y_ut;
            float yr = hoger ? y_ut : y_in;
            float topp = a.y1 - 8;
            rita_triangel(l, xl, topp, xr, topp, xr, yr, FARG_BAKGRUND);
            rita_triangel(l, xl, topp, xr, yr, xl, yl, FARG_BAKGRUND);
        }

        /*
         * Den glada bågen: en kopia av ögat självt, nedskjuten. Kvar blir
         * ögats övre kant som en båge med jämn tjocklek, ett ^ som ler.
         */
        if (o->glad > 0.01f) {
            float d = h * (1 - 0.72f * o->glad);
            lv_area_t c;
            area_satt(&c, a.x1, a.y1 + d, a.x2, a.y2 + d + 8);
            rita_rekt(l, &c, FARG_BAKGRUND, LV_OPA_COVER, r);
        }

        /* Den raka botten: ett D. */
        if (o->botten > 0.01f) {
            lv_area_t b;
            area_satt(&b, a.x1 - 6, a.y2 - o->botten * h, a.x2 + 6, a.y2 + 8);
            rita_rekt(l, &b, FARG_BAKGRUND, LV_OPA_COVER, 0);
        }

        l->_clip_area = klipp_forr;
    }

    /*
     * Ögonbrynet: ett kort, tjockt streck ovanför ögat. Höjden räknas på
     * ögats fulla höjd, inte den blinkande, så brynet står stilla när ögat
     * blinkar. Inre änden är den mot näsan; sänkt blir det argt, höjt ledset.
     */
    if (o->bryn > 0.02f) {
        float topp = cy - o->h / 2;
        float by = topp - o->h * (0.10f + 0.32f * o->bryn_hojd);
        float lut = o->bryn_lut * w * 0.16f;
        float y_in = by + lut, y_ut = by - lut;
        float xl = cx - w * 0.42f, xr = cx + w * 0.42f;
        lv_point_precise_t p[2] = {
            { (lv_value_precise_t)lroundf(xl), (lv_value_precise_t)lroundf(hoger ? y_in : y_ut) },
            { (lv_value_precise_t)lroundf(xr), (lv_value_precise_t)lroundf(hoger ? y_ut : y_in) },
        };
        rita_linje_opa(l, p, 2, (int32_t)(8 * SKALA), farg(FARG_BRYN), (lv_opa_t)(o->bryn * 255));
    }
}

static void rita_mun(lv_layer_t *l, const mun_t *m, float cx, float cy)
{
    float w = m->w, h = m->h;
    if (w < 2) return;
    int32_t tjock = (int32_t)(9 * SKALA);

    if (h > 5) {
        lv_area_t a;
        if (m->platt > 0.5f) {
            /* Ett skratt: en rundad form vars övre halva täcks, kvar blir ett D. */
            area_satt(&a, cx - w / 2, cy - h * 1.5f, cx + w / 2, cy + h / 2);
            rita_rekt(l, &a, farg(FARG_MUN), LV_OPA_COVER, LV_RADIUS_CIRCLE);
            lv_area_t b;
            area_satt(&b, cx - w / 2 - 3, cy - h * 1.5f - 3, cx + w / 2 + 3, cy - h / 2);
            rita_rekt(l, &b, FARG_BAKGRUND, LV_OPA_COVER, 0);
        } else {
            area_satt(&a, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
            rita_rekt(l, &a, farg(FARG_MUN), LV_OPA_COVER, LV_RADIUS_CIRCLE);
        }
        return;
    }

    if (m->vag > 0.05f) {
        lv_point_precise_t p[7];
        float amp = m->vag * 5.5f * SKALA;
        for (int i = 0; i < 7; i++) {
            p[i].x = (lv_value_precise_t)lroundf(cx - w / 2 + w * i / 6.0f);
            p[i].y = (lv_value_precise_t)lroundf(cy + ((i & 1) ? amp : -amp) - m->kurva * 4);
        }
        rita_linje(l, p, 7, tjock - 1, farg(FARG_MUN));
        return;
    }

    if (fabsf(m->kurva) < 0.05f) {
        lv_point_precise_t p[2] = {
            { (lv_value_precise_t)lroundf(cx - w / 2), (lv_value_precise_t)lroundf(cy) },
            { (lv_value_precise_t)lroundf(cx + w / 2), (lv_value_precise_t)lroundf(cy) },
        };
        rita_linje(l, p, 2, tjock, farg(FARG_MUN));
        return;
    }

    /* En båge genom ändpunkterna och en punkt s ovanför eller nedanför mitten. */
    float s = fabsf(m->kurva) * w * 0.42f;
    float R = (s * s + (w / 2) * (w / 2)) / (2 * s);
    float grader = asinf(begransa((w / 2) / R, 0, 1)) * 180.0f / (float)M_PI;
    if (m->kurva > 0) {
        rita_bage(l, cx, cy + s / 2 - R, R + tjock / 2.0f, 90 - grader, 90 + grader, tjock, farg(FARG_MUN));
    } else {
        rita_bage(l, cx, cy - s / 2 + R, R + tjock / 2.0f, 270 - grader, 270 + grader, tjock, farg(FARG_MUN));
    }
}

/* Var ögonen och munnen hamnar just nu, med blick, darr och andning inräknat. */
static void lagen(float *ovx, float *ovy, float *ohx, float *ohy, float *mx, float *my, float *andas)
{
    float bx = begransa(nu.blick_x + blick_x, -1.2f, 1.2f);
    float by = begransa(nu.blick_y + blick_y, -1.2f, 1.2f);
    float dx = darr_x * (2 + nu.darr * 6);
    float dy = darr_y * (2 + nu.darr * 6);
    float and = sinf(andning_fas) * andning_djup;
    *andas = and;

    float fx = (bx * 16 + dx) * SKALA;
    float fy = (by * 11 + dy - and * 2) * SKALA;
    *ovx = OGA_CX_V + fx;
    *ovy = OGA_CY + fy;
    *ohx = OGA_CX_H + fx;
    *ohy = OGA_CY + fy;
    *mx  = MUN_CX + (bx * 6 + dx * 0.5f) * SKALA;
    *my  = MUN_CY + (by * 4 + dy * 0.5f - and * 1.5f + nu.mun.y) * SKALA;
}

/* Rutan som allt ryms i. Används för att bara rita om det som rört sig. */
static void rutan(lv_area_t *ut)
{
    float ovx, ovy, ohx, ohy, mx, my, and;
    lagen(&ovx, &ovy, &ohx, &ohy, &mx, &my, &and);

    if (drom_kvar_ms > 0 || drom_nyss || start_kvar_ms > 0) {
        ut->x1 = 0; ut->y1 = 0; ut->x2 = ANSIKTE_BREDD - 1; ut->y2 = ANSIKTE_HOJD - 1;
        drom_nyss = false;
        return;
    }

    lv_area_t a;
    float m = 14;
    float vw = nu.v.w * SKALA, vh = nu.v.h * SKALA, hw = nu.h.w * SKALA, hh = nu.h.h * SKALA;
    float mw = nu.mun.w * SKALA, mhj = nu.mun.h * SKALA;
    float bv = nu.v.bryn > 0.01f ? vh * 0.6f : 0, bh = nu.h.bryn > 0.01f ? hh * 0.6f : 0;
    area_satt(ut, ovx - vw / 2 - m, ovy - vh / 2 - m - bv, ovx + vw / 2 + m, ovy + vh / 2 + m);
    area_satt(&a, ohx - hw / 2 - m, ohy - hh / 2 - m - bh, ohx + hw / 2 + m, ohy + hh / 2 + m);
    area_utvidga(ut, &a);
    if (nu.rodnad > 0.01f) { ut->y2 += (int32_t)(vh * 0.3f); }
    /* Klar-skuttet lyfter ansiktet den första stunden. Stjärnorna och hörnet
     * ritas om som egna små rutor i tick(), se ovanfor_ansiktet(). */
    if (signal_kvar > 0 && signal_typ == 5 && signal_total - signal_kvar < 800) ut->y1 -= 20;
    if (leka_kvar_ms > 0) {
        area_satt(&a, fluga_x - 18, fluga_y - 16, fluga_x + 18, fluga_y + 14);
        area_utvidga(ut, &a);
    }
    float mh = fmaxf(mhj * 1.6f, mw * 0.5f) + m;
    area_satt(&a, mx - mw / 2 - m, my - mh, mx + mw / 2 + m, my + mh);
    area_utvidga(ut, &a);
}

/*
 * Hörnet uppe till höger (ikonen, möteklockan) ritas om som en egen liten
 * ruta. Slås det ihop med ansiktets ruta blir föreningen nästan hela skärmen
 * varje bild, och LVGL-tråden svälter CPU 0 så att vakthunden larmar.
 */
static const lv_area_t HORNET = { ANSIKTE_BREDD - 100, 0, ANSIKTE_BREDD - 1, 80 };
static bool hornet_synligt(void)
{
    return ikon_kvar_ms > 0 || (signal_kvar > 0 && signal_typ == 4);
}
static bool stjarnor_synliga(void) { return signal_kvar > 0 && signal_typ == 5; }
static bool forra_stjarnor;
/* Hörnet och klar-stjärnorna, var för sig, ett varv till efteråt så att tomrummet ritas. */
static void ovanfor_ansiktet(void)
{
    bool hornet = hornet_synligt(), stj = stjarnor_synliga();
    if (hornet || forra_hornet) { lv_area_t h = HORNET; lv_obj_invalidate_area(yta, &h); }
    if (stj || forra_stjarnor) {
        /* En enda låg remsa: varje ruta kostar ett helt varv genom rita(). */
        lv_area_t a; area_satt(&a, 48 - 9, 38 - 20, 48 + 4 * 67 + 9, 38 + 20);  /* samma lägen som i rita() */
        lv_obj_invalidate_area(yta, &a);
    }
    forra_hornet = hornet; forra_stjarnor = stj;
}

/* Ett enkelt, snabbt slumptal ur ett frö. Samma frö ger samma brus. */
static uint32_t brus(uint32_t *fro)
{
    *fro = *fro * 1664525u + 1013904223u;
    return *fro >> 8;
}

static void rita_drom(lv_layer_t *l)
{
    static const uint32_t TONER[] = { 0x1A1A1A, 0x3A3A3A, 0x6A6A6A, 0xA8A8A8, 0xE0E0E0,
                                      0x3B7257, 0xF29AA6, 0xB79CFF, 0x5A6FA8 };
    const int cell = 16;
    uint32_t fro = drom_fro;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_COVER;
    for (int y = 0; y < ANSIKTE_HOJD; y += cell) {
        for (int x = 0; x < ANSIKTE_BREDD; x += cell) {
            uint32_t r = brus(&fro);
            int i = (r & 0x1F) < 24 ? (int)(r % 5) : 5 + (int)((r >> 5) % 4);
            d.bg_color = lv_color_hex(TONER[i]);
            lv_area_t a = { x, y, x + cell - 1, y + cell - 1 };
            lv_draw_rect(l, &d, &a);
        }
    }
}

static void rita(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    float ovx, ovy, ohx, ohy, mx, my, and;
    lagen(&ovx, &ovy, &ohx, &ohy, &mx, &my, &and);

    if (drom_kvar_ms > 0) { rita_drom(l); return; }
    if (start_kvar_ms > 1100) return;   /* bara texten syns i början av starten */

    /* Andningen gör ögonen aningen högre när den andas in. */
    float andas_extra = 1 + and * 0.02f;
    float snurr = (float)((lv_tick_get() / 12) % 360);

    float scen_tid = (float)(signal_total - signal_kvar) / 1000.0f;
    if (signal_kvar > 0 && signal_typ == 5) {
        float hopp = scen_tid < .7f ? sinf(scen_tid / .7f * (float)M_PI) * 16 : 0;
        ovy -= hopp; ohy -= hopp; my -= hopp;
        for (int i=0; i<5; i++) {
            float fas = scen_tid*2 + i;
            symbol(l,5,48+i*67,38+sinf(fas)*10,4+2*sinf(fas),farg(FARG_OGA));
        }
    }
    if (signal_kvar > 0 && signal_typ == 4) {
        float y = 35 + (scen_tid < .6f ? sinf(scen_tid*10)*8 : 0);
        symbol(l,4,ANSIKTE_BREDD-62,y,18,farg(FARG_OGA));
    }
    oga_t v = nu.v, h = nu.h;
    mun_t mun = nu.mun;
    if (signal_kvar > 0 && signal_typ == 5 && klar_logga) {
        v.glad = 0; v.botten = 0; v.lock = 0; v.oppen = 1; v.h = 84;
        my += 18 * SKALA;
    }
    v.w *= SKALA; v.h *= SKALA;
    h.w *= SKALA; h.h *= SKALA;
    mun.w *= SKALA; mun.h *= SKALA;

    pup_x = begransa(nu.blick_x + blick_x + sackad_x, -1, 1);
    pup_y = begransa(nu.blick_y + blick_y + sackad_y, -1, 1);
    rita_oga(l, &v, ovx, ovy, false, blink_oppen * andas_extra, snurr);
    rita_oga(l, &h, ohx, ohy, true,  blink_oppen * andas_extra, -snurr);

    /* Rodnaden: två mjuka fläckar snett under ögonen. */
    if (nu.rodnad > 0.02f) {
        float rw = v.w * 0.55f, rh = v.h * 0.22f;
        lv_area_t ra;
        area_satt(&ra, ovx - rw / 2 - v.w * 0.12f, ovy + v.h * 0.5f + 6, ovx + rw / 2 - v.w * 0.12f, ovy + v.h * 0.5f + 6 + rh);
        rita_rekt(l, &ra, FARG_RODNAD, (lv_opa_t)(nu.rodnad * 200), LV_RADIUS_CIRCLE);
        area_satt(&ra, ohx - rw / 2 + h.w * 0.12f, ohy + h.h * 0.5f + 6, ohx + rw / 2 + h.w * 0.12f, ohy + h.h * 0.5f + 6 + rh);
        rita_rekt(l, &ra, FARG_RODNAD, (lv_opa_t)(nu.rodnad * 200), LV_RADIUS_CIRCLE);
    }
    rita_mun(l, &mun, mx, my);

    /* Ikonen: studsar in uppifrån, står, tonar bort. */
    if (ikon_kvar_ms > 0) {
        float gatt = (float)(ikon_total_ms - ikon_kvar_ms);
        float in = begransa(gatt / 320.0f, 0, 1);
        float studs = in < 1 ? (1 - in) * (1 - in) * 60 - sinf(in * (float)M_PI) * 10 : 0;
        lv_opa_t opa = (lv_opa_t)(255 * begransa((float)ikon_kvar_ms / 500.0f, 0, 1));
        float ix = ANSIKTE_BREDD - 64, iy = 46 - studs;   /* mitten av ikonen */
        float w = 58, hh = 42;
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = ikon_farg; d.bg_opa = opa; d.radius = 8;
        lv_area_t a;
        if (ikon_typ == IKON_KUVERT) {
            area_satt(&a, ix - w / 2, iy - hh / 2, ix + w / 2, iy + hh / 2);
            lv_draw_rect(l, &d, &a);
            /* Fliken: två mörka streck från hörnen ned till mitten. */
            lv_draw_line_dsc_t ld;
            lv_draw_line_dsc_init(&ld);
            ld.color = FARG_PUPILL; ld.width = 4; ld.opa = opa; ld.round_start = 1; ld.round_end = 1;
            ld.p1.x = (lv_value_precise_t)lroundf(ix - w / 2 + 4); ld.p1.y = (lv_value_precise_t)lroundf(iy - hh / 2 + 4);
            ld.p2.x = (lv_value_precise_t)lroundf(ix);             ld.p2.y = (lv_value_precise_t)lroundf(iy + 4);
            lv_draw_line(l, &ld);
            ld.p1.x = (lv_value_precise_t)lroundf(ix + w / 2 - 4);
            lv_draw_line(l, &ld);
        } else {
            /* Pratbubbla: rundad ruta med en liten spets nere till vänster. */
            d.radius = 14;
            area_satt(&a, ix - w / 2, iy - hh / 2, ix + w / 2, iy + hh / 2 - 4);
            lv_draw_rect(l, &d, &a);
            lv_draw_triangle_dsc_t td;
            lv_draw_triangle_dsc_init(&td);
            td.color = ikon_farg; td.opa = opa;
            td.p[0].x = (lv_value_precise_t)lroundf(ix - w / 2 + 10); td.p[0].y = (lv_value_precise_t)lroundf(iy + hh / 2 - 8);
            td.p[1].x = (lv_value_precise_t)lroundf(ix - w / 2 + 24); td.p[1].y = (lv_value_precise_t)lroundf(iy + hh / 2 - 8);
            td.p[2].x = (lv_value_precise_t)lroundf(ix - w / 2 + 8);  td.p[2].y = (lv_value_precise_t)lroundf(iy + hh / 2 + 6);
            lv_draw_triangle(l, &td);
            lv_draw_line_dsc_t ld;
            lv_draw_line_dsc_init(&ld);
            ld.color = FARG_PUPILL; ld.opa = opa; ld.round_start = 1; ld.round_end = 1;
            if (ikon_typ == IKON_TEAMS) {
                /* Ett T. */
                ld.width = 5;
                ld.p1.x = (lv_value_precise_t)lroundf(ix - 11); ld.p1.y = (lv_value_precise_t)lroundf(iy - 11);
                ld.p2.x = (lv_value_precise_t)lroundf(ix + 11); ld.p2.y = ld.p1.y;
                lv_draw_line(l, &ld);
                ld.p1.x = (lv_value_precise_t)lroundf(ix); ld.p2.x = ld.p1.x; ld.p2.y = (lv_value_precise_t)lroundf(iy + 10);
                lv_draw_line(l, &ld);
            } else {
                /* Tre prickar. */
                for (int i = -1; i <= 1; i++) {
                    lv_area_t pa;
                    area_satt(&pa, ix + i * 13 - 3, iy - 5, ix + i * 13 + 3, iy + 1);
                    lv_draw_rect_dsc_t pd;
                    lv_draw_rect_dsc_init(&pd);
                    pd.bg_color = FARG_PUPILL; pd.bg_opa = opa; pd.radius = LV_RADIUS_CIRCLE;
                    lv_draw_rect(l, &pd, &pa);
                }
            }
        }
    }

    /* Flugan: en liten prick med två vingstreck. */
    if (leka_kvar_ms > 0) {
        lv_area_t fa;
        area_satt(&fa, fluga_x - 7, fluga_y - 6, fluga_x + 7, fluga_y + 6);
        rita_rekt(l, &fa, FARG_PUPILL, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        area_satt(&fa, fluga_x - 5, fluga_y - 4, fluga_x + 5, fluga_y + 4);
        rita_rekt(l, &fa, farg(FARG_OGA), LV_OPA_COVER, LV_RADIUS_CIRCLE);
        float vt = (float)((lv_tick_get() / 40) % 2) * 4 - 2;
        lv_point_precise_t p[2] = {
            { (lv_value_precise_t)lroundf(fluga_x - 12), (lv_value_precise_t)lroundf(fluga_y - 8 + vt) },
            { (lv_value_precise_t)lroundf(fluga_x + 12), (lv_value_precise_t)lroundf(fluga_y - 8 - vt) },
        };
        rita_linje(l, p, 2, 3, farg(FARG_BRYN));
    }
}

/* ---- Livet ------------------------------------------------------------- */

static void planera_blinkning(void)
{
    blink_lage    = BLINK_VILA;
    blink_kvar_ms = blink_dubbel ? (int32_t)slump(120, 200) : (int32_t)slump(2200, 6800);
    blink_dubbel  = !blink_dubbel && slump(0, 1) < 0.22f;
}

static void planera_blick(void)
{
    if (slump(0, 1) < 0.45f) {
        blick_mal_x = 0;
        blick_mal_y = 0;
    } else {
        blick_mal_x = slump(-0.65f, 0.65f);
        blick_mal_y = slump(-0.4f, 0.35f);
    }
    blick_kvar_ms = (int32_t)slump(1800, 7500);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    uint32_t tick_nu = lv_tick_get();
    int32_t dt = (int32_t)(tick_nu - forra_tick_ms);
    forra_tick_ms = tick_nu;
    if (dt < 1) dt = 1;
    if (dt > 100) dt = 100;
    float steg = (float)dt / TICK_MS;
    if (signal_kvar > 0) {
        int32_t fore = signal_kvar;
        signal_kvar -= dt;
        /* Överraskningen är en kort upptakt, sedan kommer det vänliga leendet. */
        if (signal_typ == 1 && signal_total - fore < 450 && signal_total - signal_kvar >= 450) {
            mal.mun.h = 0; mal.mun.w = 32; mal.mun.kurva = .6f;
        }
        if (signal_typ == 1 && fore > 1200 && signal_kvar <= 1200) {
            /* @ släpper och ansiktet ler före återgången. */
            signal_typ = 6;
            ansikte_tillfalligt(UTTRYCK_GLAD, signal_kvar > 0 ? signal_kvar : 1);
        }
        if (signal_kvar <= 0) signal_typ = 0;
    }

    /* 1. Uttrycket glider mot sitt mål, kvickt strax efter ett byte. */
    if (snabb_kvar_ms > 0) snabb_kvar_ms -= dt;
    float k = begransa((snabb_kvar_ms > 0 ? 0.34f : 0.16f) * steg, 0, 0.7f);
    if (rodnad_kvar_ms > 0) { rodnad_kvar_ms -= dt; mal.rodnad = 1; } else mal.rodnad = 0;
    float *a = (float *)&nu, *b = (float *)&mal;
    for (size_t i = 0; i < ANTAL_TAL; i++) a[i] += (b[i] - a[i]) * k;

    /* Det tillfälliga uttrycket släpper efter sin tid. */
    if (tillfalligt_kvar_ms > 0) {
        tillfalligt_kvar_ms -= dt;
        if (tillfalligt_kvar_ms <= 0) ansikte_satt_uttryck(aterga_till);
    }

    /* 2. Blinkningen: snabbt ned, lite långsammare upp. I sömnen ingen alls. */
    if (!sover) blink_kvar_ms -= dt;
    switch (blink_lage) {
    case BLINK_VILA:
        if (blink_kvar_ms <= 0) { blink_lage = BLINK_STANGER; blink_kvar_ms = 80; }
        break;
    case BLINK_STANGER:
        blink_oppen = 1 - mjuk(1 - (float)blink_kvar_ms / 80);
        if (blink_kvar_ms <= 0) { blink_lage = BLINK_OPPNAR; blink_kvar_ms = 130; blink_oppen = 0; }
        break;
    case BLINK_OPPNAR:
        blink_oppen = mjuk(1 - (float)blink_kvar_ms / 130);
        if (blink_kvar_ms <= 0) { blink_oppen = 1; planera_blinkning(); }
        break;
    }

    /* 3. Blicken vandrar, darrar och andas. */
    if (sover) { blick_mal_x = 0; blick_mal_y = 0; }
    else if (blick_lasta_ms > 0) blick_lasta_ms -= dt;
    else {
        blick_kvar_ms -= dt;
        if (blick_kvar_ms <= 0) planera_blick();
    }
    float kb = begransa(0.22f * steg, 0, 0.8f);
    blick_x += (blick_mal_x - blick_x) * kb;
    blick_y += (blick_mal_y - blick_y) * kb;

    /* Pupillernas små hopp: nytt mål då och då, dit på ett par bildrutor. */
    sackad_kvar_ms -= dt;
    if (sackad_kvar_ms <= 0) {
        sackad_mal_x = sover ? 0 : slump(-0.16f, 0.16f);
        sackad_mal_y = sover ? 0 : slump(-0.10f, 0.10f);
        sackad_kvar_ms = (int32_t)slump(400, 2600);
    }
    sackad_x += (sackad_mal_x - sackad_x) * begransa(0.55f * steg, 0, 0.9f);
    sackad_y += (sackad_mal_y - sackad_y) * begransa(0.55f * steg, 0, 0.9f);

    darr_x = darr_x * 0.82f + slump(-0.35f, 0.35f) * (0.4f + nu.darr);
    darr_y = darr_y * 0.82f + slump(-0.35f, 0.35f) * (0.4f + nu.darr);

    andning_fas += (float)dt * 2 * (float)M_PI / andning_period_ms;
    if (andning_fas > 2 * (float)M_PI) {
        andning_fas -= 2 * (float)M_PI;
        andning_period_ms = sover ? slump(5600, 7200) : slump(3600, 5200);
    }
    andning_djup += ((sover ? 2.4f : 1.0f) - andning_djup) * 0.02f * steg;

    /* Färgen glider mot sitt mål: cyan, orange, eller en tillfällig ton. */
    if (ton_kvar_ms > 0) ton_kvar_ms -= dt;
    /* Orange är en signal, inte ett tillstånd: den syns i tre sekunder och släpper sedan. */
    if (varm_auto_ms > 0) { varm_auto_ms -= dt; if (varm_auto_ms <= 0) varm_mal = 0; }
    {
        float mal_andel = (ton_kvar_ms > 0 || varm_mal > 0.5f) ? 1.0f : 0.0f;
        if (mal_andel > 0.5f) ton_aktiv = ton_kvar_ms > 0 ? ton_farg : FARG_ORANGE;
        if (fabsf(mal_andel - ton_andel) > 0.003f) {
            ton_andel += (mal_andel - ton_andel) * begransa(0.08f * steg, 0, 0.5f);
            if (fabsf(mal_andel - ton_andel) < 0.01f) ton_andel = mal_andel;
            if (replik != NULL) {
                /* Repliken följer med i en mattare variant av samma ton. */
                lv_obj_set_style_text_color(replik, lv_color_mix(farg(FARG_OGA), lv_color_hex(0x8A9AA0), 140), LV_PART_MAIN);
            }
            /* Ögon och mun ritas om ändå av steg 4 nedan; ingen helskärm behövs. */
        }
    }

    /* Repliken tonar in, står kvar och tonar ut. */
    if (replik != NULL) {
        float mal_opa = replik_kvar_ms > 0 ? 1.0f : 0.0f;
        if (replik_kvar_ms > 0) replik_kvar_ms -= dt;
        float forr = replik_opa;
        replik_opa += (mal_opa - replik_opa) * begransa(0.12f * steg, 0, 0.6f);
        if (replik_opa < 0.01f && mal_opa == 0) replik_opa = 0;
        if (fabsf(replik_opa - forr) > 0.002f) {
            lv_obj_set_style_text_opa(replik, (lv_opa_t)(replik_opa * 255), LV_PART_MAIN);
        }
    }

    /* Drömmen: bruset byter bild var 80:e millisekund. */
    if (drom_kvar_ms > 0) {
        drom_kvar_ms -= dt;
        drom_byt_ms -= dt;
        if (drom_byt_ms <= 0) { drom_fro = (uint32_t)lv_rand(1, 0x7FFFFFFF); drom_byt_ms = 80; }
        if (drom_kvar_ms <= 0) drom_nyss = true;
    }

    /* Startsekvensen: text, sedan block, sedan det vanliga ansiktet. */
    if (start_kvar_ms > 0) {
        int32_t forr = start_kvar_ms;
        start_kvar_ms -= dt;
        if (forr > 2400 && start_kvar_ms <= 2400) ansikte_sag("Power |", 600);
        if (forr > 1800 && start_kvar_ms <= 1800) ansikte_sag("Power up", 700);
        if (forr > 1100 && start_kvar_ms <= 1100) { mal = UTTRYCK[UTTRYCK_START]; nu = mal; nu.v.h = 4; nu.h.h = 4; snabb_kvar_ms = 600; }
        if (start_kvar_ms <= 0) { ansikte_satt_uttryck(start_aterga); ansikte_blinka(); }
    }

    if (ikon_kvar_ms > 0) ikon_kvar_ms -= dt;

    /* Flugan surrar omkring och blicken hänger med. */
    if (leka_kvar_ms > 0) {
        leka_kvar_ms -= dt;
        fluga_vx += slump(-1.2f, 1.2f) * steg;
        fluga_vy += slump(-1.2f, 1.2f) * steg;
        fluga_vx = begransa(fluga_vx * 0.94f, -5, 5);
        fluga_vy = begransa(fluga_vy * 0.94f, -5, 5);
        if (leka_kvar_ms < 900) { fluga_vx += 1.2f * steg; fluga_vy -= 0.8f * steg; }   /* flyger ut */
        fluga_x += fluga_vx * steg;
        fluga_y += fluga_vy * steg;
        if (leka_kvar_ms >= 900) {
            if (fluga_x < 30) { fluga_x = 30; fluga_vx = fabsf(fluga_vx); }
            if (fluga_x > ANSIKTE_BREDD - 30) { fluga_x = ANSIKTE_BREDD - 30; fluga_vx = -fabsf(fluga_vx); }
            if (fluga_y < 30) { fluga_y = 30; fluga_vy = fabsf(fluga_vy); }
            if (fluga_y > ANSIKTE_HOJD - 60) { fluga_y = ANSIKTE_HOJD - 60; fluga_vy = -fabsf(fluga_vy); }
        }
        blick_mal_x = begransa((fluga_x - ANSIKTE_BREDD / 2) / (ANSIKTE_BREDD / 2), -1, 1);
        blick_mal_y = begransa((fluga_y - OGA_CY) / (ANSIKTE_HOJD / 2), -1, 1);
        blick_lasta_ms = 300;
    }

    /* 4. Rita om bara det som rört sig: förra rutan och den nya. */
    lv_area_t ny;
    rutan(&ny);
    lv_area_t bada = forra_yta;
    area_utvidga(&bada, &ny);
    forra_yta = ny;
    lv_obj_invalidate_area(yta, &bada);
    ovanfor_ansiktet();
}

static void (*petning_krok)(void);

static int32_t svept_ms;

static void svep_handelse(lv_event_t *e)
{
    (void)e;
    svept_ms = (int32_t)lv_tick_get();
}

static void petad_handelse(lv_event_t *e)
{
    (void)e;
    /* Släppet efter ett svep är inget tryck. */
    if (svept_ms != 0 && (int32_t)lv_tick_get() - svept_ms < 400) return;
    if (petning_krok != NULL) petning_krok();
    else ansikte_petad();
}

void ansikte_vid_petning(void (*krok)(void))
{
    petning_krok = krok;
}

/* ---- Det som syns utåt ------------------------------------------------- */

void ansikte_bygg(void)
{
    lv_obj_t *skarm = lv_screen_active();
    lv_obj_set_style_bg_color(skarm, FARG_BAKGRUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(skarm, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(skarm, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(skarm, 0, LV_PART_MAIN);
    lv_obj_remove_flag(skarm, LV_OBJ_FLAG_SCROLLABLE);

    yta = lv_obj_create(skarm);
    lv_obj_set_size(yta, ANSIKTE_BREDD, ANSIKTE_HOJD);
    lv_obj_set_pos(yta, 0, 0);
    lv_obj_set_style_bg_color(yta, FARG_BAKGRUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(yta, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(yta, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(yta, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(yta, 0, LV_PART_MAIN);
    lv_obj_remove_flag(yta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(yta, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(yta, rita, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(yta, petad_handelse, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(yta, svep_handelse, LV_EVENT_GESTURE, NULL);
    lv_obj_add_flag(yta, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* Raden under ansiktet: två rader får plats mellan munnen och kanten. */
    replik = lv_label_create(skarm);
    lv_obj_set_style_text_font(replik, &lv_font_replik, LV_PART_MAIN);
    lv_obj_set_style_text_color(replik, FARG_REPLIK_K, LV_PART_MAIN);
    lv_obj_set_style_text_align(replik, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(replik, 2, LV_PART_MAIN);
    lv_obj_set_style_text_opa(replik, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_label_set_long_mode(replik, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(replik, ANSIKTE_BREDD - 2 * 18);
    lv_obj_align(replik, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(replik, "");

    ton_andel = 0;
    nu = mal = UTTRYCK[UTTRYCK_NEUTRAL];
    forra_yta.x1 = 1; forra_yta.x2 = 0;   /* tom */
    forra_tick_ms = lv_tick_get();
    planera_blinkning();
    planera_blick();
    andning_fas = slump(0, 6.28f);

    klocka = lv_timer_create(tick, TICK_MS, NULL);
}

void ansikte_satt_uttryck(uttryck_t u)
{
    if (u >= UTTRYCK_ANTAL) return;
    uttryck_nu = u;
    float r = mal.rodnad;
    mal = UTTRYCK[u];
    mal.rodnad = r;
    tillfalligt_kvar_ms = 0;
    snabb_kvar_ms = 380;
}

uttryck_t ansikte_uttryck(void)
{
    return uttryck_nu;
}

const char *uttryck_namn(uttryck_t u)
{
    return u < UTTRYCK_ANTAL ? NAMN[u] : "?";
}

void ansikte_synlig(bool synlig)
{
    if (yta == NULL) return;
    if (synlig) {
        lv_obj_remove_flag(yta, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(replik, LV_OBJ_FLAG_HIDDEN);
        forra_yta.x1 = 1; forra_yta.x2 = 0;
        lv_obj_invalidate(yta);
    } else {
        lv_obj_add_flag(yta, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(replik, LV_OBJ_FLAG_HIDDEN);
    }
}

void ansikte_varm(bool varm)
{
    varm_mal = varm ? 1.0f : 0.0f;
    varm_auto_ms = 0;
}

bool ansikte_ar_varm(void)
{
    return varm_mal > 0.5f;
}

void ansikte_ton(uint32_t hex, int32_t ms)
{
    ton_farg = lv_color_hex(hex);
    ton_kvar_ms = ms;
}

void ansikte_sag(const char *text, int32_t ms)
{
    if (replik == NULL) return;
    if (text == NULL || text[0] == '\0' || ms <= 0) { replik_kvar_ms = 0; return; }
    lv_label_set_text(replik, text);
    replik_kvar_ms = ms;
}

void ansikte_blinka(void)
{
    if (blink_lage == BLINK_VILA) blink_kvar_ms = 0;
}

void ansikte_tillfalligt(uttryck_t u, int32_t ms)
{
    if (tillfalligt_kvar_ms <= 0) aterga_till = uttryck_nu;
    ansikte_satt_uttryck(u);
    tillfalligt_kvar_ms = ms;
}

void ansikte_gaspa(void)
{
    ansikte_tillfalligt(UTTRYCK_GASPAR, 2400);
}

void ansikte_petad(void)
{
    ansikte_tillfalligt(UTTRYCK_FORVANAD, 1100);
    ansikte_blinka();
}

void ansikte_startsekvens(void)
{
    start_aterga = uttryck_nu;
    start_kvar_ms = 3000;
    ansikte_sag("Pd", 600);
}

void ansikte_dromma(int32_t ms)
{
    drom_kvar_ms = ms;
    drom_byt_ms = 0;
}

void ansikte_ikon(ansikte_ikon_t typ, uint32_t hex, int32_t ms)
{
    ikon_typ = (int)typ;
    ikon_farg = hex ? lv_color_hex(hex) : FARG_OGA;
    ikon_kvar_ms = ikon_total_ms = ms;
}

void ansikte_leka(int32_t ms)
{
    leka_kvar_ms = ms;
    fluga_x = slump(0, 1) < 0.5f ? 24 : ANSIKTE_BREDD - 24;
    fluga_y = slump(60, 200);
    fluga_vx = fluga_x < 100 ? 3 : -3;
    fluga_vy = 1;
}

void ansikte_rodna(int32_t ms)
{
    rodnad_kvar_ms = ms;
}

void ansikte_klappad(int antal)
{
    if (antal <= 1) {
        /* Första klappen: tittar upp mot handen med stora pupiller. */
        ansikte_tillfalligt(UTTRYCK_NYFIKEN, 1400);
        ansikte_titta(0, -0.9f);
    } else if (antal == 2) {
        ansikte_tillfalligt(UTTRYCK_GLAD, 1600);
        ansikte_titta(0, -0.7f);
    } else {
        /* Fler i rad: blundar nöjt och rodnar. */
        ansikte_tillfalligt(UTTRYCK_NOJD, 2600);
        ansikte_rodna(3200);
    }
}

void ansikte_sover(bool s)
{
    sover = s;
}

void ansikte_titta(float x, float y)
{
    blick_mal_x = begransa(x, -1, 1);
    blick_mal_y = begransa(y, -1, 1);
    blick_lasta_ms = 2200;
}

void ansikte_signal(int typ, int32_t ms)
{
    if (typ && !signal_typ) signal_bas = tillfalligt_kvar_ms > 0 ? aterga_till : uttryck_nu;
    klar_logga = 0;
    signal_typ = typ; signal_total = signal_kvar = ms;
    leka_kvar_ms = drom_kvar_ms = start_kvar_ms = ikon_kvar_ms = 0;
    if (!typ || ms <= 0) { signal_typ = 0; signal_kvar = 0; ansikte_satt_uttryck(sover ? UTTRYCK_SOVER : signal_bas); return; }
    ansikte_sover(false);
    ansikte_tillfalligt(typ == 5 ? UTTRYCK_VALDIGT_GLAD : UTTRYCK_FORVANAD, ms);
    if (typ != 5) {
        mal.v.bryn = mal.h.bryn = 0;
        mal.v.pupill = mal.h.pupill = .58f;
        mal.h.h = 90; /* en liten asymmetri ger ett mjukare uttryck */
        if (typ != 1) { mal.mun.h = 0; mal.mun.w = 32; mal.mun.kurva = .6f; }
    }
    ansikte_titta(0, 0);
    if (typ == 1) ansikte_ikon(IKON_KUVERT, 0, ms);
}
int ansikte_signal_typ(void) { return signal_typ; }

void ansikte_agentlogga(int aktor) { klar_logga = aktor == 2 || aktor == 3 ? aktor : 0; }
int ansikte_agentlogga_typ(void) { return signal_kvar > 0 ? (signal_typ == 5 ? klar_logga : (signal_typ == 2 || signal_typ == 3 ? signal_typ : 0)) : 0; }
