#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <SimpleTimer.h>
#include <Roomba.h>


//USER CONFIGURED SECTION START//
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASS";
const char* host = "roomba-web";
const char* mqtt_server = "YOUR_MQTT_SERVER";
const int mqtt_port = 1883;
const char *mqtt_user = "MQTT_USER";
const char *mqtt_pass = "MQTT_PASS";
const char *mqtt_client_name = "Roomba"; // Client connections can't have the same connection name
//USER CONFIGURED SECTION END//


WiFiClient espClient;
PubSubClient client(espClient);
SimpleTimer timer;
Roomba roomba(&Serial, Roomba::Baud115200);


// Variables
bool boot = true;
long battery_Current_mAh = 0;
long battery_Voltage = 0;
long battery_Total_mAh = 0;
long battery_percent = 0;
char battery_percent_send[50];
char battery_Current_mAh_send[50];
uint8_t tempBuf[10];

ESP8266WebServer server(80);
const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

//Functions

void setup_wifi() 
{
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() == WL_CONNECTED) {
    MDNS.begin(host);
    server.on("/", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/html", serverIndex);
    });
    server.on("/update", HTTP_POST, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      ESP.restart();
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.setDebugOutput(true);
        WiFiUDP::stopAll();
        Serial.printf("Update: %s\n", upload.filename.c_str());
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace)) { //start with max available size
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      }
      yield();
    });
    server.begin();
    MDNS.addService("http", "tcp", 80);

    Serial.printf("Ready! Open http://%s.local in your browser\n", host);
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi Failed");
  }
}

void reconnect() 
{
  // Loop until we're reconnected
  int retries = 0;
  while (!client.connected()) 
  {
    if(retries < 50)
    {
      // Attempt to connect
      if (client.connect(mqtt_client_name, mqtt_user, mqtt_pass, "roomba/status", 0, 0, "Dead Somewhere")) 
      {
        // Once connected, publish an announcement...
        if(boot == false)
        {
          client.publish("checkIn/roomba", "Reconnected"); 
        }
        if(boot == true)
        {
          client.publish("checkIn/roomba", "Rebooted");
          boot = false;
        }
        // ... and resubscribe
        client.subscribe("roomba/commands");
      } 
      else 
      {
        retries++;
        // Wait 5 seconds before retrying
        delay(5000);
      }
    }
    if(retries >= 50)
    {
    ESP.restart();
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) 
{
  String newTopic = topic;
  payload[length] = '\0';
  String newPayload = String((char *)payload);
  if (newTopic == "roomba/commands") 
  {
    if (newPayload == "start")
    {
      startCleaning();
    }
    if (newPayload == "stop")
    {
      stopCleaning();
    }
  }
}


void startCleaning()
{
  Serial.write(128);
  delay(50);
  Serial.write(131);
  delay(50);
  Serial.write(135);
  client.publish("roomba/status", "Cleaning");
}

void stopCleaning()
{
  Serial.write(128);
  delay(50);
  Serial.write(131);
  delay(50);
  Serial.write(143);
  client.publish("roomba/status", "Returning");
}

void sendInfoRoomba()
{
  roomba.start(); 
  roomba.getSensors(21, tempBuf, 1);
  battery_Voltage = tempBuf[0];
  delay(50);
  roomba.getSensors(25, tempBuf, 2);
  battery_Current_mAh = tempBuf[1]+256*tempBuf[0];
  delay(50);
  roomba.getSensors(26, tempBuf, 2);
  battery_Total_mAh = tempBuf[1]+256*tempBuf[0];
  if(battery_Total_mAh != 0)
  {
    int nBatPcent = 100*battery_Current_mAh/battery_Total_mAh;
    String temp_str2 = String(nBatPcent);
    temp_str2.toCharArray(battery_percent_send, temp_str2.length() + 1); //packaging up the data to publish to mqtt
    client.publish("roomba/battery", battery_percent_send);
  }
  if(battery_Total_mAh == 0)
  {  
    client.publish("roomba/battery", "NO DATA");
  }
  String temp_str = String(battery_Voltage);
  temp_str.toCharArray(battery_Current_mAh_send, temp_str.length() + 1); //packaging up the data to publish to mqtt
  client.publish("roomba/charging", battery_Current_mAh_send);
}



void setup() 
{
  Serial.begin(115200);
  Serial.write(129);
  delay(50);
  Serial.write(11);
  delay(50);
  WiFi.mode(WIFI_STA);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  timer.setInterval(5000, sendInfoRoomba);
}

void loop() 
{
  server.handleClient();
  MDNS.update();
  if (!client.connected()) 
  {
    reconnect();
  }
  client.loop();
  timer.run();
}

