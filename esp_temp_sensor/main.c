#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "mem.h"
#include "uart.h"
#include "user_interface.h"
#include "user_config.h"
//#include "blink.h"
#include "wifi.h"
#include "espconn.h"
#include "mqtt.h"
#include "main.h"

void ICACHE_FLASH_ATTR user_pre_init(void);
struct espconn esp_conn;
static const int blink_pin = 2;
static volatile os_timer_t blink_timer;

os_timer_t wifi_timer;
os_timer_t tcpTimer;
os_timer_t pingTimer;
os_timer_t pubTimer;

static const char ioTopic[4] = { 0x74, 0x65, 0x73, 0x74 }; // test
static const uint8_t ioTopic_len = 4;
static const char mqtt_ip[4] = {10, 0, 81, 146};

void ICACHE_FLASH_ATTR ping(void *arg) {
#ifdef DEBUG
    os_printf("Entered ping!\n");
#endif
    mqtt_session_t *pSession = (mqtt_session_t *)arg;
    mqttSend(pSession, NULL, 0, MQTT_MSG_TYPE_PINGREQ);
    os_timer_disarm(&pingTimer);
    os_timer_setfn(&pingTimer, (os_timer_func_t *)ping, arg);
    os_timer_arm(&pingTimer, 5000, 0);
}

void ICACHE_FLASH_ATTR con(void *arg) {
#ifdef DEBUG
    os_printf("Entered con!\n");
#endif
    mqtt_session_t *pSession = (mqtt_session_t *)arg;
    mqttSend(pSession, NULL, 0, MQTT_MSG_TYPE_CONNECT);

    os_timer_disarm(&pingTimer);
    os_timer_setfn(&pingTimer, (os_timer_func_t *)ping, arg);
    os_timer_arm(&pingTimer, 5000, 0);
}

void ICACHE_FLASH_ATTR sub(void *arg) {
#ifdef DEBUG
    os_printf("Entered sub!\n");
#endif
    mqtt_session_t *pSession = (mqtt_session_t *)arg;
    mqttSend(pSession, NULL, 0, MQTT_MSG_TYPE_SUBSCRIBE);

}

void ICACHE_FLASH_ATTR discon(void *arg) {
#ifdef DEBUG
    os_printf("Entered discon!\n");
#endif
    mqtt_session_t *pSession = (mqtt_session_t *)arg;
    mqttSend(pSession, NULL, 0, MQTT_MSG_TYPE_DISCONNECT);
    os_timer_disarm(&pingTimer);
    os_timer_disarm(&pubTimer);
}

void ICACHE_FLASH_ATTR pubuint(void *arg) {
#ifdef DEBUG
    os_printf("Entered pubuint!\n");
#endif
    mqtt_session_t *pSession = (mqtt_session_t *)arg;
    uint8_t *data = (uint8_t *)(pSession->userData);
    char *dataStr = os_zalloc(20 * sizeof(char));
    intToStr(*data, dataStr, 4);
    int32_t dataLen = os_strlen(dataStr);
    mqttSend(pSession, (uint8_t *)dataStr, dataLen, MQTT_MSG_TYPE_PUBLISH);
    os_timer_disarm(&pingTimer);
    os_timer_setfn(&pingTimer, (os_timer_func_t *)ping, arg);
    os_timer_arm(&pingTimer, 5000, 0);
}

void ICACHE_FLASH_ATTR pubfloat(void *arg) {
#ifdef DEBUG
    os_printf("Entered pubfloat!\n");
#endif
    mqtt_session_t *pSession = (mqtt_session_t *)arg;
    float *data = (float *)(pSession->userData);
    char *dataStr = os_zalloc(20 * sizeof(char));
    ftoa(*data, dataStr, 2);
    int32_t dataLen = os_strlen(dataStr);
#ifdef DEBUG
    os_printf("Encoded string: %s\tString length: %d\n", dataStr, dataLen);
#endif
    mqttSend(pSession, (uint8_t *)dataStr, dataLen, MQTT_MSG_TYPE_PUBLISH);
    os_timer_disarm(&pingTimer);
    os_timer_setfn(&pingTimer, (os_timer_func_t *)ping, arg);
    os_timer_arm(&pingTimer, 5000, 0);
}

