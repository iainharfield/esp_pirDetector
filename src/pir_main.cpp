
#include <ArduinoOTA.h>
#include <Ticker.h>
#include <AsyncMqttClient.h> 
#include <time.h>

#include "hh_defines.h"
#include "hh_utilities.h"
#include "hh_cntrl.h"

// Folling line added to stop compilation error suddenly occuring in 2024???
#include "ESPAsyncDNSServer.h"

#define ESP8266_DRD_USE_RTC true
#define ESP_DRD_USE_LITTLEFS false
#define ESP_DRD_USE_SPIFFS false
#define ESP_DRD_USE_EEPROM false
#define DOUBLERESETDETECTOR_DEBUG true
#include <ESP_DoubleResetDetector.h>

//***********************
// Template functions
//***********************
bool onMqttMessageAppExt(char *, char *, const AsyncMqttClientMessageProperties &, const size_t &, const size_t &, const size_t &);
bool onMqttMessageAppCntrlExt(char *, char *, const AsyncMqttClientMessageProperties &, const size_t &, const size_t &, const size_t &);
void appMQTTTopicSubscribe();
void telnet_extension_1(char);
void telnet_extension_2(char);
void telnet_extensionHelp(char);
void startTimesReceivedChecker();
void processCntrlTOD_Ext();

//******************************
// defined in asyncConnect.cpp
//******************************
extern void mqttTopicsubscribe(const char *topic, int qos);
extern void platform_setup(bool);
extern void handleTelnet();
extern void printTelnet(String);
extern AsyncMqttClient mqttClient;
extern void wifiSetupConfig(bool);
extern templateServices coreServices;
extern char ntptod[MAX_CFGSTR_LENGTH];

//*************************
// defined in telnet.cpp
//*************************
extern int reporting;
extern bool telnetReporting;

#define DRD_TIMEOUT 3
#define DRD_ADDRESS 0

DoubleResetDetector *drd;

// Application Specific MQTT Topics and config
//const char *oh3StateValue = "/house/pir/garage-side-door/value";  //FIXTHIS

//**********************
// application specific
//********************** 
void pirRead();

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

//bool bManMode = false; // true = Manual, false = automatic

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

// 
void pirRead()
{
    char logString[MAX_LOGSTRING_LENGTH];

    //bManMode = false;
	pinVal1 = digitalRead(inOverrideHighPin);  // pir detector pir, 1 = on
	pinVal2 = digitalRead(inOverrideLowPin);   // override 1, 0 = force on
	pinVal3 = digitalRead(inOverrideHighPin2); // override 2, 1 = force on

	if (pinVal1 == 1 && pinVal2 == 1 && pinVal3 == 0)
	{
		memset(logString, 0, sizeof logString);
		if (pinVal1 == 1 )                  // PIR detection
		{
           
            if (pirState != pirStateDetection && pirState != pirStateForceDetection)
            {
			    sprintf(logString, "%s,%s,%s,%s", ntptod, espDevice.getType().c_str(), espDevice.getName().c_str(), "PIR Detection.");
                printTelnet((String)logString);
                mqttLog(logString, REPORT_WARN, true, true);
                pirState = pirStateDetection;
                mqttClient.publish(oh3StateValue, 1, true, "DETECTION"); // Openhab rule switches ALL light on
            }
            else
            {
                digitalWrite(outRelayPin, HIGH);
                delay(100);
            }
		}
    }

    else if (pinVal2 == 0 || pinVal3 == 1 )                                      //Lights being forced on by switch
	{
            
            if (pirState != pirStateForceDetection)
            {
			    sprintf(logString, "%s,%s,%s,%s", ntptod, espDevice.getType().c_str(), espDevice.getName().c_str(), "Forced detection.");
                printTelnet((String)logString);
                mqttLog(logString, REPORT_WARN, true, true);

                pirState = pirStateForceDetection;
                mqttClient.publish(oh3StateValue, 1, true, "FORCED-DETECTION");  //  Openhab rule switches ALL light on
            }
            else
            {
                digitalWrite(outRelayPin, HIGH);                                //Switch ON only Garage path
                delay(100);
            }
		
	}
	else
	{
        
        if  (pirState != pirStateNoDetection)
        {
			sprintf(logString, "%s,%s,%s,%s", ntptod, espDevice.getType().c_str(), espDevice.getName().c_str(), "No detection.");
            mqttLog(logString, REPORT_WARN, true, true);
            if (reporting == REPORT_DEBUG)
	        { 
                 printTelnet((String)logString);                 
		    }
            
            //FIXTHIS : What happens wen the lights are in auto matic mode and are on - wont this switch them off? 
            pirState = pirStateNoDetection;
		    digitalWrite(outRelayPin, LOW);                                 //Switch OFF
            delay(100);                                                     //Relay bounce settle time
            
		    mqttClient.publish(oh3StateValue, 1, true, "NO-DETECTION");     // Openhab rule Switches OFF all lights
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

void processCntrlTOD_Ext()
{
    // Nothing to do for this app
}

 bool onMqttMessageAppCntrlExt(char *topic, char *payload, const AsyncMqttClientMessageProperties &properties, const size_t &len, const size_t &index, const size_t &total)
{
	// Nothing to do for this app
    return false;
}
