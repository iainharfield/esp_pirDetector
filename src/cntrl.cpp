
#include <AsyncMqttClient_Generic.hpp>

void cntrlMQTTTopicSubscribe();
void processCntrlTOD_Ext();
bool onMqttMessageCntrlExt(char *, char *, const AsyncMqttClientMessageProperties &, const size_t &, const size_t &, const size_t &);

// Subscribe to controler specific topics
void cntrlMQTTTopicSubscribe()
{	
    //no implementation 
}

bool onMqttMessageCntrlExt(char *, char *, const AsyncMqttClientMessageProperties &, const size_t &, const size_t &, const size_t &)
{
    //no implementation
    return false;
}  

void processCntrlTOD_Ext()
{	
    //no implementation
}