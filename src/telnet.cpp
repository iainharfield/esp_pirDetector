#include <ESP8266WiFi.h>

#include "defines.h"

// declare telnet server 
WiFiServer TelnetServer(TELNET_PORT);
WiFiClient Telnet;

void handleTelnet();
void printTelnet(String);

extern String deviceName;
extern String deviceType;
extern char wifiConfigOnboot[];
extern char ntptod[];
extern void mqttDisconnect(); 
extern bool mqttGetConnectedStatus();
extern String mqttGetClientID();
extern String getFormattedTime();
extern void telnet_extension_1(char);
extern void telnet_extension_2(char);
extern void telnet_extensionHelp(char);
extern void setWiFiConfigOnBoot(String);

extern String Router_SSID;
int reporting = REPORT_INFO;
bool telnetReporting = false;

void printTelnet(String msg)
{
  Telnet.println(msg);
}

void telnetBegin()
{
   TelnetServer.begin(); 
}

void handleTelnet()
{
    char logString[MAX_LOGSTRING_LENGTH];

  if (TelnetServer.hasClient()){
  	// client is connected
    if (!Telnet || !Telnet.connected()){
      if(Telnet) Telnet.stop();          // client disconnected
      Telnet = TelnetServer.available(); // ready for new client
    } else {
      TelnetServer.available().stop();  // have client, block new conections
    }
  }

  if (Telnet && Telnet.connected() && Telnet.available()){
    // client input processing
    while(Telnet.available())
     // Serial.write(Telnet.read()); // pass through
      // do other stuff with client input here
    //if (SERIAL.available() > 0) {
    {
        char c = Telnet.read();
        switch (c) {
          	case '\r':
        	  	Telnet.println();
            break;
          	case 'l':

        	  	int sigStrength;
        	  	sigStrength = WiFi.RSSI();
        	  	memset(logString,0, sizeof logString);
        	  	sprintf(logString,
        			  "%s%s\n\r%s%s\n\r%s%s\n\r%s%i\n\r%s%s\n\r%s%i\n\r%s%d.%d.%d.%d",
					    "Date:\t\t",getFormattedTime().c_str(),
        			"Sensor Type:\t", deviceType.c_str(),
					    "Sensor Name:\t", deviceName.c_str(),
					    "WiFi Connect:\t", WiFi.status(),
					    "SSID:\t\t", Router_SSID.c_str(),
					    "Sig Strength:\t", sigStrength,
					    "ipAddr:\t\t", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3] );
        	  	Telnet.println(logString);
        	  	
              if (!mqttGetConnectedStatus())
            	  	Telnet.println("MQTT Client:\tNOT CONNECTED");
        	  	else
        	  	{
        		  	sprintf(logString, "%s%s\n\r", "MQTT Client:\t",  mqttGetClientID().c_str());
        		  	Telnet.println(logString);
        	  	}
              telnet_extension_1(c);
            break;
           	case 'w':
                if (strcmp(wifiConfigOnboot, "NO") == 0)
                {
        	  	    Telnet.println("Set config WiFi credentials on next reboot");
                  setWiFiConfigOnBoot("YES");
                }  
                else 
                {
                  Telnet.println("Do not config WiFi credentials on next reboot - normal reboot");
                  setWiFiConfigOnBoot("NO");
                } 
            break;                        
          	case 'm':
        	  	  Telnet.println("Disconnecting from MQTT Broker ");
                mqttDisconnect(); 
            break;
          	case 'r':
        	  	  Telnet.println("you are restarting this device");
        	  	  ESP.restart();
            break;
            case 'd':
                if (reporting == REPORT_INFO )
                {
                    reporting = REPORT_DEBUG;
                    Telnet.println("DEBUG messages on");
                } 
                else
                {
                    reporting =  REPORT_INFO;
                    Telnet.println("DEBUG messages off");
                }
            break;
            case 't':
                if (telnetReporting == false )
                {
                    telnetReporting = true;
                    Telnet.println("Telnet DEBUG messages on");
                } 
                else
                {
                    telnetReporting = false;
                    Telnet.println("Telnet DEBUG messages off");
                }
            break;
			case 'h':
        	  	Telnet.println("Help");
				      Telnet.println("l\t\tList status od IoT device");
              Telnet.println("d\t\tToggle DEBUG messages over MQTT and Serial");
              Telnet.println("t\t\tToggle DEBUG messages over telnet");
				      Telnet.println("r\t\tRe-boot IoT device");
				      Telnet.println("m\t\tDisconnect MQTT broker. Should auto reconnect");
              Telnet.println("w\t\tSet config WiFi credentials on next reboot");
              telnet_extensionHelp(c);
            break;
          	default:
              telnet_extension_2(c); 	
            break;
        }
      }

  }
}