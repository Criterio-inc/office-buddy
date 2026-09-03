/*
 * Rörelsesensorn — knack och lyft ur QMI8658:s accelerometer.
 *
 * BSP:n har ingen drivrutin för kretsen, så registren skrivs direkt:
 *   0x00 WHO_AM_I = 0x05     0x02 CTRL1 (autoinkrement)
 *   0x03 CTRL2 (skala, takt) 0x08 CTRL7 (accelerometern på)
 *   0x35..0x3A AX AY AZ som int16, ±4 g ger 8192 steg per g
 *
 * Tolkningen är enkel med flit. Ett lågpassfilter följer tyngdkraften. Det
 * som avviker snabbt från den är en stöt: ett knack. Om själva tyngdkraftens
 * riktning vandrar bort från viloläget och stannar där, har någon lyft
 * kortet. Viloläget lärs in på nytt varje gång kortet legat stilla en stund,
 * så det spelar ingen roll hur det står.
 */

#include <math.h>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "humor.h"
#include "rorelse.h"

static const char *TAG = "rorelse";

#define QMI_ADRESS   0x6B
#define STEG_PER_G   8192.0f
#define TAKT_MS      20

#define KNACK_G       0.18f   /* så stor stöt som räknas; 0.35 missade lätta knackningar */
#define KNACK_PAUS_MS 700     /* två knack tätare än så blir ett */
#define LYFT_GRADER   18.0f   /* så mycket lutningen måste ändras */
#define LYFT_HALL_MS  250     /* och hålla i sig så länge */
#define VILA_MS       2500    /* stilla så länge: nytt viloläge lärs in */
#define VILA_G        0.06f   /* så nära tyngdkraften räknas som stilla */

static i2c_master_dev_handle_t don;

static bool skriv(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = { reg, v };
    return i2c_master_transmit(don, b, 2, 100) == ESP_OK;
}

static bool las(uint8_t reg, uint8_t *ut, size_t n)
{
    return i2c_master_transmit_receive(don, &reg, 1, ut, n, 100) == ESP_OK;
}

static bool las_accel(float *x, float *y, float *z)
{
    uint8_t b[6];
    if (!las(0x35, b, 6)) return false;
    *x = (int16_t)(b[0] | (b[1] << 8)) / STEG_PER_G;
    *y = (int16_t)(b[2] | (b[3] << 8)) / STEG_PER_G;
    *z = (int16_t)(b[4] | (b[5] << 8)) / STEG_PER_G;
    return true;
}

static float langd(float x, float y, float z)
{
    return sqrtf(x * x + y * y + z * z);
}

static float vinkel_grader(float ax, float ay, float az, float bx, float by, float bz)
{
    float la = langd(ax, ay, az), lb = langd(bx, by, bz);
    if (la < 0.01f || lb < 0.01f) return 0;
    float c = (ax * bx + ay * by + az * bz) / (la * lb);
    if (c > 1) c = 1;
    if (c < -1) c = -1;
    return acosf(c) * 180.0f / (float)M_PI;
}

static void handelse(humor_handelse_t h)
{
    if (bsp_display_lock(50)) {
        humor_handelse(h);
        bsp_display_unlock();
    }
}

