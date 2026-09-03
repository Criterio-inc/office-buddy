/*
 * Ljudet — få toner, låg volym, inga ord. Men som en gammal Atari.
 *
 * Buddyn är tyst av princip. Det som finns är ett litet blipp när någon
 * knackar eller petar, en stigande tvåton när den lyfts, tre glada toner
 * när något som väntade blev gjort, och en trudelutt vid start och på
 * morgonen. Tonerna är fyrkantvåg med kort avklingning, det är det som
 * gör ljudet till åttiotal, och ackordet på slutet är ett arpeggio: tre
 * toner som växlar så fort att örat hör dem samtidigt, precis som ett
 * chip med en enda röst fick låtsas spela ackord. En egen uppgift spelar,
 * så att den som ber om ett ljud aldrig behöver vänta.
 */

#include <math.h>
#include <stdint.h>

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ljud.h"

static const char *TAG = "ljud";

#define TAKT_HZ   22050
#define VOLYM     45          /* procent, avsiktligt lågt */

static esp_codec_dev_handle_t hogtalare;
static QueueHandle_t ko;
static bool pa = true;
static int  volym = VOLYM;

/* En ton: frekvens, längd, och upp till två extra toner för arpeggio (0 = ingen). */
typedef struct { float hz; int ms; float hz2, hz3; } ton_t;

#define C5 523.25f
#define D5 587.33f
#define E5 659.25f
#define G5 783.99f
#define A5 880.00f
#define C6 1046.50f
#define E6 1318.51f
#define G6 1567.98f

static const ton_t BLIPP[]     = { { E5, 60 }, { 0 } };
static const ton_t LYFT[]      = { { C5, 70 }, { G5, 120 }, { 0 } };
static const ton_t GLAD[]      = { { C5, 70 }, { E5, 70 }, { G5, 140 }, { 0 } };
static const ton_t TRUDELUTT[] = {
    { C5, 75 }, { E5, 75 }, { G5, 75 }, { C6, 110 },
    { G5, 75 }, { C6, 75 }, { E6, 130 },
    { 0,  40 },
    { C6, 420, E6, G6 },    /* ackordet: arpeggio i en enda röst */
    { 0 },
};

/*
 * Fyrkantvåg med avklingning. Volymen är lägre än för sinus eftersom en
 * fyrkant låter mycket starkare vid samma amplitud.
 */
static void spela_ton(const ton_t *t)
{
    int n = TAKT_HZ * t->ms / 1000;
    static int16_t buf[TAKT_HZ / 10];   /* 100 ms i taget */
    int skrivet = 0;
    const float toner[3] = { t->hz, t->hz2, t->hz3 };
    int antal_toner = t->hz3 > 0 ? 3 : (t->hz2 > 0 ? 2 : 1);
    const int arp_n = TAKT_HZ * 14 / 1000;   /* 14 ms per ton i arpeggiot */
    float fas = 0;
    while (skrivet < n) {
        int del = n - skrivet;
        if (del > (int)(sizeof(buf) / sizeof(buf[0]))) del = sizeof(buf) / sizeof(buf[0]);
        for (int i = 0; i < del; i++) {
            int k = skrivet + i;
            float hz = toner[(k / arp_n) % antal_toner];
            float v = 0;
            if (hz > 0) {
                fas += hz / TAKT_HZ;
                if (fas >= 1) fas -= 1;
                v = fas < 0.5f ? 1.0f : -1.0f;
            }
            /* Kort kant mot knäpp, sedan klingar tonen av som ett gammalt chip. */
            float kant = 1.0f;
            int kantn = TAKT_HZ * 4 / 1000;
            if (k < kantn) kant = (float)k / kantn;
            else if (n - k < kantn) kant = (float)(n - k) / kantn;
            float avkling = 1.0f - 0.45f * (float)k / (float)n;
            buf[i] = (int16_t)(v * kant * avkling * 0.28f * 32767);
        }
        esp_codec_dev_write(hogtalare, buf, del * (int)sizeof(int16_t));
        skrivet += del;
    }
}

static void uppgift(void *arg)
{
    (void)arg;
    humor_ljud_t l;
    for (;;) {
        if (xQueueReceive(ko, &l, portMAX_DELAY) != pdTRUE) continue;
        if (!pa || volym <= 0 || hogtalare == NULL) continue;
        const ton_t *toner = l == LJUD_BLIPP ? BLIPP : l == LJUD_LYFT ? LYFT
                           : l == LJUD_GLAD ? GLAD : TRUDELUTT;
        static const ton_t paus = { 0, 12 };
        for (const ton_t *t = toner; t->ms > 0; t++) {
            spela_ton(t);
            spela_ton(&paus);
        }
    }
}

void ljud_starta(void)
{
    hogtalare = bsp_audio_codec_speaker_init();
    if (hogtalare == NULL) {
        ESP_LOGW(TAG, "Högtalaren gick inte att starta, buddyn förblir tyst");
        return;
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate     = TAKT_HZ,
        .channel         = 1,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(hogtalare, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Kunde inte öppna högtalaren");
        hogtalare = NULL;
        return;
    }
    esp_codec_dev_set_out_vol(hogtalare, volym);
    ko = xQueueCreate(4, sizeof(humor_ljud_t));
    xTaskCreate(uppgift, "ljud", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "Högtalaren igång, volym %d %%", volym);
    ljud_spela(LJUD_TRUDELUTT);   /* hälsar när den vaknar */
}

void ljud_spela(humor_ljud_t l)
{
    if (ko != NULL) xQueueSend(ko, &l, 0);
}

void ljud_satt_pa(bool p)
{
    pa = p;
    ESP_LOGI(TAG, "Ljudet %s", p ? "på" : "av");
}

void ljud_satt_volym(int procent)
{
    if (procent < 0) procent = 0;
    if (procent > 100) procent = 100;
    volym = procent;
    if (hogtalare != NULL && procent > 0) esp_codec_dev_set_out_vol(hogtalare, procent);
}
