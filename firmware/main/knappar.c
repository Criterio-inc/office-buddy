/*
 * Knapparna — volymen.
 *
 * Två knappar sitter på sidan. BOOT går rakt till GPIO0 och är låg när den
 * trycks. Strömknappen går till AXP2101, som märker ett kort tryck i sitt
 * avbrottsregister (0x49, bit 0) när det avbrottet är påslaget (0x41, bit
 * 0). Ett långt tryck på strömknappen sköts av kretsen själv och ska inte
 * hållas inne i onödan: efter sex sekunder stänger den av strömmen.
 *
 * Nivåerna är få och tydliga. Steget hörs och syns, och sparas i NVS.
 */

#include <stdio.h>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "ansikte.h"
#include "humor.h"
#include "knappar.h"
#include "ljud.h"

static const char *TAG = "knappar";

#define GPIO_BOOT      GPIO_NUM_0
#define AXP_ADRESS     0x34
#define AXP_IRQ_EN1    0x41
#define AXP_IRQ_STAT1  0x49
#define AXP_PKEY_KORT  0x01

static const int NIVAER[] = { 0, 15, 30, 45, 60, 80 };
#define ANTAL_NIVAER   (int)(sizeof(NIVAER) / sizeof(NIVAER[0]))

static int  niva = 3;   /* 45 %, lagom diskret */
static i2c_master_dev_handle_t axp;

static bool axp_las(uint8_t reg, uint8_t *ut)
{
    return axp != NULL && i2c_master_transmit_receive(axp, &reg, 1, ut, 1, 50) == ESP_OK;
}

static bool axp_skriv(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = { reg, v };
    return axp != NULL && i2c_master_transmit(axp, b, 2, 50) == ESP_OK;
}

static void spara(void)
{
    nvs_handle_t h;
    if (nvs_open("buddy", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, "volym", niva);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void las_sparad(void)
{
    nvs_handle_t h;
    int32_t v;
    if (nvs_open("buddy", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_i32(h, "volym", &v) == ESP_OK && v >= 0 && v < ANTAL_NIVAER) niva = (int)v;
        nvs_close(h);
    }
}

static void visa(void)
{
    char rad[32];
    if (NIVAER[niva] == 0) snprintf(rad, sizeof(rad), "tyst");
    else snprintf(rad, sizeof(rad), "volym %d %%", NIVAER[niva]);
    if (bsp_display_lock(50)) {
        ansikte_sag(rad, 2500);
        bsp_display_unlock();
    }
}

static void steg(int riktning)
{
    int ny = niva + riktning;
    if (ny < 0 || ny >= ANTAL_NIVAER) {
        visa();          /* redan i ändläget: visa bara */
        return;
    }
    niva = ny;
    ljud_satt_volym(NIVAER[niva]);
    ESP_LOGI(TAG, "Volym %d %%", NIVAER[niva]);
    visa();
    if (NIVAER[niva] > 0) ljud_spela(LJUD_BLIPP);
    spara();
}

static void uppgift(void *arg)
{
    (void)arg;
    bool boot_forr = true;
    int  boot_stabil = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30));

        /* BOOT: låg när den trycks, med lite tid för studs. */
        bool boot = gpio_get_level(GPIO_BOOT) != 0;
        if (boot == boot_forr) boot_stabil = 0;
        else if (++boot_stabil >= 2) {
            boot_forr = boot;
            boot_stabil = 0;
            if (!boot) steg(-1);
        }

        /* Strömknappen: kort tryck märks av AXP2101 och nollställs med en etta. */
        uint8_t st;
        if (axp_las(AXP_IRQ_STAT1, &st) && (st & AXP_PKEY_KORT)) {
            axp_skriv(AXP_IRQ_STAT1, AXP_PKEY_KORT);
            steg(+1);
        }
    }
}

void knappar_starta(void)
{
    esp_err_t fel = nvs_flash_init();
    if (fel == ESP_ERR_NVS_NO_FREE_PAGES || fel == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    las_sparad();
    ljud_satt_volym(NIVAER[niva]);

    gpio_config_t g = {
        .pin_bit_mask = 1ULL << GPIO_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&g);

    i2c_master_bus_handle_t buss = NULL;
    if (bsp_i2c_init() == ESP_OK && i2c_master_get_bus_handle(BSP_I2C_NUM, &buss) == ESP_OK && buss != NULL) {
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = AXP_ADRESS,
            .scl_speed_hz    = 400000,
        };
        if (i2c_master_bus_add_device(buss, &cfg, &axp) == ESP_OK) {
            uint8_t en = 0;
            axp_las(AXP_IRQ_EN1, &en);
            axp_skriv(AXP_IRQ_EN1, en | AXP_PKEY_KORT);
            axp_skriv(AXP_IRQ_STAT1, AXP_PKEY_KORT);   /* rensa gammalt */
        }
    }

    xTaskCreate(uppgift, "knappar", 3072, NULL, 2, NULL);
    ESP_LOGI(TAG, "Knapparna igång: BOOT sänker, strömknappen höjer. Volym %d %%", NIVAER[niva]);
}
