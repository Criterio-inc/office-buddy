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

/* ---- Färger ------------------------------------------------------------ */

#define FARG_BAKGRUND lv_color_hex(0x000000)
#define FARG_CYAN     lv_color_hex(ANSIKTE_FARG_GRUND)   /* grundfärgen i vila */
#define FARG_ORANGE   lv_color_hex(0xFFA042)   /* varm, när något behöver dig */
#define FARG_REPLIK_K lv_color_hex(0x9AB8BF)   /* dämpat, orden ska inte tävla med ögonen */

/*
 * Färgen just nu. Målet är cyan, orange när något behöver dig, eller en
 * tillfällig ton; färgen glider dit och ritas därefter. varm_nu används
 * bara för replikens matta variant.
 */
static lv_color_t farg_ljus = { 0 };
static lv_color_t farg_mal;
static float      varm_mal;
static lv_color_t ton_farg;
static int32_t    ton_kvar_ms;
#define FARG_LJUS     farg_ljus

static float begransa(float v, float lo, float hi);

static lv_color_t farg_att_sikta_pa(void)
{
    if (ton_kvar_ms > 0) return ton_farg;
    return varm_mal > 0.5f ? FARG_ORANGE : FARG_CYAN;
}

/* Blandar två färger med en andel 0..1. */
static lv_color_t blanda(lv_color_t fran, lv_color_t till, float andel)
{
    return lv_color_mix(till, fran, (lv_opa_t)(begransa(andel, 0, 1) * 255));
}

LV_FONT_DECLARE(lv_font_replik);

/* ---- Var saker sitter -------------------------------------------------- */

/*
 * Uttrycken är ritade i en mindre skala och förstoras här, så att talen i
 * tabellen är hanterliga. 1,4 fyller glaset utan att ögonen når kanterna.
 */
#define SKALA      1.4f

#define OGA_CX_V    92.0f
#define OGA_CX_H   276.0f
#define OGA_CY     174.0f
#define MUN_CX     184.0f
#define MUN_CY     344.0f

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
} param_t;

#define ANTAL_TAL (sizeof(param_t) / sizeof(float))

/* ---- Uttrycken --------------------------------------------------------- */

#define OGA_STD .w = 84, .h = 96, .r = 0.8f, .oppen = 1
#define OGA(...) { __VA_ARGS__ }
#define BADA(o)  .v = o, .h = o

