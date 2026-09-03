/*
 * Office Buddy på kortet.
 *
 * Den här filen är avsiktligt tunn. Ansiktet bor i delat/ansikte.c och
 * humöret i delat/humor.c, båda delade med emulatorn på MacBooken, så att
 * det som syns i fönstret är exakt det som hamnar på glaset. Här finns bara
 * det som är kortets ensak: panelen, klockkretsen och LVGL-låset.
 *
 * Kortet är ett Waveshare ESP32-S3-Touch-AMOLED-1.8, 368x448 bildpunkter,
 * och sitter i datorn på USB-C hela tiden.
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ansikte.h"
#include "hardvarukoll.h"
#include "humor.h"
#include "klocka.h"
#include "knappar.h"
#include "ljud.h"
#include "panelstrom.h"
#include "protokoll.h"
#include "rorelse.h"
#include "usb_lank.h"
#include "vyer.h"

static const char *TAG = "office-buddy";

#define LJUS_VAKEN  75
#define LJUS_SOVER  12

/* ---- Lokal tid -----------------------------------------------------------
 *
 * Kortet arbetar i UTC rakt igenom (klockkretsen, systemklockan). Humöret
 * vill däremot veta vad klockan är där buddyn står. Datorn skickar sin
 * tidszonsförskjutning tillsammans med tiden, och den används. Har ingen
 * dator hört av sig gäller reserven: centraleuropeisk tid med sommartid
 * från sista söndagen i mars 01:00 UTC till sista söndagen i oktober.
 */

static int32_t forskjutning_fran_datorn = INT32_MIN;   /* sekunder öster om UTC, okänd tills datorn sagt */

static time_t sista_sondag_01_utc(int ar, int manad)
{
    struct tm t = { .tm_year = ar - 1900, .tm_mon = manad - 1, .tm_mday = 31, .tm_hour = 1 };
    time_t e = mktime(&t);              /* TZ är UTC0, satt av klocka_starta */
    struct tm *u = gmtime(&e);
    return e - (time_t)u->tm_wday * 86400;
}

static int lokal_forskjutning_s(time_t utc)
{
    if (forskjutning_fran_datorn != INT32_MIN) return (int)forskjutning_fran_datorn;
    struct tm *u = gmtime(&utc);
    int ar = u->tm_year + 1900;
    bool sommar = utc >= sista_sondag_01_utc(ar, 3) && utc < sista_sondag_01_utc(ar, 10);
    return 3600 * (sommar ? 2 : 1);
}

/* Vyerna vill ha hela datumet i lokal tid. */
static bool lokal_tid(struct tm *ut)
{
    time_t nu = time(NULL);
    if (nu < 1600000000) return false;
    time_t lokal = nu + lokal_forskjutning_s(nu);
    *ut = *gmtime(&lokal);
    return true;
}

static float lokal_timme(void)
{
    time_t nu = time(NULL);
    time_t lokal = nu + lokal_forskjutning_s(nu);
    struct tm *l = gmtime(&lokal);
    return l->tm_hour + l->tm_min / 60.0f + l->tm_sec / 3600.0f;
}

/*
 * Reserv om klockkretsen tappat tiden: byggtiden. Den är lokal tid,
 * så förskjutningen dras av. Kortet ligger då inom minuter från rätt tid
 * direkt efter en flashning, vilket räcker för humöret tills datorn ställer
 * klockan över USB.
 */
static time_t byggtid_utc(void)
{
    static const char *MAN = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char man[4] = { 0 };
    struct tm t = { 0 };
    int dag, ar, tim, min, sek;
    if (sscanf(__DATE__, "%3s %d %d", man, &dag, &ar) != 3) return 0;
    if (sscanf(__TIME__, "%d:%d:%d", &tim, &min, &sek) != 3) return 0;
    const char *p = strstr(MAN, man);
    if (p == NULL) return 0;
    t.tm_year = ar - 1900;
    t.tm_mon  = (int)(p - MAN) / 3;
    t.tm_mday = dag;
    t.tm_hour = tim;
    t.tm_min  = min;
    t.tm_sec  = sek;
    time_t som_utc = mktime(&t);
    return som_utc - lokal_forskjutning_s(som_utc);
}

/* ---- Kopplingar --------------------------------------------------------- */

/*
 * Ljuset sätts med ett kommando på samma SPI-buss som bildpunkterna går på,
 * och esp_lcd:s panel-IO tål inte att två uppgifter använder den samtidigt.
 * Buddyn ritar trettio gånger i sekunden, så bussen är sällan ledig. Under
 * LVGL-låset pågår ingen flush, och då är kommandot säkert.
 */
static void satt_ljus(int procent)
{
    if (bsp_display_lock(0)) {
        bsp_display_brightness_set(procent);
        bsp_display_unlock();
    }
}

/* Ett tryck på glaset går till humöret, som gör buddyn lite gladare. */
static int petningar;
static void petning(void)
{
    lv_indev_t *don = lv_indev_active();
    lv_point_t p = { -1, -1 };
    if (don != NULL) lv_indev_get_point(don, &p);
    petningar++;
    ESP_LOGI(TAG, "petning %d vid %d,%d", petningar, (int)p.x, (int)p.y);
    humor_handelse(HANDELSE_PETAD);
}

