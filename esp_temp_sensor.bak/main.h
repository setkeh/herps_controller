#include "os_type.h"

#define DEBUG 1

typedef struct {
  uint8_t state; /**< Led State */
} blink_packet;

typedef enum led_stste_enum {
	LED_STATE_LOW = 0,
	LED_STATE_HIGH = 1,
} LED_STATE;

extern os_timer_t tcpTimer;
extern os_timer_t pingTimer;
extern os_timer_t pubTimer;

void reverse(char *str, int len);
int intToStr(int x, char str[], int d);
void ftoa(float n, char *res, int afterpoint);

void ICACHE_FLASH_ATTR con(void *arg);
void ICACHE_FLASH_ATTR pubuint(void *arg);
void ICACHE_FLASH_ATTR pubfloat(void *arg);
void ICACHE_FLASH_ATTR sub(void *arg);
void ICACHE_FLASH_ATTR ping(void *arg);
void ICACHE_FLASH_ATTR discon(void *arg);

void ICACHE_FLASH_ATTR user_init();
