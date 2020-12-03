#include <Arduino.h>
#include <CumulocityClient.h>
#include <WiFi.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "FS.h"
#include "SPIFFS.h"
#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_BW.h>
#include <qrcode.h>
#include "credentials.h"

GxEPD2_BW<GxEPD2_213_B73, GxEPD2_213_B73::HEIGHT> display(GxEPD2_213_B73(/*CS=5*/ 5, /*DC=*/ 17, /*RST=*/ 16, /*BUSY=*/ 4)); // GDEH0213B73

/* You only need to format SPIFFS the first time you run a
   test or else use the SPIFFS plugin to create a partition
   https://github.com/me-no-dev/arduino-esp32fs-plugin */
#define FORMAT_SPIFFS_IF_FAILED true
#define RELAY_PIN 27
#define POTENTIO_PIN 34

#define CREDENTIALS_FILE "/credentials.txt"

#define SEALEVELPRESSURE_HPA (1013.25)


char clientId[20];
bool storedCredentials = false;
int relayState = HIGH;

WiFiClient wifiClient;
CumulocityClient c8yClient(wifiClient, clientId);
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

//read device credentials from the SPIFFS filesystem
void readCredentials(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\r\n", path);

  char buffer[4096];
  
  File file = fs.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to read the file");
    return;
  }
  
  int length = 0;
  while(file.available()){
    char c = file.read();
    buffer[length++] = c;
  }
  buffer[length] = '\0';
  
  tenant = strtok(buffer, "\n");
  Serial.printf("Retrieved tenant: %s\n", tenant);

  username = strtok(NULL, "\n");
  Serial.printf("Retrieved user: %s\n", username);

  c8yPassword = strtok(NULL, "\n");

  if (tenant != NULL && username != NULL && c8yPassword != NULL) {
    Serial.printf("found credentials for %s, %s\n", tenant, username);
    storedCredentials = true;
  }

  file.close();
}

//write data to a file
void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("- failed to open file for writing");
        return;
    }
    file.print(message);
    file.close();
}

//add data to a file
void appendFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Appending to file: %s\r\n", path);

    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("- failed to open file for appending");
        return;
    }
    if(file.print(message)){
        Serial.println("- message appended");
    } else {
        Serial.println("- append failed");
    }
    file.close();
}

// store retrieved credentials to file
void storeCredentials() {
  
  Credentials myCredentials = c8yClient.getCredentials();

  Serial.printf("Writing credentials %s/%s to file", myCredentials.tenant, myCredentials.username);
  
  writeFile(SPIFFS, CREDENTIALS_FILE, myCredentials.tenant);
  appendFile(SPIFFS, CREDENTIALS_FILE, "\n");
  appendFile(SPIFFS, CREDENTIALS_FILE, myCredentials.username);
  appendFile(SPIFFS, CREDENTIALS_FILE, "\n");
  appendFile(SPIFFS, CREDENTIALS_FILE, myCredentials.password);
  appendFile(SPIFFS, CREDENTIALS_FILE, "\n");

  Serial.println("Credentials stored");
}

//make a connection to cumulocity
void connectC8Y() {
  
  c8yClient.setDeviceId(clientId);

  c8yClient.connect(host, tenant, username, c8yPassword);
  
  if (!storedCredentials) {

    Serial.println("Retrieving device credentials");

    c8yClient.retrieveDeviceCredentials();
    while (!c8yClient.checkCredentialsReceived()) {
      Serial.print("#");
      delay(1000);
    }
  
    Serial.println("Reconnecting to Cumulocity");
    
    c8yClient.disconnect();
    c8yClient.reconnect();
  }
}

int switchWindmill(char* templateCode, char* payload) {

  Serial.printf("switchWindmill(template: %s, payload: %s)\n", templateCode, payload);

  if (strcmp("CLOSED", payload)==0) {
    relayState = HIGH;
  } else {
    relayState = LOW;
  }

  digitalWrite(RELAY_PIN, relayState);
  
  return 0;
}

void sendData() {

  Serial.println("SendData()");
  char* fragment = "c8y_Gearbox";
  char* series = "Temperature";
  char value[50]; 
  sprintf(value, "%.2f", bme.readTemperature()*4.3);
  char* uom = "*C";
  c8yClient.createMeasurement(fragment, series, value, uom);
  Serial.print("Send Temperature: ");
  Serial.println(value);
  
  series = "Humidity";
  sprintf(value, "%.2f", bme.readHumidity());
  uom = "%";
  c8yClient.createMeasurement(fragment, series, value, uom);
  Serial.print("Send Humidity: ");
  Serial.println(value);
  
  series = "Pressure";
  sprintf(value, "%.2f", bme.readPressure()/100.0F);
  uom = "hPa";
  c8yClient.createMeasurement(fragment, series, value, uom);
  Serial.print("Send Pressure: ");
  Serial.println(value);

  series = "Altitude";
  sprintf(value, "%.2f", bme.readAltitude(SEALEVELPRESSURE_HPA));
  uom = "m";
  c8yClient.createMeasurement(fragment, series, value, uom);
  Serial.print("Approx. Altitude = ");
  Serial.print(value);
  Serial.println(" m");

  series = "Vibration";
  sprintf(value, "%d", analogRead(POTENTIO_PIN));
  uom = "nm/s";
  c8yClient.createMeasurement(fragment, series, value, uom);
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
  qrcode_initText(&qrcode, qrcodeData, 5, ECC_MEDIUM, "https://iotedge.servers/apps/cockpit/index.html#/device/621/dashboard/574193");

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

  display.init(115200, false, 20, false);

  printQRCode();
  display.powerOff();

  bool status = bme.begin(0x76);
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
  }
  
  pinMode(POTENTIO_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, HIGH);

  connectWifi();

  getSerialNumber();

  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
      Serial.println("SPIFFS Mount Failed");
      return;
  }

  readCredentials(SPIFFS, CREDENTIALS_FILE);
  
  connectC8Y();

  if (!storedCredentials) {
    storeCredentials();
  }

  c8yClient.registerDevice(clientId, "c8y_esp32");
  
  c8yClient.setCallback(switchWindmill);
  
  c8yClient.setSupportedOperations("c8y_Relay,c8y_Command,c8y_Restart");

  c8yClient.getPendingOperations();

}

void loop() {
  
  delay(1000);
  c8yClient.loop();
  sendData();
}