 /* MAUSAM UPDATED 
   - Added commands: READWIFIFILE, FPLOG, FPPROXY, FPSER, FPLOGCLR, FPPROXYCLR, FPSERCLR, GETALLFP.
   - ESP OTA file storage in SD card and then perform ota.
   - Enhanced debug logging for better traceability.
   - Included file size(Proxy,Serial) in logs when the device connects or disconnects from MQTT.
   - Added detailed logs for MQTT disconnection reasons.
   - Implemented reset reason logging and storing in the log file.
   - Increased MQTT reconnection interval to 60 seconds.
*/
#include <WiFi.h>
#include <Adafruit_I2CDevice.h>
#include "esp_system.h"
#include "soc/efuse_reg.h"
// #include "esp_efuse.h" // for programming eFuse.
#include "esp_efuse.h"
#include "esp_efuse_table.h"




/*
  This example uses FreeRTOS softwaretimers as there is no built-in Ticker library
*/

#include "App.h"
#include <pthread.h>
#include <WiFiMulti.h>

extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
}
#include <AsyncMqttClient.h>
#include "MQTTQueue.h"
#define DEBUG_ASYNC_MQTT_CLIENT
//#define STATIC_IP  1

/* Serial Uart Buf Size for Uart 2 */
#define SERIAL_SIZE_RX  2048//1024

/* Wifi AP Related */
#define WIFI_SSID "AlteemR&D"
#define WIFI_PASSWORD "9426278047"

/* Used for Static IP */
#ifdef STATIC_IP
IPAddress local_IP(192, 168, 0, 225);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8); //optional
IPAddress secondaryDNS(8, 8, 4, 4); //optional
#endif

AsyncMqttClient mqttClient;

TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;
TimerHandle_t ledBlinkTimer;

int NoOfSSID = 0;

unsigned long previousMillis = 0;
unsigned long interval = 60000;

WiFiMulti wifiMulti;

extern bool flag_XModemTXBusy;
bool mqtt_current_status = false;
char mqtt_reason[100] = "";

bool publish_mqtt_flag = 0;
unsigned long publish_mqtt_timer = 0;
unsigned long publish_mqtt_current_timer = 0;
unsigned long mqtt_service_previous_timer = 0;
unsigned long mqtt_service_current_timer = 0;

bool mqtt_service_flag = 0;
char service_check[200] = " ";
int service_check_counter = 0;

bool connectToWifi()
{
  IsWifiConnectBusy = 1;
  int nos = NoOfSSID;
  Serial.println("Connecting to Wi-Fi");

#ifdef STATIC_IP
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS))
  {
    Serial.println("STA Failed to configure");
  }
#endif

  previousMillis = millis();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  while (nos)
  {
    
    // strcpy(WifiCred[nos-1].ssid, WIFI_SSID);
    // strcpy(WifiCred[nos-1].password, WIFI_PASSWORD);
    LOG_INFO("\r\nAdded WiFi SSID %s PASS %s to Library\r\n", WifiCred[nos - 1].ssid, WifiCred[nos - 1].password);

    wifiMulti.addAP(WifiCred[nos - 1].ssid, WifiCred[nos - 1].password);
    nos--;
  }

  if (wifiMulti.run(20000) == WL_CONNECTED)
  {
    LOG_INFO("SSID:%s,RSSI:%d,IP:%s\r\n", WiFi.SSID(), WiFi.RSSI(), WiFi.localIP().toString());

    // LOG_INTOSD("SSID:%s,RSSI:%d,IP:%s\r\n", WiFi.SSID(), WiFi.RSSI(), WiFi.localIP().toString());

    String ipStr = WiFi.localIP().toString();
    Serial.println("Debug IP: " + ipStr);
    LOG_INTOSD("SSID:%s,RSSI:%d,IP:%s\r\n", WiFi.SSID().c_str(), WiFi.RSSI(), ipStr.c_str());


    Serial.println("Wifi Connected ");
    Serial.print("IP Adress : ");
    Serial.println(WiFi.localIP());
    IsWifiConnectBusy = 0;
    return ALTEEM_SUCCESS;
  }
  else  // if not WiFi not connected
  {
    LOG_INFO("WiFi not Connected\r\n");
  }

  IsWifiConnectBusy = 0;
  return ALTEEM_ERROR;
}

