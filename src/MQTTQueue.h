
extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
}

//Queue for MQTT
extern QueueHandle_t appQueue;

extern bool publish_mqtt_flag ;
extern unsigned long publish_mqtt_timer ;
extern unsigned long publish_mqtt_current_timer;

/* Struct for MQTT Queue */
struct msgQueue
{
  int   event;
  char* payload;
  char* topic;
  long filepointer;
  uint8_t qos;
};

enum
{
  APP_MQTT_PUBLISH,
  APP_MQTT_PUBLISH_ZBCONF
};



/* Init MQTT Queue */
void Init_MQTTQueue();

/* Publish MQTT Configuration Message
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool MQTT_Publish(char *top, char *msg, int qos);

/* Publish MQTT Configuration Message
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool MQTT_PubZigbeeConfig(char *data);

/* Process Msges in MQTTQueue */
void ProcessMQTTQueue();

/* Check if Queue is Free ,Will keep 1 Msg Free for Live Data
   Return ALTEEM_SUCCESS on On Free
          ALTEEM_EERROR on Busy
*/
bool isMqttQueueFree();