/* Datorn ställer klockan och ljuset via protokollet. */
static void ny_tid(long epok, long forskjutning_s)
{
    if (forskjutning_s != LONG_MIN) forskjutning_fran_datorn = (int32_t)forskjutning_s;
    if (klocka_stall((time_t)epok)) {
        float t = lokal_timme();
        ESP_LOGI(TAG, "Klockan ställd av datorn: lokal tid %02d:%02d", (int)t, (int)((t - (int)t) * 60));
    }
}

static void nytt_ljus(int procent)
{
    satt_ljus(procent);
}

static void nytt_ljud(bool p)
{
    ljud_satt_pa(p);
}

/* Humöret får en sekund i taget, i LVGL:s egen tråd så att inget lås behövs. */
static void humor_klocka(lv_timer_t *t)
{
    (void)t;
    humor_tick(lokal_timme(), 1000);
}

/*
 * Skötseln: panelens strömförsörjning går via en I2C-expander som kan tappa
 * sitt tillstånd medan chippet kör vidare, så ljuset sätts om med jämna
 * mellanrum. Samtidigt dämpas glaset när buddyn sover.
 */
static void skotsel_uppgift(void *arg)
{
    (void)arg;
    int varv = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        const humor_t *h = humor_las();
        panelstrom_sakra();
        /* Datorn skriver var 30:e sekund. Tystnad i två minuter betyder att den sover. */
        if (bsp_display_lock(50)) {
            humor_satt_lank(usb_lank_tyst_ms() < 120000);
            bsp_display_unlock();
        }
        satt_ljus(h->sover ? LJUS_SOVER : LJUS_VAKEN);
        if (++varv % 6 == 0) {
            float t = lokal_timme();
            char strom[160];
            hardvarukoll_stromlage(strom, sizeof(strom));
            ESP_LOGI(TAG, "%02d:%02d  energi %.2f  glädje %.2f  oro %.2f  %s, %s  petningar %d  [%s]",
                     (int)t, (int)((t - (int)t) * 60), h->energi, h->gladje, h->oro,
                     humor_ord(), uttryck_namn(ansikte_uttryck()), petningar, strom);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Office Buddy startar på %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);

    /* Panelens ström och reset går via expandern; den måste drivas innan panelen startas. */
    panelstrom_starta();

    /*
     * Samma inställningar som bsp_display_start(), men med större stack åt
     * LVGL-uppgiften. Standardens 4 kB räcker för textetiketter men inte för
     * bågar och trianglar med kantutjämning, och ett stacköverflöde i den
     * uppgiften kan frysa kortet tyst.
     */
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size   = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = { .buff_dma = false, .buff_spiram = true },
    };
    cfg.lvgl_port_cfg.task_stack = 16384;
    lv_display_t *skarm = bsp_display_start_with_config(&cfg);
    if (skarm == NULL) {
        ESP_LOGE(TAG, "Skärmen gick inte att starta");
        return;
    }

    ESP_LOGI(TAG, "Tar LVGL-låset");
    if (bsp_display_lock(0)) {
        ESP_LOGI(TAG, "Bygger ansiktet");
        ansikte_bygg();
        ansikte_vid_petning(petning);
        vyer_bygg();
        vyer_satt_tid_krok(lokal_tid);
        ESP_LOGI(TAG, "Ansiktet byggt, släpper låset");
        bsp_display_unlock();
    }
    satt_ljus(LJUS_VAKEN);
    ESP_LOGI(TAG, "Ansiktet ritat och tänt");

    hardvarukoll_kor();
    {
        char strom[160];
        hardvarukoll_stromlage(strom, sizeof(strom));
        ESP_LOGI(TAG, "Strömläge vid start [%s]", strom);
    }

    /* Klockan avgör humöret, så den ställs innan humöret startar. */
    bool tid_ok = false;
    if (klocka_starta() == ESP_OK) {
        tid_ok = klocka_till_systemet();
        if (!tid_ok) {
            time_t b = byggtid_utc();
            if (b > 0 && klocka_stall(b)) {
                ESP_LOGW(TAG, "Klockkretsen hade tappat tiden, ställd efter byggtiden");
                tid_ok = true;
            }
        }
    }
    if (!tid_ok) ESP_LOGW(TAG, "Ingen tid alls, humöret utgår från förmiddag");

    float timme = tid_ok ? lokal_timme() : 10.0f;
    ESP_LOGI(TAG, "Lokal tid %02d:%02d", (int)timme, (int)((timme - (int)timme) * 60));

    if (bsp_display_lock(0)) {
        humor_starta(timme);
        lv_timer_create(humor_klocka, 1000, NULL);
        bsp_display_unlock();
    }
    ESP_LOGI(TAG, "Humöret igång: %s, %s", humor_ord(), uttryck_namn(ansikte_uttryck()));

    xTaskCreate(skotsel_uppgift, "skotsel", 4096, NULL, 3, NULL);

    protokoll_krokar_t krokar = { .tid = ny_tid, .ljus = nytt_ljus, .ljud = nytt_ljud };
    protokoll_satt_krokar(&krokar);
    usb_lank_starta();
    rorelse_starta();
    knappar_starta();   /* läser sparad volym innan högtalaren startar */
    ljud_starta();
    humor_vid_ljud(ljud_spela);
}
