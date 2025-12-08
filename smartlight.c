/*
 * ESP32 Smart Lighting Control System - ESP-IDF Version
 * 
 * Features:
 * - Auto Mode: Dark environment + Motion detected -> Auto Light ON
 * - Manual Mode: Controlled via HTTP API
 * - Web Server: Provides a control interface
 */

#include <stdio.h>
#include <string.h>
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
#include <time.h>
#include <sys/time.h>

// WiFi Configuration - Change to your WiFi credentials
#define WIFI_SSID      "4THU_Z95XZQ_2.4Ghz"
#define WIFI_PASS      "3n6xhs3z8p8f"
#define WIFI_MAXIMUM_RETRY  5

// Data Push Configuration
#define PUSH_URL       "http://myedu.webn.cc/api/sensor-data.php"  // API Endpoint (HTTP)
#define PUSH_INTERVAL  60000  // Push interval (ms), 60000ms = 1 minute
#define DEVICE_ID      "ESP32_SMART_LIGHT_001"  // Unique Device ID

// GPIO Pin Definitions
#define PIR_SENSOR_PIN      GPIO_NUM_13    // PIR Motion Sensor
#define LIGHT_SENSOR_PIN    ADC1_CHANNEL_6 // GPIO34 (ADC1_CH6)
#define RELAY_PIN           GPIO_NUM_12    // Relay Control

// Light Threshold (0-4095)
#define LIGHT_THRESHOLD     3000

// Log Tag
static const char *TAG = "SmartLight";

// System State Variables
typedef struct {
    bool is_auto_mode;      // Auto/Manual Mode
    bool is_light_on;       // Light Status
    int light_value;        // Light Sensor Value
    bool motion_detected;   // Motion Detection Status
} system_state_t;

static system_state_t system_state = {
    .is_auto_mode = true,
    .is_light_on = false,
    .light_value = 0,
    .motion_detected = false
};

// WiFi Event Group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static httpd_handle_t server = NULL;

// ADC Calibration
static esp_adc_cal_characteristics_t *adc_chars;

// ==================== WiFi Event Handling ====================

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
        ESP_LOGI(TAG, "Failed to connect to AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
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

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

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

    ESP_LOGI(TAG, "WiFi Initialization Complete");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID:%s", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Unexpected event");
    }
}

// ==================== Data Push Functionality ====================

// HTTP Event Handler
esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP Push Error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP Connected");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP Header Sent");
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP Request Finished");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP Disconnected");
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Push Sensor Data to Server
void push_sensor_data(void)
{
    // Build JSON Data
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return;
    }
    
    // Add Device Info
    cJSON_AddStringToObject(root, "deviceId", DEVICE_ID);
    
    // Add Timestamp (Unix)
    struct timeval tv;
    gettimeofday(&tv, NULL);
    cJSON_AddNumberToObject(root, "timestamp", tv.tv_sec);
    
    // Add Sensor Data
    cJSON_AddNumberToObject(root, "lightValue", system_state.light_value);
    
    // Calculate light percentage (Inverted logic: Large ADC = Low Light)
    int light_percent = (int)((1.0 - (float)system_state.light_value / 4095.0) * 100);
    cJSON_AddNumberToObject(root, "lightPercent", light_percent);
    
    cJSON_AddBoolToObject(root, "motion", system_state.motion_detected);
    cJSON_AddBoolToObject(root, "lightOn", system_state.is_light_on);
    cJSON_AddBoolToObject(root, "autoMode", system_state.is_auto_mode);
    
    // Convert to String
    char *json_str = cJSON_Print(root);
    if (json_str == NULL) {
        ESP_LOGE(TAG, "JSON Serialization Failed");
        cJSON_Delete(root);
        return;
    }
    
    ESP_LOGI(TAG, "Preparing to push data: %s", json_str);
    
    // Configure HTTP Client
    ESP_LOGI(TAG, "Configuring HTTP Client for data push");
    esp_http_client_config_t config = {
        .url = PUSH_URL,
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP Client Initialization Failed");
        free(json_str);
        cJSON_Delete(root);
        return;
    }
    
    // Set Header and Body
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_str, strlen(json_str));
    
    // Execute HTTP POST
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "Data Push Success - HTTP Status=%d, Length=%d", 
                 status_code, content_length);
    } else {
        ESP_LOGE(TAG, "Data Push Failed: %s", esp_err_to_name(err));
    }
    
    // Cleanup
    esp_http_client_cleanup(client);
    free(json_str);
    cJSON_Delete(root);
}

