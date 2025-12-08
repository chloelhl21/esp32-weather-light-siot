/*
 * ESP32 Smart Lighting Control System - WS2812 RGB Version
 * 
 * Features:
 * - WS2812 RGB LED Color Control
 * - Auto Mode: Dark environment + Motion detected -> Auto Light ON
 * - Manual Mode: Control color, brightness, and effects via HTTP API
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "led_strip.h"
#include "esp_crt_bundle.h"
#include <time.h>
#include <sys/time.h>

// ============================================================
// User Configuration Area - Modify based on your actual setup
// ============================================================

// WiFi Configuration - Change to your WiFi SSID and Password
#define WIFI_SSID      "4THU_Z95XZQ_2.4Ghz"        // WiFi Name (SSID)
#define WIFI_PASS      "3n6xhs3z8p8f"            // WiFi Password
#define WIFI_MAXIMUM_RETRY  5                // WiFi Connection Retry Count

// Data Push Configuration - Comment out PUSH_URL if not needed
#define PUSH_URL       "https://myedu.webn.cc/api/sensor-data.php"  // Data Push Server Address
#define PUSH_INTERVAL  60000                 // Push Interval (ms), 60000 = 1 minute
#define DEVICE_ID      "ESP32_SMART_LIGHT_001"  // Unique Device ID

// ============================================================
// The following configurations usually do not need modification
// ============================================================

// GPIO Pin Definitions
#define PIR_SENSOR_PIN      GPIO_NUM_13
#define LIGHT_SENSOR_PIN    ADC1_CHANNEL_6
#define WS2812_PIN          GPIO_NUM_12

// WS2812 Configuration
#define LED_STRIP_LENGTH    5
#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)

// Light Threshold
#define LIGHT_THRESHOLD     3000

// Light Effect Definitions
typedef enum {
    EFFECT_NONE = 0,
    EFFECT_FADE,
    EFFECT_BREATH,
    EFFECT_RAINBOW,
    EFFECT_RAINBOW_CYCLE
} light_effect_t;

static const char *TAG = "SmartLight";

// System State
typedef struct {
    bool is_auto_mode;
    bool is_light_on;
    int light_value;
    bool motion_detected;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t brightness;
    light_effect_t effect;
    uint16_t effect_speed;
} system_state_t;

static system_state_t system_state = {
    .is_auto_mode = true,
    .is_light_on = false,
    .light_value = 0,
    .motion_detected = false,
    .red = 255,
    .green = 255,
    .blue = 255,
    .brightness = 100,
    .effect = EFFECT_NONE,
    .effect_speed = 50
};

static led_strip_handle_t led_strip = NULL;
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static httpd_handle_t server = NULL;
static esp_adc_cal_characteristics_t *adc_chars;

// WiFi Event Handler
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying WiFi connection...");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// WiFi Initialization
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, 
                        pdFALSE, pdFALSE, portMAX_DELAY);
}

// HTTP Event Handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                printf("%.*s", evt->data_len, (char*)evt->data);
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Push Data
void push_sensor_data(void)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return;
    }
    
    // Add Device Info (Use field names expected by the server)
    cJSON_AddStringToObject(root, "deviceId", DEVICE_ID);
    
    // Add Timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    cJSON_AddNumberToObject(root, "timestamp", tv.tv_sec);
    
    // Add Sensor Data
    cJSON_AddNumberToObject(root, "lightValue", system_state.light_value);
    
    // Calculate light percentage (Note: Adjust based on your LDR wiring)
    // If strong light = low ADC, weak light = high ADC, use inverted logic
    int light_percent = (int)((1.0 - (float)system_state.light_value / 4095.0) * 100);
    cJSON_AddNumberToObject(root, "lightPercent", light_percent);
    
    cJSON_AddBoolToObject(root, "motion", system_state.motion_detected);
    cJSON_AddBoolToObject(root, "lightOn", system_state.is_light_on);
    cJSON_AddBoolToObject(root, "autoMode", system_state.is_auto_mode);
    
    // WS2812 Specific Data (Optional, server side does not verify)
    cJSON_AddNumberToObject(root, "red", system_state.red);
    cJSON_AddNumberToObject(root, "green", system_state.green);
    cJSON_AddNumberToObject(root, "blue", system_state.blue);
    cJSON_AddNumberToObject(root, "brightness", system_state.brightness);
    
    char *json_str = cJSON_Print(root);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "JSON Serialization Failed");
        cJSON_Delete(root);
        return;
    }
    
    ESP_LOGI(TAG, "Preparing to push data: %s", json_str);
    
    esp_http_client_config_t config = {
        .url = PUSH_URL,
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP Client Initialization Failed");
        free(json_str);
        cJSON_Delete(root);
        return;
    }
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_str, strlen(json_str));
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "Data Push Success - HTTP Status=%d, Length=%d", 
                 status_code, content_length);
    } else {
        ESP_LOGE(TAG, "Data Push Failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    free(json_str);
    cJSON_Delete(root);
}

// WS2812 Control Functions
void apply_brightness(uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (*r * system_state.brightness) / 100;
    *g = (*g * system_state.brightness) / 100;
    *b = (*b * system_state.brightness) / 100;
}

void set_all_leds(uint8_t r, uint8_t g, uint8_t b)
{
    apply_brightness(&r, &g, &b);
    for (int i = 0; i < LED_STRIP_LENGTH; i++) {
        led_strip_set_pixel(led_strip, i, r, g, b);
    }
    led_strip_refresh(led_strip);
}

void clear_all_leds(void)
{
    led_strip_clear(led_strip);
}

void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) {
        *r = v; *g = v; *b = v;
        return;
    }
    
    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

void turn_on_light(void)
{
    set_all_leds(system_state.red, system_state.green, system_state.blue);
    system_state.is_light_on = true;
    ESP_LOGI(TAG, "Light Turned ON RGB(%d,%d,%d)", system_state.red, system_state.green, system_state.blue);
}

void turn_off_light(void)
{
    clear_all_leds();
    system_state.is_light_on = false;
    ESP_LOGI(TAG, "Light Turned OFF");
}

void set_rgb_color(uint8_t r, uint8_t g, uint8_t b)
{
    system_state.red = r;
    system_state.green = g;
    system_state.blue = b;
    if (system_state.is_light_on) {
        set_all_leds(r, g, b);
    }
}

void set_brightness(uint8_t brightness)
{
    if (brightness > 100) brightness = 100;
    system_state.brightness = brightness;
    if (system_state.is_light_on) {
        set_all_leds(system_state.red, system_state.green, system_state.blue);
    }
}

// Light Effect Task
void light_effect_task(void *pvParameters)
{
    uint16_t hue = 0;
    float breath_phase = 0;
    
    while (1) {
        if (system_state.is_light_on && system_state.effect != EFFECT_NONE) {
            switch (system_state.effect) {
                case EFFECT_RAINBOW: {
                    uint8_t r, g, b;
                    hsv_to_rgb(hue, 255, 255, &r, &g, &b);
                    set_all_leds(r, g, b);
                    hue = (hue + 1) % 256;
                    vTaskDelay(pdMS_TO_TICKS(100 - system_state.effect_speed));
                    break;
                }
                
                case EFFECT_RAINBOW_CYCLE: {
                    for (int i = 0; i < LED_STRIP_LENGTH; i++) {
                        uint8_t r, g, b;
                        uint16_t pixel_hue = (hue + (i * 256 / LED_STRIP_LENGTH)) % 256;
                        hsv_to_rgb(pixel_hue, 255, 255, &r, &g, &b);
                        apply_brightness(&r, &g, &b);
                        led_strip_set_pixel(led_strip, i, r, g, b);
                    }
                    led_strip_refresh(led_strip);
                    hue = (hue + 1) % 256;
                    vTaskDelay(pdMS_TO_TICKS(100 - system_state.effect_speed));
                    break;
                }
                
                case EFFECT_BREATH: {
                    breath_phase += 0.05;
                    if (breath_phase >= 2 * M_PI) breath_phase = 0;
                    float brightness_factor = (sin(breath_phase) + 1.0) / 2.0;
                    uint8_t temp_brightness = system_state.brightness * brightness_factor;
                    uint8_t r = (system_state.red * temp_brightness) / 100;
                    uint8_t g = (system_state.green * temp_brightness) / 100;
                    uint8_t b = (system_state.blue * temp_brightness) / 100;
                    for (int i = 0; i < LED_STRIP_LENGTH; i++) {
                        led_strip_set_pixel(led_strip, i, r, g, b);
                    }
                    led_strip_refresh(led_strip);
                    vTaskDelay(pdMS_TO_TICKS(50));
                    break;
                }
                
                default:
                    break;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Sensor Reading
int read_light_sensor(void)
{
    uint32_t adc_reading = 0;
    for (int i = 0; i < 10; i++) {
        adc_reading += adc1_get_raw(LIGHT_SENSOR_PIN);
    }
    return (int)(adc_reading / 10);
}

bool read_pir_sensor(void)
{
    return gpio_get_level(PIR_SENSOR_PIN);
}

// HTTP Server Handler Functions
extern const char html_page_start[] asm("_binary_index_html_start");
extern const char html_page_end[] asm("_binary_index_html_end");

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const size_t html_len = html_page_end - html_page_start;
    httpd_resp_send(req, html_page_start, html_len);
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Received status query request");
    
    // Add CORS Headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "auto_mode", system_state.is_auto_mode);
    cJSON_AddBoolToObject(root, "light_on", system_state.is_light_on);
    cJSON_AddNumberToObject(root, "light_value", system_state.light_value);
    cJSON_AddBoolToObject(root, "motion", system_state.motion_detected);
    cJSON_AddNumberToObject(root, "red", system_state.red);
    cJSON_AddNumberToObject(root, "green", system_state.green);
    cJSON_AddNumberToObject(root, "blue", system_state.blue);
    cJSON_AddNumberToObject(root, "brightness", system_state.brightness);
    cJSON_AddNumberToObject(root, "effect", system_state.effect);
    
    char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t control_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Received control command request");
    
    // Add CORS Headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    
    char buf[200];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        ESP_LOGE(TAG, "Failed to receive data");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    ESP_LOGI(TAG, "Received command: %s", buf);
    
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON Parse Failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (action && cJSON_IsString(action)) {
        const char *cmd = action->valuestring;
        
        if (strcmp(cmd, "on") == 0) {
            system_state.effect = EFFECT_NONE;
            turn_on_light();
        } else if (strcmp(cmd, "off") == 0) {
            turn_off_light();
        } else if (strcmp(cmd, "toggle_mode") == 0) {
            system_state.is_auto_mode = !system_state.is_auto_mode;
        } else if (strcmp(cmd, "set_color") == 0) {
            cJSON *r = cJSON_GetObjectItem(root, "r");
            cJSON *g = cJSON_GetObjectItem(root, "g");
            cJSON *b = cJSON_GetObjectItem(root, "b");
            if (r && g && b) {
                system_state.effect = EFFECT_NONE;
                set_rgb_color(r->valueint, g->valueint, b->valueint);
            }
        } else if (strcmp(cmd, "set_brightness") == 0) {
            cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
            if (brightness) {
                set_brightness(brightness->valueint);
            }
        } else if (strcmp(cmd, "set_effect") == 0) {
            cJSON *effect = cJSON_GetObjectItem(root, "effect");
            if (effect) {
                system_state.effect = effect->valueint;
                if (system_state.effect != EFFECT_NONE && !system_state.is_light_on) {
                    turn_on_light();
                }
            }
        }
    }
    
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// OPTIONS Preflight Request Handler (CORS)
static esp_err_t options_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Received OPTIONS preflight request: %s", req->uri);
    
    // Set CORS Headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
    
    // Return 204 No Content
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// HTTP Server Start
httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
        httpd_uri_t status_uri = {.uri = "/status", .method = HTTP_GET, .handler = status_get_handler};
        httpd_uri_t control_uri = {.uri = "/control", .method = HTTP_POST, .handler = control_post_handler};
        
        // Add OPTIONS handlers for CORS preflight
        httpd_uri_t options_status_uri = {.uri = "/status", .method = HTTP_OPTIONS, .handler = options_handler};
        httpd_uri_t options_control_uri = {.uri = "/control", .method = HTTP_OPTIONS, .handler = options_handler};
        
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &options_status_uri);
        httpd_register_uri_handler(server, &control_uri);
        httpd_register_uri_handler(server, &options_control_uri);
        
        ESP_LOGI(TAG, "HTTP Server started successfully on port: 80");
    }
    return server;
}

// Sensor Monitor Task
void sensor_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor monitor task started");
    TickType_t last_push_time = 0;
    uint32_t log_counter = 0;
    bool last_motion = false;
    
    while (1) {
        system_state.light_value = read_light_sensor();
        system_state.motion_detected = read_pir_sensor();
        
        // Log immediately when PIR status changes
        if (system_state.motion_detected != last_motion) {
            ESP_LOGI(TAG, "*** PIR Status Changed: %s ***", 
                     system_state.motion_detected ? "Motion Detected" : "Motion Cleared");
            last_motion = system_state.motion_detected;
        }
        
        // Log sensor status every 2 seconds (4 loops * 500ms)
        if (log_counter % 4 == 0) {
            ESP_LOGI(TAG, "Sensor Status - Light=%d, Motion=%s, Lamp=%s(%d,%d,%d,%d%%), Mode=%s",
                     system_state.light_value,
                     system_state.motion_detected ? "YES" : "NO",
                     system_state.is_light_on ? "ON" : "OFF",
                     system_state.red, system_state.green, system_state.blue, 
                     system_state.brightness,system_state.is_auto_mode ? "Auto" : "Manual");
        }
        log_counter++;
        
        if (system_state.is_auto_mode) {
            bool should_on = (system_state.light_value > LIGHT_THRESHOLD) && system_state.motion_detected;
            
            if (should_on && !system_state.is_light_on) {
                ESP_LOGI(TAG, "Auto Mode Triggered ON - Light=%d > Threshold=%d, Motion Detected", 
                         system_state.light_value, LIGHT_THRESHOLD);
                system_state.effect = EFFECT_NONE;
                turn_on_light();
            } else if (!should_on && system_state.is_light_on) {
                ESP_LOGI(TAG, "Auto Mode Triggered OFF");
                turn_off_light();
            }
        }
        
        if ((xTaskGetTickCount() - last_push_time) > pdMS_TO_TICKS(PUSH_INTERVAL)) {
            ESP_LOGI(TAG, "Starting data push to server...");
            push_sensor_data();
            last_push_time = xTaskGetTickCount();
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Main Function
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32 Smart Lighting System - WS2812 RGB Version");
    ESP_LOGI(TAG, "Device ID: %s", DEVICE_ID);
    ESP_LOGI(TAG, "Push URL: %s", PUSH_URL);
    ESP_LOGI(TAG, "Push Interval: %d ms", PUSH_INTERVAL);
    ESP_LOGI(TAG, "Light Threshold: %d", LIGHT_THRESHOLD);
    ESP_LOGI(TAG, "========================================");
    
    ESP_ERROR_CHECK(nvs_flash_init());
    
    // Configure PIR Sensor
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIR_SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    // Configure ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(LIGHT_SENSOR_PIN, ADC_ATTEN_DB_11);
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, adc_chars);
    
    // Initialize LED Strip
    led_strip_config_t strip_config = {
        .strip_gpio_num = WS2812_PIN,
        .max_leds = LED_STRIP_LENGTH,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_STRIP_RMT_RES_HZ,
        .flags.with_dma = false,
    };
    
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
    
    // Initialize WiFi
    wifi_init_sta();
    
    // Start HTTP Server
    ESP_LOGI(TAG, "Starting HTTP Server...");
    start_webserver();
    
    // Create Tasks
    ESP_LOGI(TAG, "Creating Sensor Monitor Task...");
    xTaskCreate(sensor_monitor_task, "sensor_monitor", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Creating Light Effect Task...");
    xTaskCreate(light_effect_task, "light_effect", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "System initialization complete, starting operation");
    ESP_LOGI(TAG, "========================================");
}
