/**
 * Main code 
 * author Edwin M
 */

#include <Arduino.h>
#include "WiFi.h"
#include "WiFiManager.h"
#include "defines.h"

typedef struct {
  float pin;        /* what pin are we connected to */
  float current;    /* what is the current erading from the load */
  bool state;       /* on or off */
} load_t;

load_t load_1;
load_t load_2;
load_t load_3;
load_t load_4;

/*Queues*/
QueueHandle_t load_queue;

/*============= tasks */

/*===========to read and store current */
void read_current(void* params);

/* to control the loads */
void load_control(void* params);

/* to publish data readings to MQTT */
void publish_readings(void* params);

/*=========global functions */
void setup_wifi_provisioner();

/* initialize MQTT */
void init_mqtt();

/* buzzer control  */
void buzzer();

/* onboard LED control */
void led_control();

/*========================tasks*/
/*===========to read and store current */
void read_current(void* params) {
  for(;;) {

  }
}


/*=========================================================== */
void setup() {
  Serial.begin(BAUDRATE);
  pinMode(LED_BUILTIN, OUTPUT);

  setup_wifi_provisioner();

  /*======== create queues*/
  load_queue = xQueueCreate(10, sizeof(load_t));
  if(load_queue != NULL) {
    Serial.println("[+]Load data queue created OK"); 
  } else {
    Serial.println("[-]Failed to create load data queue");
  }
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(5));
}

/*========================================function implementation*/
void setup_wifi_provisioner() {
  WiFi.mode(WIFI_STA);

  WiFiManager wm;
  bool res;
  res = wm.autoConnect("load_mngmt", "11223344556677889900");

  if(!res) {
    Serial.println("Failed to connect");
  } else {
    Serial.println("Conneted to WiFiManager");
  }
  
}
