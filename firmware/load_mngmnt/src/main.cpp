/**
 * Main code 
 * author Edwin M
 */

#include <Arduino.h>
#include "WiFi.h"
#include "WiFiManager.h"
#include "defines.h"
#include "ACS712.h"
#include "mongoose.h"



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

/* Timers */
TimerHandle_t mqtt_pub_timer = NULL;

/*============= tasks */

/*===========to read and store current */
void read_current_task(void* params);

/* to control the loads */
void load_control_task(void* params);

/* to publish data readings to MQTT */
void publish_readings_task(void* params);

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

  float feed_ld = 0.0f;
  float load_1 = 0.0f;
  float load_2 = 0.0f;
  float load_3 = 0.0f;
  float load_4 = 0.0f;

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

  /*============== create tasks*/
  BaseType_t a = xTaskCreate(read_current_task, "read_current", 1024, NULL,  1, NULL);
  if(a != NULL) {
    Serial.println("[+] read current task created OK");
  } else {
    Serial.println("[-] Failed to create read current_task");
  }

  BaseType_t b = xTaskCreate(load_control_task, "load_control", 1024, NULL,  1, NULL);
  if(b != NULL) {
    Serial.println("[+] load_control_task created OK");
  } else {
    Serial.println("[-] Failed to create load_control_task");
  }

  BaseType_t c = xTaskCreate(publish_readings_task, "publish_readings", 2048, NULL,  1, NULL);
  if(c != NULL) {
    Serial.println("[+] publish_readings_task created OK");
  } else {
    Serial.println("[-] Failed to create publish_readings_task");
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
