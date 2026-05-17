#pragma once 

#include <Arduino.h>

/*================ define global pins */
#define LOAD_1 (5)
#define LOAD_2 (18)
#define LOAD_3 (19)
#define LOAD_4 (23)

/*============= AC712 pins*/
#define ACS712_FEED     (32)
#define ACS712_LOAD_1   (33)
#define ACS712_LOAD_2   (34)
#define ACS712_LOAD_3   (35)
#define ACS712_LOAD_4   (25)

#define BUZZER          (17)
#define BAUDRATE        (115200)

/*============MQTT config*/
const char* MQTT_HOST = "broker.hivemq.com:1883";
uint16_t MQTT_PORT = 1883;

const char* commands_topic = "load_mngr/commands";
const char* data_topic = "load_mngr/data";
#define MQTT_PUBLISH_PERIOD  (2000)