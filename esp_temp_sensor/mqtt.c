// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <stdint.h>
#include <math.h>
#include "user_interface.h"
#include "ets_sys.h"
#include "osapi.h"
#include "mem.h"
#include "gpio.h"
#include "espconn.h"
#include "os_type.h"
#include "mqtt.h"
#include "main.h"

/* Functions we will need to implement:
 * Send -- will handle all sending of all packets
 * Connect -- set up TCP connection and parameters
 * Publish -- send message to server
 * Subscribe -- we probably won't need this
 * We just want to connect, and publish info. We don't care about
 * security or QoS in this basic implementation.
 */

static os_timer_t MQTT_KeepAliveTimer;
static os_timer_t waitForWifiTimer;

// reverses a string 'str' of length 'len'
void reverse(char *str, int len) {
    int i=0, j=len-1, temp;
    while (i<j) {
        temp = str[i];
        str[i] = str[j];
        str[j] = temp;
        i++;
        j--;
    }
}

// Converts a given integer x to string str[].  d is the number
// of digits required in output. If d is more than the number
// of digits in x, then 0s are added at the beginning.
int intToStr(int x, char str[], int d) {
    int i = 0;
    while (x) {
        str[i++] = (x%10) + '0';
        x = x/10;
    }

    // If number of digits required is more, then
    // add 0s at the beginning
    while (i < d)
        str[i++] = '0';

    reverse(str, i);
    str[i] = '\0';
    return i;
}

// Converts a floating point number to string.
void ftoa(float n, char *res, int afterpoint) {
    // Extract integer part
    int ipart = (int)n;

    // Extract floating part
    float fpart = n - (float)ipart;

    // convert integer part to string
    int i = intToStr(ipart, res, 0);

    // check for display option after point
    if (afterpoint != 0) {
        res[i] = '.';  // add dot

        // Get the value of fraction part upto given no.
        // of points after dot. The third parameter is needed
        // to handle cases like 233.007
        fpart = fpart * pow(10, afterpoint);

        intToStr((int)fpart, res + i + 1, afterpoint);
    }
}

void ICACHE_FLASH_ATTR data_sent_callback(void *arg) {
#ifdef DEBUG
    os_printf("Data sent!\n");
#endif
    return;
}

void ICACHE_FLASH_ATTR data_recv_callback(void *arg, char *pdata, unsigned short len) {
    struct espconn *pConn = arg;
    mqtt_session_t *session = pConn->reverse;
    // deal with received data
#ifdef DEBUG
    os_printf("Received data of length %d -- %s \r\n", len, pdata);
    os_printf("Hex dump: ");
    for(int i = 0; i < len; i++) {
        os_printf("(%d): %x ", i, pdata[i]);
    }
    os_printf("\n");
#endif
    mqtt_message_type msgType = ((mqtt_message_type)pdata[0] >> 4) & 0x0F;
    switch(msgType) {
        case MQTT_MSG_TYPE_CONNACK:
            os_printf("CONNACK recieved...\n");
            switch(pdata[3]) {
                case 0:
                    os_printf("Connection accepted.\n");
                    break;
                case 1:
                    os_printf("Connection refused -- incorrect protocol version.\n");
                    break;
                case 2:
                    os_printf("Connection refused -- illegal identifier.\n");
                    break;
                case 3:
                    os_printf("Connection refused -- broker offline or not available.\n");
                    break;
                case 4:
                    os_printf("Connection refused -- bad username or password.\n");
                    break;
                case 5:
                    os_printf("Connection refused -- not authorized.\n");
                    break;
                default:
                    os_printf("Connection refused -- illegal CONNACK return code.\n");
                    break;
            }
            if(session->connack_cb != NULL) {
                session->connack_cb(pdata);
            }
            break;
        case MQTT_MSG_TYPE_PUBLISH:
            os_printf("Application message from server: %s\n", &pdata[3]); // probably incorrect
            if(session->publish_cb != NULL) {
                session->publish_cb(pdata);
            }
            break;
        case MQTT_MSG_TYPE_SUBACK:
            os_printf("Subscription acknowledged\n");
            break;
        case MQTT_MSG_TYPE_UNSUBACK:
            os_printf("Unsubscription acknowledged\n");
            break;
        case MQTT_MSG_TYPE_PINGRESP:
            os_printf("Pong!\n");
            break;
        // all remaining cases listed to avoid warnings
        case MQTT_MSG_TYPE_CONNECT:
        case MQTT_MSG_TYPE_PUBACK:
        case MQTT_MSG_TYPE_PUBREC:
        case MQTT_MSG_TYPE_PUBREL:
        case MQTT_MSG_TYPE_PUBCOMP:
        case MQTT_MSG_TYPE_SUBSCRIBE:
        case MQTT_MSG_TYPE_UNSUBSCRIBE:
        case MQTT_MSG_TYPE_PINGREQ:
        case MQTT_MSG_TYPE_DISCONNECT:
        default:
            if(msgType == MQTT_MSG_TYPE_DISCONNECT) {
                os_printf("MQTT Disconnect packet\n");
            }
            return;
            break;

    }
}

