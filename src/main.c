/**
 */

#include "driver/uart.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_smartconfig.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h" 
#include "freertos/semphr.h"

#define TAG "SETUP"

SemaphoreHandle_t sem = xSemaphoreCreateBinary();

static void STA_start_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  ESP_LOGI("TAG", "WIFI Start event");
  ESP_ERROR_CHECK( esp_wifi_connect() );  // try to connect
}

static void STA_disconnected_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  ESP_LOGI(TAG, "STA disconnected event");
  int &retry = *((int*)arg);
  if (retry) {
    const auto* evt = (wifi_event_sta_disconnected_t*)event_data;
    ESP_LOGI(TAG, "Disconnected from %s", evt->ssid);
    ESP_LOGI(TAG, "Reason: %d", evt->reason);

    ESP_LOGI("TAG", "WIFI STA disconnected event, retry %d", retry--);
    printf("Connection WI-FI perdue, reconnection (%d/3)...\n", 3-retry);
    ESP_ERROR_CHECK( esp_wifi_connect() );  // try to reconnect
  } else {
    ESP_LOGI("TAG", "Starting SmartConfig");
    printf("Lancement de SmartConfig...\n");
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t smart_cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&smart_cfg) );  
  }
}

static void SC_got_ssid_pass_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  ESP_LOGI(TAG, "Got SSID and password event");
  const auto *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
  ESP_LOGI(TAG, "SSID: %s", evt->ssid);
  ESP_LOGI(TAG, "Pass: %s", evt->password);

  wifi_config_t wifi_config = { 0 };
  strncpy((char*)wifi_config.sta.ssid, (char*)evt->ssid, sizeof(wifi_config.sta.ssid));
  strncpy((char*)wifi_config.sta.password, (char*)evt->password, sizeof(wifi_config.sta.password));
  
  ESP_ERROR_CHECK( esp_wifi_disconnect() );
  ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
  ESP_LOGI(TAG, "Connect to %s", (char*)evt->ssid);
  ESP_ERROR_CHECK( esp_wifi_connect() );
}

static void IP_STA_got_ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  ESP_LOGI(TAG, "Got IP event");
  auto *ip_info = (esp_netif_ip_info_t*)arg;
  const auto *evt = (ip_event_got_ip_t *)event_data;
  ESP_LOGI(TAG, "IP " IPSTR, IP2STR(&(evt->ip_info.ip)));
  memcpy(ip_info, &evt->ip_info, sizeof(esp_netif_ip_info_t));

  xSemaphoreGive(sem);
}

/**
 * @see esp-idf/components/esp_wifi/include/esp_wifi_types_generic.h
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  assert(event_base == WIFI_EVENT);
  switch (event_id) {
    case WIFI_EVENT_STA_START:            // 2
      ESP_LOGI(TAG, "Event STA start");
      break;
    case WIFI_EVENT_STA_CONNECTED:        // 4
      ESP_LOGI(TAG, "STA connected.");
      break;
    case WIFI_EVENT_STA_DISCONNECTED:     // 5
      ESP_LOGE(TAG,"STA disconnected!");
      break;
    case WIFI_EVENT_HOME_CHANNEL_CHANGE:  // 43
      ESP_LOGI(TAG, "Home channel change.");
      break;
    default:
      ESP_LOGI(TAG, "WIFI_EVENT: %d", event_id);
  }
}

/**
 * @see esp-idf/components/esp_netif/include/esp_netif_types.h
 */
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  assert(event_base == IP_EVENT);
  switch (event_id) {
    case IP_EVENT_STA_GOT_IP:
      ESP_LOGI(TAG, "Got IP!");
      break;
    default:
      ESP_LOGI(TAG, "IP_EVENT: %d", event_id);
  }
}



static void sc_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  assert(event_base == SC_EVENT);
  switch (event_id) {
    case SC_EVENT_SCAN_DONE:                /*!< Station smartconfig has finished to scan for APs */
      ESP_LOGI(TAG, "SC_EVENT: finished to scan for APs");
      break;
    case SC_EVENT_FOUND_CHANNEL:            /*!< Station smartconfig has found the channel of the target AP */
      ESP_LOGI(TAG, "SC_EVENT: found the channel for target AP");
      break;
    case SC_EVENT_GOT_SSID_PSWD:            /*!< Station smartconfig got the SSID and password */
      ESP_LOGI(TAG, "SC_EVENT: get SSID and password");
      break;
    case SC_EVENT_SEND_ACK_DONE:            /*!< Station smartconfig has sent ACK to cellphone */
      ESP_LOGI(TAG, "SC_EVENT: has sent ACK to cellphone");
      break;
    default:
      ESP_LOGI(TAG, "SC_EVENT: %d", event_id);
  }
}


void setup() {
  ESP_ERROR_CHECK( esp_event_loop_create_default() );
  esp_rom_delay_us(2000000UL);

  printf("\n\n" __FILE__);
  printf(" - copyright (c) MSibert - 2025\n");


  ESP_LOGI(TAG, "esp_netif_init");
  ESP_ERROR_CHECK(esp_netif_init());
  
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);

  wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK( esp_wifi_init(&wifi_cfg) );

  ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, &STA_start_event_handler, NULL) );

  static int retry = 3;
  ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &STA_disconnected_event_handler, &retry) );

  ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, SC_EVENT_GOT_SSID_PSWD, &SC_got_ssid_pass_event_handler, NULL) );

  static esp_netif_ip_info_t ip_info;
  ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &IP_STA_got_ip_event_handler, &ip_info) );

#if 0
  ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL) );
  ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL) );
  ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &sc_event_handler, NULL) );
#endif

  ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );

  ESP_LOGI(TAG, "ESP WiFi Start");
  ESP_ERROR_CHECK( esp_wifi_start() );

  if (!xSemaphoreTake(sem, portMAX_DELAY)) {
    ESP_LOGI(TAG, "Error xSemaphoreTake");
    abort();
  }

  ESP_LOGI(TAG, "Get IP info");
  // esp_netif_ip_info_t ip_info;
  // esp_err_t err = esp_netif_get_ip_info(sta_netif, &ip_info);
  // ESP_ERROR_CHECK(err);
  ESP_LOGI(TAG, "IP " IPSTR, IP2STR(&(ip_info.ip)));


  esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("fr.pool.ntp.org");
  sntp_config.start = true;
  esp_netif_sntp_init(&sntp_config);
  auto status = sntp_get_sync_status();
  ESP_LOGI(TAG, "SNTP status: %d", (int)status);
  
}

void loop() {
  time_t now;
  time(&now);

  struct tm *utc = gmtime(&now);  // conversion en UTC

  char iso8601[30];
  strftime(iso8601, sizeof(iso8601), "%Y-%m-%dT%H:%M:%SZ", utc);
  
  ESP_LOGI(TAG, "The current date/time: %s", iso8601);
  

}
