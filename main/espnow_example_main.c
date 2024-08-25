/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
   This example shows how to use ESPNOW.
   Prepare two device, one for sending ESPNOW data and another for receiving
   ESPNOW data.
*/
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_crc.h"
#include "espnow_example.h"
#include "driver/gpio.h"

#define ESPNOW_MAXDELAY 512

// -- Motor define --
#define OUTPUT_GPIO0 0
#define OUTPUT_GPIO1 1
#define OUTPUT_GPIO2 2
#define OUTPUT_GPIO3 3

static void config_gpio_output(gpio_num_t gpioNum);

static const char *TAG = "espnow_example";

static QueueHandle_t s_example_espnow_queue;

static uint8_t s_example_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// static uint16_t s_example_espnow_seq[EXAMPLE_ESPNOW_DATA_MAX] = {0, 0};

/* WiFi should start before using ESPNOW */
static void example_wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
#endif
}

/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */

static void example_espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    example_espnow_event_t evt;
    example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
    uint8_t *mac_addr = recv_info->src_addr;
    uint8_t *des_addr = recv_info->des_addr;

    if (mac_addr == NULL || data == NULL || len <= 0)
    {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }

    if (IS_BROADCAST_ADDR(des_addr))
    {
        /* If added a peer with encryption before, the receive packets may be
         * encrypted as peer-to-peer message or unencrypted over the broadcast channel.
         * Users can check the destination address to distinguish it.
         */
        ESP_LOGD(TAG, "Receive broadcast ESPNOW data");
    }
    else
    {
        ESP_LOGD(TAG, "Receive unicast ESPNOW data");
    }

    evt.id = EXAMPLE_ESPNOW_RECV_CB;
    memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_cb->data = malloc(len);
    if (recv_cb->data == NULL)
    {
        ESP_LOGE(TAG, "Malloc receive data fail");
        return;
    }
    memcpy(recv_cb->data, data, len);
    recv_cb->data_len = len;

    if (xQueueSend(s_example_espnow_queue, &evt, ESPNOW_MAXDELAY) != pdTRUE)
    {
        ESP_LOGW(TAG, "Send receive queue fail");
        free(recv_cb->data);
    }
}

static void example_espnow_task(void *pvParameter)
{
    example_espnow_event_t evt;

    while (xQueueReceive(s_example_espnow_queue, &evt, portMAX_DELAY) == pdTRUE)
    {
        switch (evt.id)
        {
        case EXAMPLE_ESPNOW_SEND_CB:
        {
        }
        case EXAMPLE_ESPNOW_RECV_CB:
        {
            example_espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
            uint8_t data[2] = {0};

            for (int i = 0; i < recv_cb->data_len; i++)
            {
                data[i] = recv_cb->data[i];
            }

            // ESP_LOGI(TAG, "data value data x = %d", data[0]);
            // ESP_LOGI(TAG, "data value data y = %d", data[1]);

            //Y

            if(data[1] > 190) // Move Up

            {
                gpio_set_level(OUTPUT_GPIO0, 1);
                gpio_set_level(OUTPUT_GPIO1, 0);
            }
            else if(data[1] < 20) // Move down
            {
                gpio_set_level(OUTPUT_GPIO0, 0);
                gpio_set_level(OUTPUT_GPIO1, 1);
            }
            else // Default
            {
                gpio_set_level(OUTPUT_GPIO0, 0);
                gpio_set_level(OUTPUT_GPIO1, 0);
            }

            // X 

            if(data[0] > 240) // Move Right
            {
                gpio_set_level(OUTPUT_GPIO2, 1);
                gpio_set_level(OUTPUT_GPIO3, 0);
            }
            else if(data[0] == 0) // Move Left
            {
                gpio_set_level(OUTPUT_GPIO2, 0);
                gpio_set_level(OUTPUT_GPIO3, 1);
            }
            else // Default
            {
                gpio_set_level(OUTPUT_GPIO2, 0);
                gpio_set_level(OUTPUT_GPIO3, 0);
            }

            free(recv_cb->data);
            break;
        }
        default:

            break;
        }
    }
}

static esp_err_t example_espnow_init(void)
{

    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (s_example_espnow_queue == NULL)
    {
        ESP_LOGE(TAG, "Create mutex fail");
        return ESP_FAIL;
    }

    /* Initialize ESPNOW and register sending and receiving callback function. */
    ESP_ERROR_CHECK(esp_now_init());
    // ESP_ERROR_CHECK(esp_now_register_send_cb(example_espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(example_espnow_recv_cb));
#if CONFIG_ESPNOW_ENABLE_POWER_SAVE
    ESP_ERROR_CHECK(esp_now_set_wake_window(CONFIG_ESPNOW_WAKE_WINDOW));
    ESP_ERROR_CHECK(esp_wifi_connectionless_module_set_wake_interval(CONFIG_ESPNOW_WAKE_INTERVAL));
#endif
    /* Set primary master key. */
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK));

    xTaskCreate(example_espnow_task, "example_espnow_task", 2048, NULL, 4, NULL);

    return ESP_OK;
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    example_wifi_init();
    example_espnow_init();

    config_gpio_output(OUTPUT_GPIO0);
    config_gpio_output(OUTPUT_GPIO1);
    config_gpio_output(OUTPUT_GPIO2);
    config_gpio_output(OUTPUT_GPIO3);

    gpio_set_level(OUTPUT_GPIO0, 0);
    gpio_set_level(OUTPUT_GPIO1, 0);
    gpio_set_level(OUTPUT_GPIO2, 0);
    gpio_set_level(OUTPUT_GPIO3, 0);

    while(1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void config_gpio_output(gpio_num_t gpioNum)
{
    gpio_reset_pin(gpioNum);
    gpio_set_direction(gpioNum, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(gpioNum, GPIO_PULLUP_ONLY);
}
