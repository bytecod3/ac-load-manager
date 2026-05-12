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

/**====================== MQTT functions */

// event handler function
static void fn(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_OPEN) {
    MG_INFO(("%lu CREATED", c->id));
    // c->is_hexdumping = 1;

  } else if (ev == MG_EV_ERROR) {
    // On error, log error message
    MG_ERROR(("%lu ERROR %s", c->id, (char *) ev_data));

  } else if (ev == MG_EV_CONNECT) {

    /* prepare MQTT TLS options */
    #if USING_TLS
      if(mg_url_is_ssl(MQTT_HOST)) {
        struct mg_tls_opts tls_opts = {
          // .ca = mg_str(root_ca),
          .name = mg_str("ebd627a5b511476dae2e77a7aac9064b.s1.eu.hivemq.cloud")
        };

        mg_tls_init(c, &tls_opts);
      }
    #else 
      const struct mg_mqtt_opts opts = {
          .client_id = mg_str("lolin_esp"),
        
      };
      mg_mqtt_connect(c, MQTT_HOST, &opts, fn, NULL);


    #endif
    
    

  } else if(ev == MG_EV_TLS_HS) {
    MG_INFO(("%s\r\n", "TLS handshake complete"));
    
  }else if (ev == MG_EV_MQTT_OPEN) {
    // MQTT connect is successful
    struct mg_str subt = mg_str(sub_topic);
    struct mg_str pubt = mg_str(pub_topic), data = mg_str("hello from desorption");
    MG_INFO(("%lu CONNECTED to %s", c->id, MQTT_HOST));

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
    mg_mqtt_pub(c, &pub_opts);

    MG_INFO(("%lu PUBLISHED %.*s -> %.*s", c->id, (int) data.len, data.buf,
             (int) pubt.len, pubt.buf));

    /* update my global flag -> todo use semaphore here */
    mqtt_open = true;

  } else if (ev == MG_EV_MQTT_MSG) {

    // When we get echo response, print it
    struct mg_mqtt_message *mm = (struct mg_mqtt_message *) ev_data;

    MG_INFO(("%lu RECEIVED %.*s <- %.*s", c->id, (int) mm->data.len,
             mm->data.buf, (int) mm->topic.len, mm->topic.buf));

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
    .user = mg_str(MQTT_USERNAME),
    .pass = mg_str(MQTT_PASSWORD),
    .topic = mg_str(pub_topic),
    .message = mg_str("bye"),
    .qos = s_qos,
    .version = 4,
    .clean = true
  };

  if(s_conn == NULL) s_conn = mg_mqtt_connect(mgr, MQTT_HOST, &opts, fn, NULL);
}


/**
 * timer fucntion to publish MQTT data at set interval
 */
static void timer_publish(void* arg) {
  struct sensor_data _sensor_data;
  char recv_buffer[320];
  mg_mgr* mgr = (mg_mgr*) arg;
  struct mg_mqtt_opts pub_opts;

  struct mg_str pubt = mg_str(pub_topic);
  struct mg_str data;

  memset(&pub_opts, 0, sizeof(pub_opts));
  pub_opts.topic = pubt;


  if (!mqtt_open) {
    Serial.println("MQTT is not open");
  } else {

    // fetch data from queue
    if(xQueueReceive(sensor_data_q, &_sensor_data, 0) == pdPASS) {
      Serial.println("Formatting received buffer");

      // format the data 
      int buf_size = snprintf(recv_buffer, sizeof(recv_buffer), "tt802:%.1f,\r\ntt803:%.1f,\r\ntt804: %.1f,\r\ntt805: %.1f,\r\ntt806:%.1f,\r\n tt807:%.1f,\r\n tt808:%.1f,\r\ntt809:%.1f,\r\n tt810:%.1f,\r\nplenum_pres:%d,\r\npt8003:%d,\r\npt8004:%d,\r\nco2conc:%.2f,\r\no2conc:%.2f,\r\nflowrate:%.2f,\r\nsteam_inst_rate:%.2f,\r\nsteam_ttl_rate:%.2f\r\n ",

      _sensor_data.tt_802,
      _sensor_data.tt_803,
      _sensor_data.tt_804,
      _sensor_data.tt_805,
      _sensor_data.tt_806,
      _sensor_data.tt_807,
      _sensor_data.tt_808,
      _sensor_data.tt_809,
      _sensor_data.tt_810,
      _sensor_data.plenum_pres,
      _sensor_data.pt_8003,
      _sensor_data.pt_8004,
      _sensor_data.co2_conc,
      _sensor_data.o2_conc,
      _sensor_data.flowrate,
      _sensor_data.inst_steam,
      _sensor_data.total_steam
    
    );

    Serial.println(recv_buffer);

      struct mg_str data = mg_str(recv_buffer);
      
      pub_opts.message = data;
      pub_opts.qos = s_qos, pub_opts.retain = false;
      mg_mqtt_pub(s_conn, &pub_opts); 
      // MG_INFO(("mongoose data -> %s\r\n", recv_buffer));

    } else {

      Serial.println("Could not receive from sensor queue");
    }

  }
  
}

/* local MQTT loop */
void mqtt_loop_task(void* params) {
  struct mg_mgr mgr;                // Event manager
  // signal(SIGINT, signal_handler);   // Setup signal handlers - exist event
  // signal(SIGTERM, signal_handler);  // manager loop on SIGINT and SIGTERM
  mg_mgr_init(&mgr);                // Initialise event manager
  mg_timer_add(&mgr, 3000, MG_TIMER_REPEAT | MG_TIMER_RUN_NOW, timer_fn, &mgr);

  // add a data publish timer 
  mg_timer_add(&mgr, 2000, MG_TIMER_REPEAT | MG_TIMER_RUN_NOW, timer_publish, &mgr );

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

  xTaskCreate(
    mqtt_loop_task,
    "mqtt_loop_task",
    8000,
    NULL,
    1,
    NULL
  );

  /* init mqtt publish timer */
  mqtt_pub_timer = xTimerCreate(
    "mqtt_pub_timer",     /* timer name */
    MQTT_PUBLISH_PERIOD,  /* timer period */
    pdTRUE,               /* auto reload -> periddic timer*/
    NULL,                 /* Timer ID*/
    NULL           /* callback fucntion that publishes data to broker */
  );

  xTimerStart(mqtt_pub_timer, 0); 

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