void connectToMqtt()
{

  Serial.println("inside mqtt connection funcion");

  LOG_INFO("Connecting to MQTT flag_BrokerConfigRx = %d\r\n", flag_BrokerConfigRx);
  if (flag_BrokerConfigRx == BROKER_CONFIG_NOT_RX)
  {
    LOG_INFO("Connect_MQtt_Using_IP %d \r\n", Connect_Mqtt_Using_IP);  
    if(Connect_Mqtt_Using_IP == false)
    {
      // mqttClient.setServer(IPAddress(192,168,0,253) , 1883);
      mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    }
    else
    {
        LOG_INFO("Connecting to MQTT_Broker With New Ip Address %s Port %d\r\n", MQTT_HOST, MQTT_PORT);
        IPAddress Myip;
        if(Myip.fromString(MQTT_HOST) == true)
        {
            LOG_INFO("Ip Address Converted From String \r\n");
        }
        Serial.println(Myip);
        Serial.println(MQTT_PORT);
        // mqttClient.setServer(IPAddress(192,168,0,253) , 1883);
        mqttClient.setServer(Myip , MQTT_PORT);
    }
  }
  else
  {
    LOG_INFO("Connecting to MQTT Broker When Broker Config is Received %s Port %d\r\n", MQTT_HOST, MQTT_PORT);

  }

  if(flag_XModemTXBusy == false)
  {
    
    // mqttClient.connect();
  if (!mqttClient.connected())
   {
    mqttClient.connect();
   }
    

  
  }
}

void WiFiEvent(WiFiEvent_t event)
{
  Serial.printf("[WiFi-event] event: %d\n", event);
  switch (event) {
    // case SYSTEM_EVENT_STA_GOT_IP:
     case ARDUINO_EVENT_WIFI_STA_GOT_IP: 
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      digitalWrite(WIFI_LED, 1);
      connectToMqtt();
      break;
    // case SYSTEM_EVENT_STA_DISCONNECTED:
       case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: 
      Serial.println("WiFi lost connection");
      xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
      digitalWrite(WIFI_LED, 0);
      break;
  }
}

