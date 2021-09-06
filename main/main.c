/* WiFi Connection Example using WPA2 Enterprise
 *
 * Original Copyright (C) 2006-2016, ARM Limited, All Rights Reserved, Apache 2.0 License.
 * Additions Copyright (C) Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD, Apache 2.0 License.
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_sntp.h"

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_vfs_semihost.h"
#include "esp_vfs_fat.h"
#include "esp_spiffs.h"
#include "sdmmc_cmd.h"
#include "mdns.h"
#include "lwip/apps/netbiosns.h"

#if CONFIG_WEB_DEPLOY_SD
    #include "driver/sdmmc_host.h"
#endif

#define WIFI_SSID CONFIG_WIFI_SSID
#define EAP_METHOD CONFIG_EAP_METHOD

#define EAP_ID CONFIG_EAP_ID
#define EAP_USERNAME CONFIG_EAP_USERNAME
#define EAP_PASSWORD CONFIG_EAP_PASSWORD

#define MDNS_INSTANCE "esp home web server"

esp_err_t start_rest_server(const char *base_path);

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* esp netif object representing the WIFI station */
static esp_netif_t *sta_netif = NULL;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

static const char *TAG = "server";

/* CA cert, taken from wpa2_ca.pem
   Client cert, taken from wpa2_client.crt
   Client key, taken from wpa2_client.key

   The PEM, CRT and KEY file were provided by the person or organization
   who configured the AP with wpa2 enterprise.

   To embed it in the app binary, the PEM, CRT and KEY file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
#ifdef CONFIG_VALIDATE_SERVER_CERT
    extern uint8_t ca_pem_start[] asm("_binary_wpa2_ca_pem_start");
    extern uint8_t ca_pem_end[]   asm("_binary_wpa2_ca_pem_end");
#endif /* CONFIG_VALIDATE_SERVER_CERT */

#ifdef CONFIG_EAP_METHOD_TLS
    extern uint8_t client_crt_start[] asm("_binary_wpa2_client_crt_start");
    extern uint8_t client_crt_end[]   asm("_binary_wpa2_client_crt_end");
    extern uint8_t client_key_start[] asm("_binary_wpa2_client_key_start");
    extern uint8_t client_key_end[]   asm("_binary_wpa2_client_key_end");
#endif /* CONFIG_EAP_METHOD_TLS */

#if defined CONFIG_EAP_METHOD_TTLS
    esp_eap_ttls_phase2_types TTLS_PHASE2_METHOD = CONFIG_EAP_METHOD_TTLS_PHASE_2;
#endif /* CONFIG_EAP_METHOD_TTLS */

// If semihost is used
#if CONFIG_WEB_DEPLOY_SEMIHOST
esp_err_t init_fs(void)
    {
        esp_err_t ret = esp_vfs_semihost_register(CONFIG_WEB_MOUNT_POINT, CONFIG_HOST_PATH_TO_MOUNT);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register semihost driver (%s)!", esp_err_to_name(ret));
            return ESP_FAIL;
        }
        return ESP_OK;
    }
#endif

// If SD card is used as external storage
#if CONFIG_WEB_DEPLOY_SD
    esp_err_t init_fs(void)
    {
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

        gpio_set_pull_mode(15, GPIO_PULLUP_ONLY); // CMD
        gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);  // D0
        gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);  // D1
        gpio_set_pull_mode(12, GPIO_PULLUP_ONLY); // D2
        gpio_set_pull_mode(13, GPIO_PULLUP_ONLY); // D3

        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = true,
            .max_files = 4,
            .allocation_unit_size = 16 * 1024
        };

        sdmmc_card_t *card;
        esp_err_t ret = esp_vfs_fat_sdmmc_mount(CONFIG_WEB_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount filesystem.");
            } else {
                ESP_LOGE(TAG, "Failed to initialize the card (%s)", esp_err_to_name(ret));
            }
            return ESP_FAIL;
        }
        /* print card info if mount successfully */
        sdmmc_card_print_info(stdout, card);
        return ESP_OK;
    }
#endif