static void uppgift(void *arg)
{
    (void)arg;
    float gx = 0, gy = 0, gz = 1;         /* tyngdkraften, lågpassad */
    float vx = 0, vy = 0, vz = 1;         /* viloläget, inlärt */
    bool  vila_kand = false;
    /* Filtret startar från första avläsningen, annars ser starten ut som en
     * stöt. Knacket hålls dessutom tyst första sekunden. */
    int32_t stilla_ms = 0, lyft_ms = 0, knack_paus_ms = 1000;
    if (las_accel(&gx, &gy, &gz) == false) { gx = 0; gy = 0; gz = 1; }
    bool  lyft_pagar = false;
    int   varv = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TAKT_MS));
        float x, y, z;
        if (!las_accel(&x, &y, &z)) continue;

        /* Tyngdkraften följer långsamt; det snabba är stöten. */
        gx += (x - gx) * 0.08f;
        gy += (y - gy) * 0.08f;
        gz += (z - gz) * 0.08f;
        float stot = langd(x - gx, y - gy, z - gz);

        if (knack_paus_ms > 0) knack_paus_ms -= TAKT_MS;

        /* Stilla: nära tyngdkraften länge nog. Då lärs viloläget in. */
        if (stot < VILA_G) {
            stilla_ms += TAKT_MS;
            if (stilla_ms >= VILA_MS) {
                float andring = vila_kand ? vinkel_grader(gx, gy, gz, vx, vy, vz) : 999;
                if (!vila_kand || andring > 3.0f) {
                    vx = gx; vy = gy; vz = gz;
                    if (!vila_kand) ESP_LOGI(TAG, "Viloläge inlärt: %.2f %.2f %.2f g", vx, vy, vz);
                    vila_kand = true;
                }
                if (lyft_pagar) {
                    lyft_pagar = false;
                    ESP_LOGI(TAG, "Nedställd");
                }
            }
        } else {
            stilla_ms = 0;
        }

        /* Nästan-knack loggas så att tröskeln går att ställa efter verkliga knackningar. */
        if (stot > KNACK_G * 0.5f && stot <= KNACK_G && knack_paus_ms <= 0 && !lyft_pagar) {
            knack_paus_ms = KNACK_PAUS_MS;
            ESP_LOGI(TAG, "Svag stöt %.2f g, under tröskeln %.2f", stot, KNACK_G);
        }

        /* Knack: en kort, tydlig stöt, inte mitt i ett lyft. */
        if (stot > KNACK_G && knack_paus_ms <= 0 && !lyft_pagar) {
            knack_paus_ms = KNACK_PAUS_MS;
            ESP_LOGI(TAG, "Knack, stöt %.2f g", stot);
            handelse(HANDELSE_KNACK);
        }

        /* Lyft: tyngdkraftens riktning har vandrat bort från viloläget och stannat. */
        if (vila_kand && !lyft_pagar) {
            float v = vinkel_grader(gx, gy, gz, vx, vy, vz);
            if (v > LYFT_GRADER) {
                lyft_ms += TAKT_MS;
                if (lyft_ms >= LYFT_HALL_MS) {
                    lyft_pagar = true;
                    lyft_ms = 0;
                    stilla_ms = 0;
                    ESP_LOGI(TAG, "Lyft, lutning %.0f grader", v);
                    handelse(HANDELSE_LYFT);
                }
            } else {
                lyft_ms = 0;
            }
        }

        if (++varv % (60000 / TAKT_MS) == 0) {
            ESP_LOGI(TAG, "g %.2f %.2f %.2f  stöt %.2f  %s", gx, gy, gz, stot, lyft_pagar ? "lyft" : "vilar");
        }
    }
}

void rorelse_starta(void)
{
    i2c_master_bus_handle_t buss = NULL;
    if (bsp_i2c_init() != ESP_OK ||
        i2c_master_get_bus_handle(BSP_I2C_NUM, &buss) != ESP_OK || buss == NULL) {
        ESP_LOGW(TAG, "Ingen I2C-buss, rörelsesensorn hoppas över");
        return;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = QMI_ADRESS,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(buss, &cfg, &don) != ESP_OK) {
        ESP_LOGW(TAG, "Kunde inte lägga till QMI8658 på bussen");
        return;
    }

    uint8_t vem = 0;
    if (!las(0x00, &vem, 1) || vem != 0x05) {
        ESP_LOGW(TAG, "QMI8658 svarar inte som väntat (WHO_AM_I 0x%02x), rörelsesensorn hoppas över", vem);
        return;
    }

    /* Mjuk reset, sedan: autoinkrement, ±4 g i 112 Hz, bara accelerometern. */
    skriv(0x60, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(20));
    skriv(0x02, 0x40);
    skriv(0x03, 0x16);
    skriv(0x08, 0x01);
    vTaskDelay(pdMS_TO_TICKS(20));

    float x, y, z;
    if (las_accel(&x, &y, &z)) {
        ESP_LOGI(TAG, "QMI8658 igång: %.2f %.2f %.2f g (längd %.2f)", x, y, z, langd(x, y, z));
    }
    xTaskCreate(uppgift, "rorelse", 4096, NULL, 3, NULL);
}
