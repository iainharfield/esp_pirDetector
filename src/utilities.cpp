#include "defines.h"
#include <Arduino.h>
#include <time.h>
#include <AsyncMqttClient_Generic.hpp>

extern AsyncMqttClient mqttClient;
extern void printTelnet(String);      // defined in telnet.cpp


extern String deviceName;
extern String deviceType;
extern char ntptod[MAX_CFGSTR_LENGTH];
extern bool ntpTODReceived;
extern bool telnetReporting;

//Publish a log message to 
bool mqttLog(const char* msg, bool mqtt, bool monitor)   // FIXTHIS
{
  //Serial.print("sensor Name: "); Serial.println(sensorName);

  if (ntpTODReceived == false)
    sprintf(ntptod, "%s", "0000-00-00T00:00:00");

  char logMsg[MAX_LOGSTRING_LENGTH]; 
  memset(logMsg,0, sizeof logMsg);

  sprintf(logMsg, "%s,%s,%s,%s", ntptod, deviceType.c_str(), deviceName.c_str(), msg);    
  if (mqtt)
  {
        mqttClient.publish(oh3StateLog, 0, true, logMsg);
  }
  if (monitor)
  {
        Serial.println(logMsg);
  }
  if (telnetReporting == true)
  {
          printTelnet(logMsg);
  }

	return false;
}

int get_weekday(char * str) 
{
  struct tm tm;
  memset((void *) &tm, 0, sizeof(tm));
  if (strptime(str, "%Y-%m-%d", &tm) != NULL) 
  {
    time_t t = mktime(&tm);
    if (t >= 0) 
    {
      return localtime(&t)->tm_wday; // Sunday=0, Monday=1, etc.
    }
  }
  return -1;
}