void ICACHE_FLASH_ATTR connected_callback(void *arg) {
    struct espconn *pConn = arg;
    mqtt_session_t *pSession = pConn->reverse;
#ifdef DEBUG
    os_printf("Connected callback\n");
#endif
    espconn_regist_recvcb(pConn, (espconn_recv_callback)data_recv_callback);
    espconn_regist_sentcb(pConn, (espconn_sent_callback)data_sent_callback);
    // enable keepalive
    espconn_set_opt(pConn, ESPCONN_KEEPALIVE);
    pSession->validConnection = 1;
}

void ICACHE_FLASH_ATTR reconnected_callback(void *arg, sint8 err) {
    os_printf("Reconnected?\n");
    os_printf("Error code: %d\n", err);
}

void ICACHE_FLASH_ATTR disconnected_callback(void *arg) {
    os_printf("Disconnected\n");
    os_timer_disarm(&pubTimer);
    os_timer_disarm(&pingTimer);
}

uint8_t ICACHE_FLASH_ATTR tcpConnect(void *arg) {
    os_timer_disarm(&waitForWifiTimer);
    struct ip_info ipConfig;
    mqtt_session_t *session = arg;
    wifi_get_ip_info(STATION_IF, &ipConfig);
    if (wifi_station_get_connect_status() == STATION_GOT_IP && ipConfig.ip.addr != 0) {
        LOCAL struct espconn conn;
        LOCAL struct _esp_tcp tcp_s;
        conn.reverse = arg;
#ifdef DEBUG
        os_printf("Entered tcpConnect\n");
#endif
        wifi_get_ip_info(STATION_IF, &ipConfig);
        // set up basic TCP connection parameters
#ifdef DEBUG
        os_printf("about to set up TCP params\n");
#endif
        conn.proto.tcp = &tcp_s;
        conn.type = ESPCONN_TCP;
        conn.proto.tcp->local_port = espconn_port();
        conn.proto.tcp->remote_port = session->port;
        conn.state = ESPCONN_NONE;
        os_memcpy(conn.proto.tcp->remote_ip, session->ip, 4);
#ifdef DEBUG
        os_printf("About to register callbacks\n");
#endif
        // register callbacks
        espconn_regist_connectcb(&conn, (espconn_connect_callback)connected_callback);
        espconn_regist_reconcb(&conn, (espconn_reconnect_callback)reconnected_callback);
        espconn_regist_disconcb(&conn, (espconn_connect_callback)disconnected_callback);
#ifdef DEBUG
        os_printf("About to connect\n");
#endif
        //make the connection
        if(espconn_connect(&conn) == 0) {
            os_printf("Connection successful\n");
        } else {
            os_printf("Connection error\n");
        }
        session->activeConnection = &conn;
        sub(arg);
#ifdef DEBUG
        os_printf("About to return from TCP connect\n");
#endif
        return 0;
    } else {
        // set timer to try again
        os_timer_setfn(&waitForWifiTimer, (os_timer_func_t *)tcpConnect, session);
        os_timer_arm(&waitForWifiTimer, 1000, 0);
        return 2;
    }
    return 0;
}


