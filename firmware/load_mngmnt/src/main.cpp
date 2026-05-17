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
#include <ArduinoJson.h>
#include "pins.h"

/* Timers */
TimerHandle_t mqtt_pub_timer = NULL;

/* MQTT variables */
static struct mg_connection* s_conn;    /* Client connection handle */
static int s_qos = 1;                   /* QOS */
bool mqtt_open = false;

// A list of subscription, held in memory
struct sub {
  struct sub *next;
  struct mg_connection *c;
  struct mg_str topic;
  uint8_t qos;
};
static struct sub *s_subs = NULL;

// Handle interrupts, like Ctrl-C
static int s_signo;
static void signal_handler(int signo) {
  s_signo = signo;
}

static size_t mg_mqtt_next_topic(struct mg_mqtt_message *msg,
                                 struct mg_str *topic, uint8_t *qos,
                                 size_t pos) {
  unsigned char *buf = (unsigned char *) msg->dgram.buf + pos;
  size_t new_pos;
  if (pos >= msg->dgram.len) return 0;

  topic->len = (size_t) (((unsigned) buf[0]) << 8 | buf[1]);
  topic->buf = (char *) buf + 2;
  new_pos = pos + 2 + topic->len + (qos == NULL ? 0 : 1);
  if ((size_t) new_pos > msg->dgram.len) return 0;
  if (qos != NULL) *qos = buf[2 + topic->len];
  return new_pos;
}

size_t mg_mqtt_next_sub(struct mg_mqtt_message *msg, struct mg_str *topic,
                        uint8_t *qos, size_t pos) {
  uint8_t tmp;
  return mg_mqtt_next_topic(msg, topic, qos == NULL ? &tmp : qos, pos);
}

size_t mg_mqtt_next_unsub(struct mg_mqtt_message *msg, struct mg_str *topic,
                          size_t pos) {
  return mg_mqtt_next_topic(msg, topic, NULL, pos);
}

/*End of MQTT variables */

typedef struct {
  char payload[200];
} mqtt_payload;


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

void init_loads();

/* initialize all loads */
void init_loads() {
  load_1 = {
    .pin = 4,
    .current = 0,
    .state = 0
  };

  load_2 = {
    .pin = 5,
    .current = 0,
    .state = 0
  };

  load_3 = {
    .pin = 6,
    .current = 0,
    .state = 0
  };

  load_4 = {
    .pin = 7,
    .current = 0,
    .state = 0
  };

}

