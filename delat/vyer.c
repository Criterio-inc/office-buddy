#include <stdio.h>
#include <string.h>

#include "ansikte.h"
#include "humor.h"
#include "vyer.h"

LV_FONT_DECLARE(lv_font_siffror);
LV_FONT_DECLARE(lv_font_datum);
LV_FONT_DECLARE(lv_font_replik);

#define FARG_TEXT   lv_color_hex(ANSIKTE_FARG_GRUND)
#define FARG_DAMPAD lv_color_hex(0x8FA08C)
#define FARG_SVAG   lv_color_hex(0x263329)
#define FARG_VARM   lv_color_hex(0xFFA042)

#define TIMER_STD_MIN   25
#define TIMER_STEG_MIN  5
#define TIMER_MAX_MIN   120

static bool (*tid_krok)(struct tm *);

static vy_t aktiv = VY_ANSIKTE;
static lv_obj_t *vy_klocka, *vy_timer;
static lv_obj_t *k_tid, *k_datum, *k_sekund;             /* klockan */
static lv_obj_t *t_tid, *t_hint, *t_stapel, *t_lage;     /* timern */
static lv_timer_t *puls;

/* Timern. Sekunder kvar räknas ned i puls(); kor säger om den går. */
static int  timer_satt_s = TIMER_STD_MIN * 60;
static int  timer_kvar_s = TIMER_STD_MIN * 60;
static bool timer_kor;
static bool timer_slut_larmar;
static int32_t gest_nyss_ms;   /* ett svep följs av ett släpp som inte får räknas som tryck */

static const char *const VECKODAG[] = { "söndag", "måndag", "tisdag", "onsdag", "torsdag", "fredag", "lördag" };
static const char *const MANAD[]    = { "januari", "februari", "mars", "april", "maj", "juni", "juli",
                                        "augusti", "september", "oktober", "november", "december" };

/* ---- Klockan ------------------------------------------------------------ */

static void klocka_uppdatera(void)
{
    struct tm t;
    if (tid_krok == NULL || !tid_krok(&t)) {
        lv_label_set_text(k_tid, "--:--");
        lv_label_set_text(k_datum, "klockan är inte ställd");
        lv_obj_set_width(k_sekund, 0);
        return;
    }
    char rad[64];
    snprintf(rad, sizeof(rad), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(k_tid, rad);
    snprintf(rad, sizeof(rad), "%s %d %s", VECKODAG[t.tm_wday % 7], t.tm_mday, MANAD[t.tm_mon % 12]);
    lv_label_set_text(k_datum, rad);
    /* Sekunderna som en tunn linje som växer över glaset, aldrig som siffror. */
    lv_obj_set_width(k_sekund, (ANSIKTE_BREDD - 2 * 24) * t.tm_sec / 59);
}

/* ---- Timern ------------------------------------------------------------- */

static void timer_uppdatera(void)
{
    char rad[32];
    int s = timer_kvar_s < 0 ? 0 : timer_kvar_s;
    snprintf(rad, sizeof(rad), "%02d:%02d", s / 60, s % 60);
    lv_label_set_text(t_tid, rad);
    lv_obj_set_style_text_color(t_tid, timer_kor ? FARG_TEXT : FARG_DAMPAD, LV_PART_MAIN);
    lv_obj_set_width(t_stapel, timer_satt_s > 0 ? (ANSIKTE_BREDD - 2 * 24) * s / timer_satt_s : 0);
    lv_label_set_text(t_lage, timer_kor ? "räknar ned" : (timer_kvar_s < timer_satt_s ? "paus" : ""));
    lv_label_set_text(t_hint, timer_kor ? "tryck: paus" : "upptill +5, nedtill -5\nmitten: starta");
}

static void timer_klar(void)
{
    timer_kor = false;
    timer_kvar_s = timer_satt_s;
    timer_slut_larmar = true;
    vyer_visa(VY_ANSIKTE);
    ansikte_varm(true);
    ansikte_sag("tiden är ute", 30 * 60 * 1000);
    ansikte_tillfalligt(UTTRYCK_ENTUSIASTISK, 3000);
    humor_ljud(LJUD_TRUDELUTT);
}

/* Ett tryck i timervyn: övre tredjedelen lägger till, nedre drar ifrån, mitten startar. */
static void timer_tryck(int32_t y)
{
    if (timer_kor) { timer_kor = false; timer_uppdatera(); return; }
    if (y < ANSIKTE_HOJD / 3) {
        timer_satt_s += TIMER_STEG_MIN * 60;
        if (timer_satt_s > TIMER_MAX_MIN * 60) timer_satt_s = TIMER_MAX_MIN * 60;
        timer_kvar_s = timer_satt_s;
    } else if (y > 2 * ANSIKTE_HOJD / 3) {
        timer_satt_s -= TIMER_STEG_MIN * 60;
        if (timer_satt_s < 60) timer_satt_s = 60;
        timer_kvar_s = timer_satt_s;
    } else {
        timer_kor = true;
    }
    timer_uppdatera();
}

/* ---- Pulsen: en gång i sekunden ---------------------------------------- */

static void puls_cb(lv_timer_t *t)
{
    (void)t;
    if (timer_kor) {
        if (--timer_kvar_s <= 0) timer_klar();
    }
    if (aktiv == VY_KLOCKA) klocka_uppdatera();
    if (aktiv == VY_TIMER)  timer_uppdatera();
}

/* ---- Svep och tryck ------------------------------------------------------ */

static void gest(lv_event_t *e)
{
    (void)e;
    lv_indev_t *don = lv_indev_active();
    if (don == NULL) return;
    lv_dir_t d = lv_indev_get_gesture_dir(don);
    gest_nyss_ms = (int32_t)lv_tick_get();
    if (d == LV_DIR_LEFT)       vyer_nasta();
    else if (d == LV_DIR_RIGHT) vyer_forra();
}

static bool nyss_svept(void)
{
    return (int32_t)lv_tick_get() - gest_nyss_ms < 400 && gest_nyss_ms != 0;
}

static void tryck_i_vy(lv_event_t *e)
{
    if (nyss_svept()) return;
    lv_indev_t *don = lv_indev_active();
    lv_point_t p = { 0, 0 };
    if (don != NULL) lv_indev_get_point(don, &p);
    if (lv_event_get_target(e) == vy_timer) timer_tryck(p.y);
    /* Ett tryck på klockan gör ingenting; den är till för att titta på. */
}

/* Ansiktets egna tryck går fortfarande till dess krok, men inte efter ett svep. */
static void tryck_pa_ansiktet(lv_event_t *e)
{
    (void)e;
    if (timer_slut_larmar) {
        timer_slut_larmar = false;
        ansikte_varm(false);
        ansikte_sag("", 0);
    }
}

/* ---- Bygge ---------------------------------------------------------------- */

static lv_obj_t *behallare(lv_obj_t *skarm)
{
    lv_obj_t *o = lv_obj_create(skarm);
    lv_obj_set_size(o, ANSIKTE_BREDD, ANSIKTE_HOJD);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_HIDDEN);
    return o;
}