void onMqttConnect(bool sessionPresent)
{
  LOG_INTOSD("\r\n");
  memset(mqtt_reason, 0, sizeof(mqtt_reason));
  sprintf(mqtt_reason, "Connected to MQTT\r\n");
  digitalWrite(SRV_LED, 1);
  Serial.println("Connected to MQTT.");
  mqtt_current_status = true;
  // LOG_INTOSD("Connected to MQTT\r\n");

  if (flag_BrokerConfigRx)
  {
    IsMQTTConBrokerConfig = true;
  }

  /* If broker Config Not Received than Publish Message For Request */
  if (flag_BrokerConfigRx == BROKER_CONFIG_NOT_RX)
  {
    /* Set Counter for Retry after timeout to 0 */
    counter_BrokerResp = 0;
    flag_EnCounting = true;
    Read_UDID();

    LOG_INFO("Subscribe topic : %s\r\n", Topic_SubBrokerConfig);
    mqttClient.subscribe(Topic_SubBrokerConfig, 2);

    uint16_t ret = mqttClient.publish(Topic_PubBrokerConfig, 2, true, "{\"DN\":\"\",\"IP\":0,\"AD\":\"\",\"PO\":0}", strlen("{\"DN\":\"\",\"IP\":0,\"AD\":\"\",\"PO\":0}"));
    if (ret < 0)
    {
      LOG_ERROR("Publish Broker Config Request Msg Failed %d\r\n", ret);
      LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - Publish Broker Config Request Msg Failed %d\r\n", __FILE__, __FUNCTION__, __LINE__, ret);
    }
  }
  else
  {
    LOG_INFO("Subscribe topic : %s\r\n", Topic_SubZigbeeConfig);
    uint16_t ret = mqttClient.subscribe(Topic_SubZigbeeConfig, 2);//MQTT_IF_Subscribe(mqttClientHandle, Topic_SubZigbeeConfig, MQTT_QOS_2, ZigbeeConfigRxCallback);

    LOG_INFO("Subscribe topic : %s\r\n", Topic_SubLogData);
    ret = mqttClient.subscribe(Topic_SubLogData, 2);//MQTT_IF_Subscribe(mqttClientHandle, Topic_SubLogData, MQTT_QOS_2, LogReqRxCallback);

    LOG_INFO("Subscribe topic : %s\r\n", Topic_SubOtaData);
    ret = mqttClient.subscribe(Topic_SubOtaData, 2);//MQTT_IF_Subscribe(mqttClientHandle, Topic_SubOtaData, MQTT_QOS_2, OtaReqRxCallback);

    LOG_INFO("Subscribe topic : %s\r\n", Topic_SubService);
        ret = mqttClient.subscribe(Topic_SubService, 2); 

    if (ret < 0)
    {
      LOG_ERROR("Failed to Subscribed all topics\r\n");
      LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d  - Failed to Subscribed all topics\r\n", __FILE__, __FUNCTION__, __LINE__);
    }
    else
    {
      LOG_INFO("Subscribed to all topics successfully\r\n");
    }
  }
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{

  memset(mqtt_reason, 0, sizeof(mqtt_reason));
  digitalWrite(SRV_LED, 0);
  Serial.println("Disconnected from MQTT.");
  mqtt_current_status = false;
  mqtt_service_flag = 0;
  service_check_counter = 0; 

  switch (reason)
  {

  case AsyncMqttClientDisconnectReason::TCP_DISCONNECTED :
    sprintf(mqtt_reason,"Mqtt disconnect Reason:Tcp disconnect\r\n");
    break;
  case AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION :
    sprintf(mqtt_reason,"Mqtt disconnect Reason:Unacceptable protocol\r\n");
    break;
  case AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED :
     sprintf(mqtt_reason,"Mqtt disconnect Reason:Identifier rejected\r\n");
    break;
  case AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE :
     sprintf(mqtt_reason,"Mqtt disconnect Reason:Server unavailable\r\n");
    break;
  case AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS :
     sprintf(mqtt_reason,"Mqtt disconnect Reason:Malform credential\r\n");
    break;
  case AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED :
     sprintf(mqtt_reason,"Mqtt disconnect Reason:Mqtt not authorized\r\n");
    break;
  case AsyncMqttClientDisconnectReason::ESP8266_NOT_ENOUGH_SPACE :
     sprintf(mqtt_reason,"Mqtt disconnect Reason:Esp not enough space\r\n");
    break;
  case AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT :
     sprintf(mqtt_reason,"Mqtt disconnect Reason:Tls bad fingerprint\r\n");
    break;

  default :
     sprintf(mqtt_reason,"Mqtt disconnect Reason:Invalid Reason\r\n");
    break;
 }
  


  if (uxSemaphoreGetCount(MQTTBUSYSem) == 0)
  {
    xSemaphoreGive(MQTTBUSYSem);
  }

  // LOG_INTOSD("Disconnected from MQTT\r\n");

  if (WiFi.isConnected())
  {
    xTimerStart(mqttReconnectTimer, 0);
    Serial.println("MQTT RECONNECT TIMER START ");
  }
  // Serial.println(" on Mqtt disconnect end ");
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos)
{
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttUnsubscribe(uint16_t packetId)
{
  Serial.println("Unsubscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
  LOG_INFO("MQTT Message received. Length %d\r\n", len);

  char Data[2048] = "";
  strncpy(Data, payload, len);

  if (strstr(topic, Topic_SubBrokerConfig))
  {
    BrokerConfigRxCallback(Data);
  }
  else if (strstr(topic, Topic_SubZigbeeConfig))
  {
    ZigbeeConfigRxCallback(Data);
  }
  else if (strstr(topic, Topic_SubLogData))
  { 
   
    LogReqRxCallback(Data);
  }
  else if (strstr(topic, Topic_SubOtaData))
  {
    OtaReqRxCallback(Data);
  }
  else if (strstr(topic, Topic_SubService))
  {

   
    if (strstr(Data, service_check))
    {
      LOG_INTOSD("Mqtt Service is Active\r\n");
      mqtt_service_flag = 1;
    }
    else 
    {
      LOG_INTOSD("Mqtt Service is not Active\r\n");
    }
   
  }
}

void onMqttPublish(uint16_t packetId)
{

  publish_mqtt_flag = 0;
  

  if (uxSemaphoreGetCount(MQTTBUSYSem) == 0)
  {
    struct msgQueue queueElement;
    xQueueReceive( appQueue, &queueElement, portMAX_DELAY);
    free(queueElement.topic);
    free(queueElement.payload);
    xSemaphoreGive(MQTTBUSYSem);
  }

  if (packetId == 0)
  {
    LOG_INFO("MQTT Publish Ack Failed\r\n");
    LOG_INTOSD("MQTT Publish Ack Failed\r\n");

  }
  Serial.println("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

/* CallBack for Led Blink Timer */
static void ledBlinkTimer_CallBack( TimerHandle_t xTimer )
{
  digitalWrite(TIM_LED, !digitalRead(TIM_LED));
  /* If Broker Config Not Received after SomeTime Than Retry */
  if (flag_BrokerConfigRx == BROKER_CONFIG_NOT_RX)
  {
    if (flag_EnCounting)
    {
      counter_BrokerResp++;
    }
    else
    {
      counter_BrokerResp = 0;
    }

    if (counter_BrokerResp > BROKER_CONFIG_WAITIME) /* Time Elapsed */
    {
      /* Reconnect MQTT */
      LOG_INFO("Broker Config Timeout Occurred,MQTT Reconnecting\r\n");
      Mqtt_Reconnect();
      counter_BrokerResp = 0;
      flag_EnCounting = false;
    }
  }
  else
  {
    counter_BrokerResp = 0;
  }

  if (mqttClient.connected() == false && (IsWifiConnectBusy == false))
  {
    counter_WifiReconnect++;
    // Serial.print(" --counter_WifiReconnect : ");
    // Serial.println(counter_WifiReconnect);

    if (counter_WifiReconnect > MQTT_RECONNECT_TIME)
    {
      counter_WifiReconnect = 0;
      LOG_INFO("MQTT Dis Wifi Reconnect Timeout\r\n");
      LOG_INTOSD("MQTT Dis Wifi Reconnect Timeout\r\n");
      WiFi.disconnect();
    }
  }
  else
  {
    counter_WifiReconnect = 0;
  }
}


void getResetReasonString(esp_reset_reason_t reason) {

  switch (reason) {
    case ESP_RST_POWERON: LOG_INTOSD("Power-on Reset\r\n");
      break;
    case ESP_RST_EXT: LOG_INTOSD("External Reset\r\n");
      break;
    case ESP_RST_DEEPSLEEP: LOG_INTOSD("Reset after exiting deep sleep mode\r\n");
      break;  
    case ESP_RST_BROWNOUT: LOG_INTOSD("Brownout Reset\r\n");
      break;
    case ESP_RST_SDIO: LOG_INTOSD("SDIO Reset\r\n");
      break;
    case ESP_RST_SW: LOG_INTOSD("Software Reset\r\n");
      break;
    case ESP_RST_PANIC: LOG_INTOSD("Software reset due to exception/panic\r\n");
      break;
    case ESP_RST_INT_WDT: LOG_INTOSD("Interrupt Watchdog Reset\r\n");
      break;
    case ESP_RST_TASK_WDT: LOG_INTOSD("Task Watchdog Reset\r\n");
      break;
    case ESP_RST_WDT: LOG_INTOSD("Other Watchdog Reset\r\n");
      break;
    case ESP_RST_USB: LOG_INTOSD("USB Reset\r\n");
      break;
    case ESP_RST_JTAG: LOG_INTOSD("JTAG Reset\r\n");
      break;
    case ESP_RST_EFUSE: LOG_INTOSD("Efuse Error Reset\r\n");
      break;
    case ESP_RST_PWR_GLITCH: LOG_INTOSD("Power Glitch Reset\r\n");
      break;
    case ESP_RST_CPU_LOCKUP: LOG_INTOSD("CPU Lockup Reset\r\n");
      break;
    default: LOG_INTOSD("Unknown Reset Reason\r\n");
      break;
  }
}


void permanentlySetFlashVoltage(bool use_3v3) {
  // WARNING: THIS IS PERMANENT AND CAN BRICK YOUR DEVICE!
  
  // Set to ignore GPIO12
  esp_efuse_write_field_bit(ESP_EFUSE_XPD_SDIO_FORCE);
  
  // Enable internal regulator
  esp_efuse_write_field_bit(ESP_EFUSE_XPD_SDIO_REG);
  
  // Set voltage (0=1.8V, 1=3.3V)
  if(use_3v3) {
    esp_efuse_write_field_bit(ESP_EFUSE_XPD_SDIO_TIEH);
  } else {
    esp_efuse_write_field_bit(ESP_EFUSE_XPD_SDIO_TIEH); // Still writes 0
  }
  
  Serial.println("eFuses written. RESTART YOUR DEVICE.");
}


void setup()
{
  Serial.begin(115200);


// INITIAL SETUP...BURN THE EFUSE IF NECESSARY FOR PROPER OPERATION.

/*
uint32_t flash_voltage = esp_efuse_read_field_bit(ESP_EFUSE_XPD_SDIO_FORCE);
Serial.printf("XPD_SDIO_FORCE: %d\n", flash_voltage);

uint32_t flash_voltage_reg = esp_efuse_read_field_bit(ESP_EFUSE_XPD_SDIO_REG);
Serial.printf("XPD_SDIO_REG: %d\n", flash_voltage_reg);

uint32_t flash_voltage_tieh = esp_efuse_read_field_bit(ESP_EFUSE_XPD_SDIO_TIEH);
Serial.printf("XPD_SDIO_TIEH: %d\n", flash_voltage_tieh);

if( flash_voltage_tieh == 0 )
{
  Serial.println("Flash voltage is setting to 3.3V.");
  permanentlySetFlashVoltage(true); 
}
else
{
  Serial.println("Flash voltage is set to 1.8V.");
  // permanentlySetFlashVoltage(false); 
}

*/


  /* init Serial UART 2 */
  Serial2.setRxBufferSize(SERIAL_SIZE_RX);
  Serial2.begin(115200);
  Serial2.setTimeout(250);    //250 ms
  Serial2.setPins(13, 12);

  /* Uart for Debug */
  
  Serial.println();
   
  /* Init LED Pin */
  //Pin Used in SD Card pinMode(LED, OUTPUT);
  pinMode(WIFI_LED, OUTPUT);
  pinMode(SRV_LED, OUTPUT);
  pinMode(TIM_LED, OUTPUT);

  Serial.println("MqttReconnectTimer Start... ");

  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(60000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  Serial.println("ledBlinkTimer Start... ");
  ledBlinkTimer = xTimerCreate("ledBlinkTimer", pdMS_TO_TICKS(500), pdTRUE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(ledBlinkTimer_CallBack));
  Serial.println("ledBlinkTimerv 2 ... ");
  xTimerStart(ledBlinkTimer, 0);
  
  Serial.println("Wifi event  Start... ");
  WiFi.onEvent(WiFiEvent);
  
  Serial.println("mqtt keep alive Start... ");
  mqttClient.setKeepAlive(15);
  Serial.println("onconnect mqtt start Start... ");
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onSubscribe(onMqttSubscribe);
  mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.onPublish(onMqttPublish);

  /* Init RTC */
  Serial.println("RTC INIT ");
  Init_RTC();

  /* Init SD Card Interface */
  Serial.println("SD CARD INIT");
  Init_SDCard();


  
  /* Initialize LOG File Interface */
 
  Serial.println("LOG INIT");
  Init_LOG();

  esp_reset_reason_t resetReason = esp_reset_reason();
  getResetReasonString(resetReason);
 
  Serial.println("MQTT INIT ");
  /* Init and Start MQTT Msg Queue */
  Init_MQTTQueue();
  Serial.println("START APP");
  StartApp();

  /* check Borker Config ang Get MQTT Host */
  Serial.println("CHECK BROKER CONFIG ");
  CheckBrokerConfigfrmFile();

  /* Read and Store Wifi Cred into Struct */
  Serial.println("READ AND STORE WIFI CREADANCIAL");
  NoOfSSID = ReadWifiCred_FromSD();
  Serial.println("CONNECT TO WIFI ");
  connectToWifi();


}

void loop()
{ 
  if ( (mqttClient.connected() == true ) && (publish_mqtt_flag == 1 ))
  {
   publish_mqtt_current_timer = millis();
   if((publish_mqtt_current_timer - publish_mqtt_timer) > 15000)
    {
      Serial.println(" ");
      Serial.println("DISCONNECTING MQTT CLIENT ");
      LOG_INTOSD("Ack time out\r\n");
      mqttClient.disconnect(true);
      publish_mqtt_flag = 0;
    }
  }

  ProcessMQTTQueue();

  if (WiFi.isConnected() == false)
  {
    Serial.println("DISCONNECT TO WIFI ");
  if (IsWifiConnectBusy == false)
    {
      Serial.println("Reconnecting wifi");
      LOG_ERROR("Reconnecting Wifi\r\n");
      connectToWifi();
    }
      
  }


  // mqtt_service_current_timer = millis();

  // if (((mqtt_service_current_timer - mqtt_service_previous_timer) > 60000)  && (mqtt_service_flag == 0) && (mqttClient.connected()== true))
  // {
    
  //     Serial.println("MQTT Service Check Message Publish ");
  //     sprintf(service_check, "MQTT_Service_Check_%d", service_check_counter++);
  //     MQTT_Publish(Topic_PubService,service_check,2);
  //     LOG_INTOSD("%s\r\n", service_check);
  //     mqtt_service_previous_timer = mqtt_service_current_timer;

  // }

 }
