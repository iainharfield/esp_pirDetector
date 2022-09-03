/****************************************************************************************************************************
  FullyFeatureSSL_ESP8266.ino 2022

  AsyncMqttClient_Generic is a library for ESP32, ESP8266, Protenta_H7, STM32F7, etc. with current AsyncTCP support

  Based on and modified from :

  1) async-mqtt-client (https://github.com/marvinroger/async-mqtt-client)

  Built by Khoi Hoang https://github.com/khoih-prog/AsyncMqttClient_Generic
 *****************************************************************************************************************************/

#include <ESP8266WiFi.h>
#include <time.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
#include <ESPAsync_WiFiManager.h> // needed for SSID and Password config
#include <AsyncMqtt_Generic.h>    // compile issues need to edit json file
#include "LittleFS.h"
#include <ArduinoJson.h>

#include "defines.h"
#include "utilities.h"

WiFi_STA_IPConfig WM_STA_IPconfig;

void connectToWifi();
void onWifiConnect(const WiFiEventStationModeGotIP &event);
void onWifiDisconnect(const WiFiEventStationModeDisconnected &event);
void onMqttConnect(bool);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttSubscribe(const uint16_t &packetId, const uint8_t &qos);
void onMqttUnsubscribe(const uint16_t &packetId);
void onMqttMessage(char *topic, char *payload, const AsyncMqttClientMessageProperties &properties, const size_t &len, const size_t &index, const size_t &total);
void onMqttPublish(const uint16_t &packetId);
void mqttTopicsubscribe(const char *topic, int qos);
void mqttDisconnect();
bool mqttGetConnectedStatus();
String mqttGetClientID();
void platform_setup(bool);

void wifianagerconfigneboot();
int loadFileFSConfigFile();
bool saveFileFSConfigFile();
void setWiFiConfigOnBoot(String);
void wifiSetupConfig(bool);
void initSTAIPConfigStruct(WiFi_STA_IPConfig &);

void todNTPUpdate();
void showTime();
String getFormattedTime();
void ota_setup();

// Implementation in telnet.cpp
extern void telnetBegin();
extern void printTelnet(String);
// Implementation in application code
extern bool onMqttMessageAppExt(char *topic, char *payload, const AsyncMqttClientMessageProperties &properties, const size_t &len, const size_t &index, const size_t &total);
extern bool onMqttMessageCntrlExt(char *, char *, const AsyncMqttClientMessageProperties &, const size_t &, const size_t &, const size_t &);

extern void appMQTTTopicSubscribe();
extern void cntrlMQTTTopicSubscribe();
extern void processAppTOD_Ext();
extern void processCntrlTOD_Ext();

extern devConfig espDevice;
extern String deviceType;
extern String deviceName;

extern bool telnetReporting;
extern int reporting;

// const char *PubTopic  = "async-mqtt/ESP8266_Pub";               // Topic to publish

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;

Ticker mqttReconnectTimer;
Ticker wifiReconnectTimer;
Ticker todUpdateTimer;
Ticker processTOD;

AsyncMqttClient mqttClient;
char mqttClientID[MAX_CFGSTR_LENGTH];
char willTopic[MAX_CFGSTR_LENGTH];
uint16_t packetIdSub;

char ntptod[MAX_CFGSTR_LENGTH];

const char *iotBoard = ARDUINO_BOARD;

time_t now; // this is the epoch

bool ntpTODReceived = false;
bool ohTODReceived = false;

int dayNum = -1; // 0=Sun 1=Mon, 2=Tue 3=Wed, 4=Thu 5=Fri, 6=Sat
// bool weekDay = true;
int ohTimenow = 0;   //1234 mans 12:34

// wifi server config
// Set up LittleFS
#define FileFS LittleFS
#define FS_Name "LittleFS"
#define FILE_NOT_FOUND 2
char configFileName[] = "/platform/config.json";
char wifiConfigOnboot[10] = "NO";
char mqttBrokerIPAddr[16] = "192.168.0.1";
char mqttBrokerPort[5] = "1883";
char esp_ipAddress[15];
char esp_GWipAddress[15];
char esp_netMask[15];

// Keep local copy of Wifi credentials
String Router_SSID;
String Router_Pass;

char ipAddr[MAX_CFGSTR_LENGTH];

templateServices coreServices(false); // initialise

