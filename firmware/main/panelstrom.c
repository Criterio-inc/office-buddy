/*
 * Panelens ström och reset.
 *
 * Ur Waveshares schema för ESP32-S3-Touch-AMOLED-1.8, expandern TCA9554:
 *   EXIO0  LCD_RESET      panelens reset
 *   EXIO1  DSI_PWR_EN     panelens strömaktivering
 *   EXIO2  TP_RESET       pekskärmens reset
 *   EXIO3  RTC_INT        ingång
 *   EXIO4  SYS_OUT        ingång
 *   EXIO5  AXP_IRQ        ingång
 *   EXIO6  QMI_INT1       ingång
 *   EXIO7  SDCS           minneskortets chip select
 *
 * BSP 2.0.3 rör aldrig expandern, och kretsen vaknar med alla pinnar som
 * ingångar. Panelens ström hängde därför på ett pull-up-motstånd och en
 * flytande ingång, som sjönk efter fem till åtta minuter: glaset slocknade
 * medan chippet körde vidare, och strömkretsens register var oförändrade.
 * Fabriksfirmwaren drev pinnarna. Nu gör vi det också.
 */

#include "bsp/esp-bsp.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "panelstrom.h"

static const char *TAG = "panelstrom";

#define EXIO_LCD_RESET  IO_EXPANDER_PIN_NUM_0
#define EXIO_LCD_STROM  IO_EXPANDER_PIN_NUM_1
#define EXIO_TP_RESET   IO_EXPANDER_PIN_NUM_2
#define EXIO_SDCS       IO_EXPANDER_PIN_NUM_7
#define UTGANGAR        (EXIO_LCD_RESET | EXIO_LCD_STROM | EXIO_TP_RESET | EXIO_SDCS)

static esp_io_expander_handle_t exp;

static bool hamta(void)
{
    if (exp != NULL) return true;
    if (bsp_i2c_init() != ESP_OK) return false;
    exp = bsp_io_expander_init();
    return exp != NULL;
}

void panelstrom_starta(void)
{
    if (!hamta()) {
        ESP_LOGW(TAG, "Expandern svarar inte, panelen får klara sig på pull-up");
        return;
    }
    /* Utgångar, och ström samt minneskortets CS höga direkt så inget flimrar. */
    esp_io_expander_set_dir(exp, UTGANGAR, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(exp, EXIO_LCD_STROM | EXIO_SDCS, 1);

    /* Reset-pulståg åt panelen och pekskärmen, så att båda startar i känt läge. */
    esp_io_expander_set_level(exp, EXIO_LCD_RESET | EXIO_TP_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_io_expander_set_level(exp, EXIO_LCD_RESET | EXIO_TP_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    uint32_t niva = 0;
    esp_io_expander_get_level(exp, 0xFF, &niva);
    ESP_LOGI(TAG, "Expandern driver panelen: ström, reset och pekskärm höga (pinnar 0x%02x)", (unsigned)niva);
}

void panelstrom_sakra(void)
{
    if (!hamta()) return;
    esp_io_expander_set_dir(exp, UTGANGAR, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(exp, UTGANGAR, 1);
}