// Data Push Task
void data_push_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Data Push Task Started, URL: %s", PUSH_URL);
    ESP_LOGI(TAG, "Push Interval: %d ms", PUSH_INTERVAL);
    
    // Wait for WiFi to stabilize
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    while (1) {
        // Push data
        push_sensor_data();
        
        // Wait for next push
        vTaskDelay(pdMS_TO_TICKS(PUSH_INTERVAL));
    }
}

// ==================== Hardware Control Functions ====================

// Turn On Light
void turn_on_light(void)
{
    gpio_set_level(RELAY_PIN, 1);
    system_state.is_light_on = true;
    ESP_LOGI(TAG, "Light Turned ON");
}

// Turn Off Light
void turn_off_light(void)
{
    gpio_set_level(RELAY_PIN, 0);
    system_state.is_light_on = false;
    ESP_LOGI(TAG, "Light Turned OFF");
}

// Read Light Sensor
int read_light_sensor(void)
{
    return adc1_get_raw(LIGHT_SENSOR_PIN);
}

// Read PIR Sensor
bool read_pir_sensor(void)
{
    int level = gpio_get_level(PIR_SENSOR_PIN);
    // Debug Log: Output raw PIR level
    ESP_LOGD(TAG, "PIR Raw Level: %d", level);
    
    // ⚠️ PIR Sensor logic is inverted
    return !level;  // Inverted: LOW=Motion, HIGH=No Motion
    
    // return level;  // Normal: HIGH=Motion, LOW=No Motion (Disabled)
}

// ==================== HTTP Server Handling ====================

