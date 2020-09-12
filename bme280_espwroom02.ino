/***************************************************************************
  Copyright (c) 2020, Yoshiharu ITO
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  The views and conclusions contained in the software and documentation are those
  of the authors and should not be interpreted as representing official policies,
  either expressed or implied, of the FreeBSD Project.
***************************************************************************/

#include <FS.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ArduinoJson.h>

#define STRING_LENGTH_MAX 32

#include <ESP8266WiFi.h>

#define WIFI_ID "your ssid"
#define WIFI_PASS "your passwd"

char wifi_id[STRING_LENGTH_MAX]         = WIFI_ID;
char wifi_pass[STRING_LENGTH_MAX]       = WIFI_PASS;

#include <ESP8266WebServer.h>

ESP8266WebServer server(80);

// https://github.com/nara256/IIJMachinistClient
#include "IIJMachinistClient.h"

#define MACHINIST_AGENT_NAME      "Home"
#define MACHINIST_NAMESPACE_NAME  "Environment Sensor"
#define MACHINIST_METRICNAME_TEMP "Room Temperture"
#define MACHINIST_METRICNAME_HUMI "Room Humidity"
#define MACHINIST_METRICNAME_PRESS "Room Atmospheric Pressure"

#define MACHINIST_API_KEY "your api key"

char agentName[STRING_LENGTH_MAX]       = MACHINIST_AGENT_NAME;
char namespaceName[STRING_LENGTH_MAX]   = MACHINIST_NAMESPACE_NAME;
char metricNameTemp[STRING_LENGTH_MAX]  = MACHINIST_METRICNAME_TEMP;
char metricNameHumi[STRING_LENGTH_MAX]  = MACHINIST_METRICNAME_HUMI;
char metricNamePress[STRING_LENGTH_MAX] = MACHINIST_METRICNAME_PRESS;

char machinist_api_key[STRING_LENGTH_MAX] = MACHINIST_API_KEY;

IIJMachinistClient *c;

#define BME_SCK   14
#define BME_MISO  12
#define BME_MOSI  13
#define BME_CS    15

//Adafruit_BME280 bme; // I2C
Adafruit_BME280 bme(BME_CS); // hardware SPI
//Adafruit_BME280 bme(BME_CS, BME_MOSI, BME_MISO, BME_SCK); // software SPI

#include <Ticker.h>

Ticker ticker1;

bool readyForTicker = true;
unsigned long intervalTimeSec = 300;

void setup() {
  Serial.begin(115200);
  while (!Serial);   // time to get serial running
  Serial.println(F("BME280 test"));

  unsigned status;

  // default settings
  status = bme.begin();
  // You can also pass in a Wire library object like &Wire2
  // status = bme.begin(0x76, &Wire2)
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
    Serial.print("SensorID was: 0x"); Serial.println(bme.sensorID(), 16);
    Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
    Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
    Serial.print("        ID of 0x60 represents a BME 280.\n");
    Serial.print("        ID of 0x61 represents a BME 680.\n");
    while (1) delay(10);
  }

  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  if (!loadConfig()) {
    Serial.println("Failed to load config");
  } else {
    Serial.println("Config loaded");
  }

  WiFi.mode(WIFI_STA);
  //WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
  WiFi.begin(WIFI_ID, WIFI_PASS);

  // wait for WiFi connection
  Serial.print("\nWaiting for WiFi to connect...");
  while ((WiFi.status() != WL_CONNECTED)) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println(" connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.on("/config.json", HTTP_GET, []() {
    String html = getConfig();
    server.send(200, "text/html", html);
  });

  server.on("/config.json", HTTP_POST, []() {
    DynamicJsonDocument root(1024);
    DeserializationError error = deserializeJson(root, server.arg("plain"));

    if (error) {
      server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
    }
    else {
      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) {
        server.send(404, "text/plain", "Failed to open config file for writing");
      }

      serializeJson(root, configFile);
      server.send(200, "text/html", "SUCCESS.");
    }
  });

  server.begin();

  c = new IIJMachinistClient(machinist_api_key);
  c->setDebugSerial(Serial);
  c->init();

  ticker1.attach_ms(intervalTimeSec * 1000, setReadyForTicker);
}