void platform_setup(bool configWiFi)
{
  // Serial.println(ASYNC_MQTT_GENERIC_VERSION);
  uint16_t int_mqttBrokerPort;

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);
  String temp = mqttBrokerPort;
  int_mqttBrokerPort = temp.toInt();
  mqttClient.setServer(mqttBrokerIPAddr, int_mqttBrokerPort);

  configTime(MY_TZ, MY_NTP_SERVER); // doing this before wifi is connected - seems to ok!

  int rslt = loadFileFSConfigFile();
  if (rslt == FILE_NOT_FOUND)
  {
    Serial.println("Config file not found");
    saveFileFSConfigFile(); // initialise config with defult values
  }

  wifiSetupConfig(configWiFi);

  connectToWifi();
}

void wifiSetupConfig(bool configWiFi)
{
#define HTTP_PORT 80

  char customhtml_ipv4[150] = "type=\"text\" minlength=\"7\" maxlength=\"15\" size=\"15\" pattern=\"^((\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])\\.){3}(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])$\"";
  char customhtml_port[150] = "type=\"text\" minlength=\"4\" maxlength=\"4\" size=\"4\"pattern=\"[0-9]{4}\"";

  AsyncWebServer webServer(HTTP_PORT);
  DNSServer dnsServer;

  ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer); //, "Personalized-HostName");
  ESPAsync_WMParameter p_mqttBrokerIP(p_mqttBrokerIP_Label, "MQTT Broker IP address", mqttBrokerIPAddr, 16, customhtml_ipv4, WFM_LABEL_AFTER);
  ESPAsync_WMParameter p_mqttBrokerPort(p_mqttBrokerPort_Label, "MQTT Broker Port", mqttBrokerPort, 5, customhtml_port, WFM_LABEL_AFTER);

  ESPAsync_wifiManager.addParameter(&p_mqttBrokerIP);
  ESPAsync_wifiManager.addParameter(&p_mqttBrokerPort);

  Router_SSID = ESPAsync_wifiManager.WiFi_SSID();
  Router_Pass = ESPAsync_wifiManager.WiFi_Pass();

  // if not SSID or password not set then start Config Portal
  if (Router_SSID == "" || Router_Pass == "") // if either not set assume an inital config needed
  {
    configWiFi = true;
  }
  if (strcmp(wifiConfigOnboot, "YES") == 0)
  {
    Serial.println("wifiConfigOnboot == YES");
    configWiFi = true;
    setWiFiConfigOnBoot("NO");
  }
  if (configWiFi == true)
  {
    initSTAIPConfigStruct(WM_STA_IPconfig);
    String portalSSID = "ESP-" + espDevice.getType() + "-" + espDevice.getName();
    Serial.println("ESP Self-Stored: SSID = " + portalSSID);

    ESPAsync_wifiManager.setConfigPortalTimeout(0);

    ESPAsync_wifiManager.setSTAStaticIPConfig(WM_STA_IPconfig);
    ESPAsync_wifiManager.startConfigPortal(portalSSID.c_str());

    // get the updated values and write to config file.  Note SSID and PASS are stored by the portal service.
    Router_SSID = ESPAsync_wifiManager.WiFi_SSID();
    Router_Pass = ESPAsync_wifiManager.WiFi_Pass();
    strcpy(mqttBrokerIPAddr, p_mqttBrokerIP.getValue());
    strcpy(mqttBrokerPort, p_mqttBrokerPort.getValue());

    saveFileFSConfigFile();

    configWiFi = false;
  }
  Serial.println("ESP Self-Stored: SSID = " + Router_SSID + ", Pass = " + Router_Pass + ", Broker IP = " + mqttBrokerIPAddr + ", Broker Port = " + mqttBrokerPort);
}

void mqttTopicsubscribe(const char *topic, int qos)
{
  packetIdSub = mqttClient.subscribe(topic, qos);
}

void mqttDisconnect()
{
  mqttClient.disconnect(true);
}

bool mqttGetConnectedStatus()
{
  return mqttClient.connected();
}
String mqttGetClientID()
{
  return mqttClient.getClientId();
}
//

void connectToWifi()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Connecting to Wi-Fi...");
    // WiFi.mode(WIFI_STA);
    WiFi.begin(Router_SSID, Router_Pass);
  }
  else
    Serial.println("Already connected to Wi-Fi...");
}

void connectToMqtt()
{
  Serial.println("Connecting to MQTT...");

  memset(willTopic, 0, sizeof willTopic);
  String dt = espDevice.getType();
  String dn = espDevice.getName();
  dt.toLowerCase(); // needs to be lower case for MQTT topic name
  dn.toLowerCase();

  sprintf(willTopic, "/house/%s/%s/lwt", dt.c_str(),dn.c_str());

  memset(mqttClientID, 0, sizeof mqttClientID);
  sprintf(mqttClientID, "%s:%s", deviceName.c_str(), WiFi.macAddress().c_str());

  mqttClient.setClientId(mqttClientID);
  mqttClient.setWill(willTopic, 1, true, "Offline"); // Check Case
  mqttClient.connect();
}

