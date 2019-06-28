/*  All rights reserverd to CleverBit Ltd.
    2019
*/

//#define DEBUG_ESP_HTTP_CLIENT
//#define DEBUG_ESP_SSL
//#define DEBUG_ESP_CORE
//#define DEBUG_ESP_WIFI
//#define DEBUG_ESP_OOM

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <EEPROM.h>

#define ERROR(args...)    Serial.printf(args)
#define DEBUG(args...)    if (DEBUG_SENSOR) Serial.printf(args)


#define WIFI_SSID       "Korman"
#define WIFI_PASSWORD   "0544349636"

#define WIFI_DELAY      1000
#define WIFI_RETRIES    100

#define CLIENT_ID       1

#define SENSOR_PIN      A0

#define TIMEZONE_HOUR_OFFSET    3 /* Israel Daylight Time (IDT) offset from UTC */

#define EEPROM_ADDR     0
#define EEPROM_SIZE     512

#define TIME_HOUR     (1000000UL * 60 * 60)


#define DEBUG_SENSOR_DELAY (1000000UL * 5) /* 5 seconds */

const bool DEBUG_SENSOR = false; /* don't do deep sleep, just a short delay and then reset */

struct sensor_state {
  int     read_hour;
  String  wall_time_str;
} my_state[] = { /* 5, 11, 14, 16 */
  { 0,  "NaN"   }, /* this means we need to ask for NTP time and calculate the correct state */
  { 5,  "5:00"  },
  { 11, "11:00" },
  { 14, "14:00" },
  { 16, "16:00" },
  { -1, ""      }, /* -1 means end of array */
};

ESP8266WiFiMulti wifi_client;
BearSSL::WiFiClientSecure https_client;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

uint8_t state_index;
int current_hour;
int sensor_reading;
int sleep_for;

int serial_setup() {
  Serial.begin(115200);
  if (DEBUG_SENSOR)
    Serial.setDebugOutput(true);
  Serial.setTimeout(2000);
  while (!Serial) {}

  DEBUG("Connected to serial port\n");

  return 0;
}

int wifi_connect() {
  int retries = WIFI_RETRIES;

  WiFi.mode(WIFI_STA);
  wifi_client.addAP(WIFI_SSID, WIFI_PASSWORD);
  while ((wifi_client.run() != WL_CONNECTED) && retries > 0) {
    delay(WIFI_DELAY);
  }

  if (wifi_client.run() != WL_CONNECTED) {
    ERROR("Wifi connection failed, run=%d\n", wifi_client.run());
    return -1;
  }

  return 0;
}

int get_time() {
  timeClient.begin();
  /* FIXME: add retry here, it fails sometimes */
  if (!timeClient.forceUpdate()) {
    ERROR("Failed to force NTP update\n");
    return -1;
  }
  current_hour = timeClient.getHours() + TIMEZONE_HOUR_OFFSET;   /* return the hour of the day in UTC (24 hour clock) */
  timeClient.end();
  DEBUG("current hour = %d\n", current_hour);

  return 0;
}

int read_sensor() {
  sensor_reading = analogRead(SENSOR_PIN);

  DEBUG("Got analog reading of %d\n", sensor_reading);

  return 0;
}

int upload_reading() {
  String url = String("https://script.google.com/macros/s/AKfycbzOcsNRdSiyISag2sByJOp8E96_M3Ezh8u4qw2c/exec?");
  HTTPClient https;
  int http_code;

  https_client.setInsecure();

  url += String("sensor_id=") + String(CLIENT_ID) + String("&reading=") + String(sensor_reading);
  /* DEBUG */
  url += String("&current_hour=") + String(current_hour) + String("&state_index=") + String(state_index) +
         String("&sleep_for=") + String(sleep_for);

  DEBUG("HTTP request url: %s\n", url.c_str());

  if (https.begin(https_client, url)) {
    http_code = https.GET();

    if (http_code == HTTP_CODE_OK || http_code == HTTP_CODE_FOUND) {
      String payload = https.getString();

      DEBUG("HTTP Code: %s\n", https.errorToString(http_code).c_str());
      if (DEBUG_SENSOR)
        Serial.println(payload);
    } else {
      ERROR("HTTP GET failed => %s (%d)\n", https.errorToString(http_code).c_str(), http_code);
    }

    https.end();
  }

  return 0;
}

int eeprom_read() {
  EEPROM.begin(EEPROM_SIZE);
  state_index = EEPROM.read(EEPROM_ADDR);
  DEBUG("read state index %d\n", state_index);

  return 0;
}

int eeprom_write() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(EEPROM_ADDR, state_index);
  DEBUG("wrote state index %d\n", state_index);

  return 0;
}

void suspend() {
  DEBUG("going to sleep for %d hours\n", sleep_for);
  if (DEBUG_SENSOR) {
    DEBUG("DEBUG - Going to sleep for %d\n", DEBUG_SENSOR_DELAY);
    delay(DEBUG_SENSOR_DELAY);
    ESP.restart();
  } else {
    ESP.deepSleep(sleep_for * TIME_HOUR);
  }
}

int calc_sleep() {
  int next_read_hour = my_state[state_index + 1].read_hour;

  if (next_read_hour == -1) { /* wrap-around */
    DEBUG("handling wraparound...\n");
    state_index = 1;
    next_read_hour = my_state[1].read_hour;
    sleep_for = 24 + next_read_hour - current_hour;
  } else {
    state_index += 1;
    sleep_for = next_read_hour - current_hour;
  }

  return 0;
}

int init_state() {
  if (state_index == 0 || state_index == 255) {
    /* need to calculate next sleep and state */
    int i;

    for (i = 0; my_state[i].read_hour != -1; ++i) {
      if (current_hour < my_state[i].read_hour)
        break;
    }

    if (i == 0)
      while (my_state[++i].read_hour != -1);

    state_index = i - 1;
  }

  return 0;
}

void setup() {
  int err;

  err = serial_setup();
  if (err)
    return;

  err = eeprom_read();
  if (err) {
    ERROR("EEPROM read failed => %d\n", err);
    return;
  }

  err = wifi_connect();
  if (err) {
    ERROR("Wifi connect failed => %d\n", err);
    return;
  }

  err = get_time();
  if (err) {
    ERROR("Time retrieval failed => %d\n", err);
    return;
  }

  err = init_state();
  if (err) {
    ERROR("State init failed => %d\n", err);
    return;
  }

  err = calc_sleep();
  if (err) {
    ERROR("Sleep calculation failed => %d\n", err);
    return;
  }

  err = read_sensor();
  if (err) {
    ERROR("Sensor reading failed => %d\n", err);
    return;
  }

  err = upload_reading();
  if (err) {
    ERROR("Upload failed => %d\n", err);
    return;
  }

  err = eeprom_write();
  if (err) {
    ERROR("EEPROM write failed => %d\n", err);
    return;
  }

  suspend();
}

void loop() {
}
