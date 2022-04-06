/**
 * This sketch connects an Airsense DIY sensor to a WiFi network, and runs a
 * tiny HTTP server to serve air quality metrics to Prometheus.
 */

#include <AirGradient.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>

#include <ESP8266WebServer.h>
#include <WiFiClient.h>

#include <Wire.h>
#include "SSD1306Wire.h"

AirGradient ag = AirGradient();

// Config ----------------------------------------------------------------------

// Optional.
const char* deviceId = "";

// Hardware options for Airsense DIY sensor.
const bool hasPM = true;
const bool hasCO2 = true;
const bool hasSHT = true;
const bool showName = true;
const float calib_temp = -0.4f;

// WiFi and IP connection info. If enabled, the display will show values only when the sensor has wifi connection
boolean connectWIFI = true;
const int port = 9926;

// The frequency of measurement updates.
const int updateFrequency = 5000;

// For housekeeping.
long lastUpdate;
int counter = 0;
//pm2 remebered default value of 22 that does not hurt anyone
int pm2_remember = 22;

// Config End ------------------------------------------------------------------

SSD1306Wire display(0x3c, SDA, SCL);
ESP8266WebServer server(port);

void setup() {
  Serial.begin(9600);
  
  // Init Display.
  display.init();
  display.flipScreenVertically();
  showTextRectangle("Init", String(ESP.getChipId(),HEX),true);

  // Enable enabled sensors.
  if (hasPM) ag.PMS_Init();
  if (hasCO2) ag.CO2_Init();
  if (hasSHT) ag.TMP_RH_Init(0x44);

  if (connectWIFI) connectToWifi();
  delay(2000);

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Hostname: ");
  Serial.println(WiFi.hostname());
  server.on("/", HandleRoot);
  server.on("/metrics", HandleRoot);
  server.onNotFound(HandleNotFound);

  server.begin();
  Serial.println("HTTP server started at ip " + WiFi.localIP().toString() + ":" + String(port));
  showTextRectangle("Listening To", WiFi.localIP().toString() + ":" + String(port),true);
  delay(1000);
}

void loop() {
  long t = millis();

  server.handleClient();
  updateScreen(t);
}

  // Wifi Manager
  void connectToWifi() {
    WiFi.mode(WIFI_STA); // set station mode
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.printDiag(Serial);
    WiFiManager wifiManager;
    //WiFi.disconnect(); //to delete previous saved hotspot
    String HOTSPOT = "Airsense-" + String(ESP.getChipId(), HEX);
    WiFi.printDiag(Serial);
    wifiManager.setHostname(HOTSPOT);
    wifiManager.setTimeout(120);
    WiFi.printDiag(Serial);
    if (!wifiManager.autoConnect((const char * ) HOTSPOT.c_str())) {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      ESP.restart();
      delay(5000);
    }
  }

String GenerateMetrics() {
  String message = "";
  String HOTSPOT = "Airsense-" + String(ESP.getChipId(), HEX);
  String idString = "{id=\"" + HOTSPOT + "\"}";

  if (hasPM) {
    int stat = ag.getPM2_Raw();
    if (stat == 0) {
      //pm2 value of 0 is plain wrong, we need to change that (take last remembered value or default of 22)
      for(int i=0; i < 3; i++) {
        //try again of a maximum of 3 times
        delay(100);
        stat = ag.getPM2_Raw();
        if (stat != 0) break;
      }
      //if it is still 0 after 3 retries, change read value to last remembered pm2 value
      if (stat == 0) {
        stat = pm2_remember;
      }
      else {
        pm2_remember = stat;
      }
    }
    else {
      //save read pm2 value to pm2 value to remember
      pm2_remember = stat;
    }

    message += "# HELP pm02 Particulate Matter PM2.5 value\n";
    message += "# TYPE pm02 gauge\n";
    message += "pm02";
    message += idString;
    message += String(stat);
    message += "\n";
  }

  if (hasCO2) {
    int stat = ag.getCO2_Raw();

    message += "# HELP rco2 CO2 value, in ppm\n";
    message += "# TYPE rco2 gauge\n";
    message += "rco2";
    message += idString;
    message += String(stat);
    message += "\n";
  }

  if (hasSHT) {
    TMP_RH stat = ag.periodicFetchData();

    message += "# HELP atmp Temperature, in degrees Celsius\n";
    message += "# TYPE atmp gauge\n";
    message += "atmp";
    message += idString;
    message += String(stat.t + calib_temp);
    message += "\n";

    message += "# HELP rhum Relative humidity, in percent\n";
    message += "# TYPE rhum gauge\n";
    message += "rhum";
    message += idString;
    message += String(stat.rh);
    message += "\n";
  }

  return message;
}

void HandleRoot() {
  server.send(200, "text/plain", GenerateMetrics() );
}

void HandleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/html", message);
}

// DISPLAY
void showTextRectangle(String ln1, String ln2, boolean small) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  if (small) {
    display.setFont(ArialMT_Plain_10);
  } else {
    display.setFont(ArialMT_Plain_24);
  }
  display.drawString(32, 16, ln1);
  display.drawString(32, 36, ln2);
  display.display();
}

void updateScreen(long now) {
  if ((now - lastUpdate) > updateFrequency) {
    WiFi.printDiag(Serial);
    // Take a measurement at a fixed interval.
    switch (counter) {
      case 0:
        if (hasPM) {
          int stat = ag.getPM2_Raw();
          showTextRectangle("PM2",String(stat),false);
        }
        break;
      case 1:
        if (hasCO2) {
          int stat = ag.getCO2_Raw();
          showTextRectangle("CO2", String(stat), false);
        }
        break;
      case 2:
        if (hasSHT) {
          TMP_RH stat = ag.periodicFetchData();
          showTextRectangle("TMP", String(stat.t + calib_temp, 1) + "C", false);
        }
        break;
      case 3:
        if (hasSHT) {
          TMP_RH stat = ag.periodicFetchData();
          showTextRectangle("HUM", String(stat.rh) + "%", false);
        }
        break;
      case 4:
        if (showName) {
          showTextRectangle("Airsense-", String(ESP.getChipId(), HEX), true);
        }
        break;
    }
    counter++;
    if (counter > 4) counter = 0;
    lastUpdate = millis();
  }
}
