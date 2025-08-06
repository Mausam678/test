
#include "MQTTQueue.h"
#include "App.h"
#include <AsyncMqttClient.h>

extern AsyncMqttClient mqttClient;

int queueSize = 30;

//Queue for MQTT
QueueHandle_t appQueue;

/* Init MQTT Queue */
void Init_MQTTQueue()
{
  appQueue = xQueueCreate( queueSize, sizeof(msgQueue));

  if (appQueue == NULL)
  {
    LOG_ERROR("creating the queue failed");
    LOG_INTOSD("creating the queue failed");
  }
}

/* Publish MQTT Configuration Message
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool MQTT_Publish(char *top, char *msg, int qos)
{
  struct msgQueue queueElement;

  queueElement.event = APP_MQTT_PUBLISH;

  char *payload, *topic;
  payload = (char*)malloc(strlen(msg) + 1);
  topic = (char*)malloc(strlen(top) + 1);

  memcpy(payload, msg, strlen(msg));
  payload[strlen(msg)] = 0;

  memcpy(topic, top, strlen(top));
  topic[strlen(top)] = 0;

  queueElement.topic = topic;
  queueElement.payload = payload;
  queueElement.qos = qos;

  if (xQueueSend(appQueue, ( void * ) &queueElement, ( TickType_t ) 0) != pdTRUE)
  {
    LOG_ERROR("Publish Zigbee Config Message Failed,msg queue send %d\r\n");
    LOG_INTOSD("Publish Zigbee Config Message Failed,msg queue send %d\r\n");
    return ALTEEM_ERROR;
  }
  return ALTEEM_SUCCESS;
}

/* Publish MQTT Configuration Message
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool MQTT_PubZigbeeConfig(char *data)
{
  struct msgQueue queueElement;

  queueElement.event = APP_MQTT_PUBLISH_ZBCONF;

  char *payload;
  payload = (char*)malloc(strlen(data) + 1);

  memcpy(payload, data, strlen(data));
  payload[strlen(data)] = 0;

  queueElement.payload = payload;

  if (xQueueSend(appQueue, ( void * ) &queueElement, ( TickType_t ) 0) != pdTRUE)
  {
    LOG_ERROR("Publish Zigbee Config Message Failed,msg queue send error \r\n");
    LOG_INTOSD("Publish Zigbee Config Message Failed,msg queue send error \r\n");
    return ALTEEM_ERROR;
  }
  return ALTEEM_SUCCESS;
}

/* Process Msges in MQTTQueue */
void ProcessMQTTQueue()
{

  struct msgQueue queueElement;

  if ((IsMQTTConBrokerConfig) && (mqttClient.connected()) /*&& (!IsMQTTBusy)*/ && (uxSemaphoreGetCount(MQTTBUSYSem) == 1))
  {
    if (xQueuePeek( appQueue, &queueElement, ( TickType_t ) 10) == pdPASS)
    {
      if (queueElement.event == APP_MQTT_PUBLISH)
      {
        LOG_INFO("Publishing Zigbee Message Topic-%s DataLen-%d Qos-%d\r\n", queueElement.topic, strlen(queueElement.payload), queueElement.qos);
        // LOG_INFO("Data Publish !!\r\n");
        if (queueElement.qos != 0)
        {
          xSemaphoreTake(MQTTBUSYSem, portMAX_DELAY);
        }

        uint16_t ret = mqttClient.publish(queueElement.topic, queueElement.qos, false, queueElement.payload, strlen(queueElement.payload));
        if (ret < 0)
        {
          LOG_ERROR("Publishing Zigbee Config Message Failed %d\r\n", ret);
          LOG_INTOSD("Publishing Zigbee Config Message Failed %d\r\n", ret);

          if (queueElement.qos != 0)
          {
            xSemaphoreGive(MQTTBUSYSem);
          }
        }
        else
        { 
          publish_mqtt_flag = 1;
          publish_mqtt_timer = millis();
          LOG_INFO("Published Zigbee Message,Topic:%s packet id %d\r\n", queueElement.topic, ret);
          

          if (queueElement.qos != 0)
          {
            
            Serial.println(" Queue Element Qos != 0");
          }
          else
          { 
            Serial.println(" Queue Element Qos = 0");
            xQueueReceive( appQueue, &queueElement, portMAX_DELAY);
             Serial.println("Queue Element free ");
            free(queueElement.topic);
             Serial.println("Queue Element payload free");
            free(queueElement.payload);
            Serial.println("Queue Element ");
                    }
          
        }
      }
    }
  }
  else {
    
  }
}

/* Check if Queue is Free ,Will keep 1 Msg Free for Live Data
   Return ALTEEM_SUCCESS on On Free
          ALTEEM_EERROR on Busy
*/
bool isMqttQueueFree()
{
  if (uxQueueSpacesAvailable(appQueue) > 0)
  {
    return ALTEEM_SUCCESS;
  }
  else
  {
    return ALTEEM_ERROR;
  }
}
