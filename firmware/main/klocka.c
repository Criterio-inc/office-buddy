/*
 * Klockan — PCF85063 på I2C-adress 0x51.
 *
 * Kortet har en riktig realtidsklocka med egen kristall. Ställd en gång
 * fortsätter den gå, och med ett backupbatteri på RTC-paddarna överlever
 * den till och med att huvudströmmen bryts.
 *
 * Nyttan: när datorn inte svarar, till exempel för
 * att Macen sover, kan skärmen säga hur gammal senaste hämtningen är i
 * stället för att bara konstatera att kontakten är borta.
 *
 * Kretsen lagrar tiden i BCD, alltså två decimala siffror per byte.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "klocka.h"

static const char *TAG = "klocka";

#define PCF85063_ADRESS 0x51
#define REG_CONTROL_1   0x00
#define REG_SEKUNDER    0x04   /* sedan följer min, tim, dag, veckodag, mån, år */

/* Bit 7 i sekundregistret betyder att klockan tappat tiden. */
#define FLAGGA_OSTABIL  0x80

static i2c_master_dev_handle_t klockan;

static uint8_t till_bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int     fran_bcd(uint8_t v) { return ((v >> 4) & 0x0F) * 10 + (v & 0x0F); }

esp_err_t klocka_starta(void)
{
    if (klockan != NULL) return ESP_OK;

    /*
     * Kortet arbetar genomgående i UTC: kretsen lagrar UTC, servern skickar
     * UTC, och med tidszonen satt så gör mktime samma sak som timegm, som
     * inte finns i ESP-IDF:s C-bibliotek.
     */
    setenv("TZ", "UTC0", 1);
    tzset();

    i2c_master_bus_handle_t buss = NULL;
    esp_err_t fel = bsp_i2c_init();
    if (fel != ESP_OK) return fel;
    fel = i2c_master_get_bus_handle(BSP_I2C_NUM, &buss);
    if (fel != ESP_OK || buss == NULL) return ESP_FAIL;

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PCF85063_ADRESS,
        .scl_speed_hz    = 100000,
    };
    return i2c_master_bus_add_device(buss, &cfg, &klockan);
}

bool klocka_las(struct tm *ut)
{
    if (klockan == NULL) return false;

    uint8_t reg = REG_SEKUNDER;
    uint8_t d[7];
    if (i2c_master_transmit_receive(klockan, &reg, 1, d, sizeof(d), 300) != ESP_OK) {
        return false;
    }

    /* Kretsen säger själv ifrån om den tappat tiden sedan den senast ställdes. */
    if (d[0] & FLAGGA_OSTABIL) {
        ESP_LOGW(TAG, "Klockan har tappat tiden och är inte att lita på");
        return false;
    }

    memset(ut, 0, sizeof(*ut));
    ut->tm_sec  = fran_bcd(d[0] & 0x7F);
    ut->tm_min  = fran_bcd(d[1] & 0x7F);
    ut->tm_hour = fran_bcd(d[2] & 0x3F);
    ut->tm_mday = fran_bcd(d[3] & 0x3F);
    ut->tm_wday = d[4] & 0x07;
    ut->tm_mon  = fran_bcd(d[5] & 0x1F) - 1;      /* kretsen räknar 1-12 */
    ut->tm_year = fran_bcd(d[6]) + 100;           /* kretsen räknar från 2000 */
    return true;
}

bool klocka_stall(time_t epok)
{
    if (klockan == NULL) return false;

    struct tm t;
    gmtime_r(&epok, &t);

    uint8_t paket[8] = {
        REG_SEKUNDER,
        till_bcd(t.tm_sec),          /* bit 7 nollas, alltså tiden är giltig */
        till_bcd(t.tm_min),
        till_bcd(t.tm_hour),
        till_bcd(t.tm_mday),
        (uint8_t)t.tm_wday,
        till_bcd(t.tm_mon + 1),
        till_bcd(t.tm_year - 100),
    };
    if (i2c_master_transmit(klockan, paket, sizeof(paket), 300) != ESP_OK) {
        ESP_LOGW(TAG, "Kunde inte ställa klockan");
        return false;
    }

    /* Håll systemklockan i takt med kretsen, så att time(NULL) fungerar. */
    struct timeval tv = { .tv_sec = epok, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    ESP_LOGI(TAG, "Klockan ställd till %04d-%02d-%02d %02d:%02d:%02d UTC",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return true;
}

bool klocka_till_systemet(void)
{
    struct tm t;
    if (!klocka_las(&t)) return false;

    t.tm_isdst = 0;
    time_t epok = mktime(&t);
    struct timeval tv = { .tv_sec = epok, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    ESP_LOGI(TAG, "Läste klockan: %04d-%02d-%02d %02d:%02d:%02d UTC",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return true;
}