// Return HTML Page
static const char* get_html_page(void)
{
    return "<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"    <title>Smart Lighting Control</title>\n"
"    <style>\n"
"        * { margin: 0; padding: 0; box-sizing: border-box; }\n"
"        body {\n"
"            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;\n"
"            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);\n"
"            min-height: 100vh;\n"
"            display: flex;\n"
"            justify-content: center;\n"
"            align-items: center;\n"
"            padding: 20px;\n"
"        }\n"
"        .container {\n"
"            background: white;\n"
"            border-radius: 20px;\n"
"            box-shadow: 0 20px 60px rgba(0,0,0,0.3);\n"
"            padding: 40px;\n"
"            max-width: 500px;\n"
"            width: 100%;\n"
"        }\n"
"        h1 { color: #333; text-align: center; margin-bottom: 30px; font-size: 28px; }\n"
"        .status-card { background: #f8f9fa; border-radius: 15px; padding: 20px; margin-bottom: 25px; }\n"
"        .status-item {\n"
"            display: flex;\n"
"            justify-content: space-between;\n"
"            padding: 12px 0;\n"
"            border-bottom: 1px solid #e0e0e0;\n"
"        }\n"
"        .status-item:last-child { border-bottom: none; }\n"
"        .status-label { font-weight: 600; color: #555; }\n"
"        .status-value { font-size: 18px; font-weight: bold; color: #667eea; }\n"
"        .indicator {\n"
"            display: inline-block;\n"
"            width: 12px;\n"
"            height: 12px;\n"
"            border-radius: 50%;\n"
"            margin-right: 8px;\n"
"        }\n"
"        .indicator.on { background: #4caf50; box-shadow: 0 0 10px #4caf50; }\n"
"        .indicator.off { background: #9e9e9e; }\n"
"        .mode-selector { display: flex; gap: 10px; margin-bottom: 25px; }\n"
"        .mode-btn {\n"
"            flex: 1;\n"
"            padding: 15px;\n"
"            border: 2px solid #667eea;\n"
"            background: white;\n"
"            color: #667eea;\n"
"            border-radius: 10px;\n"
"            cursor: pointer;\n"
"            font-size: 16px;\n"
"            font-weight: 600;\n"
"            transition: all 0.3s;\n"
"        }\n"
"        .mode-btn.active { background: #667eea; color: white; }\n"
"        .control-buttons { display: flex; gap: 15px; }\n"
"        .control-btn {\n"
"            flex: 1;\n"
"            padding: 18px;\n"
"            border: none;\n"
"            border-radius: 10px;\n"
"            font-size: 18px;\n"
"            font-weight: 600;\n"
"            cursor: pointer;\n"
"            transition: all 0.3s;\n"
"            color: white;\n"
"        }\n"
"        .control-btn.on { background: linear-gradient(135deg, #4caf50, #45a049); }\n"
"        .control-btn.off { background: linear-gradient(135deg, #f44336, #e53935); }\n"
"        .control-btn:disabled { opacity: 0.5; cursor: not-allowed; }\n"
"        .light-bar {\n"
"            height: 20px;\n"
"            background: #e0e0e0;\n"
"            border-radius: 10px;\n"
"            overflow: hidden;\n"
"        }\n"
"        .light-fill {\n"
"            height: 100%;\n"
"            background: linear-gradient(90deg, #ffd700, #ffed4e);\n"
"            transition: width 0.3s;\n"
"            display: flex;\n"
"            align-items: center;\n"
"            justify-content: flex-end;\n"
"            padding-right: 10px;\n"
"            font-size: 12px;\n"
"            font-weight: bold;\n"
"            color: #333;\n"
"        }\n"
"        .refresh-indicator { text-align: center; color: #999; font-size: 12px; margin-top: 15px; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class=\"container\">\n"
"        <h1>💡 Smart Light Control</h1>\n"
"        <div class=\"status-card\">\n"
"            <div class=\"status-item\">\n"
"                <span class=\"status-label\">Light Status</span>\n"
"                <span class=\"status-value\">\n"
"                    <span class=\"indicator\" id=\"lightIndicator\"></span>\n"
"                    <span id=\"lightStatus\">-</span>\n"
"                </span>\n"
"            </div>\n"
"            <div class=\"status-item\">\n"
"                <span class=\"status-label\">Motion</span>\n"
"                <span class=\"status-value\">\n"
"                    <span class=\"indicator\" id=\"motionIndicator\"></span>\n"
"                    <span id=\"motionStatus\">-</span>\n"
"                </span>\n"
"            </div>\n"
"            <div class=\"status-item\">\n"
"                <span class=\"status-label\">Light Intensity</span>\n"
"                <div style=\"flex: 1; margin-left: 20px;\">\n"
"                    <div class=\"light-bar\">\n"
"                        <div class=\"light-fill\" id=\"lightBar\">-</div>\n"
"                    </div>\n"
"                </div>\n"
"            </div>\n"
"        </div>\n"
"        <div class=\"mode-selector\">\n"
"            <button class=\"mode-btn active\" id=\"autoModeBtn\" onclick=\"setMode(true)\">🤖 Auto Mode</button>\n"
"            <button class=\"mode-btn\" id=\"manualModeBtn\" onclick=\"setMode(false)\">👆 Manual Mode</button>\n"
"        </div>\n"
"        <div class=\"control-buttons\">\n"
"            <button class=\"control-btn on\" id=\"onBtn\" onclick=\"controlLight(true)\" disabled>Turn ON 💡</button>\n"
"            <button class=\"control-btn off\" id=\"offBtn\" onclick=\"controlLight(false)\" disabled>Turn OFF 🌙</button>\n"
"        </div>\n"
"        <div class=\"refresh-indicator\">Auto refreshing... <span id=\"updateTime\"></span></div>\n"
"    </div>\n"
"    <script>\n"
"        let currentMode = true;\n"
"        async function updateStatus() {\n"
"            try {\n"
"                const response = await fetch('/status');\n"
"                const data = await response.json();\n"
"                document.getElementById('lightStatus').textContent = data.lightOn ? 'ON' : 'OFF';\n"
"                document.getElementById('lightIndicator').className = 'indicator ' + (data.lightOn ? 'on' : 'off');\n"
"                document.getElementById('motionStatus').textContent = data.motion ? 'Detected' : 'Clear';\n"
"                document.getElementById('motionIndicator').className = 'indicator ' + (data.motion ? 'on' : 'off');\n"
"                const lightPercent = Math.round((data.lightValue / 4095) * 100);\n"
"                document.getElementById('lightBar').style.width = lightPercent + '%';\n"
"                document.getElementById('lightBar').textContent = lightPercent + '%';\n"
"                currentMode = data.autoMode;\n"
"                updateModeUI();\n"
"                document.getElementById('updateTime').textContent = new Date().toLocaleTimeString();\n"
"            } catch (error) {\n"
"                console.error('Update failed:', error);\n"
"            }\n"
"        }\n"
"        function updateModeUI() {\n"
"            const autoBtn = document.getElementById('autoModeBtn');\n"
"            const manualBtn = document.getElementById('manualModeBtn');\n"
"            const onBtn = document.getElementById('onBtn');\n"
"            const offBtn = document.getElementById('offBtn');\n"
"            if (currentMode) {\n"
"                autoBtn.classList.add('active');\n"
"                manualBtn.classList.remove('active');\n"
"                onBtn.disabled = true;\n"
"                offBtn.disabled = true;\n"
"            } else {\n"
"                autoBtn.classList.remove('active');\n"
"                manualBtn.classList.add('active');\n"
"                onBtn.disabled = false;\n"
"                offBtn.disabled = false;\n"
"            }\n"
"        }\n"
"        async function setMode(isAuto) {\n"
"            try {\n"
"                const response = await fetch('/mode', {\n"
"                    method: 'POST',\n"
"                    headers: {'Content-Type': 'application/json'},\n"
"                    body: JSON.stringify({auto: isAuto})\n"
"                });\n"
"                if (response.ok) {\n"
"                    currentMode = isAuto;\n"
"                    updateModeUI();\n"
"                    updateStatus();\n"
"                }\n"
"            } catch (error) {\n"
"                console.error('Mode set failed:', error);\n"
"            }\n"
"        }\n"
"        async function controlLight(turnOn) {\n"
"            try {\n"
"                const response = await fetch('/control', {\n"
"                    method: 'POST',\n"
"                    headers: {'Content-Type': 'application/json'},\n"
"                    body: JSON.stringify({light: turnOn})\n"
"                });\n"
"                if (response.ok) updateStatus();\n"
"            } catch (error) {\n"
"                console.error('Control failed:', error);\n"
"            }\n"
"        }\n"
"        updateStatus();\n"
"        setInterval(updateStatus, 1000);\n"
"    </script>\n"
"</body>\n"
"</html>";
}