void loop() {
  server.handleClient();

  if (readyForTicker) {
    readyForTicker = false;
    sendMetricdata();
  }

  delay(500);
}

void setReadyForTicker()
{
  readyForTicker = true;
}

void sendMetricdata()
{
  String jsonBuffer = "";
  jsonBuffer += "{";
  jsonBuffer += "\"agent\":\"" + (String)agentName + "\",";
  jsonBuffer += "\"metrics\": [";
  jsonBuffer += "{";
  jsonBuffer += "\"name\":\"" + (String)metricNameTemp + "\",";
  jsonBuffer += "\"namespace\":\"" + (String)namespaceName + "\",";
  jsonBuffer += "\"data_point\": {";
  jsonBuffer += "\"value\":" + (String)bme.readTemperature();
  jsonBuffer += "}";
  jsonBuffer += "},";
  jsonBuffer += "{";
  jsonBuffer += "\"name\":\"" + (String)metricNameHumi + "\",";
  jsonBuffer += "\"namespace\":\"" + (String)namespaceName + "\",";
  jsonBuffer += "\"data_point\": {";
  jsonBuffer += "\"value\":" + (String)bme.readHumidity();
  jsonBuffer += "}";
  jsonBuffer += "},";
  jsonBuffer += "{";
  jsonBuffer += "\"name\":\"" + (String)metricNamePress + "\",";
  jsonBuffer += "\"namespace\":\"" + (String)namespaceName + "\",";
  jsonBuffer += "\"data_point\": {";
  jsonBuffer += "\"value\":" + (String)(bme.readPressure() / 100.0F);
  jsonBuffer += "}";
  jsonBuffer += "}";
  jsonBuffer += "]";
  jsonBuffer += "}";

  c->post(jsonBuffer);
}

bool defaultConfig(void)
{
  DynamicJsonDocument json(1024);

  json["agentName"] = agentName;
  json["namespaceName"] = namespaceName;
  json["metricNameTemp"] = metricNameTemp;
  json["metricNameHumi"] = metricNameHumi;
  json["metricNamePress"] = metricNamePress;

  json["wifi_id"] = wifi_id;
  json["wifi_pass"] = wifi_pass;
  json["machinist_api_key"] = machinist_api_key;

  json["intervalTimeSec"] = intervalTimeSec;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    //Serial.println("Failed to open config file for writing");
    return false;
  }

  serializeJson(json, configFile);
  return true;
}

bool loadConfig(void)
{
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Create new file with default values");
    return defaultConfig();
  }

  if (configFile.size() > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  DynamicJsonDocument json(1024);
  DeserializationError error = deserializeJson(json, configFile.readString());
  if (error) {
    Serial.println("Failed to parse config file");
    return false;
  }

  strncpy(agentName, json["agentName"], STRING_LENGTH_MAX);
  strncpy(namespaceName, json["namespaceName"], STRING_LENGTH_MAX);
  strncpy(metricNameTemp, json["metricNameTemp"], STRING_LENGTH_MAX);
  strncpy(metricNameHumi, json["metricNameHumi"], STRING_LENGTH_MAX);
  strncpy(metricNamePress, json["metricNamePress"], STRING_LENGTH_MAX);
  strncpy(wifi_id, json["wifi_id"], STRING_LENGTH_MAX);
  strncpy(wifi_pass, json["wifi_pass"], STRING_LENGTH_MAX);
  strncpy(machinist_api_key, json["machinist_api_key"], STRING_LENGTH_MAX);
  intervalTimeSec = json["intervalTimeSec"];

  return true;
}

String getConfig(void)
{
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Create new file with default values");
  }

  if (configFile.size() > 1024) {
    return (String)"Config file size is too large";
  }

  return configFile.readString();
}
