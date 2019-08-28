#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <FS.h>
#include <CapacitiveSensor.h>

#define DEBUG false

extern "C" {
  #include "pwm.h"
}

ESP8266WebServer webServer(80);

uint8_t power = 0;
uint8_t brightness = 0;
#define MAX_BRIGHTNESS 100

const uint32_t pwmPeriod = 5000; // * 200ns ^= 1 kHz
uint32_t pwmPins[1][3] = { {PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14, 14} }; // MUX, FUNC, PIN
uint32_t pwmDuty[1] = { 0 };

CapacitiveSensor capSens[3] = { CapacitiveSensor(5,4),
                                CapacitiveSensor(5,12),
                                CapacitiveSensor(5,13) };
uint8_t capSensLooper = 0;
uint32_t capSensTimer = millis();
uint16_t capThreshold = 100;

void setPWM() {
  if (power) {
    uint32_t newDuty = (pwmPeriod * brightness) / MAX_BRIGHTNESS;
    pwmDuty[0] = newDuty > pwmPeriod ? pwmPeriod : newDuty;
    pwm_set_duty(pwmDuty[0], 0);
    if (DEBUG) {
      Serial.print("New duty: ");
      Serial.println(pwmDuty[0]);
    }
  } else {
    pwm_set_duty(0, 0);
    if (DEBUG) Serial.println("Power off");
  }
  pwm_start();
}

void loadSettings() {
  brightness = EEPROM.read(0);
  power = EEPROM.read(1);

  setPWM();
}

void sendString(String value) { webServer.send(200, "text/plain", value); }

void sendInt(uint8_t value) { sendString(String(value)); }

void setPower(uint8_t value) {
  power = value == 0 ? 0 : 1;

  setPWM();

  EEPROM.write(1, power);
  EEPROM.commit();
}

void setBrightness(uint8_t value) {
  brightness = value > MAX_BRIGHTNESS ? MAX_BRIGHTNESS : value;

  setPWM();

  EEPROM.write(0, brightness);
  EEPROM.commit();
}

void setup() {
  pinMode(14, OUTPUT);
  digitalWrite(14, LOW);
  pwm_init(pwmPeriod, pwmDuty, 1, pwmPins);
  pwm_start();

  if (DEBUG) Serial.begin(115200);
  delay(100);
  Serial.setDebugOutput(DEBUG);

  WiFi.hostname("paraplU");
  WiFiManager wifiManager;
  wifiManager.setDebugOutput(DEBUG);
  if (!wifiManager.autoConnect("paraplU")) {
    delay(1000);
    ESP.restart();
    delay(1000);
  }

  EEPROM.begin(512);

  loadSettings();

  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }

  SPIFFS.begin();

  webServer.on("/get", []() {
    String getVar = webServer.arg("var");
    if (getVar == "power") {
      sendInt(power);
    } else if (getVar == "brightness") {
      sendInt(brightness);
    } else {
      sendString("");
    }
  });

  webServer.on("/set", []() {
    sendString("");
    String setVar = webServer.arg("var");
    if (setVar == "power") {
      setPower(webServer.arg("val").toInt());
    } else if (setVar == "brightness") {
      setBrightness(webServer.arg("val").toInt());
    }
  });

  webServer.serveStatic("/cache.manifest", SPIFFS, "/cache.manifest");
  webServer.serveStatic("/", SPIFFS, "/index.htm", "max-age=86400");

  webServer.begin();
  if (DEBUG) Serial.println("HTTP web server started");

  MDNS.begin("paraplU");
}

void loop() {
  webServer.handleClient();
  yield();

  if (millis() - capSensTimer >= 500) {
    long capMeasure =  capSens[capSensLooper].capacitiveSensor(10);
    if (capMeasure > capThreshold) {
      capSensTimer = millis();
      if (DEBUG) {
        Serial.print("Sensor ");
        Serial.print(capSensLooper);
        Serial.print(": ");
        Serial.println(capMeasure);
      }
      switch (capSensLooper) {
        case 0:
          if (!power) {
            setPower(1);
          } else {
            setBrightness(brightness + sqrt(brightness * 1.5));
          }
          break;
        case 1:
          setPower(!power);
          break;
        case 2:
          if (!power) {
            setPower(1);
          } else {
            setBrightness((brightness - (brightness * 0.2)) < 1 ? 1 : brightness - (brightness * 0.2));
          }
          break;
      }
    }
    capSensLooper = capSensLooper >= 2 ? 0 : capSensLooper + 1;
  }
}