static const param_t UTTRYCK[UTTRYCK_ANTAL] = {
    [UTTRYCK_NEUTRAL]      = { BADA(OGA(OGA_STD)),
                               .mun = { .w = 52, .kurva = 0.35f } },
    [UTTRYCK_GLAD]         = { BADA(OGA(OGA_STD, .botten = 0.32f)),
                               .mun = { .w = 64, .kurva = 0.9f } },
    [UTTRYCK_VALDIGT_GLAD] = { BADA(OGA(OGA_STD, .glad = 0.72f, .bryn = 1, .bryn_hojd = 0.45f)),
                               .mun = { .w = 84, .h = 42, .platt = 1 } },
    [UTTRYCK_FORVANAD]     = { BADA(OGA(.w = 84, .h = 88, .r = 1, .oppen = 1, .bryn = 1, .bryn_hojd = 0.6f)),
                               .mun = { .w = 24, .h = 32 }, .blick_y = -0.1f },
    [UTTRYCK_ENTUSIASTISK] = { BADA(OGA(OGA_STD, .glad = 0.55f, .botten = 0.1f, .bryn = 1, .bryn_hojd = 0.5f)),
                               .mun = { .w = 92, .h = 46, .platt = 1 } },
    [UTTRYCK_NOJD]         = { BADA(OGA(OGA_STD, .glad = 0.92f)),
                               .mun = { .w = 52, .kurva = 0.6f } },
    [UTTRYCK_BLINKNING]    = { .v = OGA(OGA_STD, .glad = 0.95f), .h = OGA(OGA_STD),
                               .mun = { .w = 48, .kurva = 0.7f } },
    [UTTRYCK_LEDSEN]       = { BADA(OGA(OGA_STD, .lock = 0.28f, .lutning = -0.7f, .bryn = 1, .bryn_hojd = 0.3f, .bryn_lut = -0.6f)),
                               .mun = { .w = 46, .kurva = -0.55f, .vag = 0.35f }, .blick_y = 0.25f },
    [UTTRYCK_BESVIKEN]     = { BADA(OGA(OGA_STD, .lock = 0.45f, .lutning = -0.35f, .bryn = 1, .bryn_hojd = 0.15f, .bryn_lut = -0.3f)),
                               .mun = { .w = 42, .kurva = -0.5f } },
    [UTTRYCK_OROLIG]       = { BADA(OGA(.w = 82, .h = 86, .r = 1, .oppen = 1, .lock = 0.18f, .lutning = -0.6f, .bryn = 1, .bryn_hojd = 0.4f, .bryn_lut = -0.7f)),
                               .mun = { .w = 30, .h = 14, .platt = 1 }, .darr = 0.3f },
    [UTTRYCK_ARG]          = { BADA(OGA(OGA_STD, .lock = 0.3f, .lutning = 0.85f, .bryn = 1, .bryn_hojd = 0.2f, .bryn_lut = 0.8f)),
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
    [UTTRYCK_STRESSAD]     = { BADA(OGA(.w = 66, .h = 66, .r = 1, .oppen = 1, .lock = 0.1f, .lutning = -0.3f, .bryn = 1, .bryn_hojd = 0.35f, .bryn_lut = -0.5f)),
                               .mun = { .w = 52, .vag = 1 }, .darr = 1 },
    [UTTRYCK_NYFIKEN]      = { .v = OGA(.w = 96, .h = 106, .r = 0.8f, .oppen = 1, .bryn = 1, .bryn_hojd = 0.55f),
                               .h = OGA(.w = 72, .h = 80, .r = 0.8f, .oppen = 1, .lock = 0.1f),
                               .mun = { .w = 30, .kurva = 0.45f }, .blick_x = 0.3f, .blick_y = -0.15f },
    [UTTRYCK_KAR]          = { BADA(OGA(.w = 92, .h = 88, .r = 0.5f, .oppen = 1, .form = 1)),
                               .mun = { .w = 56, .kurva = 0.8f } },
    [UTTRYCK_OVERVALDIGAD] = { BADA(OGA(.w = 84, .h = 84, .r = 1, .oppen = 1, .form = 2)),
                               .mun = { .w = 50, .vag = 0.8f }, .darr = 0.6f },
    [UTTRYCK_SOVER]        = { BADA(OGA(OGA_STD, .glad = 1)),
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

/* Andningen. */
static float   andning_fas;
static float   andning_period_ms = 4300;
static float   andning_djup = 1;       /* 1 vaken, större i sömnen */
static bool    sover;

static uint32_t forra_tick_ms;
static lv_area_t forra_yta;    /* det som ritades förra varvet, ska ritas om */

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
    rita_rekt(l, &a, FARG_LJUS, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    area_satt(&a, cx + w * 0.5f - 2 * r, cy - h * 0.38f, cx + w * 0.5f, cy - h * 0.38f + 2 * r);
    rita_rekt(l, &a, FARG_LJUS, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    rita_triangel(l, cx - w * 0.5f, cy - h * 0.38f + r * 1.02f,
                     cx + w * 0.5f, cy - h * 0.38f + r * 1.02f,
                     cx,            cy + h * 0.5f, FARG_LJUS);
}

static void rita_spiral(lv_layer_t *l, float cx, float cy, float w, float snurr)
{
    float r = w * 0.5f;
    int32_t b = (int32_t)(6 * SKALA);
    rita_bage(l, cx, cy, r,             snurr,        snurr + 290, b, FARG_LJUS);
    rita_bage(l, cx, cy, r * 0.66f,     snurr + 120,  snurr + 400, b, FARG_LJUS);
    rita_bage(l, cx, cy, r * 0.33f,     snurr + 240,  snurr + 480, b, FARG_LJUS);
}

/*
 * Ett öga. hoger säger vilket, eftersom lockets lutning speglas: det inre
 * hörnet sitter till höger på vänster öga och till vänster på höger öga.
 */
static void rita_oga(lv_layer_t *l, const oga_t *o, float cx, float cy, bool hoger, float oppen_extra, float snurr)
{
    float w = o->w;
    float h = o->h * begransa(o->oppen * oppen_extra, 0.05f, 1);

    if (o->form > 1.5f) { rita_spiral(l, cx, cy, w, snurr); return; }
    if (o->form > 0.5f) { rita_hjarta(l, cx, cy, w, h);     return; }

    lv_area_t a;
    area_satt(&a, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
    int32_t r = (int32_t)(o->r * fminf(w, h) / 2);

    rita_rekt(l, &a, FARG_LJUS, LV_OPA_COVER, r);

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
        rita_linje_opa(l, p, 2, (int32_t)(8 * SKALA), FARG_LJUS, (lv_opa_t)(o->bryn * 255));
    }
}

static void rita_mun(lv_layer_t *l, const mun_t *m, float cx, float cy)
{
    float w = m->w, h = m->h;
    int32_t tjock = (int32_t)(9 * SKALA);

    if (h > 5) {
        lv_area_t a;
        if (m->platt > 0.5f) {
            /* Ett skratt: en rundad form vars övre halva täcks, kvar blir ett D. */
            area_satt(&a, cx - w / 2, cy - h * 1.5f, cx + w / 2, cy + h / 2);
            rita_rekt(l, &a, FARG_LJUS, LV_OPA_COVER, LV_RADIUS_CIRCLE);
            lv_area_t b;
            area_satt(&b, cx - w / 2 - 3, cy - h * 1.5f - 3, cx + w / 2 + 3, cy - h / 2);
            rita_rekt(l, &b, FARG_BAKGRUND, LV_OPA_COVER, 0);
        } else {
            area_satt(&a, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
            rita_rekt(l, &a, FARG_LJUS, LV_OPA_COVER, LV_RADIUS_CIRCLE);
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
        rita_linje(l, p, 7, tjock - 1, FARG_LJUS);
        return;
    }

    if (fabsf(m->kurva) < 0.05f) {
        lv_point_precise_t p[2] = {
            { (lv_value_precise_t)lroundf(cx - w / 2), (lv_value_precise_t)lroundf(cy) },
            { (lv_value_precise_t)lroundf(cx + w / 2), (lv_value_precise_t)lroundf(cy) },
        };
        rita_linje(l, p, 2, tjock, FARG_LJUS);
        return;
    }

    /* En båge genom ändpunkterna och en punkt s ovanför eller nedanför mitten. */
    float s = fabsf(m->kurva) * w * 0.42f;
    float R = (s * s + (w / 2) * (w / 2)) / (2 * s);
    float grader = asinf(begransa((w / 2) / R, 0, 1)) * 180.0f / (float)M_PI;
    if (m->kurva > 0) {
        rita_bage(l, cx, cy + s / 2 - R, R + tjock / 2.0f, 90 - grader, 90 + grader, tjock, FARG_LJUS);
    } else {
        rita_bage(l, cx, cy - s / 2 + R, R + tjock / 2.0f, 270 - grader, 270 + grader, tjock, FARG_LJUS);
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

    lv_area_t a;
    float m = 14;
    float vw = nu.v.w * SKALA, vh = nu.v.h * SKALA, hw = nu.h.w * SKALA, hh = nu.h.h * SKALA;
    float mw = nu.mun.w * SKALA, mhj = nu.mun.h * SKALA;
    float bv = nu.v.bryn > 0.01f ? vh * 0.6f : 0, bh = nu.h.bryn > 0.01f ? hh * 0.6f : 0;
    area_satt(ut, ovx - vw / 2 - m, ovy - vh / 2 - m - bv, ovx + vw / 2 + m, ovy + vh / 2 + m);
    area_satt(&a, ohx - hw / 2 - m, ohy - hh / 2 - m - bh, ohx + hw / 2 + m, ohy + hh / 2 + m);
    area_utvidga(ut, &a);
    float mh = fmaxf(mhj * 1.6f, mw * 0.5f) + m;
    area_satt(&a, mx - mw / 2 - m, my - mh, mx + mw / 2 + m, my + mh);
    area_utvidga(ut, &a);
}

static void rita(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    float ovx, ovy, ohx, ohy, mx, my, and;
    lagen(&ovx, &ovy, &ohx, &ohy, &mx, &my, &and);

    /* Andningen gör ögonen aningen högre när den andas in. */
    float andas_extra = 1 + and * 0.02f;
    float snurr = (float)((lv_tick_get() / 12) % 360);

    oga_t v = nu.v, h = nu.h;
    mun_t mun = nu.mun;
    v.w *= SKALA; v.h *= SKALA;
    h.w *= SKALA; h.h *= SKALA;
    mun.w *= SKALA; mun.h *= SKALA;

    rita_oga(l, &v, ovx, ovy, false, blink_oppen * andas_extra, snurr);
    rita_oga(l, &h, ohx, ohy, true,  blink_oppen * andas_extra, -snurr);
    rita_mun(l, &mun, mx, my);
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

    /* 1. Uttrycket glider mot sitt mål. */
    float k = begransa(0.16f * steg, 0, 0.7f);
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
    farg_mal = farg_att_sikta_pa();
    if (farg_ljus.red != farg_mal.red || farg_ljus.green != farg_mal.green || farg_ljus.blue != farg_mal.blue) {
        farg_ljus = blanda(farg_ljus, farg_mal, 0.08f * steg);
        /* Sista biten tas i ett steg, annars fastnar avrundningen strax intill. */
        if (abs((int)farg_ljus.red - farg_mal.red) < 3 && abs((int)farg_ljus.green - farg_mal.green) < 3 &&
            abs((int)farg_ljus.blue - farg_mal.blue) < 3) farg_ljus = farg_mal;
        if (replik != NULL) {
            /* Repliken följer med i en mattare variant av samma färg. */
            lv_obj_set_style_text_color(replik, lv_color_mix(farg_ljus, lv_color_hex(0x8A9AA0), 140), LV_PART_MAIN);
        }
        lv_obj_invalidate(yta);
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

    /* 4. Rita om bara det som rört sig: förra rutan och den nya. */
    lv_area_t ny;
    rutan(&ny);
    lv_area_t bada = forra_yta;
    area_utvidga(&bada, &ny);
    forra_yta = ny;
    lv_obj_invalidate_area(yta, &bada);
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

    farg_ljus = FARG_CYAN;
    farg_mal  = FARG_CYAN;
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
    mal = UTTRYCK[u];
    tillfalligt_kvar_ms = 0;
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
