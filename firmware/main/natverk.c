/*
 * Nätverkslänken: samma rader som över USB, fast över wifi.
 *
 * Datorn kan sitta i en annan del av huset, eller inte alls i samma sladd
 * som kortet. Så länge båda är på samma nät hittar länktjänsten kortet på
 * office-buddy.local och skriver sina rader dit. Varje rad besvaras med en
 * rad som börjar med "ob ", precis som över USB.
 *
 * Wifi-uppgifterna kommer ur secrets.h, som genereras ur macOS-nyckelringen
 * av verktyg/generera-secrets.sh och aldrig ligger i git. Saknas filen byggs
 * firmwaren utan wifi.
 *
 * Fällorna från Projektpulsen gäller: inget blockerande i händelsehandlaren,
 * ingen strömsparning, octal-PSRAM på 40 MHz med CAPS_ALLOC (sdkconfig).
 */

#include <string.h>

#if __has_include("secrets.h")
#include "secrets.h"
#define HAR_WIFI 1
#else
#define HAR_WIFI 0
#endif

#include "esp_log.h"

#if HAR_WIFI

#include "bsp/esp-bsp.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs_flash.h"

#include "natverk.h"
#include "protokoll.h"
#include "usb_lank.h"

static const char *TAG = "natverk";

#define UPPKOPPLAD_BIT BIT0
#define LANK_PORT      8740
#define VARDNAMN       "office-buddy"

static EventGroupHandle_t handelser;
static int  misslyckade_forsok;
static char ip_text[16];

static void wifi_handelse(void *arg, esp_event_base_t bas, int32_t id, void *data)
{
    (void)arg;
    if (bas == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (bas == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(handelser, UPPKOPPLAD_BIT);
        ip_text[0] = '\0';
        misslyckade_forsok++;
        if (misslyckade_forsok % 10 == 1) ESP_LOGW(TAG, "Wifi tappat, försöker igen (försök %d)", misslyckade_forsok);
        esp_wifi_connect();
    } else if (bas == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *h = (ip_event_got_ip_t *)data;
        snprintf(ip_text, sizeof(ip_text), IPSTR, IP2STR(&h->ip_info.ip));
        ESP_LOGI(TAG, "Uppkopplad, IP %s, når mig på %s.local:%d", ip_text, VARDNAMN, LANK_PORT);
        misslyckade_forsok = 0;
        xEventGroupSetBits(handelser, UPPKOPPLAD_BIT);
    }
}

/* En anslutning i taget: läser rader, svarar, tills datorn lägger på. */
static void betjana(int s)
{
    char buf[512], rad[256], svar[256];
    int  n = 0;
    struct timeval t = { .tv_sec = 300 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
    for (;;) {
        int r = recv(s, buf + n, sizeof(buf) - n - 1, 0);
        if (r <= 0) break;
        n += r;
        buf[n] = '\0';
        char *slut;
        while ((slut = strchr(buf, '\n')) != NULL) {
            size_t langd = (size_t)(slut - buf);
            if (langd >= sizeof(rad)) langd = sizeof(rad) - 1;
            memcpy(rad, buf, langd);
            rad[langd] = '\0';
            memmove(buf, slut + 1, (size_t)(n - (slut - buf) - 1));
            n -= (int)(slut - buf) + 1;
            buf[n] = '\0';
            if (rad[0] == '\0' || rad[0] == '\r') continue;

            usb_lank_markera_rad();
            bool ok = false;
            if (bsp_display_lock(0)) {
                ok = protokoll_rad(rad, svar, sizeof(svar));
                bsp_display_unlock();
            }
            if (!ok) ESP_LOGW(TAG, "%s", svar);
            size_t sl = strlen(svar);
            if (sl < sizeof(svar) - 1) { svar[sl] = '\n'; svar[sl + 1] = '\0'; }
            if (send(s, svar, strlen(svar), 0) < 0) return;
        }
        if (n >= (int)sizeof(buf) - 1) n = 0;   /* skräp utan radslut kastas */
    }
}

static void lyssnare(void *arg)
{
    (void)arg;
    int lyss = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int ett = 1;
    setsockopt(lyss, SOL_SOCKET, SO_REUSEADDR, &ett, sizeof(ett));
    struct sockaddr_in adr = { .sin_family = AF_INET, .sin_port = htons(LANK_PORT), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(lyss, (struct sockaddr *)&adr, sizeof(adr)) < 0 || listen(lyss, 2) < 0) {
        ESP_LOGE(TAG, "Kunde inte lyssna på port %d", LANK_PORT);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Lyssnar på port %d", LANK_PORT);
    for (;;) {
        struct sockaddr_in fran;
        socklen_t fl = sizeof(fran);
        int s = accept(lyss, (struct sockaddr *)&fran, &fl);
        if (s < 0) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        ESP_LOGI(TAG, "Datorn ansluten över wifi");
        betjana(s);
        close(s);
        ESP_LOGI(TAG, "Datorn la på");
    }
}

void natverk_starta(void)
{
    esp_err_t fel = nvs_flash_init();
    if (fel == ESP_ERR_NVS_NO_FREE_PAGES || fel == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    handelser = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t grund = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&grund));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_handelse, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_handelse, NULL, NULL));

    wifi_config_t inst = { 0 };
    strncpy((char *)inst.sta.ssid, WIFI_SSID, sizeof(inst.sta.ssid) - 1);
    strncpy((char *)inst.sta.password, WIFI_LOSENORD, sizeof(inst.sta.password) - 1);
    inst.sta.failure_retry_cnt = 5;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &inst));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    /* Namnet på nätet, så att länktjänsten slipper veta någon IP-adress. */
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(VARDNAMN);
        mdns_instance_name_set("Office Buddy");
        mdns_service_add(NULL, "_office-buddy", "_tcp", LANK_PORT, NULL, 0);
    }

    xTaskCreate(lyssnare, "natlank", 6144, NULL, 3, NULL);
    ESP_LOGI(TAG, "Wifi startat, söker %s", WIFI_SSID);
}

bool natverk_uppkopplat(void)
{
    return handelser != NULL && (xEventGroupGetBits(handelser) & UPPKOPPLAD_BIT) != 0;
}

const char *natverk_ip(void)
{
    return ip_text;
}

#else  /* utan secrets.h */

#include "natverk.h"

void natverk_starta(void)
{
    ESP_LOGI("natverk", "Ingen secrets.h: byggd utan wifi, buddyn går på USB");
}

bool natverk_uppkopplat(void) { return false; }
const char *natverk_ip(void) { return ""; }

#endif
