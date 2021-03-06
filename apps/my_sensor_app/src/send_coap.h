//  Send sensor data to a CoAP server like thethings.io.  The CoAP payload will be encoded as JSON.
//  The sensor data will be transmitted over WiFi via the ESP8266 transceiver.
#ifndef __SEND_COAP_H__
#define __SEND_COAP_H__
#if MYNEWT_VAL(SENSOR_COAP)   //  If we are sending sensor data to CoAP server...

//  Define SERVER_NETWORK_INTERFACE, the network interface for transmitting to CoAP Server (via ESP8266)
//  and SENSOR_NETWORK_INTERFACE, the network interface for transmitting to Collector Node (via nRF24L01)

#if MYNEWT_VAL(ESP8266)       //  If ESP8266 WiFi is enabled...
#include <esp8266/esp8266.h>  //  For ESP8266_DEVICE
#define SERVER_NETWORK_INTERFACE ESP8266_DEVICE  //  Will be set to "esp8266_0"
#endif  //  MYNEWT_VAL(ESP8266)

#if MYNEWT_VAL(NRF24L01)        //  If nRF24L01 Wireless Network is enabled...
#include <nrf24l01/nrf24l01.h>  //  For NRF24L01_DEVICE
#define SENSOR_NETWORK_INTERFACE NRF24L01_DEVICE  //  Will be set to "nrf24l01_0"
#endif  //  MYNEWT_VAL(NRF24L01)

#ifdef __cplusplus
extern "C" {  //  Expose the types and functions below to C functions.
#endif

struct sensor_value;

//  Start the Network Task in the background.  The Network Task to prepare the network drivers
//  (ESP8266 and nRF24L01) for transmitting sensor data messages.  
//  Connecting the ESP8266 to the WiFi access point may be slow so we do this in the background.
//  Also perform WiFi Geolocation if it is enabled.  Return 0 if successful.
int start_network_task(void);

//  Compose a CoAP message (CBOR or JSON) with the sensor value in val and transmit to the
//  Collector Node (if this is a Sensor Node) or to the CoAP Server (if this is a Collector Node
//  or Standalone Node).  
//  For Sensor Node or Standalone Node: sensor_node is the sensor name ("bme280_0" or "temp_stm32_0")
//  For Collector Node: sensor_node is the Sensor Node Address of the Sensor Node that transmitted
//  the sensor data (like "b3b4b5b6f1")
//  The message will be enqueued for transmission by the CoAP / OIC Background Task 
//  so this function will return without waiting for the message to be transmitted.  
//  Return 0 if successful, SYS_EAGAIN if network is not ready yet.
int send_sensor_data(struct sensor_value *val, const char *device_name);

#ifdef __cplusplus
}
#endif

#endif  //  MYNEWT_VAL(SENSOR_COAP)
#endif  //  __SEND_COAP_H__
