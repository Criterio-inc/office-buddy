/*
 * USB-länken: datorn skriver rader, kortet svarar.
 *
 * Kortets USB-Serial/JTAG är också konsolen, så loggen och svaren går på
 * samma ström. Svaren börjar med "ob " och är därför lätta att plocka ut.
 */

#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "protokoll.h"
#include "usb_lank.h"

static const char *TAG = "usb";
static uint32_t senaste_rad_ms;

static void lasare(void *arg)
{
    (void)arg;
    char rad[256], svar[256];
    for (;;) {
        if (fgets(rad, sizeof(rad), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        senaste_rad_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        /* Tolkningen rör humöret och ansiktet, som lever i LVGL:s tråd. */
        bool ok = false;
        if (bsp_display_lock(0)) {
            ok = protokoll_rad(rad, svar, sizeof(svar));
            bsp_display_unlock();
        }
        printf("%s\n", svar);
        fflush(stdout);
        if (!ok) ESP_LOGW(TAG, "%s", svar);
    }
}

int32_t usb_lank_tyst_ms(void)
{
    return (int32_t)((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) - senaste_rad_ms);
}

void usb_lank_starta(void)
{
    senaste_rad_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    usb_serial_jtag_vfs_use_driver();
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CRLF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
    setvbuf(stdin, NULL, _IONBF, 0);
    xTaskCreate(lasare, "usb", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "Lyssnar på rader från datorn");
}
