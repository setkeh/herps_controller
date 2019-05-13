/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/********************************************************************************/
/* Some portions of this file were taken from the NodeMCU firmware project,     */
/* at https://github.com/nodemcu/nodemcu-firmware. The original author is       */
/* listed as "zeroday nodemcu.com". The code was licensed under the MIT	        */
/* license, which allows me to relicense it under the MPL, which I have done.   */
/********************************************************************************/

/**
 * @file
 * @brief This is the main header file for all MQTT/TCP related functions.
 */

#include "user_interface.h"
#include "ets_sys.h"
#include "osapi.h"
#include "espconn.h"
#include "os_type.h"

#define DEBUG 1 /**< This define enables or disables serial debug output in most places */

/**
 * @typedef
 * This is the enum for message types.
 *
 * This enum was taken from the NodeMCU mqtt headers. Credit is given to "zeroday" of nodemcu.com. This enum gives us both easily readable names for each message type, but also the correct value for each type. These are used in mqttSend() to construct the correct packet type.
 */
typedef enum mqtt_message_enum {
    MQTT_MSG_TYPE_CONNECT = 1,
    MQTT_MSG_TYPE_CONNACK = 2,
    MQTT_MSG_TYPE_PUBLISH = 3,
    MQTT_MSG_TYPE_PUBACK = 4,
    MQTT_MSG_TYPE_PUBREC = 5,
    MQTT_MSG_TYPE_PUBREL = 6,
    MQTT_MSG_TYPE_PUBCOMP = 7,
    MQTT_MSG_TYPE_SUBSCRIBE = 8,
    MQTT_MSG_TYPE_SUBACK = 9,
    MQTT_MSG_TYPE_UNSUBSCRIBE = 10,
    MQTT_MSG_TYPE_UNSUBACK = 11,
    MQTT_MSG_TYPE_PINGREQ = 12,
    MQTT_MSG_TYPE_PINGRESP = 13,
    MQTT_MSG_TYPE_DISCONNECT = 14
} mqtt_message_type;

/**
 * @struct mqtt_packet_t
 * A structure which contains all the information for an MQTT packet.
 *
 * This structure contains pointers to the three main parts of an MQTT packet: the fixed header, the variable header, and the payload. Not all message types contain a variable header or payload, but *all* message types have a fixed header.
 */
typedef struct {
    uint8_t *fixedHeader; /**< The pointer to the fixed header bytes */
    uint32_t fixedHeader_len; /**< The length of the fixed header */
    uint8_t *varHeader; /**< The pointer to the variable header bytes */
    uint32_t varHeader_len; /**< The length of the variable header */
    uint8_t *payload; /**< The pointer to the payload bytes */
    uint32_t payload_len; /**< The length of the payload */
    uint32_t length; /**< The full length of the packet */
    mqtt_message_type msgType; /**< What message type this packet contains */
} mqtt_packet_t;

/**
 * @struct mqtt_session_t
 * Structure that contains all the information to establish and maintain a TCP-based MQTT connection to the broker
 */
typedef struct {
    uint8_t ip[4]; /**< An array containing the four bytes of the IP address of the broker */
    uint32_t port; /**< The port the broker is listening on */
    uint32_t localPort; /**< The local port returned by the ESP8266 function espconn_port() */
    uint8_t *client_id; /**< Pointer to the client ID string */
    uint32_t client_id_len; /**< Length of the client ID string */
    uint8_t *topic_name; /**< Pointer to the topic name string */
    uint32_t topic_name_len; /**< Length of the topic name string */
    uint8_t qos_level; /**< QOS level of the MQTT connection (always 0, not currently used) */
    uint8_t *username; /**< Pointer to the username string, for brokers which require authentication */
    uint32_t username_len; /**< The length of the username string */
    uint8_t *password; /**< Pointer to the password string, for brokers which require password authentication */
    uint32_t password_len; /**< The length of the password string */
    char validConnection; /**< Boolean value which indicates whether or not the TCP connection is established, set in tcpConnect() */
    struct espconn *activeConnection; /**< A pointer to the espconn structure containing details of the TCP connection */
    void *userData; /**< Used to pass data to the PUBLISH function */
    // Add pointers to user callback functions
    void (*publish_cb)(void *arg); /**< Pointer to user callback function for publish */
    void (*connack_cb)(void *arg); /**< Pointer to user callback function for connack */
} mqtt_session_t;

/**
 * A function which initiates the connection to the TCP server.
 * @param arg A pointer to void, so it can be called from a timer or task
 * @return 0 on success
 */
uint8_t ICACHE_FLASH_ATTR tcpConnect(void *arg);
void ICACHE_FLASH_ATTR disconnected_callback(void *arg);
void ICACHE_FLASH_ATTR reconnected_callback(void *arg, sint8 err);

/**
 * A callback function which is called when TCP connection is successful.
 * @param *arg A pointer to void, which needs to be cast to espconn *pConn
 * @return Void
 *
 * This function is called once the TCP connection to the server is completed. This allows us to register the callbacks for received and sent data, as well as enable TCP keepalive and set validConnection in mqtt_session_t to 1 to show the connection has been successful.
 */
void ICACHE_FLASH_ATTR connected_callback(void *arg);

/**
 * A callback function that deals with received data.
 * @param *arg A pointer to void, which needs to be cast to espconn *pConn
 * @param *pdata A pointer to the received data blob
 * @param len The length of *pdata
 * @return Void
 *
 * At this point the function does little more than detect what type of MQTT message is sent, and prints out debug info to the serial port. The next few versions should improve this so the user can register their own callback functions which will be called when the particular message type is receieved.
 */
void ICACHE_FLASH_ATTR data_recv_callback(void *arg, char *pdata, unsigned short len);
void ICACHE_FLASH_ATTR data_sent_callback(void *arg);
void ICACHE_FLASH_ATTR pingAlive(void *arg);

/**
 * A function which takes a uint32_t and returns a pointer to a properly formatted MQTT length.
 * @param trueLength the length in bytes
 * @return encoded multi-byte number per MQTT standards. The first byte this pointer
 * returns is the number of bytes to follow.

 * The MQTT standard uses a very strange method of encoding the lengths of the various sections
 * of the packets. This makes it simpler to implement in code.
 */
uint8_t ICACHE_FLASH_ATTR *encodeLength(uint32_t trueLength);

/**
 * This function handles all the sending of various MQTT messages.
 * @param session a pointer to the active mqtt_session_t
 * @param data a pointer to the data to be published; only applied to MQTT_MSG_TYPE_PUBLISH
 * @param len the length of the above data
 * @param msgType the type of message to be sent, one of the mqtt_message_type
 * @return -1 in case of error, 0 otherwise
 */
uint8_t ICACHE_FLASH_ATTR mqttSend(mqtt_session_t *session, uint8_t *data, uint32_t len, mqtt_message_type msgType);
