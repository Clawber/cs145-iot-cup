// Include essential libraries
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <time.h>
#include <WiFiManager.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "EasyBuzzer.h"

// Include definitions

#define SMOKE_INT 5 // Define SMOKE_INT to be the time interval (in seconds) for sending of smoke data
#define BUZZER_PIN 25 // Define BUZZER_PIN to be the pin used for the buzzer
#define SENSOR_PIN 32 // Define SENSOR_PIN to be the pin used for the sensor
#define TRIGGER_PIN 0 // Define TRIGGER_PIN to be the pin used for the WiFiManager

// Initialize smoke_read for smoke readings
int smoke_read;

// Setup WiFiMulti variable for scanning WiFi access points
WiFiMulti WiFiMulti;

// Setup secure WiFi client
WiFiClientSecure client;

// Setup HTTP client
HTTPClient http;

// Setup WiFi Manager
WiFiManager wm;

// Buffer to contain sensor data to be sent to the server
char buffer[200];

// Not sure if WiFiClientSecure checks the validity date of the certificate. 
// Setting clock just to be sure...
void setClock() {
  configTime(28800, 0, "pool.ntp.org");

  Serial.print(F("Waiting for NTP time sync: "));
  time_t nowSecs = time(nullptr);
  int ntp_timeout = 0;
  while (nowSecs < 8 * 3600 * 2) {
    if (++ntp_timeout == 100) { ESP.restart(); }
    delay(500);
    Serial.print(F("."));
    yield();
    nowSecs = time(nullptr);
  }

  Serial.println();
  char * curr_time = ctime(&nowSecs);
  Serial.print(F("Current time: "));
  Serial.print(curr_time);
}

// Run ESP32 configuration
void IRAM_ATTR esp32config(){
  // set configportal timeout
  wm.setConfigPortalTimeout(120);

  if (!wm.startConfigPortal("OnDemandAP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  
  setClock();
}

int time_offset() {
  time_t raw_time = time(nullptr);
  struct tm * curr_time = localtime(&raw_time);
  return (SMOKE_INT - (curr_time->tm_sec % SMOKE_INT))*1000;
}

void setup() {
  // Set up Serial Monitor at 115200 baud
  Serial.begin(115200);
  
  EasyBuzzer.setPin(BUZZER_PIN);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT);
  
  // Attach interrupt for ESP32 configuration
	attachInterrupt(TRIGGER_PIN, esp32config, FALLING);

  Serial.println();
  Serial.println();
  Serial.println();

  WiFi.mode(WIFI_STA);

  // Insecure local access
  client.setInsecure();

  // allow reuse (if server supports it)
  http.setReuse(true);
}

void loop() {
  if ( WiFi.status() != WL_CONNECTED ) {
    esp32config();
  }
  // Buzzer alarm sequence
  smoke_read = analogRead(SENSOR_PIN);
  EasyBuzzer.update();
  if (smoke_read >= 384) {
    /* Beep at a given frequency for 100 times. */
    EasyBuzzer.beep(1000);
  }
  else {
    /* Stop beeping if smoke_read is low */
    EasyBuzzer.stopBeep();
  }
  Serial.print("[HTTP] begin...\n");
  if (http.begin("http://192.168.1.19:8000/sensors")) {  // HTTP
    Serial.print("[HTTP] POST...\n");
    // payload
    // Calibrate time to nearest 10 seconds
    delay(time_offset());
    sprintf(buffer, "{\"device_id\": \"ESP32_EYRON\", \"smoke_read\": %d, \"time\": %d}", smoke_read, time(nullptr));
    Serial.print(buffer);
    // start connection and send HTTP header
    int httpCode = http.POST(buffer);
    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] POST... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        Serial.println(payload);
      }
    } else {
      Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  } else {
    Serial.printf("[HTTP] Unable to connect\n");
  }
  Serial.println();
  Serial.println("Waiting 10s before the next round...");
}