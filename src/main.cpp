#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "FS.h"
#include "SPIFFS.h"
#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <qrcode.h>
#include "credentials.h"
#define CSV_PARSER_DONT_IMPORT_SD
#include <CSV_Parser.h>

GxEPD2_BW<GxEPD2_213_B73, GxEPD2_213_B73::HEIGHT> display(GxEPD2_213_B73(/*CS=5*/ 5, /*DC=*/ 17, /*RST=*/ 16, /*BUSY=*/ 4)); // GDEH0213B73

/* You only need to format SPIFFS the first time you run a
   test or else use the SPIFFS plugin to create a partition
   https://github.com/me-no-dev/arduino-esp32fs-plugin */
#define FORMAT_SPIFFS_IF_FAILED true
#define RELAY_PIN 27
#define POTENTIO_PIN 34



#define CREDENTIALS_FILE "/credentials.txt"

#define SEALEVELPRESSURE_HPA (1013.25)

#include <functional>


char clientId[20];
bool storedCredentials = false;
int relayState = HIGH;
int timer;

String pubChannel;

WiFiClient wifiClient;
PubSubClient _client = PubSubClient(wifiClient);//, clientId);
Adafruit_BME280 bme;


//Serial Number taken from the ESP32, not tested on ESP8266
void getSerialNumber() {

  uint64_t chipid = ESP.getEfuseMac();
  uint16_t chip = (uint16_t)(chipid >> 32);

  snprintf(clientId, 19, "ESP32-%04X%08X", chip, (uint32_t)chipid);

  Serial.printf("Serial Number is: %s\n", clientId);
}

void helloWorld()
{
  display.setRotation(1);
  uint16_t x = (display.width() - 160) / 2;
  uint16_t y = display.height() / 2;
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(x, y); // start writing at this position
    display.setTextSize(2);
    display.print("Hello World!");
  }
  while (display.nextPage());
  //Serial.println("helloWorld done");
}

//connect to a WiFi access point
void connectWifi() {
  WiFi.begin(ssid, wifiPassword);

  Serial.print("Connecting to WiFi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    if (tries++ > 10) {
      WiFi.begin(ssid, wifiPassword);
      tries = 0;
    }

  }
  Serial.println("connected to wifi");
}

int switchWindmill(char* templateCode, char* payload) {

  Serial.printf("switchWindmill(template: %s, payload: %s)\n", templateCode, payload);

  if (strcmp("CLOSED", payload)==0) {
    relayState = HIGH;
  } else {
    relayState = LOW;
  }

  Serial.printf("writing %d to pin %d.", relayState, RELAY_PIN);

  digitalWrite(RELAY_PIN, relayState);
  
  return 0;
}

void handleOperation(char* payload) {

    Serial.printf("handleOperation(payload: %s)\n", payload);

    String myPayload = payload;
    myPayload += "\r\n";
    CSV_Parser cp(myPayload.c_str(), "sss", false);

    char* templateCode = ((char**)cp[0])[0];
    char* myClientId = ((char**)cp[1])[0];
    char* content = ((char**)cp[2])[0];

    Serial.printf("template: %s, myClientId: %s, clientId: %s.\n", templateCode, myClientId, clientId);
    if(true) { //strcmp(clientId, myClientId)) {
        String fragment;
        String message;

        if (strcmp(templateCode, "518")==0) {
            fragment = "c8y_Relay";
        } else if (strcmp(templateCode, "510")==0) {
            fragment = "c8y_Restart";
        } else if (strcmp(templateCode, "511")==0) {
            fragment = "c8y_Command";
        } else if (strcmp(templateCode, "513")==0) {
            fragment = "c8y_Configuration";
        } else if (strcmp(templateCode, "515")==0) {
            fragment = "c8y_Firmware";
        } else if (strcmp(templateCode, "516")==0) {
            fragment = "c8y_Software";
        } else if (strcmp(templateCode, "519")==0) {
            fragment = "c8y_RelayArray";
        }

        message = "501," + fragment;
        _client.publish(pubChannel.c_str(), message.c_str());

        int status = switchWindmill(templateCode, content);

        if (status == 0) {
            message = "503," + fragment;
            _client.publish(pubChannel.c_str(), message.c_str());
        } else {
            message = "502," + fragment;
            _client.publish(pubChannel.c_str(), message.c_str());
        }
    }
}

void callbackHandler(char* topic, byte* payload, unsigned int length) {

  //Serial.printf("message on topic: %s\n", topic);
	//Serial.printf("callbackHandler(topic: %s, payload: %s)\n", topic, payload);

	char myPayload[length+1];

	strncpy(myPayload, (char*)payload,length);
	myPayload[length] = '\0';

	if (strcmp(topic, "c8y/s/ds") == 0 && length > 0) {
    handleOperation(myPayload);
  }
}

bool connect(char* host) {

    String myClientId = "d:"; 
    myClientId += clientId;

    char* _clientId = (char*) malloc(myClientId.length() +1);
    strcpy(_clientId,myClientId.c_str());

    _client.setServer(host, 1883);

    bool success = _client.connect(_clientId, "s/us", 0, 
        false, "400,c8y_ConnectionEvent,\"Connections lost.\"");

    if (!success) {

        Serial.print("Unable to connect to Cumulocity ");
        Serial.println(_client.state());
    } else {
        Serial.println("Connected to cumulocity.");
        _client.setCallback(callbackHandler);
    }

    return success;
}