// HTTP GET Handler - Index
static esp_err_t root_get_handler(httpd_req_t *req)
{
    // Add CORS Header
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    const char* html = get_html_page();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

// HTTP GET Handler - Status Query
static esp_err_t status_get_handler(httpd_req_t *req)
{
    // Add CORS Headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "lightOn", system_state.is_light_on);
    cJSON_AddBoolToObject(root, "autoMode", system_state.is_auto_mode);
    cJSON_AddNumberToObject(root, "lightValue", system_state.light_value);
    cJSON_AddBoolToObject(root, "motion", system_state.motion_detected);
    
    const char *json_str = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    
    free((void *)json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// HTTP POST Handler - Light Control
static esp_err_t control_post_handler(httpd_req_t *req)
{
    // Add CORS Headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    
    char content[100];
    int ret = httpd_req_recv(req, content, sizeof(content));
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    cJSON *light = cJSON_GetObjectItem(root, "light");
    if (cJSON_IsBool(light) && !system_state.is_auto_mode) {
        if (cJSON_IsTrue(light)) {
            turn_on_light();
        } else {
            turn_off_light();
        }
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":true}", 17);
    } else {
        httpd_resp_set_type(req, "application/json");
        // Replaced Chinese error message with English
        const char *err_msg = "{\"success\":false,\"message\":\"Cannot control manually in auto mode\"}";
        httpd_resp_send(req, err_msg, strlen(err_msg));
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

// HTTP POST Handler - Mode Switch
static esp_err_t mode_post_handler(httpd_req_t *req)
{
    // Add CORS Headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    
    char content[100];
    int ret = httpd_req_recv(req, content, sizeof(content));
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    cJSON *auto_mode = cJSON_GetObjectItem(root, "auto");
    if (cJSON_IsBool(auto_mode)) {
        system_state.is_auto_mode = cJSON_IsTrue(auto_mode);
        ESP_LOGI(TAG, "Mode Switched: %s", system_state.is_auto_mode ? "Auto" : "Manual");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":true}", 17);
    } else {
        httpd_resp_send_500(req);
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

// HTTP OPTIONS Handler - CORS Preflight (/mode)
static esp_err_t mode_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// HTTP OPTIONS Handler - CORS Preflight (/control)
static esp_err_t control_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// URI Handlers Definition
static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler
};

static const httpd_uri_t status = {
    .uri       = "/status",
    .method    = HTTP_GET,
.handler   = status_get_handler
};

static const httpd_uri_t control = {
    .uri       = "/control",
    .method    = HTTP_POST,
    .handler   = control_post_handler
};

static const httpd_uri_t mode = {
    .uri       = "/mode",
    .method    = HTTP_POST,
    .handler   = mode_post_handler
};

static const httpd_uri_t mode_options = {
    .uri       = "/mode",
    .method    = HTTP_OPTIONS,
    .handler   = mode_options_handler
};

static const httpd_uri_t control_options = {
    .uri       = "/control",
    .method    = HTTP_OPTIONS,
    .handler   = control_options_handler
};

// Start HTTP Server
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Starting HTTP Server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &status);
        httpd_register_uri_handler(server, &control);
        httpd_register_uri_handler(server, &control_options);
        httpd_register_uri_handler(server, &mode);
        httpd_register_uri_handler(server, &mode_options);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

// ==================== Main Task ====================

// Sensor Reading Task
void sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor task started");
    uint32_t log_counter = 0;
    bool last_motion = false;
    
    while (1) {
        // Read Sensors
        system_state.light_value = read_light_sensor();
        system_state.motion_detected = read_pir_sensor();
        
        // Log immediately when PIR status changes
        if (system_state.motion_detected != last_motion) {
            ESP_LOGI(TAG, "*** PIR Status Changed: %s ***", 
                     system_state.motion_detected ? "Motion Detected" : "Motion Cleared");
            last_motion = system_state.motion_detected;
        }
        
        // Log status every 1 second (10 loops)
        if (log_counter % 10 == 0) {
            int light_percent = (int)((1.0 - (float)system_state.light_value / 4095.0) * 100);
            ESP_LOGI(TAG, "Sensors: ADC=%d, Light=%d%%, Motion=%s, Lamp=%s, Mode=%s",
                     system_state.light_value,
                     light_percent,
                     system_state.motion_detected ? "YES" : "NO",
                     system_state.is_light_on ? "ON" : "OFF",
                     system_state.is_auto_mode ? "Auto" : "Manual");
        }
        log_counter++;
        
        // Auto Mode Logic
        if (system_state.is_auto_mode) {
            // Note: Photoresistor is grounded (pulldown configuration). 
            // Strong light = Low ADC value, Weak light = High ADC value.
            bool should_light_on = (system_state.light_value > LIGHT_THRESHOLD) && 
                                   system_state.motion_detected;
            
            if (should_light_on && !system_state.is_light_on) {
                turn_on_light();
            } else if (!should_light_on && system_state.is_light_on) {
                turn_off_light();
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay
    }
}

// ==================== Initialization Functions ====================

void hardware_init(void)
{
    // Configure GPIO
    gpio_config_t io_conf = {};
    
    // PIR Sensor Input (Enable pull-down to prevent floating)
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << PIR_SENSOR_PIN);
    io_conf.pull_down_en = 1;  // Enable internal pull-down
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    
    // Relay Output
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << RELAY_PIN);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    
    // Initialize Relay to OFF
    gpio_set_level(RELAY_PIN, 0);
    
    // Configure ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(LIGHT_SENSOR_PIN, ADC_ATTEN_DB_11);
    
    // ADC Calibration
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 
                             1100, adc_chars);
    
    ESP_LOGI(TAG, "Hardware initialization complete");
}

// ==================== Main Function ====================

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Smart Lighting System Starting");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Hardware Init
    hardware_init();
    
    // WiFi Init
    wifi_init_sta();
    
    // Start Web Server
    server = start_webserver();
    
    // Create Sensor Task
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
    
    // Create Data Push Task
    xTaskCreate(data_push_task, "data_push_task", 8192, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "System initialization complete, starting operation");
}
