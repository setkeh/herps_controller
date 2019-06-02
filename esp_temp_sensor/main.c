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
#include "main.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pb_common.h"
#include "protobuff.pb.h"

void ICACHE_FLASH_ATTR user_pre_init(void);
struct espconn esp_conn;
static const int blink_pin = 2;
static volatile os_timer_t blink_timer;

os_timer_t wifi_timer;

void ICACHE_FLASH_ATTR
protobuff_test(void *arg) {
  uint8_t buffer[128];
  size_t message_length;
  bool status;
  
  {
    EnvironmentMessage message = EnvironmentMessage_init_zero;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    message.DeviceID = "TestDevice1234";
    message.Humidity = 89.74;
    message.Temperature = 34.2;

    status = pb_encode(&stream, EnvironmentMessage_fields, &message);
    message_length = stream.bytes_written;
        
      /* Then just check for any errors.. */
      if (!status)
      {
        printf("Encoding failed: %s\n", PB_GET_ERROR(&stream));
        return 1;
      }
  }
  
  {
        /* Allocate space for the decoded message. */
        EnvironmentMessage message = EnvironmentMessage_init_zero;
        
        /* Create a stream that reads from the buffer. */
        pb_istream_t stream = pb_istream_from_buffer(buffer, message_length);
        
        /* Now we are ready to decode the message. */
        status = pb_decode(&stream, EnvironmentMessage_fields, &message);
        
        /* Check for errors... */
        if (!status)
        {
            printf("Decoding failed: %s\n", PB_GET_ERROR(&stream));
            return 1;
        }
        
        /* Print the data contained in the message. */
        printf("DeviceID was %s!\n", message.DeviceID);
        printf("Humidity number was %d!\n", (int)message.Humidity);
        printf("Temperature number was %d!\n", (int)message.Temperature);
  }
    
  return 0;
}


void ICACHE_FLASH_ATTR
wifi_timer_cb(void *arg) {

  os_timer_disarm(&wifi_timer);

  uint8 status;
  struct ip_info ipconfig;
  status = wifi_station_get_connect_status();

  wifi_get_ip_info(STATION_IF, &ipconfig);

  if (status == STATION_GOT_IP && ipconfig.ip.addr != 0) {
    protobuff_test(arg);
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