// If deployed from SPIFFS
#if CONFIG_WEB_DEPLOY_SF
    esp_err_t init_fs(void)
    {
        esp_vfs_spiffs_conf_t conf = {
            .base_path = CONFIG_WEB_MOUNT_POINT,
            .partition_label = NULL,
            .max_files = 5,
            .format_if_mount_failed = false
        };
        esp_err_t ret = esp_vfs_spiffs_register(&conf);

        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount or format filesystem");
            } else if (ret == ESP_ERR_NOT_FOUND) {
                ESP_LOGE(TAG, "Failed to find SPIFFS partition");
            } else {
                ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
            }
            return ESP_FAIL;
        }

        size_t total = 0, used = 0;
        ret = esp_spiffs_info(NULL, &total, &used);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
        }
        return ESP_OK;
    }
#endif

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
}

static void initialise_wifi(void)
{
    #ifdef CONFIG_VALIDATE_SERVER_CERT
        unsigned int ca_pem_bytes = ca_pem_end - ca_pem_start;
    #endif /* CONFIG_VALIDATE_SERVER_CERT */

    #ifdef CONFIG_EAP_METHOD_TLS
        unsigned int client_crt_bytes = client_crt_end - client_crt_start;
        unsigned int client_key_bytes = client_key_end - client_key_start;
    #endif /* CONFIG_EAP_METHOD_TLS */

        ESP_ERROR_CHECK(esp_netif_init());
        wifi_event_group = xEventGroupCreate();
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        sta_netif = esp_netif_create_default_wifi_sta();
        assert(sta_netif);

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
        ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
        ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
        ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
        wifi_config_t wifi_config = {
            .sta = {
                .ssid = WIFI_SSID,
            },
        };
        ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
        ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)EAP_ID, strlen(EAP_ID)) );

    #ifdef CONFIG_VALIDATE_SERVER_CERT
        ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_ca_cert(ca_pem_start, ca_pem_bytes) );
    #endif /* CONFIG_VALIDATE_SERVER_CERT */

    #ifdef CONFIG_EAP_METHOD_TLS
        ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_cert_key(client_crt_start, client_crt_bytes,\
                client_key_start, client_key_bytes, NULL, 0) );
    #endif /* CONFIG_EAP_METHOD_TLS */

    #if defined CONFIG_EAP_METHOD_PEAP || CONFIG_EAP_METHOD_TTLS
        ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_username((uint8_t *)EAP_USERNAME, strlen(EAP_USERNAME)) );
        ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_password((uint8_t *)EAP_PASSWORD, strlen(EAP_PASSWORD)) );
    #endif /* CONFIG_EAP_METHOD_PEAP || CONFIG_EAP_METHOD_TTLS */

    #if defined CONFIG_EAP_METHOD_TTLS
        ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_ttls_phase2_method(TTLS_PHASE2_METHOD) );
    #endif /* CONFIG_EAP_METHOD_TTLS */
        ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_enable() );
        ESP_ERROR_CHECK( esp_wifi_start() );
}

static void initialise_mdns(void)
{
    mdns_init();
    mdns_hostname_set(CONFIG_MDNS_HOST_NAME);
    mdns_instance_name_set(MDNS_INSTANCE);

    mdns_txt_item_t serviceTxtData[] = {
        {"board", "esp32"},
        {"path", "/"}
    };

    ESP_ERROR_CHECK(mdns_service_add("ESP32-WebServer", "_http", "_tcp", 80, serviceTxtData,
                                     sizeof(serviceTxtData) / sizeof(serviceTxtData[0])));
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    initialise_wifi();

    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // display local ip address
    esp_netif_ip_info_t ip;
    memset(&ip, 0, sizeof(esp_netif_ip_info_t));
    if (esp_netif_get_ip_info(sta_netif, &ip) == 0) {
        ESP_LOGI(TAG, "~~~~~~~~~~~");
        ESP_LOGI(TAG, "IP:"IPSTR, IP2STR(&ip.ip));
        ESP_LOGI(TAG, "MASK:"IPSTR, IP2STR(&ip.netmask));
        ESP_LOGI(TAG, "GW:"IPSTR, IP2STR(&ip.gw));
        ESP_LOGI(TAG, "~~~~~~~~~~~");
    }

    vTaskDelay(5000 / portTICK_PERIOD_MS);

    initialise_mdns();
    netbiosns_init();
    netbiosns_set_name(CONFIG_MDNS_HOST_NAME);
    ESP_ERROR_CHECK(init_fs());
    ESP_ERROR_CHECK(start_rest_server(CONFIG_WEB_MOUNT_POINT));
}