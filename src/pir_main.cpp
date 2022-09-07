
//#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
#include <time.h>
#include <AsyncMqttClient_Generic.hpp>

#define ESP8266_DRD_USE_RTC true
#define ESP_DRD_USE_LITTLEFS false
#define ESP_DRD_USE_SPIFFS false
#define ESP_DRD_USE_EEPROM false
#define DOUBLERESETDETECTOR_DEBUG true
#include <ESP_DoubleResetDetector.h>

#include "defines.h"
#include "utilities.h"

//***********************
// Application functions
//**********************
bool onMqttMessageAppExt(char *, char *, const AsyncMqttClientMessageProperties &, const size_t &, const size_t &, const size_t &);    // Required by template
void appMQTTTopicSubscribe();
void pirRead();
void telnet_extension_1(char);      // Required by template
void telnet_extension_2(char);      // Required by template
void telnet_extensionHelp(char);    // Required by template
void processTOD_Ext();              // Required by template

//******************************
// defined in asyncConnect.cpp
//******************************
extern void mqttTopicsubscribe(const char *topic, int qos);
extern void platform_setup(bool);
extern void handleTelnet();
extern void printTelnet(String);
extern AsyncMqttClient mqttClient;
extern void wifiSetupConfig(bool);
extern char ntptod[MAX_CFGSTR_LENGTH];

// defined in telnet.cpp
//*************************
extern int reporting;
extern bool telnetReporting;

#define DRD_TIMEOUT 3
#define DRD_ADDRESS 0

DoubleResetDetector *drd;

// Application Specific MQTT Topics and config
//const char *oh3StateValue = "/house/pir/garage-side-door/value";  //FIXTHIS
const char *oh3StateValue = "/house/pir/garage-side-door/runtime-state"; // e.g. DETECTION, NO-DETECTION, FORCED-DETECTION

String deviceName = "garage-side-door";
String deviceType = "PIR";

//Ticker sensorReadTimer;
//int sensorValue;

devConfig espDevice;

int outRelayPin         = D5;	// Output. wemos D1. LIght on or off
int inOverrideHighPin   = D0;	// Input1. Manual override. If high hold output ON
int inOverrideLowPin    = D3;	// Input2. Manual override. If low hold output ON. D3 is a pulled up pin.
int inOverrideHighPin2  = D7;   // Input3. Manual override. If high hold output ON

int pinVal1 = 0;    // PIR output. inOverrideHighPin
int pinVal2 = 1;    // inOverrideLowPin
int pinVal3 = 0;    // inOverrideHighPin2

#define pirStateNoDetection     0
#define pirStateDetection       1
#define pirStateForceDetection  2
int pirState = pirStateNoDetection;

bool bManMode = false; // true = Manual, false = automatic

void setup()
{
    bool configWiFi = false;
    Serial.begin(115200);
    while (!Serial)
        delay(300);

    espDevice.setup(deviceName, deviceType);

    Serial.println("\nStarting PIR detector Garage path ");
    Serial.println(ARDUINO_BOARD);

    drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);
    if (drd->detectDoubleReset())
    {
        configWiFi = true;
    }

    // Platform setup: Set up and manage: WiFi, MQTT and Telnet
    platform_setup(configWiFi);

    // Application setup
    //pinMode(A0, INPUT);
    //sensorReadTimer.attach(90, sensorRead); // read every 5 mins

	pinMode(outRelayPin, OUTPUT);
	pinMode(inOverrideLowPin, INPUT);
	pinMode(inOverrideHighPin, INPUT);    

    digitalWrite(outRelayPin, LOW);
}

void loop()
{
    drd->loop();

    // Go look for OTA request
    ArduinoOTA.handle();

    handleTelnet();

    pirRead();
}

// FIXTHIS Check for relay bounce. Somethines see on, off, on
void pirRead()
{
    char logString[MAX_LOGSTRING_LENGTH];

    bManMode = false;
	pinVal1 = digitalRead(inOverrideHighPin);  // detector
	pinVal2 = digitalRead(inOverrideLowPin);   // override 1
	pinVal3 = digitalRead(inOverrideHighPin2); // override 2

	if (pinVal1 == 1 || pinVal2 == 0 || pinVal3 == 1)
	{

		bManMode = true;
		memset(logString, 0, sizeof logString);
		if (pinVal1 == 1 )
		{
            if (pirState != pirStateDetection)
            {
			    sprintf(logString, "%s,%s,%s,%s", ntptod, espDevice.getType().c_str(), espDevice.getName().c_str(), "PIR Detection.");
                printTelnet((String)logString);
                mqttLog(logString, true, true);
                pirState = pirStateDetection;
                mqttClient.publish(oh3StateValue, 1, true, "DETECTION");
            }
            else
            {
                digitalWrite(outRelayPin, HIGH);
                delay(100);
            }
		}
		else
		{
            if (pirState != pirStateDetection)
            {
			    sprintf(logString, "%s,%s,%s,%s", ntptod, espDevice.getType().c_str(), espDevice.getName().c_str(), "Forced detection.");
                printTelnet((String)logString);
                mqttLog(logString, true, true);

                pirState = pirStateDetection;
                mqttClient.publish(oh3StateValue, 1, true, "FORCED-DETECTION");
            }
            else
            {
                digitalWrite(outRelayPin, HIGH);
                delay(100);
            }
		}
	}
	else
	{
		bManMode = false;

        if  (pirState != pirStateNoDetection)
        {
           //if (reporting == REPORT_DEBUG)
	        //{
					sprintf(logString, "%s,%s,%s,%s", ntptod, espDevice.getType().c_str(), espDevice.getName().c_str(), "No detection.");
                    printTelnet((String)logString);
					mqttLog(logString, true, true);
		    //}
            pirState = pirStateNoDetection;
		    digitalWrite(outRelayPin, LOW);
            delay(100);
            
		    mqttClient.publish(oh3StateValue, 1, true, "NO-DETECTION");
        }           
	}
}

// Process any application specific inbound MQTT messages
// Return False if none
// Return true if an MQTT message was handled here
bool onMqttMessageAppExt(char *topic, char *payload, const AsyncMqttClientMessageProperties &properties, const size_t &len, const size_t &index, const size_t &total)
{
    return false;
}

// Subscribe to application specific topics
void appMQTTTopicSubscribe()
{
    mqttClient.publish(oh3StateValue, 1, true, "NO-DETECTION");
    // mqttTopicsubscribe(oh3StateValue, 2);
}

// Write out ove telnet session and application specific infomation
void telnet_extension_1(char c)
{
    char logString[MAX_LOGSTRING_LENGTH];
    memset(logString, 0, sizeof logString);
    sprintf(logString, "%s%s%d%s%d%s%d%s%d\n\r", "Pin Values: \t", "Hold ON 1: ", pinVal1,  " Hold ON 2: ", pinVal2 ," Hold ON 3: ",pinVal3," Output: ",digitalRead(outRelayPin) );
    printTelnet((String)logString);
}

// Process any application specific telnet commannds
void telnet_extension_2(char c)
{
    printTelnet((String)c);
}

// Process any application specific telnet commannds help information
void telnet_extensionHelp(char c)
{
    printTelnet((String) "x\t\tSome description");
}

void drdDetected()
{
    Serial.println("Double resert detected");
}

//**********************************************************************
// Main Application
// chect current time against configutation and decide what to do.
// Send appropriate MQTT command for each control.
// Run this everytime the TOD changes
// If TOD event is not received then nothing happens
//**********************************************************************
void processTOD_Ext()
{
    // Nothing to do for this app
}

 