void onWifiConnect(const WiFiEventStationModeGotIP &event)
{
  (void)event;

  Serial.print("Connected to Wi-Fi. IP address: ");
  Serial.println(WiFi.localIP());

  sprintf(esp_ipAddress, "%s", WiFi.localIP().toString().c_str());
  sprintf(esp_GWipAddress, "%s", WiFi.gatewayIP().toString().c_str());
  sprintf(esp_netMask, "%s", WiFi.subnetMask().toString().c_str());
  saveFileFSConfigFile();

  // configTime(MY_TZ, MY_NTP_SERVER);  // doing this before wifi is connected - seems to ok!

  Serial.print("Start NTP timer");
  todUpdateTimer.attach(30, todNTPUpdate);

  Serial.print("Start OTA setup");
  ota_setup();

  telnetBegin();

  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event)
{
  (void)event;

  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);

  todUpdateTimer.detach(); // id no wifi dont use NTP - seems to cause huge delays
}

void printSeparationLine()
{
  Serial.println("************************************************");
}

void onMqttConnect(bool sessionPresent)
{
  mqttLog("MQTT Connected.", true, true);
  // printTelnet("connected to MQTT");
  //Serial.print("Connected to MQTT broker: ");
  //Serial.print(mqttBrokerIPAddr);
  //Serial.print(", port: ");
  //Serial.println(mqttBrokerPort);

  // Serial.print("Session present: "); Serial.println(sessionPresent);
mqttLog(willTopic, true, true);
  mqttClient.publish(willTopic, 1, true, "online");

  // Subscribe to Managment topics
  packetIdSub = mqttClient.subscribe(oh3CommandIOT, 2);
  packetIdSub = mqttClient.subscribe(oh3CommandTOD, 2);

  //*********************************************************
  // FIX THIS mage objet calss and configurable
  // Launch any application subscriptions
  // this function must be imlemented by the application
  //**********************************************************

  cntrlMQTTTopicSubscribe();
  appMQTTTopicSubscribe();
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
  (void)reason;

  Serial.println("Disconnected from MQTT.");
  // printTelnet("Disconnected from MQTT");

  if (WiFi.isConnected())
  {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttSubscribe(const uint16_t &packetId, const uint8_t &qos)
{
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(const uint16_t &packetId)
{
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttPublish(const uint16_t &packetId)
{
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char *topic, char *payload, const AsyncMqttClientMessageProperties &properties, const size_t &len, const size_t &index, const size_t &total)
{

  char logString[MAX_LOGSTRING_LENGTH];
  char iotCmd[10][21]; // 10 strings each 20 chars long
  long timeNow[4];     // Contains Current time of day [HH],[MM]

  // char str2[100];  //FIX THIS
  // memset(str2,0, sizeof str2);

  (void)payload;

  char mqtt_payload[len + 1];
  mqtt_payload[len] = '\0';
  strncpy(mqtt_payload, payload, len);

  if (reporting == REPORT_DEBUG)
  {
    mqttLog(mqtt_payload, true, true);
  }

  /*    Serial.println("Publish received.");
      Serial.print("  topic: ");
      Serial.println(topic);
      Serial.print("  qos: ");
      Serial.println(properties.qos);
      Serial.print("  dup: ");
      Serial.println(properties.dup);
      Serial.print("  retain: ");
      Serial.println(properties.retain);
      Serial.print("  len: ");
      Serial.println(len);
      Serial.print("  index: ");
      Serial.println(index);
      Serial.print("  total: ");
      Serial.println(total);
      Serial.print("  payload: ");
      Serial.println(mqtt_payload);
      */

  /******************************************
   * Time of day event
   *     Process in times
   *     process is in NEXT mode
   ******************************************/
  if (strcmp(topic, oh3CommandTOD) == 0)
  {

    dayNum = get_weekday(mqtt_payload);
    coreServices.setDayNumber(dayNum);

    int day, year, month, hr, min, sec;
    sscanf(mqtt_payload, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hr, &min, &sec);
    timeNow[0] = hr;
    timeNow[1] = min;
    ohTimenow = (timeNow[0] * 100) + timeNow[1];

    ohTODReceived = true;

    // Notify Controller and Application that TOD has been received
    processTOD.once(2, processCntrlTOD_Ext);
  }
  else if (strcmp(topic, oh3CommandIOT) == 0)
  {
    int i = 0;
    char *pch;

    /*
     * Parse the string into an array of strings
     */

    pch = strtok(mqtt_payload, ","); // Get the fist string
    while (pch != NULL)
    {
      memset(iotCmd[i], 0, sizeof iotCmd[i]);
      strcpy(iotCmd[i], pch);
      pch = strtok(NULL, ",");
      if (reporting == REPORT_DEBUG)
      {
        mqttLog(iotCmd[i], true, true);
      }
      i++;
    }
    if (strcmp(iotCmd[0], "IOT-IDENTITY") == 0) //
    {
      String dt = espDevice.getType();
      String dn = espDevice.getName();
      dt.toLowerCase(); // needs to be lower case for MQTT topic name
      dn.toLowerCase();
      String oh3CommandIdentity = "/house/" + dt + "/" + dn + "/identity";

      memset(logString, 0, sizeof logString);
      sprintf(logString, "%s%s,%s%s", "ipAddr:", WiFi.localIP().toString().c_str(), "SSID:", Router_SSID.c_str());
      mqttLog(logString, true, true);

      memset(logString, 0, sizeof logString);
      sprintf(logString, "%s,%s,%s,%s,%s", iotBoard, deviceType.c_str(), deviceName.c_str(), WiFi.localIP().toString().c_str(), Router_SSID.c_str());
      mqttClient.publish(oh3CommandIdentity.c_str(), 1, true, logString);
    }
    if (strcmp(iotCmd[0], "IOT-RESET") == 0) //
    {
      if (strcmp(iotCmd[1], WiFi.localIP().toString().c_str()) == 0)
      {
        memset(logString, 0, sizeof logString);
        sprintf(logString, "%s%s,%s%s", "ipAddr:", WiFi.localIP().toString().c_str(), "SSID:", Router_SSID.c_str());
        mqttLog(logString, true, true);

        ESP.restart();
      }
    }
  }
  //****************************
  // Implement contoller and Application MQTT   FIXTHIS - shit logic
  //****************************
  onMqttMessageCntrlExt(topic, payload, properties, len, index, total);
  
  onMqttMessageAppExt(topic, payload, properties, len, index, total);
  //      {
  //          memset(logString, 0, sizeof logString);
  //          sprintf(logString, "%s%s %s", "Template: Unknown message received: ", topic, mqtt_payload);
  //          mqttLog(logString, true, true);
  //      }    
  //}
}

void todNTPUpdate()
{
  memset(ntptod, 0, sizeof ntptod);
  sprintf(ntptod, "%s", getFormattedTime().c_str());

  if (getFormattedTime().substring(0, 4) != "1970") // seem to take 30 to 60seconds. MQTT connection should be done by then?  Mmmm not sure I like!
  {
    if (reporting == REPORT_DEBUG)
    {
      mqttLog(ntptod, true, true);
    }
    // todUpdateTimer.detach();  // TOD updated so stop looking
    ntpTODReceived = true;
  }
}

void ota_setup()
{
  Serial.println("Setting Up OTA");
  /************************
   * OTA setup
   ************************/
  ArduinoOTA.onStart([]()
                     { Serial.println("Start"); });

  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
		  Serial.printf("Error[%u]: ", error);
		  if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		  else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		  else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		  else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		  else if (error == OTA_END_ERROR) Serial.println("End Failed"); });
  ArduinoOTA.begin();
}

String getFormattedTime()
{
  // time_t rawtime = timeClient.getEpochTime();
  time_t rawtime = time(&now);
  struct tm *ti;
  ti = localtime(&rawtime);

  uint16_t year = ti->tm_year + 1900;
  String yearStr = String(year);

  uint8_t month = ti->tm_mon + 1;
  String monthStr = month < 10 ? "0" + String(month) : String(month);

  uint8_t day = ti->tm_mday;
  String dayStr = day < 10 ? "0" + String(day) : String(day);

  uint8_t hours = ti->tm_hour;
  String hoursStr = hours < 10 ? "0" + String(hours) : String(hours);

  uint8_t minutes = ti->tm_min;
  String minuteStr = minutes < 10 ? "0" + String(minutes) : String(minutes);

  uint8_t seconds = ti->tm_sec;
  String secondStr = seconds < 10 ? "0" + String(seconds) : String(seconds);

  return yearStr + "-" + monthStr + "-" + dayStr + "T" +
         hoursStr + ":" + minuteStr + ":" + secondStr;
}

bool saveFileFSConfigFile()
{
  Serial.println(F("Saving config"));

  DynamicJsonDocument json(1024);

  json["Config_WiFi_OnReboot"] = wifiConfigOnboot;
  json["MQTT_Broker_IP_Address"] = mqttBrokerIPAddr;
  json["MQTT_Broker_Port"] = mqttBrokerPort;
  json["IP_Address"] = esp_ipAddress;
  json["GW_IP_Address"] = esp_GWipAddress;
  json["Net_Mask"] = esp_netMask;

  File configFile = FileFS.open(configFileName, "w");

  if (!configFile)
  {
    Serial.println(F("Failed to open config file for writing"));
    return false;
  }

  // serializeJson(json, Serial);
  serializeJsonPretty(json, Serial);
  Serial.println();
  // Write data to file
  serializeJson(json, configFile);
  // Close file
  configFile.close();

  return true;
}

int loadFileFSConfigFile()
{
  // clean FS, for testing
  // FileFS.format();

  // read configuration from FS json
  Serial.println(F("Mounting FS..."));

  if (FileFS.begin())
  {
    // Serial.println(F("Mounted file system"));

    if (FileFS.exists(configFileName))
    {
      // file exists, reading and loading
      Serial.println(F("Reading config file"));
      File configFile = FileFS.open(configFileName, "r");

      if (configFile)
      {
        Serial.print(F("Opened config file, size = "));
        size_t configFileSize = configFile.size();
        Serial.println(configFileSize);

        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[configFileSize + 1]);

        configFile.readBytes(buf.get(), configFileSize);

        Serial.print(F("\nJSON parseObject() result : "));

        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get(), configFileSize);

        if (deserializeError)
        {
          Serial.println(F("failed"));
          configFile.close();
          return 3;
        }
        else
        {
          Serial.println(F("OK"));

          if (json["Config_WiFi_OnReboot"])
          {
            Serial.println(F("Initialising wifiConfigOnReboot"));
            strncpy(wifiConfigOnboot, json["Config_WiFi_OnReboot"], sizeof(wifiConfigOnboot));
          }
          if (json["MQTT_Broker_IP_Address"])
          {
            Serial.println(F("Initialising mqttBrokerIPAddr"));
            strncpy(mqttBrokerIPAddr, json["MQTT_Broker_IP_Address"], sizeof(mqttBrokerIPAddr));
          }
          if (json["MQTT_Broker_Port"])
          {
            Serial.println(F("Initialising mqttBrokerPort"));
            strncpy(mqttBrokerPort, json["MQTT_Broker_Port"], sizeof(mqttBrokerPort));
          }
          if (json["IP_Address"])
          {
            Serial.println(F("Fetching last IP Address"));
            strncpy(esp_ipAddress, json["IP_Address"], sizeof(esp_ipAddress));
          }
          if (json["GW_IP_Address"])
          {
            Serial.println(F("Fetching last GW IP Address"));
            strncpy(esp_GWipAddress, json["GW_IP_Address"], sizeof(esp_GWipAddress));
          }
          if (json["Net_Mask"])
          {
            Serial.println(F("Fetching last network mask"));
            strncpy(esp_netMask, json["Net_Mask"], sizeof(esp_netMask));
          }
        }
        // serializeJson(json, Serial);
        serializeJsonPretty(json, Serial);
        configFile.close();
      }
    }
    else
    {
      Serial.println(F("Config file does not exist"));
      return FILE_NOT_FOUND;
    }
  }
  else
  {
    Serial.println(F("failed to mount FS"));
    return 1;
  }
  return 0;
}

void setWiFiConfigOnBoot(String state)
{
  if (state == "YES" || state == "NO")
  {
    strcpy(wifiConfigOnboot, state.c_str());
    saveFileFSConfigFile();
  }
}

void deleteConfigfile()
{
  LittleFS.remove(configFileName);
}

void initSTAIPConfigStruct(WiFi_STA_IPConfig &in_WM_STA_IPconfig)
{
  IPAddress ip;
  IPAddress gwip;
  IPAddress netmask;
  ip.fromString(esp_ipAddress);
  gwip.fromString(esp_GWipAddress);
  netmask.fromString(esp_netMask);

  IPAddress stationIP = ip;
  IPAddress gatewayIP = gwip;
  IPAddress netMask = netmask;
  in_WM_STA_IPconfig._sta_static_ip = stationIP;
  in_WM_STA_IPconfig._sta_static_gw = gatewayIP;
  in_WM_STA_IPconfig._sta_static_sn = netMask;
}