/*  All rights reserverd to CleverBit Ltd.
    2019
*/

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>

#include <WiFiClientSecureBearSSL.h>

#define WIFI_SSID       "Korman"
#define WIFI_PASSWORD   "0544349636"

#define WIFI_DELAY      1000
#define WIFI_RETRIES    100

#define GURL
#define CLIEND_ID       1

#define SENSOR_PIN      A0

ESP8266WiFiMulti wifi_client;
BearSSL::WiFiClientSecure https_client;
int sensor_reading;

int serial_setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.setTimeout(2000);
  while (!Serial) {}

  Serial.println("Connected to serial port");

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
    Serial.printf("Wifi connection failed, run=%d\n", wifi_client.run());
    return -1;
  }

  return 0;
}

int read_sensor() {
  sensor_reading = analogRead(SENSOR_PIN);

  return 0;
}

int upload_reading() {
  String url = String("https://script.google.com/macros/s/AKfycbzOcsNRdSiyISag2sByJOp8E96_M3Ezh8u4qw2c/exec?");
  HTTPClient https;
  int http_code;

  https_client.setInsecure();

  url += String("sensor_id=") + String(CLIEND_ID) + String("&reading=") + String(sensorValue) +
         String("&time=") + String(ticks++);

  Serial.println(String("[HTTPS] begin with: ") + url);

  if (https.begin(https_client, url)) {
    http_code = https.GET();
    Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
    
    if (http_code == HTTP_CODE_OK) {
      String payload = https.getString();
      
      Serial.println(payload);
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(http_code).c_str());
    }

    https.end();
  }

  return 0;
}

void setup() {
  int err;

  err = serial_setup();
  if (err)
    return;

  err = wifi_connect();
  if (err) {
    ERROR("Wifi connect failed => %d\n", err);
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

  /// old

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  Serial.println();

  for (uint8_t t = 4; t > 0; t--) {
    Serial.printf("[SETUP] WAIT %d...\n", t);
    Serial.flush();
    delay(1000);
  }

  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {

  int err;
  double ticks = 0;
  double wait_time;
  int sensorValue;
  String url = String("https://script.google.com/macros/s/AKfycbzOcsNRdSiyISag2sByJOp8E96_M3Ezh8u4qw2c/exec?");

  sensorValue = analogRead(sensorPin);

  // wait for WiFi connection
  if ((WiFiMulti.run() == WL_CONNECTED)) {

    std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);

    client->setInsecure();

    HTTPClient https;

    url += String("sensor_id=") + String(CLIEND_ID) + String("&reading=") + String(sensorValue) +
           String("&time=") + String(ticks++);

    Serial.println(String("[HTTPS] begin with: ") + url);

    if (https.begin(*client, url)) {

      int httpCode = https.GET();

      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        Serial.printf("[HTTPS] GET... code: %d\n", httpCode);

        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          String payload = https.getString();
          Serial.println(payload);
        }
      } else {
        Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
      }

      https.end();
      wait_time = 1000 * 60 * 60; /* collect each hour */
    } else {
      Serial.printf("[HTTPS] Unable to connect\n");
      wait_time = 1000 * 60; /* retry in a minute */
    }
  } else {
    wait_time = 1000; /* 1 second to wait for WIFI */
  }

  Serial.println("Wait for next round...");
  delay(wait_time);
}