blink_timerfunc(void *arg, int val)
{
  wifi_get_ip_info(0, &info);
  blink_packet Data;
  blink_packet *pData = &Data;
  mqtt_session_t *pSession = (mqtt_session_t *)arg;
  pData->state += (uint8_t)0;

  //Do blinky stuff
  if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & (1 << blink_pin))
  {
    // set gpio low
    gpio_output_set(0, (1 << blink_pin), 0, 0);
    pData->state += LED_STATE_LOW;
    #ifdef DEBUG
      os_printf("LED state - %d - %s IP: %d.%d.%d.%d\n", pData->state, "LOW", IP2STR(&info.ip.addr));
    #endif
    pSession->userData = (void *)&pData->state;
    pubuint(pSession);
    return;
  }
  else
  {
    // set gpio high
    gpio_output_set((1 << blink_pin), 0, 0, 0);
    #ifdef DEBUG
      os_printf("LED state - %d - %s IP: %d.%d.%d.%d\n", pData->state, "HIGH", IP2STR(&info.ip.addr));
    #endif
    pData->state += LED_STATE_HIGH;
    pSession->userData = (void *)&pData->state;
    pubuint(pSession);
    return;
  }
}

void ICACHE_FLASH_ATTR
init_mqtt(void) {
  os_printf("Entering MQTT Init");
  LOCAL mqtt_session_t globalSession;
  LOCAL mqtt_session_t *pGlobalSession = &globalSession;
  pGlobalSession->port = 1883; // mqtt port
  os_memcpy(pGlobalSession->ip, mqtt_ip, 4);
  /*pGlobalSession->topic_name_len = ioTopic_len;
  pGlobalSession->topic_name = os_zalloc(sizeof(uint8_t) * pGlobalSession->topic_name_len);*/
  pGlobalSession->topic_name_len = 4;
  pGlobalSession->topic_name = "test";
  os_memcpy(pGlobalSession->topic_name, /*ioTopic*/"test", pGlobalSession->topic_name_len);
  os_printf("MQTT Memory Opts Set");

  os_printf("Arm the TCP timer\n");
  os_timer_setfn(&tcpTimer, (os_timer_func_t *)tcpConnect, pGlobalSession);
  os_timer_arm(&tcpTimer, 12000, 0);
  os_printf("Arm Ping timer\n");
  os_timer_setfn(&pingTimer, (os_timer_func_t *)con, pGlobalSession);
  os_timer_arm(&pingTimer, 16000, 0);

  // setup blink timer (500ms, repeating)
  os_printf("Arm Blink Timer\n");
  os_timer_setfn(&blink_timer, (os_timer_func_t *)blink_timerfunc, NULL);
  os_timer_arm(&blink_timer, 20000, 1);
}

void ICACHE_FLASH_ATTR
wifi_timer_cb(void *arg) {

  os_timer_disarm(&wifi_timer);

  uint8 status;
  struct ip_info ipconfig;
  status = wifi_station_get_connect_status();

  wifi_get_ip_info(STATION_IF, &ipconfig);

  if (status == STATION_GOT_IP && ipconfig.ip.addr != 0) {
    init_mqtt();
    return;
  } else {
    os_timer_arm(&wifi_timer, 2000, 1);
  }
}

void ICACHE_FLASH_ATTR 
user_init()
{
  uart_div_modify(0, UART_CLK_FREQ / 115200);
  system_set_os_print(TRUE);

  wifi_init();

  // init gpio subsytem
  gpio_init();
  
  // configure UART TXD to be GPIO1, set as output
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2); 

  os_timer_disarm(&wifi_timer); 
  os_timer_setfn(&wifi_timer, wifi_timer_cb, NULL); /* Set callback for timer */
  os_timer_arm(&wifi_timer, 2000 , 1);
}