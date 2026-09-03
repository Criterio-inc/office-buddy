/*
 * Hårdvarukoll — frågar kortet vad som faktiskt sitter på det.
 *
 * Körs en gång vid start och skriver till loggen. Kostar ingenting i drift
 * och är värd att ha kvar: den svarar på frågor som annars kräver att man
 * litar på ett datablad, till exempel om ett batteri är inkopplat just nu.
 */

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_psram.h"

#include "bsp/esp-bsp.h"
#include <stdio.h>
#include "hardvarukoll.h"

static const char *TAG = "hardvara";

/* Kretsar vi vet kan sitta på bussen, enligt Waveshares beskrivning. */
static const struct {
    uint8_t     adress;
    const char *namn;
} KANDA[] = {
    { 0x18, "ES8311 ljudcodec" },
    { 0x20, "TCA9554 I/O-expander" },
    { 0x34, "AXP2101 strömhantering" },
    { 0x15, "CST816S pekskärm" },
    { 0x51, "PCF85063 realtidsklocka" },
    { 0x6B, "QMI8658 rörelsesensor" },
};

static const char *namn_for(uint8_t adress)
{
    for (size_t i = 0; i < sizeof(KANDA) / sizeof(KANDA[0]); i++) {
        if (KANDA[i].adress == adress) return KANDA[i].namn;
    }
    return "okänd";
}

/* Läser ett register ur en I2C-krets. Returnerar false om den inte svarar. */
static bool las_register(i2c_master_bus_handle_t buss, uint8_t adress,
                         uint8_t reg, uint8_t *ut)
{
    i2c_master_dev_handle_t don;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = adress,
        .scl_speed_hz    = 100000,
    };
    if (i2c_master_bus_add_device(buss, &cfg, &don) != ESP_OK) return false;

    esp_err_t fel = i2c_master_transmit_receive(don, &reg, 1, ut, 1, 200);
    i2c_master_bus_rm_device(don);
    return fel == ESP_OK;
}

void hardvarukoll_kor(void)
{
    ESP_LOGI(TAG, "──────── vad som sitter på kortet ────────");

    /* Processorn och minnet, avläst ur chippet. */
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);

    ESP_LOGI(TAG, "Chip      : ESP32-S3 rev v%d.%d, %d kärnor",
             chip.revision / 100, chip.revision % 100, chip.cores);
    ESP_LOGI(TAG, "Trådlöst  : %s%s%s",
             (chip.features & CHIP_FEATURE_WIFI_BGN) ? "wifi 802.11 b/g/n (endast 2,4 GHz)" : "",
             (chip.features & CHIP_FEATURE_BLE) ? ", Bluetooth LE" : "",
             (chip.features & CHIP_FEATURE_BT) ? ", Bluetooth klassisk" : "");
    ESP_LOGI(TAG, "Flash     : %lu MB", (unsigned long)(flash / (1024 * 1024)));
    ESP_LOGI(TAG, "PSRAM     : %u MB, varav %u kB ledigt just nu",
             (unsigned)(esp_psram_get_size() / (1024 * 1024)),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    ESP_LOGI(TAG, "Internt   : %u kB ledigt av %u kB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024));

    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        ESP_LOGI(TAG, "MAC       : %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    /* Kretsarna på I2C-bussen. BSP:n har redan startat den åt oss. */
    i2c_master_bus_handle_t buss = NULL;
    if (bsp_i2c_init() != ESP_OK ||
        i2c_master_get_bus_handle(BSP_I2C_NUM, &buss) != ESP_OK || buss == NULL) {
        ESP_LOGW(TAG, "Kom inte åt I2C-bussen, hoppar över kretssökningen");
        return;
    }

    ESP_LOGI(TAG, "── kretsar som svarar på I2C ──");
    for (uint8_t adress = 0x08; adress < 0x78; adress++) {
        if (i2c_master_probe(buss, adress, 200) == ESP_OK) {
            ESP_LOGI(TAG, "  0x%02x  %s", adress, namn_for(adress));
        }
    }

    /*
     * Batteriet. AXP2101 vet om något är inkopplat, vilket är mer värt än
     * vad databladet säger: kontakten finns alltid, batteriet gör det inte.
     */
    uint8_t status;
    if (las_register(buss, 0x34, 0x00, &status)) {
        /* Register 0x00, bit 3: batteri anslutet. Bit 5: laddar. */
        bool anslutet = (status & 0x08) != 0;
        bool stromin  = (status & 0x20) != 0;
        ESP_LOGI(TAG, "── batteri ──");
        ESP_LOGI(TAG, "  AXP2101 svarar, statusregister 0x%02x", status);
        ESP_LOGI(TAG, "  Batteri inkopplat : %s", anslutet ? "JA" : "nej");
        ESP_LOGI(TAG, "  Extern ström      : %s", stromin ? "ja" : "nej");
    } else {
        ESP_LOGW(TAG, "AXP2101 svarade inte på 0x34");
    }

    ESP_LOGI(TAG, "─────────────────────────────────────────");
}

void hardvarukoll_stromlage(char *ut, size_t storlek)
{
    i2c_master_bus_handle_t buss = NULL;
    if (bsp_i2c_init() != ESP_OK ||
        i2c_master_get_bus_handle(BSP_I2C_NUM, &buss) != ESP_OK || buss == NULL) {
        snprintf(ut, storlek, "I2C otillgänglig");
        return;
    }
    /* AXP2101: 0x00 status, 0x80 DCDC på/av, 0x90 och 0x91 LDO på/av.
     * TCA9554: 0x01 utgångar, 0x03 riktning (1 = ingång). */
    static const struct { uint8_t adress, reg; const char *namn; } REG[] = {
        { 0x34, 0x00, "axp.status" }, { 0x34, 0x80, "dcdc" },
        { 0x34, 0x90, "ldo" },        { 0x34, 0x91, "ldo2" },
        { 0x20, 0x00, "exp.in" },     { 0x20, 0x01, "exp.ut" },     { 0x20, 0x03, "exp.rikt" },
    };
    size_t n = 0;
    for (size_t i = 0; i < sizeof(REG) / sizeof(REG[0]) && n < storlek; i++) {
        uint8_t v = 0;
        bool ok = las_register(buss, REG[i].adress, REG[i].reg, &v);
        n += (size_t)snprintf(ut + n, storlek - n, "%s%s=%s", i ? " " : "", REG[i].namn,
                              ok ? "" : "?");
        if (ok && n < storlek) n += (size_t)snprintf(ut + n, storlek - n, "%02x", v);
    }
}