uint8_t ICACHE_FLASH_ATTR *encodeLength(uint32_t trueLength) {
    uint8_t *encodedByte = os_zalloc(sizeof(uint8_t) * 5); // can't be more than 5 bytes
    uint8_t numBytes = 1;
    do {
        encodedByte[numBytes] = trueLength % 128;
        trueLength /= 128;
        if(trueLength > 0) {
            encodedByte[numBytes] |= 128;
        }
        numBytes++;
    } while(trueLength > 0);
    encodedByte[0] = numBytes - 1;

    return encodedByte;

}

void ICACHE_FLASH_ATTR pingAlive(void *arg) {
    mqtt_session_t *pSession = (mqtt_session_t *)arg;
    mqttSend(pSession, NULL, 0, MQTT_MSG_TYPE_PINGREQ);
}

uint8_t ICACHE_FLASH_ATTR mqttSend(mqtt_session_t *session, uint8_t *data, uint32_t len, mqtt_message_type msgType) {
    if(session->validConnection == 1) {
        os_timer_disarm(&MQTT_KeepAliveTimer); // disable timer if we are called
#ifdef DEBUG
        os_printf("Entering mqttSend!\n");
#endif
        LOCAL mqtt_packet_t packet;
        LOCAL mqtt_packet_t *pPacket = &packet;
        uint8_t *fullPacket;
        uint8_t *remaining_len_encoded;
        switch(msgType) {
            case MQTT_MSG_TYPE_CONNECT: {
                const uint8_t varDefaults[10] = { 0x00, 0x04, 0x4D, 0x51, 0x54, 0x54, 0x04, 0xC2, 0x00, 0x32 }; // variable header is always the same for Connect
                pPacket->fixedHeader = (uint8_t *)os_zalloc(sizeof(uint8_t) * 5); // fixed header cannot be longer than 5
                pPacket->fixedHeader[0] = ((uint8_t)msgType << 4) & 0xF0; // make sure lower 4 are clear
                // prepare variable header
                pPacket->varHeader = (uint8_t *)os_zalloc(sizeof(uint8_t) * 10); // 10 bytes for connect
                os_memcpy(pPacket->varHeader, varDefaults, 10); // copy defaults
                pPacket->varHeader_len = 10;
                // prepare payload
                uint32_t offset = 0; // keep track of how many bytes we have copied
                uint32_t maxPayloadLength = session->client_id_len + session->username_len + session->password_len + 12; // length info can only take a maximum of 12 bytes
                pPacket->payload = (uint8_t *)os_zalloc(sizeof(uint8_t) * maxPayloadLength);
                // copy client id to payload: 2 bytes for size, then client id.
                // insert a 0
                os_memset(pPacket->payload + offset, 0, 1); // MSB is always 0
                offset += 1;
                os_memcpy(pPacket->payload + offset, &session->client_id_len, 1); // LSB is length of string
                offset += 1;
                os_memcpy(pPacket->payload + offset, session->client_id, session->client_id_len); // and copy string
                offset += session->client_id_len;
                // copy username to payload: same as client id, 2 bytes for size, then username
                // insert a 0
                os_memset(pPacket->payload + offset, 0, 1);
                offset += 1;
                os_memcpy(pPacket->payload + offset, &session->username_len, 1);
                offset += 1;
                os_memcpy(pPacket->payload + offset, session->username, session->username_len);
                offset += session->username_len;
                // Password: same as username and client id, 2 bytes for size, then password
                // insert a 0
                os_memset(pPacket->payload + offset, 0, 1);
                offset += 1;
                os_memcpy(pPacket->payload + offset, &session->password_len, 1);
                offset += 1;
                os_memcpy(pPacket->payload + offset, session->password, session->password_len);
                offset += session->password_len;
                pPacket->payload_len = offset; // the length of the payload is the same as our offset
#ifdef DEBUG
                os_printf("Total offset: %d\n", offset);
#endif
                // the remaining length is the size of the packet, minus the first byte and the bytes taken by the remaining length bytes themselves
                // they are encoded per the MQTT spec, section 2.2.3
                // the first byte returned by encodeLength is the number of bytes to follow, as it can be anywhere from 1 to 4 bytes to encode the length
                remaining_len_encoded = encodeLength((uint32_t)(pPacket->varHeader_len + pPacket->payload_len));
                os_memcpy(pPacket->fixedHeader + 1, remaining_len_encoded + 1, remaining_len_encoded[0]);
                pPacket->fixedHeader_len = remaining_len_encoded[0] + 1; // fixed header length is the number of bytes used to encode the remaining length, plus 1 for the fixed CONNECT byte
                // the full length is the length of the varHeader, payload, and fixedHeader
                pPacket->length = (pPacket->varHeader_len + pPacket->payload_len + pPacket->fixedHeader_len);
#ifdef DEBUG
                os_printf("Packet length: %d\n", pPacket->length);
#endif
                // construct packet data
                fullPacket = (uint8_t *)os_zalloc(sizeof(uint8_t) * pPacket->length);
                os_memcpy(fullPacket, pPacket->fixedHeader, pPacket->fixedHeader_len);
                os_memcpy(fullPacket + pPacket->fixedHeader_len, pPacket->varHeader, pPacket->varHeader_len);
                os_memcpy(fullPacket + pPacket->fixedHeader_len + pPacket->varHeader_len, pPacket->payload, pPacket->payload_len);
                break;
            }
            case MQTT_MSG_TYPE_PUBLISH: {
                // prepare for publish
                pPacket->fixedHeader = (uint8_t *)os_zalloc(sizeof(uint8_t) * 5);
                pPacket->fixedHeader[0] = (MQTT_MSG_TYPE_PUBLISH << 4) & 0xF0; // clear lower 4 bits, we don't need DUP, QOS or RETAIN

                // variable header
                // A PUBLISH Packet MUST NOT contain a Packet Identifier if its QoS value is set to 0 [MQTT-2.3.1-5].
                pPacket->varHeader_len = session->topic_name_len + 2; // we have no packet identifier, as we are QOS 0
                pPacket->varHeader = (uint8_t *)os_zalloc(sizeof(uint8_t) * pPacket->varHeader_len);
                os_memset(pPacket->varHeader, 0, 1);
                os_memcpy(pPacket->varHeader + 1, &session->topic_name_len, 1);
                os_memcpy(pPacket->varHeader + 2, session->topic_name, session->topic_name_len);

                //payload
                pPacket->payload_len = len;
                pPacket->payload = (uint8_t *)os_zalloc(sizeof(uint8_t) * pPacket->payload_len);
                os_memcpy(pPacket->payload, data, pPacket->payload_len);
                // calculate remaining length for fixed header
                remaining_len_encoded = encodeLength((uint32_t)(pPacket->varHeader_len + pPacket->payload_len));
                os_memcpy(pPacket->fixedHeader + 1, remaining_len_encoded + 1, remaining_len_encoded[0]);
                pPacket->fixedHeader_len = remaining_len_encoded[0] + 1; // fixed header length is the number of bytes used to encode the remaining length, plus 1 for the fixed CONNECT byte
                // the full length is the length of the varHeader, payload, and fixedHeader
                pPacket->length = (pPacket->varHeader_len + pPacket->payload_len + pPacket->fixedHeader_len);
                os_printf("Packet length: %d\n", pPacket->length);
                // construct packet data
                fullPacket = (uint8_t *)os_zalloc(sizeof(uint8_t) * pPacket->length);
                os_memcpy(fullPacket, pPacket->fixedHeader, pPacket->fixedHeader_len);
                os_memcpy(fullPacket + pPacket->fixedHeader_len, pPacket->varHeader, pPacket->varHeader_len);
                os_memcpy(fullPacket + pPacket->fixedHeader_len + pPacket->varHeader_len, pPacket->payload, pPacket->payload_len);
                break;
            }
            case MQTT_MSG_TYPE_SUBSCRIBE:
            case MQTT_MSG_TYPE_UNSUBSCRIBE:
                // prepare for subscribe
                pPacket->fixedHeader = (uint8_t *)os_zalloc(sizeof(uint8_t) * 5);
                pPacket->fixedHeader[0] = ((msgType << 4) & 0xF0) | 0x02;

                pPacket->varHeader_len = 2;
                pPacket->varHeader = (uint8_t *)os_zalloc(sizeof(uint8_t) * pPacket->varHeader_len);
                os_memset(pPacket->varHeader, 0, 2); // set packet ID to 0

                uint8_t extraByte = (msgType == MQTT_MSG_TYPE_SUBSCRIBE) ? 3 : 2;
                pPacket->payload_len = session->topic_name_len + extraByte;
                pPacket->payload = (uint8_t *)os_zalloc(sizeof(uint8_t) * pPacket->payload_len);
                pPacket->payload[1] = session->topic_name_len % 0xFF;
                pPacket->payload[0] = session->topic_name_len / 0xFF;
                os_memcpy(pPacket->payload + 2, session->topic_name, session->topic_name_len); // copy topic name
                if(msgType == MQTT_MSG_TYPE_SUBSCRIBE) pPacket->payload[session->topic_name_len+2] = 0;

                // calculate remaining length for fixed header
                remaining_len_encoded = encodeLength((uint32_t)(pPacket->varHeader_len + pPacket->payload_len));
                os_memcpy(pPacket->fixedHeader + 1, remaining_len_encoded + 1, remaining_len_encoded[0]);
                pPacket->fixedHeader_len = remaining_len_encoded[0] + 1; // fixed header length is the number of bytes used to encode the remaining length, plus 1 for the fixed CONNECT byte
                // the full length is the length of the varHeader, payload, and fixedHeader
                pPacket->length = (pPacket->varHeader_len + pPacket->payload_len + pPacket->fixedHeader_len);
                os_printf("Packet length: %d\n", pPacket->length);
                // construct packet data
                fullPacket = (uint8_t *)os_zalloc(sizeof(uint8_t) * pPacket->length);
                os_memcpy(fullPacket, pPacket->fixedHeader, pPacket->fixedHeader_len);
                os_memcpy(fullPacket + pPacket->fixedHeader_len, pPacket->varHeader, pPacket->varHeader_len);
                os_memcpy(fullPacket + pPacket->fixedHeader_len + pPacket->varHeader_len, pPacket->payload, pPacket->payload_len);

                break;
            case MQTT_MSG_TYPE_PINGREQ:
            case MQTT_MSG_TYPE_DISCONNECT:
                // PINGREQ has no varHeader or payload, it's just two bytes
                // 0xC0 0x00
                pPacket->fixedHeader = (uint8_t *)os_zalloc(sizeof(uint8_t) * 2);
                pPacket->fixedHeader[0] = (msgType << 4) & 0xF0; // bottom nibble must be clear
                pPacket->fixedHeader[1] = 0; // remaining length is zero
                pPacket->fixedHeader_len = 2;
                pPacket->length = pPacket->fixedHeader_len;
                // In order to avoid undefined behaviour, we must allocate
                // something to varHeader and payload, as they get passed
                // to free() at the end of this function
                pPacket->varHeader = (uint8_t *)os_zalloc(1);
                pPacket->payload = (uint8_t *)os_zalloc(1);
                // copy the fixedHeader to fullPacket, and we're done!
                fullPacket = (uint8_t *)os_zalloc(sizeof(uint8_t) * pPacket->length);
                os_memcpy(fullPacket, pPacket->fixedHeader, pPacket->fixedHeader_len);
                break;
            default:
                // something has gone wrong
                os_printf("Attempt to send incorrect packet type: %d", (uint8_t)msgType);
                return -1;
        }
#ifdef DEBUG
        os_printf("About to send MQTT command type: %d...\n", (uint8_t)msgType);
#endif
        espconn_send(session->activeConnection, fullPacket, pPacket->length);
        os_free(fullPacket);
        os_free(pPacket->fixedHeader);
        os_free(pPacket->varHeader);
        os_free(pPacket->payload);
        // set up keepalive timer
        if(msgType != MQTT_MSG_TYPE_DISCONNECT) {
            os_timer_setfn(&MQTT_KeepAliveTimer, (os_timer_func_t *)pingAlive, session);
            os_timer_arm(&MQTT_KeepAliveTimer, 30000, 0);
        }
    } else {
        os_printf("No wifi! Narf!\n");
    }
    return 0;
}
