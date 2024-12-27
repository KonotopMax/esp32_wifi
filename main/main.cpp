#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_mac.h"

// Настройки WiFi
#define AP_SSID "Agrokurs_wifi"
#define AP_CHANNEL 4
#define MAX_CLIENTS 4
#define UDP_PORT 4445

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

static const char *TAG = "wifi_ap";
static int sock = -1;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Станция "MACSTR" подключилась, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Станция "MACSTR" отключилась, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    // Настройка статического IP
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 0, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 0, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .max_connection = MAX_CLIENTS,
            .authmode = WIFI_AUTH_OPEN,
            .ssid_hidden = 0,
            .beacon_interval = 100,
            .pairwise_cipher = WIFI_CIPHER_TYPE_NONE,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = false
            },
        },
    };
    
    // Копируем SSID
    memcpy(wifi_config.ap.ssid, AP_SSID, strlen(AP_SSID));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP запущена. SSID:%s channel:%d (открытая сеть без пароля)",
             AP_SSID, AP_CHANNEL);
}

void init_udp_server(void)
{
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Не удалось создать socket: errno %d", errno);
        return;
    }

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int err = bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Не удалось привязать socket: errno %d", errno);
        close(sock);
        return;
    }

    ESP_LOGI(TAG, "UDP сервер запущен на порту %d", UDP_PORT);
}

void udp_server_task(void *pvParameters)
{
    char rx_buffer[128];
    char reply[256];

    while (1) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                          (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            ESP_LOGE(TAG, "Ошибка приема: errno %d", errno);
            continue;
        }

        rx_buffer[len] = 0;
        ESP_LOGI(TAG, "Получено %d байт: %s", len, rx_buffer);

        snprintf(reply, sizeof(reply), "Сообщение получено: %.128s", rx_buffer);
        sendto(sock, reply, strlen(reply), 0,
               (struct sockaddr *)&source_addr, sizeof(source_addr));
    }
}

extern "C" void app_main(void)
{
    // Инициализация NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Запуск WiFi AP
    wifi_init_softap();

    // Инициализация UDP сервера
    init_udp_server();

    // Создание задачи для UDP сервера
    xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL);
}