// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "esp_log.h"


#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"

#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "sdkconfig.h"

#include "camera.h"
#include "httpd.h"
#include "bitmap.h"
static int camera_handle(HttpdConnData *connData);
const HttpdBuiltInUrl cameraUrls[] = {
    {"/cam",   camera_handle, NULL},
    {NULL,  NULL,           NULL} //end marker
};

static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const static int CONNECTED_BIT = BIT0;

const static char *HTTPD_APP = "HTTPD";

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
            
            //init app here
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            /* This is a workaround as ESP32 WiFi libs don't currently
               auto-reassociate. */
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wifi_conn_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(HTTPD_APP, "start the WIFI SSID:[%s] password:[%s]\n", CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
    ESP_ERROR_CHECK(esp_wifi_start());
}

static const char* TAG = "camera_demo";


static int camera_handle(HttpdConnData *connData)
{
    esp_err_t err;
    int done = 0;
    int idx = 0, sz = camera_get_fb_width()*camera_get_fb_height()*3;
    if(connData->cgiData != NULL) {
        idx = (int)connData->cgiData;
        ESP_LOGD(TAG, "More = %d", idx);
        int sz_write = sz - idx;
        
        if(sz_write > 1024)
            sz_write = 1024;
        else {
            done = 1;
        }
        httpdSend(connData, (const char *)(camera_get_fb() + idx), sz_write);
        idx += sz_write;
        connData->cgiData = idx;
    } else {
        httpdStartResponse(connData, 200);
        httpdHeader(connData, "Content-Type", "image/bmp");
        httpdEndHeaders(connData);
        err = camera_run();
        if (err != ESP_OK){
            ESP_LOGD(TAG, "Camera capture failed with error = %d", err);
        } else {
            ESP_LOGD(TAG, "Done=%d", sz);
            char *bmp_header = bmp_create_header(camera_get_fb_width(), camera_get_fb_height());
            httpdSend(connData, (const char *)bmp_header, sizeof(bitmap));
            httpdSend(connData, (const char *)camera_get_fb(), 512);
            connData->cgiData = (void*)512;
            free(bmp_header);
            // camera_print_fb();
        }

    }
    
    if(done) {
        return HTTPD_CGI_DONE;
    }
    return HTTPD_CGI_MORE;
}

void app_main()
{
    camera_config_t config = {
        .ledc_channel = LEDC_CHANNEL_0,
        .ledc_timer = LEDC_TIMER_0,
        .pin_d0 = 4,
        .pin_d1 = 5,
        .pin_d2 = 18,
        .pin_d3 = 19,
        .pin_d4 = 36,
        .pin_d5 = 39,
        .pin_d6 = 34,
        .pin_d7 = 35,
        .pin_xclk = 21,
        .pin_pclk = 22,
        .pin_vsync = 25,
        .pin_href = 23,
        .pin_sscb_sda = 26,
        .pin_sscb_scl = 27,
        .pin_reset = 9,
        .xclk_freq_hz = 10000000
    };

    esp_err_t err  = camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error = %d", err);
        return;
    }
    wifi_conn_init();

    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    httpdInit(80);
    httpdAddRouter(cameraUrls, NULL);
    
    // while(true){
    //     err = camera_run();
    //     if (err != ESP_OK){
	   //      ESP_LOGD(TAG, "Camera capture failed with error = %d", err);
    //     } else {
	   //      ESP_LOGD(TAG, "Done");
	   //      camera_print_fb();
    //     }
    //     vTaskDelay(1000 / portTICK_RATE_MS);
    // }
}