void registerDevice(char* deviceName, char* deviceType){

	Serial.printf("registerDevice(%s, %s)\n", deviceName, deviceType);

	char mqttMessage[1024];
	// = (char*) malloc(strlen(_deviceId) + strlen(deviceType)+6);
	sprintf(mqttMessage, "100,%s,%s", deviceName, deviceType);

	_client.publish(pubChannel.c_str(), mqttMessage);
}

void setSupportedOperations(char* operations) {
    
  Serial.printf("setSupportedOperations(%s)\n", operations);

  String myOperations = "114,";
  myOperations += operations;
	_client.publish(pubChannel.c_str(), myOperations.c_str());
  _client.subscribe("c8y/s/ds");
}

void getPendingOperations() {

    Serial.printf("getPendingOperations()\n");

    _client.publish(pubChannel.c_str(), "500");
}

void createMeasurement(char* fragment, char* series, char* value, char* unit) {

  char mqttMessage[2048];//  = (char*) malloc(8+ strlen(fragment)+ strlen(series) + strlen(value) + strlen(unit));
	sprintf(mqttMessage, "200,%s,%s,%s,%s", fragment, series, value, unit);
	_client.publish(pubChannel.c_str(), mqttMessage);
}

void sendData() {

  Serial.println("SendData()");
  char* fragment = "c8y_Gearbox";
  char* series = "Temperature";
  char value[50]; 
  sprintf(value, "%.2f", bme.readTemperature()*4.3);
  char* uom = "*C";
  createMeasurement(fragment, series, value, uom);
  Serial.print("Send Temperature: ");
  Serial.println(value);
  
  series = "Humidity";
  sprintf(value, "%.2f", bme.readHumidity());
  uom = "%";
  createMeasurement(fragment, series, value, uom);
  Serial.print("Send Humidity: ");
  Serial.println(value);
  
  series = "Pressure";
  sprintf(value, "%.2f", bme.readPressure()/100.0F);
  uom = "hPa";
  createMeasurement(fragment, series, value, uom);
  Serial.print("Send Pressure: ");
  Serial.println(value);

  series = "Altitude";
  sprintf(value, "%.2f", bme.readAltitude(SEALEVELPRESSURE_HPA));
  uom = "m";
  createMeasurement(fragment, series, value, uom);
  Serial.print("Approx. Altitude = ");
  Serial.print(value);
  Serial.println(" m");

  series = "Vibration";
  sprintf(value, "%d", analogRead(POTENTIO_PIN));
  uom = "nm/s";
  createMeasurement(fragment, series, value, uom);
  Serial.print("Send Vibration: ");
  Serial.println(value);
}

void drawBox(int box_x, int box_y, int box_w, int box_h) {
  Serial.printf("width: %d, height: %d", display.width(), display.height());
  display.setRotation(1);
  display.setPartialWindow(box_x, box_y, box_w, box_h);
  display.firstPage();
  do {
    display.fillRect(box_x, box_y, box_w, box_h, GxEPD_BLACK);
    display.fillRect(box_x+2, box_y+2, 24, 24, GxEPD_WHITE);
    display.fillRect(box_x+28,box_y+2, box_w-31, box_h-4, GxEPD_WHITE);
  } while (display.nextPage());
}

void printQRCode() {
  // Create the QR code
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(5)];
  qrcode_initText(&qrcode, qrcodeData, 5, ECC_MEDIUM, "116697");//"https://iotedge.servers/apps/cockpit/index.html#/device/621/dashboard/574193");

  byte box_x = 5;
  byte box_y = 5;
  byte box_s = 3.5;
  byte init_x = box_x;

  //display.clearScreen();
  
  display.firstPage();
  do {

    display.fillScreen(GxEPD_WHITE);

    for (uint8_t y = 0; y < qrcode.size; y++) {
      // Each horizontal module
      for (uint8_t x = 0; x < qrcode.size; x++) {
        if(qrcode_getModule(&qrcode, x, y)){
          display.fillRect(box_x, box_y, box_s, box_s, GxEPD_BLACK);
        } else {
        }
        box_x = box_x + box_s;
      }
      box_y = box_y + box_s;
      box_x = init_x;
    }

  } while (display.nextPage());

  box_x=130, box_y=16;
  int box_w=120, box_h=28;
  drawBox(box_x, box_y, box_w, box_h);
  box_y +=32;
  drawBox(box_x, box_y, box_w, box_h);
  box_y +=32;
  drawBox(box_x, box_y, box_w, box_h);
  //display.display();
}

//============================================================================
// Setup and Loop methods
//============================================================================

void setup() {

  Serial.begin(115200);

  getSerialNumber();

  pubChannel = "c8y/s/us/";
  pubChannel = pubChannel + clientId;
  display.init(115200, false, 20, false);

  printQRCode();
  //helloWorld();
  display.powerOff();

  bool status = bme.begin(0x76);
  
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
  }
  
  pinMode(POTENTIO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, HIGH);

  connectWifi();


  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
      Serial.println("SPIFFS Mount Failed");
      return;
  }

  connect(host);

  registerDevice(clientId, "c8y_esp32");
  
  setSupportedOperations("c8y_Relay,c8y_Command,c8y_Restart");

  getPendingOperations();

}

void loop() {
  
  delay(500);
  bool myConnected = _client.loop();

  if (!myConnected) {
    connect(host);
  }

  timer += 500;
  if (timer >= 10000) {
    sendData();
    timer = 0;
  }

}