static lv_obj_t *etikett(lv_obj_t *f, const lv_font_t *font, lv_color_t farg, lv_align_t al, int32_t y)
{
    lv_obj_t *l = lv_label_create(f);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, farg, LV_PART_MAIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(l, ANSIKTE_BREDD - 2 * 16);
    lv_obj_align(l, al, 0, y);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *linje(lv_obj_t *f, int32_t y, lv_color_t farg)
{
    lv_obj_t *o = lv_obj_create(f);
    lv_obj_set_size(o, 0, 3);
    lv_obj_set_style_bg_color(o, farg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 2, LV_PART_MAIN);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(o, LV_ALIGN_TOP_LEFT, 24, y);
    return o;
}

void vyer_bygg(void)
{
    lv_obj_t *skarm = lv_screen_active();

    /* Klockan: tiden stor mitt på, datumet under, sekunderna som en linje. */
    vy_klocka = behallare(skarm);
    k_tid    = etikett(vy_klocka, &lv_font_siffror, FARG_TEXT, LV_ALIGN_CENTER, -30);
    k_datum  = etikett(vy_klocka, &lv_font_datum, FARG_DAMPAD, LV_ALIGN_CENTER, 62);
    lv_obj_t *spar = linje(vy_klocka, 330, FARG_SVAG);
    lv_obj_set_width(spar, ANSIKTE_BREDD - 2 * 24);
    k_sekund = linje(vy_klocka, 330, FARG_TEXT);

    /* Timern: samma form, med en stapel som krymper och en rad som förklarar. */
    vy_timer = behallare(skarm);
    t_lage   = etikett(vy_timer, &lv_font_replik, FARG_DAMPAD, LV_ALIGN_TOP_MID, 44);
    t_tid    = etikett(vy_timer, &lv_font_siffror, FARG_DAMPAD, LV_ALIGN_CENTER, -30);
    lv_obj_t *spar2 = linje(vy_timer, 300, FARG_SVAG);
    lv_obj_set_width(spar2, ANSIKTE_BREDD - 2 * 24);
    t_stapel = linje(vy_timer, 300, FARG_VARM);
    t_hint   = etikett(vy_timer, &lv_font_replik, FARG_DAMPAD, LV_ALIGN_BOTTOM_MID, -24);
    lv_label_set_long_mode(t_hint, LV_LABEL_LONG_WRAP);

    lv_obj_add_event_cb(vy_timer, tryck_i_vy, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(vy_klocka, tryck_i_vy, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(skarm, gest, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(skarm, tryck_pa_ansiktet, LV_EVENT_CLICKED, NULL);

    puls = lv_timer_create(puls_cb, 1000, NULL);
    timer_uppdatera();
}

void vyer_satt_tid_krok(bool (*krok)(struct tm *))
{
    tid_krok = krok;
}

void vyer_visa(vy_t vy)
{
    if (vy >= VY_ANTAL) return;
    aktiv = vy;
    if (vy == VY_KLOCKA) klocka_uppdatera();
    if (vy == VY_TIMER)  timer_uppdatera();
    ansikte_synlig(vy == VY_ANSIKTE);
    if (vy == VY_KLOCKA) lv_obj_remove_flag(vy_klocka, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(vy_klocka, LV_OBJ_FLAG_HIDDEN);
    if (vy == VY_TIMER)  lv_obj_remove_flag(vy_timer,  LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(vy_timer,  LV_OBJ_FLAG_HIDDEN);
}

vy_t vyer_aktiv(void)
{
    return aktiv;
}

void vyer_nasta(void)
{
    vyer_visa((vy_t)((aktiv + 1) % VY_ANTAL));
}

void vyer_forra(void)
{
    vyer_visa((vy_t)((aktiv + VY_ANTAL - 1) % VY_ANTAL));
}

void vyer_timer_satt(int minuter)
{
    if (minuter <= 0) { timer_kor = false; timer_satt_s = timer_kvar_s = TIMER_STD_MIN * 60; timer_uppdatera(); return; }
    if (minuter > TIMER_MAX_MIN) minuter = TIMER_MAX_MIN;
    timer_satt_s = timer_kvar_s = minuter * 60;
    timer_uppdatera();
}

void vyer_timer_starta(bool kor)
{
    timer_kor = kor;
    timer_uppdatera();
}

int vyer_timer_kvar_s(void)
{
    return timer_kor ? timer_kvar_s : -1;
}
