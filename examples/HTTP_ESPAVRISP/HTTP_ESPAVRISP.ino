#include <SPI.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266AVRISPWebServer.h>
#include <ESP8266mDNS.h>

#include "FS.h"

#define LED_OUT    16
const uint8_t reset_pin = 5;

const char* ssid = "******";
const char* password = "********";

ESP8266AVRISPWebServer server = ESP8266AVRISPWebServer(80, reset_pin);

String html_home;

void request_handler()
{
  server.send(200, "text/html", html_home);
}

void prepareFile() {

  Serial.println("Prepare file system");
  SPIFFS.begin();

  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    Serial.println("file open failed");
  } else {
    Serial.println("file open success");

    html_home = "";
    while (file.available()) {
      //Serial.write(file.read());
      String line = file.readStringUntil('\n');
      html_home += line + "\n";
      if(html_home.length() > 100){        
         Serial.print(html_home);
         html_home = "";
      }
    }
    Serial.print(html_home);
    file.close();
    if(html_home != ""){
       Serial.print(html_home);
       html_home = "";
    }
  }
}

void setup() {
  //Serial.begin(921600);
  Serial.begin(115200);
  server.setReset(false);

  //Serial.setDebugOutput(true);

  Serial.println();
  Serial.println();
  Serial.println();

  //  for(uint8_t t = 4; t > 0; t--) {
  //      Serial.printf("[SETUP] BOOT WAIT %d...\n", t);
  //      Serial.flush();
  //      delay(1000);
  //  }

  pinMode(LED_OUT, OUTPUT);
  digitalWrite(LED_OUT, 1);

  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  prepareFile();

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  // handle index
  //server.on("/", request_handler);
  server.serveStatic("/", SPIFFS, "/index.html");

  server.begin();

  // Add service to MDNS
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);

  digitalWrite(LED_OUT, 0);

  Serial.printf("Server Start\n");
}

int ledState = LOW;

unsigned long previousMillis = 0;
const long interval = 1000;

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    if (ledState == LOW)
      ledState = HIGH;  // Note that this switches the LED *off*
    else
      ledState = LOW;   // Note that this switches the LED *on*
    digitalWrite(LED_OUT, ledState);
  }
  server.handleClient2();
}