void init_load_control_pins() {
  pinMode(LOAD_1_CONTROL_PIN, OUTPUT);
  pinMode(LOAD_2_CONTROL_PIN, OUTPUT);
  pinMode(LOAD_3_CONTROL_PIN, OUTPUT);
  pinMode(LOAD_4_CONTROL_PIN, OUTPUT);

}

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
void read_current_task(void* params) {

  JsonDocument doc;
  

  mqtt_payload msg;

  for(;;) { 

    // call function to read current here
    load_1.current = 89;
    load_2.current = 77;
    load_3.current = 22;
    load_4.current = 12;

    /* clear previous json */
    doc.clear();
    JsonArray loads = doc["loads"].to<JsonArray>();

    load_t *arr[] = {&load_1, &load_2, &load_3, &load_4};

    for (int i= 0; i < 4; i++) {
      JsonObject obj = loads.add<JsonObject>();

      obj["pin"] = arr[i]->pin;
      obj["current"] = arr[i]->current;
      obj["state"] = arr[i]->state;
    }

    serializeJson(doc, msg.payload, sizeof(msg.payload));

    xQueueOverwrite(load_queue, &msg);

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

volatile bool led_state = 0;

/* to control the loads */
void load_control_task(void* params) {

  TickType_t x_last_wake_time = xTaskGetTickCount();
  const TickType_t x_period = pdMS_TO_TICKS(1500);

  digitalWrite(LOAD_1_CONTROL_PIN, HIGH);
  digitalWrite(LOAD_2_CONTROL_PIN, HIGH);
  digitalWrite(LOAD_3_CONTROL_PIN, HIGH);
  digitalWrite(LOAD_4_CONTROL_PIN, HIGH);

  for(;;) {

    led_state = !led_state;

    digitalWrite(ONBOARD_LED, led_state);
    

    vTaskDelayUntil(&x_last_wake_time, x_period);
    vTaskDelay(pdMS_TO_TICKS(5));
  }

}

/* to publish data readings to MQTT */
void publish_readings_task(void* params) {
  for(;;) {

    vTaskDelay(pdMS_TO_TICKS(10));
  }

}

/**====================== MQTT functions */

// event handler function
static void fn(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_OPEN) {
    MG_INFO(("%lu CREATED", c->id));
    // c->is_hexdumping = 1;

  } else if (ev == MG_EV_ERROR) {
    // On error, log error message
    MG_ERROR(("%lu ERROR %s", c->id, (char *) ev_data));

  } else if(ev == MG_EV_TLS_HS) {
    MG_INFO(("%s\r\n", "TLS handshake complete"));
    
  }else if (ev == MG_EV_MQTT_OPEN) {
    // MQTT connect is successful
    MG_INFO(("%lu CONNECTED to %s", c->id, MQTT_HOST));

    struct mg_str subt = mg_str(commands_topic);
    struct mg_str pubt = mg_str(data_topic), data = mg_str("hello from load manager");
    

    struct mg_mqtt_opts sub_opts;
    memset(&sub_opts, 0, sizeof(sub_opts));
    sub_opts.topic = subt;
    sub_opts.qos = s_qos;
    mg_mqtt_sub(c, &sub_opts);

    MG_INFO(("%lu SUBSCRIBED to %.*s", c->id, (int) subt.len, subt.buf));

    struct mg_mqtt_opts pub_opts;
    memset(&pub_opts, 0, sizeof(pub_opts));
    pub_opts.topic = pubt;
    pub_opts.message = data;
    pub_opts.qos = s_qos, pub_opts.retain = false;

    /* publish data */
    mg_mqtt_pub(c, &pub_opts);

    MG_INFO(("%lu PUBLISHED %.*s -> %.*s", c->id, (int) data.len, data.buf,
             (int) pubt.len, pubt.buf));

    /* update my global flag -> todo use semaphore here */
    mqtt_open = true;

  } else if (ev == MG_EV_MQTT_MSG) {
    /* received handler */

    // When we get echo response, print it
    struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;

    MG_INFO(("%lu RECEIVED %.*s <- %.*s", c->id, (int) mm->data.len,
             mm->data.buf, (int) mm->topic.len, mm->topic.buf));

    /* respond to received MQTT messages here */
    if(mg_match(mm->topic, mg_str("load_mngr/commands"), NULL)) {

      /* decode message */
      JsonDocument doc;
      deserializeJson(doc, mm->data.buf);

      /* extract necessary values */

      if(strncmp(mm->data.buf, "\"OFF\"", mm->data.len) == 0) {
        MG_INFO(("Control load 1 pin"));
        digitalWrite(LOAD_1_CONTROL_PIN, LOW);
      }

      if(strncmp(mm->data.buf, "\"ON\"", mm->data.len) == 0) {
        digitalWrite(LOAD_1_CONTROL_PIN, HIGH);
      }

    }


  } else if (ev == MG_EV_CLOSE) {
    MG_INFO(("%lu CLOSED", c->id));
    s_conn = NULL;  // Mark that we're closed

    /* update my global flag -> todo use semaphore here */
    mqtt_open = false;

  } else if (ev == MG_EV_POLL) {

  }

}


/**
 * timer fucntion to recreate client connection if it is closed 
 */
static void timer_fn(void* arg) {
  struct mg_mgr* mgr = (struct mg_mgr*) arg;

  struct mg_mqtt_opts opts = {
    .client_id = mg_str("lolin_esp"),
    .version = 4,
    .clean = true
  };

  if(s_conn == NULL){
    s_conn = mg_mqtt_connect(mgr, MQTT_HOST, &opts, fn, NULL);
  } 
}


/**
 * timer function to publish MQTT data at set interval
 */
static void timer_publish(void* arg) {
  
  mqtt_payload recvd_payload;

  // char recv_buffer[320];
  mg_mgr* mgr = (mg_mgr*) arg;

  struct mg_str pubt = mg_str(data_topic);
  struct mg_str data;

  int test_data = 34;
  const char* test_str = "Load mangr";

  if (!mqtt_open) {
    Serial.println("MQTT is not open");

  } else {

    /* receive from queue */
    if(xQueueReceive(load_queue, &recvd_payload, 0)) {
      if(s_conn != NULL && mqtt_open) {
        struct mg_mqtt_opts pub_opts;
        pub_opts.topic = pubt;
        pub_opts.message = mg_str(recvd_payload.payload);
        pub_opts.qos = s_qos, pub_opts.retain = false;

        mg_mqtt_pub(s_conn, &pub_opts);
      }
    }
     
  }
}

/* local MQTT loop */
void mqtt_loop_task(void* params) {
  struct mg_mgr mgr;                // Event manager
  // signal(SIGINT, signal_handler);   // Setup signal handlers - exist event
  // signal(SIGTERM, signal_handler);  // manager loop on SIGINT and SIGTERM
  mg_mgr_init(&mgr);                // Initialise event manager
  mg_timer_add(&mgr, 3000, MG_TIMER_REPEAT , timer_fn, &mgr);

  // add a data publish timer 
  mg_timer_add(&mgr, 2000, MG_TIMER_REPEAT, timer_publish, &mgr );

  mg_log_set(MG_LL_DEBUG  );
  mg_log_set_fn( [](char ch, void*) {
    Serial.print(ch);
  }, NULL);

  MG_INFO(("Starting on %s", MQTT_HOST));      // Inform that we're starting
                                               // MQTT conenction is inferred to the periodic timer 

  for(;;) {
    mg_mgr_poll(&mgr, 1000);                    // Event loop, 1s timeout
    vTaskDelay(pdMS_TO_TICKS(10));

  }

}

/**=======================End  */


/*=========================================================== */
void setup() {
  Serial.begin(BAUDRATE);
  pinMode(LED_BUILTIN, OUTPUT);

  setup_wifi_provisioner();

  init_loads();

  pinMode(ONBOARD_LED, OUTPUT);

  /* init load control pins */
  init_load_control_pins();

  /*======== create queues*/
  load_queue = xQueueCreate(1, sizeof(mqtt_payload));
  if(load_queue != NULL) {
    Serial.println("[+]Load data queue created OK"); 
  } else {
    Serial.println("[-]Failed to create load data queue");
  }

  /*============== create tasks*/
  BaseType_t a = xTaskCreate(read_current_task, "read_current", 2000, NULL,  1, NULL);
  if(a != pdPASS) {
    Serial.println("[+] read current task created OK");
  } else {
    Serial.println("[-] Failed to create read current_task");
  }

  BaseType_t b = xTaskCreate(load_control_task, "load_control", 1024, NULL,  1, NULL);
  if(b != pdPASS) {
    Serial.println("[+] load_control_task created OK");
  } else {
    Serial.println("[-] Failed to create load_control_task");
  }

  BaseType_t c = xTaskCreate(publish_readings_task, "publish_readings", 2048, NULL,  1, NULL);
  if(c != pdPASS) {
    Serial.println("[+] publish_readings_task created OK");
  } else {
    Serial.println("[-] Failed to create publish_readings_task");
  }

  xTaskCreate(
    mqtt_loop_task,
    "mqtt_loop_task",
    8000,
    NULL,
    1,
    NULL
  );

  /* init mqtt publish timer */
  // mqtt_pub_timer = xTimerCreate(
  //   "mqtt_pub_timer",     /* timer name */
  //   MQTT_PUBLISH_PERIOD,  /* timer period */
  //   pdTRUE,               /* auto reload -> periddic timer*/
  //   NULL,                 /* Timer ID*/
  //   NULL           /* callback fucntion that publishes data to broker */
  // );

  // xTimerStart(mqtt_pub_timer, 0); 

}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(5));
}

/*========================================function implementation*/
void setup_wifi_provisioner() {
  WiFi.mode(WIFI_STA);

  WiFiManager wm;
  bool res;
  res = wm.autoConnect("load_mngmt", "123456789");

  if(!res) {
    Serial.println("Failed to connect");
  } else {
    Serial.println("Conneted to WiFiManager");
  }
  
}
