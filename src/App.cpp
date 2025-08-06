#include "HardwareSerial.h"
#include <WiFi.h>
#include "App.h"
#include <pthread.h>
#include <stddef.h>
#include <AsyncMqttClient.h>
#include "FS.h"
#include "SD_MMC.h"
#include "RTClib.h"
#include "MQTTQueue.h"
#include <esp32fota.h>
#include <HTTPClient.h>
#include "xmodem.h"
#include <Update.h>
#include <esp_task_wdt.h>

extern TimerHandle_t ledBlinkTimer;
extern AsyncMqttClient mqttClient;

/* BROKER DETAILS for INITIAL CONFIG */
char MQTT_HOST[100] = "device.alteemiot.com";
int MQTT_PORT = 88;
bool Connect_Mqtt_Using_IP = false;

/* Topics to Be Subscribed */
char Topic_SubBrokerConfig[100] = "/AT/CN/WF/";
char Topic_SubZigbeeConfig[100] = "/AT/CN/ZB/";
char Topic_SubLogData[100] = "/AT/LG/";
char Topic_SubOtaData[100] = "/AT/OT/";
char Topic_SubService[100] = "/AT/SV/PG/";

/* Topics For Publish */

char Topic_PubOtaData[100] = "/AR/OT/";
char Topic_PubLogData[100] = "/AR/LG/";
char Topic_PubBrokerConfig[100] = "/AR/CN/WF/";
char Topic_PubZigbeeConfig[100] = "/AR/CN/ZB/";
char Topic_PubProxyData[100] = "/AR/DT/ZB/";
char Topic_PubSerialData[100] = "/AR/DS/ZB/";
char Topic_PubLiveSerialData[100] = "/AR/LS/ZB/";
char Topic_PubLiveProxyData[100] = "/AR/LT/ZB/";
char Topic_PubMonitorData[100] = "/AR/MN/ZB/";
char Topic_PubErrorData[100] = "/AR/ER/ZB/";
char Topic_PubService[100] = "/AR/SV/PG/";
char payload_sd[200] = " ";
/* Flag for XModem Busy */

const char* otaEspfile = "/OTAT.bin";

bool flag_XModemTXBusy = 0;
 
/* If Broker Configuration Received */
char flag_BrokerConfigRx = 0;

/* Counter if Broker Config Not Rxed */
uint16_t counter_BrokerResp = 0;

/* Counter for Wifi Reconnect if MQTT Disconnected  */
uint16_t counter_WifiReconnect = 0;

bool flag_EnCounting = 0, flag_TimeSyncDone = 0, flag_LogReqRx = 0;

/* If Connected with Broker Received from Configuration */
bool IsMQTTConBrokerConfig = false;

/* If MQTT is Busy Sending Msg */
bool IsMQTTBusy = false;

/* If UDID Already Appended to Topic Name */
bool IsUDIDAppended = false;

/* If Wifi Connection Process is Busy */
bool IsWifiConnectBusy = false;

/* If OTA Update Req Rxed */
bool flag_OtaUpdateStart = 0;

/* If UNREG Config Rxed */
bool flag_UNAUTH = 0;



WifiCred_t WifiCred[30];

/* All Application Related Flags */
bool flag_SDInit = 0; // If SD Card init succeed



uint8_t NoOfCreds;

/* Variables for File pointer Values */
long FilePointer_ProxyData;
long FilePointer_SerialData;
long FilePointer_LogData;

/* Variables for File Sze Values */
long FileSize_ProxyData;
long FileSize_SerialData;
long FileSize_LogData;

/* Mutex For File Access */
SemaphoreHandle_t ProxyDataGetMutex, SerialDataGetMutex, LogGetMutex;
SemaphoreHandle_t MQTTBUSYSem;

//  declare a event grounp handler variable
EventGroupHandle_t xEventGroup;

/* Buffer for Storing Data from SD Files */
char sddata_buff[MQTT_BUF_SIZE + 20];

RTC_DS3231 rtc;

/* IP Addr of Broker */
IPAddress ip;

/* Web Addr of Broker */
char server_ad[100] = "";

int port = 0;

char buffer[1024] = "";
char log_buf[1300] = "";
char buffer_[1024] = "";
char buffer_er[1024] = "";

char g_OtaTarFileURL[500];
char g_OtaTarFileLoc[500];

/* OTA Image type if for Router or CoOrdinator */
char OTAImageType;

/* Link of File to Download From
   Used in OTA Updates
*/
char httpLink[500];
char payload_cmd[20];
bool flag_esp_ota = false; 
bool previous_mqtt_status = false;

bool Flag_RxCallBack_Data_Available = false;

esp32FOTA esp32FOTA("esp32-fota-https", 1, false, true);

/* Application Task Function Handling */
void AppTaskFunc(void *pEntry);
void Check_Payload_Data(void);
void performOTAFromSD(const char* file);
void performOTAUpdate(const char *firmwareUrl);
bool fireware(const char *fileUrl);
void otaDownloadTask(void *parameter);
void wifireadfromsd(void);


/* Starts Thread Handling Application */
void StartApp()
{
  ProxyDataGetMutex = xSemaphoreCreateMutex();
  if (ProxyDataGetMutex != NULL)
  {
    /* The semaphore was created successfully and
      can be used. */
  }
  else
  {
    LOG_ERROR("ProxyData Mutex Init Failed\n\r");
    LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - ProxyData Mutex Init Failed\n\r", __FILE__, __FUNCTION__, __LINE__);
  }

  SerialDataGetMutex = xSemaphoreCreateMutex();
  if (SerialDataGetMutex != NULL)
  {
    /* The semaphore was created successfully and
      can be used. */
  }
  else
  {
    LOG_ERROR("SerialData Mutex Init Failed\n\r");
    LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - SerialData Mutex Init Failed\n\r", __FILE__, __FUNCTION__, __LINE__);
    // return ALTEEM_ERROR;
  }

  MQTTBUSYSem = xSemaphoreCreateBinary();
  xSemaphoreGive(MQTTBUSYSem);

  xTaskCreate(AppTaskFunc, "AppTaskFunc", 8192, NULL, 3, NULL);
}

/* Application Task Function Handling */
void AppTaskFunc(void *parameter)
{
  FilePointer_ProxyData = ReadFilePointer_FromSD(PROXYDATA);
  LOG_INFO("FilePointer_ProxyData : %d\r\n", FilePointer_ProxyData);
  FilePointer_SerialData = ReadFilePointer_FromSD(SERIALDATA);
  LOG_INFO("FilePointer_SerialData : %d\r\n", FilePointer_SerialData);
  FilePointer_LogData = ReadFilePointer_FromSD(LOGDATA);
  LOG_INFO("FilePointer_LogData : %d\r\n", FilePointer_LogData);

  Serial.println(" UPDATeing file proxy ");
  UpdateFileSize(PROXYDATA);
  Serial.println(" UPDATeing file serial ");
  UpdateFileSize(SERIALDATA);
  Serial.println(" UPDATeing file log ");
  UpdateFileSize(LOGDATA);

  Serial.println(" runing while loop ");
 
  while (1)
  {
    if (flag_UNAUTH)
    {
      digitalWrite(WIFI_LED, 0);
      digitalWrite(SRV_LED, 0);
      digitalWrite(TIM_LED, 0);
      while (1)
      {
        digitalWrite(WIFI_LED, !(digitalRead(WIFI_LED)));
        digitalWrite(SRV_LED, !(digitalRead(SRV_LED)));
        digitalWrite(TIM_LED, !(digitalRead(TIM_LED)));
        delay(1000);
      }
    }

    if ( previous_mqtt_status != mqtt_current_status )
    {

     
      File fpr;
      Serial.print("MQTT Status Changed : ");
      Serial.println(mqtt_reason);
      LOG_INTOSD(mqtt_reason);
      long sr_pr = ReadFilePointer_FromSD(SERIALDATA);
      long px_pr = ReadFilePointer_FromSD(PROXYDATA);
      fpr = SD_MMC.open(proxydatafile);
      long PROXY_file_size = fpr.size();
      fpr.close(); 
      fpr = SD_MMC.open(serialdatafile);
      long SERIAL_file_size = fpr.size();
      fpr.close(); 

     char mqttPayload[200]; // Adjust the size as needed
     sprintf(mqttPayload, "SrP %ld PxP %ld : SrS %ld PxS %ld \r\n", sr_pr, px_pr ,SERIAL_file_size , PROXY_file_size);
     LOG_INTOSD(mqttPayload);
    }
    previous_mqtt_status = mqtt_current_status;

     
    /* Check if Data Available on UART */
    UART_HandleData();

    /* Check if Data Available on SD Card */
    SD_HandleData();

    Check_Payload_Data();

    /* If OTA Update Req */
    if (flag_OtaUpdateStart)
    {
      flag_OtaUpdateStart = 0;
      Start_ESPOTA();
    }

    // Pause the task again for 500ms
    vTaskDelay(1 / portTICK_PERIOD_MS); // delayMicroseconds(1000);
  }
}

/* Application Task Function Handling */
void UART_HandleData(void)
{
  /* Check if Data Available on UART */
  if ((flag_XModemTXBusy == false) && (Serial2.available() > 0))
  {
    /* Read Available Data on UART */
    char Rcvdbuffer[1024] = "";
    size_t bytesRead;
    int index = 0, uartread_failures = 0;

    /* Read Data Untill New Line */
    //    int16_t status = Serial2.read();      //Disabled Ashok

    int16_t status = Serial2.peek(); // Read First Byte, But keeps into Buffer as it is
    if (status == -1)
    {
      LOG_ERROR("UART2 Read Error, Read Cancelled\r\n");
      return;
      // goto ABORT_READ;
    }

    LOG_INFO("UART_HandleData Function\r\n");

    bytesRead = Serial2.readBytesUntil('\n', Rcvdbuffer, 1024);
    Rcvdbuffer[bytesRead] = '\0'; // Position of \n where we have to put NULL

    bytesRead++; // Total Char including NULL   //ADDED ON 29.01.24

    LOG_INFO("Data Rx on Uart1, Length with NULL : %d,Data : %s\r\n", bytesRead, Rcvdbuffer);

    // 29.01.2024 FOR DIGIT MISSING

    // With NULL From Router = 50    rrr = 050   pos49 = NULL, pos48 = \n
    // received msglen in Cord = 50  ccc = 050,  pos49 = NULL, pos48 = \n, Send 49 bytes after removing NULL
    // ESP readBytesUntil will return 48 i.e. position of \n

    if (Rcvdbuffer[bytesRead - 2] == '#')
    {
      uint16_t position_h = bytesRead - 2; // Position of #
      char text_with_length[7];
      strncpy(text_with_length, &(Rcvdbuffer[position_h - 6]), 6); // Starting position of rrrccc
      text_with_length[6] = '\0';
      uint32_t value_with_length = atoi(text_with_length);

      uint16_t Len_Router, Len_Coordinator;

      Len_Router = value_with_length / 1000;
      Len_Coordinator = value_with_length % 1000;

      LOG_INFO("Text Len : %s, Value : %d, RLen : %d, CLen : %d\r\n", text_with_length, value_with_length, Len_Router, Len_Coordinator);

      if (Len_Router != Len_Coordinator)
      {
        LOG_INFO("DIGIT MISSING RT - CR LENGTH MISMATCH.\r\n");
        LOG_INTOSD("DIGIT MISSING RT - CR LENGTH MISMATCH.\r\n");
        Rcvdbuffer[bytesRead - 1] = '\n'; // Just to get proper print in SDCard, else these 2 statements are not required.
        Rcvdbuffer[bytesRead] = '\0';
        LOG_INTOSD(Rcvdbuffer);
        return;
      }
      else if (Len_Coordinator != (bytesRead + 1))
      {
        LOG_INFO("DIGIT MISSING ESP - CR LENGTH MISMATCH.\r\n");
        LOG_INTOSD("DIGIT MISSING ESP - CR LENGTH MISMATCH.\r\n");
        Rcvdbuffer[bytesRead - 1] = '\n'; // Just to get proper print in SDCard, else these 2 statements are not required.
        Rcvdbuffer[bytesRead] = '\0';
        LOG_INTOSD(Rcvdbuffer);
        return;
      }
      else
      {
        Rcvdbuffer[position_h - 6] = '\0';
      }
    }

    /* Check if Message is for Zigbee Config and is valid data */
    if(strstr(Rcvdbuffer, "UI") && strstr(Rcvdbuffer, "{") && strstr(Rcvdbuffer, "}"))
    {
      /* Publish Zigbee Config Message */
      if (IsMQTTConBrokerConfig && mqttClient.connected() && (isMqttQueueFree() == ALTEEM_SUCCESS))
      {
        /* To Remove Garbage Data */
        char Temp_PubBuffer[1024] = "";
        strncpy(Temp_PubBuffer, strstr(Rcvdbuffer, "{"), strstr(Rcvdbuffer, "}") - strstr(Rcvdbuffer, "{") + 1);

        bool rt = MQTT_Publish(Topic_PubZigbeeConfig, Rcvdbuffer, 2);
        if (rt != ALTEEM_SUCCESS)
        {
          LOG_ERROR("Publish Zigbee Config Message Failed \r\n");
          LOG_INTOSD("Publish Zigbee Config Message Failed \r\n");
        }
        else
        {
          RTC_ReadTime();
          LOG_INFO("%sPublished Zigbee Config Message\t\r\n", timestamp);
        }
      }
      else
      {
        LOG_ERROR("Publish Zigbee Config Message Failed,MQTT Not Connected\r\n");
      }
    }
    /* Check if Time Sync Request Message from Router Device */
    else if (strstr(Rcvdbuffer, "\"TSU\"") && strstr(Rcvdbuffer, "{") && strstr(Rcvdbuffer, "}"))
    {
      // Ex Data : {"TSU":"L001","T0":900}
      //    Response  :[{"TSU":"L001","NM":"L001,"DL":545}]
      char payload[255] = "";
      strcpy(payload, Rcvdbuffer);

      char device_name[10] = "";
      char *t = strstr(payload, "TSU") + 6;
      strncpy(device_name, t, strstr(t, "\"") - t);

      /* Update Time Value */
      RTC_ReadTime();

      struct tm dateTime; // SlDateTime_t dateTime =  {0};

      sscanf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d", &dateTime.tm_year, &dateTime.tm_mon, &dateTime.tm_mday, &dateTime.tm_hour, &dateTime.tm_min, &dateTime.tm_sec);

      uint32_t Cur_ElapsedSec = (dateTime.tm_min * 60) + dateTime.tm_sec;

      uint32_t Cur_RemainSec = (3600 - Cur_ElapsedSec);

      uint32_t Req_DelaySec = 0, DataSendTime = 0;
      char ConfigSubStr[200] = "";
      char *index;

      /* Fetch DataSendTime */
      index = (strstr(payload, "T0")) + 4;
      DataSendTime = atoi(index);
      Req_DelaySec = (Cur_RemainSec % DataSendTime);

      Req_DelaySec = DataSendTime - Req_DelaySec;

      sprintf(ConfigSubStr, "{\"TSU\":\"%s\",\"NM\":\"%s\",\"DL\":%d}\n", device_name, device_name, Req_DelaySec); // Ex. [{"TSU":"L001","NM":"L001,"DL":545}]

      size_t bytesWritten = 0;

      Serial2.println(ConfigSubStr);
      LOG_INFO("Zigbee Time Sync MSG Write Success\r\n");
      LOG_INTOSD("Zigbee Time Sync MSG Write Success\r\nMSG : %s\r\n", ConfigSubStr);
    }
    /* Normal Data Message */
    else if (strstr(Rcvdbuffer, "{") && strstr(Rcvdbuffer, "}"))
    {
      uint16_t ret = 0;
      /* Publish Zigbee Data Message */
      char Temp_PubBuffer[1024] = "", Temp_SaveBuffer[1024] = "";

      RTC_ReadTime();

      /* Add Timestamp and Remove '{' from Data */
      sprintf(Temp_SaveBuffer, "{\"TS\":\"%s\",%s", timestamp, &Rcvdbuffer[1]);

      /* To Check RTC Working */
      LOG_INFO("Temp_SaveBuffer : %s\r\n", Temp_SaveBuffer);
      //  LOG_INFO("Temp_SaveBuffer \r\n");
      /* 1 Format json with Time Value */

      /* If MQTT Is Connected */
      if (IsMQTTConBrokerConfig && mqttClient.connected() && (isMqttQueueFree() == ALTEEM_SUCCESS))
      {
        if (strstr(Rcvdbuffer, "DIS")) /* Check if Data is Serial Data */
        {
          sprintf(Temp_PubBuffer, "[%s]\n", Temp_SaveBuffer);

          if (strstr(Rcvdbuffer, "DISL")) /* Check if Live Data */
          {
            char Temp_TopicBuf[100] = "";
            sprintf(Temp_TopicBuf, "%s/D", Topic_PubLiveSerialData);

            bool rt = MQTT_Publish(Temp_TopicBuf, Temp_PubBuffer, 0);
            if (rt != ALTEEM_SUCCESS)
            {
              LOG_ERROR("Publish Zigbee SerialData Live Message Failed\r\n");
              /* No Need to Save into Memory as Live Data */
            }
            else
            {
              LOG_INFO("Published Zigbee SerialData Live Message\t\r\n");
            }
          }
          else if(IsMQTTConBrokerConfig && mqttClient.connected() && (isMqttQueueFree() == ALTEEM_SUCCESS)/* &&  mqtt_service_flag == 1)*/) /* Not Live Data */
          {
            char Temp_TopicBuf[100] = "";
            sprintf(Temp_TopicBuf, "%s/D", Topic_PubSerialData);

            bool rt = MQTT_Publish(Temp_TopicBuf, Temp_PubBuffer, 2);
            if (rt != ALTEEM_SUCCESS)
            {
              LOG_ERROR("Publish Zigbee SerialData Message Failed,Saving Into Memory\r\n");

              /* Save into Memory */
              bool Ret = SaveData_IntoSD(Temp_SaveBuffer);
              if (Ret != ALTEEM_SUCCESS)
              {
                LOG_ERROR("Saving Into Memory Failed\r\n");
              }
            }
            else
            {
              LOG_INFO("Published Zigbee SerialData Message\r\n");
            }
          }
          else
          {
              bool Ret = SaveData_IntoSD(Temp_SaveBuffer);
              if (Ret != ALTEEM_SUCCESS)
              {
                LOG_ERROR("Saving Into Memory Failed\r\n");
              }
          }
        }
        else if (strstr(Rcvdbuffer, "DIER")) /* Check if Data is Router Error Data */
        {
          sprintf(Temp_PubBuffer, "[%s]\n", Temp_SaveBuffer);

          char Temp_TopicBuf[100] = "";
          sprintf(Temp_TopicBuf, "%s/D", Topic_PubErrorData);

          bool rt = MQTT_Publish(Temp_TopicBuf, Temp_PubBuffer, 0);
          if (rt != ALTEEM_SUCCESS)
          {
            LOG_ERROR("Publish Zigbee Monitor Message Failed\r\n");
            /* No Need to Save into Memory as Live Data */
          }
          else
          {
            LOG_INFO("Published Zigbee Monitor Message\r\n");
          }
        }
        else if (strstr(Rcvdbuffer, "DIMN")) /* Check if Data is Serial Data */
        {
          sprintf(Temp_PubBuffer, "[%s]\n", Temp_SaveBuffer);

          char Temp_TopicBuf[100] = "";
          sprintf(Temp_TopicBuf, "%s/D", Topic_PubMonitorData);

          bool rt = MQTT_Publish(Temp_TopicBuf, Temp_PubBuffer, 0);
          if (rt != ALTEEM_SUCCESS)
          {
            LOG_ERROR("Publish Zigbee Monitor Message Failed %d\r\n", ret);
            /* No Need to Save into Memory as Live Data */
          }
          else
          {
            LOG_INFO("Published Zigbee Monitor Message\t\r\n");
          }
        }
        else /* Proxy Data */
        {
          sprintf(Temp_PubBuffer, "{\"DATA\":[%s]}\n", Temp_SaveBuffer);
          if (strstr(Rcvdbuffer, "DIL")) /* Check if Live Data */
          {
            char Temp_TopicBuf[100] = "";
            sprintf(Temp_TopicBuf, "%s/D", Topic_PubLiveProxyData);
            bool rt = MQTT_Publish(Temp_TopicBuf, Temp_PubBuffer, 0);
            if (rt != ALTEEM_SUCCESS)
            {
              LOG_ERROR("Publish Zigbee ProxyData Message Failed %d\r\n", ret);
              /* No Need to Save into Memory as Live Data */
            }
            else
            {
              LOG_INFO("Published Zigbee ProxyData Live Message\t\r\n");
            }
          }
          else if (IsMQTTConBrokerConfig && mqttClient.connected() && (isMqttQueueFree() == ALTEEM_SUCCESS) /* &&  mqtt_service_flag == 1)*/)//======================
          {
            char Temp_TopicBuf[100] = "";
            sprintf(Temp_TopicBuf, "%s/D", Topic_PubProxyData);
            bool rt = MQTT_Publish(Temp_TopicBuf, Temp_PubBuffer, 2);
            if (rt != ALTEEM_SUCCESS)
            {
              LOG_ERROR("Publish Zigbee ProxyData Message Failed %d,Saving Into Memory\r\n", ret);

              /* Save into Memory */
              bool Ret = SaveData_IntoSD(Temp_SaveBuffer);
              if (Ret != ALTEEM_SUCCESS)
              {
                LOG_ERROR("Saving Into Memory Failed\r\n");
              }
            }
            else
            {
              LOG_INFO("Published Zigbee ProxyData Message\r\n");
            }
          }
          else
          {
            bool Ret = SaveData_IntoSD(Temp_SaveBuffer);
              if (Ret != ALTEEM_SUCCESS)
              {
                LOG_ERROR("Saving Into Memory Failed\r\n");
              }
          }
        }
      }
      else /* As MQTT not Connected Save into Memory */
      {
        /* Save into Memory Card File */
        // digitalWrite(SRV_LED, 0);

        /* Check If Data is not Live Data or Error Log Then Save into SD Card */
        if (!((strstr(Rcvdbuffer, "DISL")) || (strstr(Rcvdbuffer, "DIL")) || (strstr(Rcvdbuffer, "DIER")) || (strstr(Rcvdbuffer, "DIMN"))))
        {
          LOG_ERROR("MQTT Not Connected or Queue is Full,Saving Into Memory\r\n");
          /* Save into Memory */
          bool Ret = SaveData_IntoSD(Temp_SaveBuffer);
          if (Ret != ALTEEM_SUCCESS)
          {
            LOG_ERROR("Saving Into Memory Failed\r\n");
          }
        }
        // else   - NOT REQUIRED - 12.05.2025
        // {
        //   LOG_ERROR("MQTT Not Connected or Queue is Full,Saving Into Memory\r\n");

        //   /* Save into Memory */
        //   bool Ret = SaveData_IntoSD(Temp_SaveBuffer);
        //   if (Ret != ALTEEM_SUCCESS)
        //   {
        //     LOG_ERROR("Saving Into Memory Failed\r\n");
        //   }
        // }
      }
    }
    //  ABORT_READ : if (0);
  }
}

/* Print Debig Data on Uart
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool LOG_ERROR(const char *pcFormat, ...)
{
  memset(buffer_er, 0, 1024);
  va_list args;
  va_start(args, pcFormat);
  vsnprintf(buffer_er, 1024, pcFormat, args);
  va_end(args);
  Serial.print("ERROR : ");
  Serial.print(buffer_er);
  return true;
}

/* Print Debig Data on Uart
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool LOG_INFO(const char *pcFormat, ...)
{
  memset(buffer_, 0, 1024);
  va_list args;
  va_start(args, pcFormat);
  vsnprintf(buffer_, 1024, pcFormat, args);
  va_end(args);
  Serial.print("INFO : ");
  Serial.print(buffer_);
  return true;
}

/* Read UDID of Device
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool Read_UDID()
{
  if (IsUDIDAppended == false)
  {
    byte MacAd[6];
    WiFi.macAddress(MacAd);
    char buf[34] = "";
    sprintf(buf, "%02x%02x%02x%02x%02x%02x", MacAd[5], MacAd[4], MacAd[3], MacAd[2], MacAd[1], MacAd[0]);
    strcat(Topic_SubBrokerConfig, buf);
    strcat(Topic_PubBrokerConfig, buf);
    LOG_INFO("Topic_SubBrokerConfig : %s\t Topic_PubBrokerConfig : %s\n\r", Topic_SubBrokerConfig, Topic_PubBrokerConfig);
    LOG_INFO("UDID Append on Topics Successful\n\r");
    IsUDIDAppended = true;
    return ALTEEM_SUCCESS;
  }
  else
  {
    return ALTEEM_ERROR;
  }
}

/* Insert Into LOG File of Memory Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool LOG_INTOSD(const char *pcFormat, ...)
{
  if (flag_SDInit == ALTEEM_SUCCESS)
  {
    /* Check and Wait for Mutex */
    xSemaphoreTake(LogGetMutex, portMAX_DELAY); // pthread_mutex_lock(&SerialDataGetMutex);

    /* Prepare Char Buffer From Provided Variables */
    memset(buffer, 0, 1024);
    va_list args;
    va_start(args, pcFormat);
    vsnprintf(buffer, 1024, pcFormat, args);
    va_end(args);

    /* Open and Write into LOG File */
    File flog;
    flog = SD_MMC.open(logfile, FILE_APPEND); // fopen(logfile, "a+");                                                             //flog = fopen(logfile, "a+");
    if (!flog)
    {
      LOG_ERROR("Error: could not Open Log File\r\n");
      flog.close();
      xSemaphoreGive(LogGetMutex);
      return -1;
    }

    if (RTC_ReadTime() != ALTEEM_SUCCESS)
    {
      LOG_INFO("RTC_ReadTime Failed");
    }

    memset(log_buf, 0, 1300);
    sprintf(log_buf, "Log Time : %s \t %s", timestamp, buffer);

    flog.print(log_buf);
    flog.close();
   
    UpdateFileSize(LOGDATA);
    
    xSemaphoreGive(LogGetMutex);
    // Serial.println(" inside log to save function");
    return ALTEEM_SUCCESS;
  }
  else
  {
    return ALTEEM_ERROR;
  }
}

/* Callback Function for Broker Config From device.alteemiot.com */
void BrokerConfigRxCallback(char *payload)
{
  char payload_config[200] = " ";
  LOG_INFO("Broker Config Recvd from AlteemIOT :%s\r\n", payload);

  strcpy(payload_config, payload);

  char *pointer = strstr(payload_config, "\"TI\"");
  *pointer = 0;


  /* Check for Valid Responce*/
  if (strstr(payload, "DN") && (flag_BrokerConfigRx != BROKER_CONFIG_RX))
  {
    /* Check If Authorized Device */
    if (strstr(payload, "UNREG") == NULL)
    {
      flag_EnCounting = false;

      char dev_name[20] = "";
      char *p = strstr(payload, ":") + 2;
      strncpy(dev_name, p, strstr(p, "\"") - p);

      flag_BrokerConfigRx = BROKER_CONFIG_RX;

      /* Append Device name into All Topics */
      strcat(Topic_PubZigbeeConfig, dev_name);
      strcat(Topic_PubLiveProxyData, dev_name);
      strcat(Topic_PubLiveSerialData, dev_name);
      strcat(Topic_PubLogData, dev_name);
      strcat(Topic_PubProxyData, dev_name);
      strcat(Topic_PubSerialData, dev_name);
      strcat(Topic_PubMonitorData, dev_name);
      strcat(Topic_PubOtaData, dev_name);
      strcat(Topic_PubService, dev_name);

      strcat(Topic_SubLogData, dev_name);
      strcat(Topic_SubOtaData, dev_name);
      strcat(Topic_SubService, dev_name);
      strcat(Topic_SubZigbeeConfig, dev_name);

      char *index = strstr(p, ":") + 1;

      /* Check for IP Address or Web Address */
      if (*index == '0') // Web Address
      {
        char *index_ad = strstr(index, ":") + 2;

        strncpy(server_ad, index_ad, strstr(index_ad, "\"") - index_ad);
        port = atoi(strstr(index_ad, ":") + 1);

        LOG_INFO("Broker Addr :%s port :%d\r\n", server_ad, port);

        mqttClient.setServer(server_ad, port);
      }
      else if (*index == '1') // IP Address
      {
        char *index_ad = strstr(index, ":") + 2;
        char ip_ad[100] = "";
        strncpy(ip_ad, index_ad, strstr(index_ad, "\"") - index_ad);
        int port = atoi(strstr(index_ad, ":") + 1);
        ip.fromString(ip_ad);

        LOG_INFO("Broker IP :%s port :%d\r\n", ip_ad, port);

        Serial.print("IP Address is ");
        Serial.println(ip);

        mqttClient.setServer(ip, port);
      }

      if (flag_TimeSyncDone == false)
      {
        char *un = strstr(payload, "\"TI\"");
        if (un)
        {
          LOG_INFO("Synchronizing Clock from Config Data\r\n");
          // Format "2021-04-03T16:28:44"
          struct tm dT; // SlDateTime_t dT =  {0};

          un = strstr(un, ":");
          dT.tm_year = atoi(un + 2);

          un = strstr(un, "-");
          dT.tm_mon = atoi(un + 1);

          un = strstr(un + 1, "-");
          dT.tm_mday = atoi(un + 1);

          un = strstr(un, "T");
          dT.tm_hour = atoi(un + 1);

          un = strstr(un, ":");
          dT.tm_min = atoi(un + 1);

          un = strstr(un + 1, ":");
          dT.tm_sec = atoi(un + 1);

#ifndef EXT_RTC

#else
          LOG_INFO("Time From Broker Config : %d:%d:%d %d:%d:%d\r\n", dT.tm_mday, dT.tm_mon, dT.tm_year, dT.tm_hour, dT.tm_min, dT.tm_sec);
          LOG_INTOSD("Time From Broker Config : %d:%d:%d %d:%d:%d\r\n", dT.tm_mday, dT.tm_mon, dT.tm_year, dT.tm_hour, dT.tm_min, dT.tm_sec);
          rtc.adjust(DateTime(dT.tm_year, dT.tm_mon, dT.tm_mday, dT.tm_hour, dT.tm_min, dT.tm_sec));
          flag_TimeSyncDone = 1;
#endif
        }
      }

   //-----Added by Mausam ----------------------------------------   

    if (strcmp(payload_config, payload_sd) == 0)
      {
        Serial.println(" SAME CONFIG RECEIVE ");

        IsMQTTConBrokerConfig = true;

        LOG_INFO("Subscribe topic : %s\r\n", Topic_SubZigbeeConfig);
        uint16_t ret = mqttClient.subscribe(Topic_SubZigbeeConfig, 2); 

        LOG_INFO("Subscribe topic : %s\r\n", Topic_SubLogData);
        ret = mqttClient.subscribe(Topic_SubLogData, 2); 

        LOG_INFO("Subscribe topic : %s\r\n", Topic_SubOtaData);
        ret = mqttClient.subscribe(Topic_SubOtaData, 2); 

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
    else 
      {

         Serial.println("NEW CONFIG RECEIVE");
         SaveBrokerConfig_IntoSD(payload);
         Mqtt_Reconnect();

      }
     

  }

 else
  { /* Invalid Device, Stop EveryThing and Blink ALL Leds for Indication */
      flag_BrokerConfigRx = BROKER_CONFIG_INVALID;

      /* Stop Timer To Stop TIM Led Blinking */
      // Timer_stop(timer0);
      xTimerStop(ledBlinkTimer, 0);
      flag_UNAUTH = 1;
   }
  }
  else
  {
    LOG_ERROR("Invalid Broker Config Data Recvd from AlteemIOT\r\n");
    LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - Invalid Broker Config Data Recvd from AlteemIOT\r\n", __FILE__, __FUNCTION__, __LINE__);
  }
}






/* Callback Function for Zigbee Config */
void ZigbeeConfigRxCallback(char *payload)
{
  LOG_INFO("Zigbee Config Recvd :%s\r\n", payload);
  size_t bytesWritten = 0;

  if (strstr(payload, "\"NM\"")) // is Configuration String
  {
    /*********** Logic for Delay Calculation for Syncing with Time ***********/

    /* Update Time Value */
    RTC_ReadTime();

    struct tm dateTime; // SlDateTime_t dateTime =  {0};

    sscanf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d", &dateTime.tm_year, &dateTime.tm_mon, &dateTime.tm_mday, &dateTime.tm_hour, &dateTime.tm_min, &dateTime.tm_sec);

    uint32_t Cur_ElapsedSec = (dateTime.tm_min * 60) + dateTime.tm_sec;

    uint32_t Cur_RemainSec = (3600 - Cur_ElapsedSec);

    uint32_t Req_DelaySec = 0, DataSendTime = 0;
    char ConfigSubStr[500] = "";
    char *index;

    /* Fetch DataSendTime */
    if (strstr(payload, "TO")) // Proxy String
    {
      index = (strstr(payload, "TO")) + 4;
      DataSendTime = atoi(index);
      Req_DelaySec = (Cur_RemainSec % DataSendTime);
    }
    else if (strstr(payload, "T0")) // Serial String
    {

      index = (strstr(payload, "T0")) + 4;
      DataSendTime = atoi(index);
      Req_DelaySec = (Cur_RemainSec % DataSendTime);
    }

    index = (strstr(payload, "MD")) + 5;
    strncpy(ConfigSubStr, payload, (index - payload));

    Req_DelaySec = DataSendTime - Req_DelaySec;

    char TempStr[100] = "";
    sprintf(TempStr, ",\"DL\":%d", Req_DelaySec);
    strcat(ConfigSubStr, TempStr);
    strcat(ConfigSubStr, index);
    LOG_INFO("Current Time : %s , Modified Config String After Adding Delay: %s", timestamp, ConfigSubStr);

    Serial2.print(ConfigSubStr);

    /* Code for Error Checking */
    uint8_t checksum = 0;
    char checksum_str[5];
    int i = 0;
    for (i = 0; i < strlen(ConfigSubStr); i++)
    {
      checksum += ConfigSubStr[i];
    }

    LOG_INFO("Zigbee Config CheckSum is %d\r\n", checksum);
    sprintf(checksum_str, "%d", checksum);
    Serial2.println(checksum_str);
  }
  else if (strstr(payload, "\"MST\"")) // is Configuration String
  {
    /*********** Logic for Delay Calculation for Syncing with Time ***********/

    /* Update Time Value */
    RTC_ReadTime();

    struct tm dateTime; // SlDateTime_t dateTime =  {0};

    sscanf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d", &dateTime.tm_year, &dateTime.tm_mon, &dateTime.tm_mday, &dateTime.tm_hour, &dateTime.tm_min, &dateTime.tm_sec);

    uint32_t Cur_ElapsedSec = (dateTime.tm_min * 60) + dateTime.tm_sec;

    uint32_t Cur_RemainSec = (3600 - Cur_ElapsedSec);

    uint32_t Req_DelaySec = 0, DataSendTime = 0;
    char ConfigSubStr[1000] = "";
    char *index;

    /* Fetch DataSendTime */
    if (strstr(payload, "DST")) // Proxy String
    {
      index = (strstr(payload, "DST")) + 5;
      DataSendTime = atoi(index);
      DataSendTime /= 1000; // Convert ms to Sec
      Req_DelaySec = (Cur_RemainSec % DataSendTime);
    }

    index = strstr(payload, "},");
    strncpy(ConfigSubStr, payload, (index - payload));

    Req_DelaySec = DataSendTime - Req_DelaySec;

    char TempStr[100] = "";
    sprintf(TempStr, ",\"SDL\":%d", Req_DelaySec);
    strcat(ConfigSubStr, TempStr);
    strcat(ConfigSubStr, index);
    LOG_INFO("Current Time : %s , Modified Config String After Adding Delay: %s", timestamp, ConfigSubStr);
    Serial2.println(ConfigSubStr);
  }
  else
  {
    Serial2.println(payload);
  }
}

/*********** Callback Function for Log Request ***********/

void LogReqRxCallback(char *temp_payload)
{
  LOG_INFO("Log Request Recvd  :%s\r\n", temp_payload);
  strcpy(payload_cmd, temp_payload);
  Flag_RxCallBack_Data_Available = 1;
}

void Check_Payload_Data(void)
{ 

  if(Flag_RxCallBack_Data_Available != 0)
  {
    Serial.println(" Inside Check_Payload_Data function ");
    Flag_RxCallBack_Data_Available = 0;

  // ADDED COMMANDS 

  if (strstr(payload_cmd, "LOGREQ"))
  { 
  
  File fpr = SD_MMC.open(logfile);
  long file_size = fpr.size();
  fpr.close(); 
  long FilePointer_LogData_ = ReadFilePointer_FromSD(LOGDATA);
  LOG_INFO("FilePointer_LogData : %d\r\n", FilePointer_LogData_);
  char mqttPayload[120]; // Adjust the size as needed
  sprintf(mqttPayload, "LOG REQUEST RECEIVE : FilePointer_LogData = %ld : File Size = %ld ", FilePointer_LogData_, file_size);
  MQTT_Publish(Topic_PubLogData, mqttPayload, 2);
  SaveFilePointer_IntoSD(LOGDATA, 0);
  flag_LogReqRx = 1;

    // log_file();

  }

  else if (strstr(payload_cmd, "READWIFIFILE"))
  { 
  
  File fpr = SD_MMC.open(wififile);
  long file_size = fpr.size();
  fpr.close(); 
  char mqttPayload[120]; 
  sprintf(mqttPayload, "WIFI READ REQUEST RECEIVE : File Size = %ld ", file_size);
  MQTT_Publish(Topic_PubLogData, mqttPayload, 2);

  wifireadfromsd();
   }
  
   else if (strstr(payload_cmd, "FPLOGCLR"))
  {   
     MQTT_Publish(Topic_PubLogData, "LOG POINTER CLEAR RECEIVE ", 2);
     SaveFilePointer_IntoSD(LOGDATA, 0);
     LOG_INTOSD("Cleared log Pointer File\r\n");
    
  }

  else if (strstr(payload_cmd, "FPPROXYCLR"))
  {
   MQTT_Publish(Topic_PubLogData, "PROXY POINTER CLEAR RECEIVE ", 2);
   SaveFilePointer_IntoSD(PROXYDATA, 0);
   LOG_INTOSD("Cleared Proxy Pointer File\r\n");
  }


  else if (strstr(payload_cmd, "FPSERCLR"))
  {
    MQTT_Publish(Topic_PubLogData, "SERIAL POINTER CLEAR RECEIVE ", 2);
    SaveFilePointer_IntoSD(SERIALDATA, 0);
    LOG_INTOSD("Cleared Serial Pointer File\r\n");

  }


  
  else if ( strstr( payload_cmd, "GETALLFP"))
  {
     long sr_pr = ReadFilePointer_FromSD(SERIALDATA);
     long px_pr = ReadFilePointer_FromSD(PROXYDATA);
     long lg_pr = ReadFilePointer_FromSD(LOGDATA);
    
      File fpr = SD_MMC.open(logfile);
      long LOG_file_size = fpr.size();
      fpr.close(); 
        fpr = SD_MMC.open(proxydatafile);
      long PROXY_file_size = fpr.size();
      fpr.close(); 
        fpr = SD_MMC.open(serialdatafile);
      long SERIAL_file_size = fpr.size();
      fpr.close(); 

     char mqttPayload[300]; // Adjust the size as needed
     sprintf(mqttPayload, "FilePointer SerialData = %ld : FilePointer ProxyData = %ld : FilePointer LogData = %ld\nFile Serial Data       = %ld : File Proxy Data       = %ld : File Log Data       = %ld\nPointer SerialVariable = %ld : Proxy Pointer Variable= %ld : Log Pointer Variable= %ld\n", sr_pr, px_pr , lg_pr,SERIAL_file_size , PROXY_file_size , LOG_file_size,FilePointer_SerialData,FilePointer_ProxyData,FilePointer_LogData);
     MQTT_Publish(Topic_PubLogData, mqttPayload, 2);

  }


  else if (strstr(payload_cmd, "LOGCLR"))
  {
    MQTT_Publish(Topic_PubLogData, "LOG CLEAR RECEIVE ", 2);
    Clear_LogFile();
  }

  else if (strstr(payload_cmd, "PROXYCLR"))
  {
    MQTT_Publish(Topic_PubLogData, "PROXY CLEAR RECEIVE ", 2);
    Clear_ProxyFile();
  }

     else if (strstr(payload_cmd, "SERCLR"))
  {
    MQTT_Publish(Topic_PubLogData, "SERIAL CLEAR RECEIVE ", 2);
    Clear_SerialFile();
  }




  else if (strstr(payload_cmd, "BRCLR")) // To clear Saved Broker Config from SD Card
  {
    /* Delete File */
    Clear_BrokerConfigFile();

    /* Rester Device So It Will COnnect Req Broker Config From Alteem IOT */
    ESP.restart();
  }
}
}



void wifireadfromsd(){

File fcd;
      fcd = SD_MMC.open(wififile); // fcd = fopen(logfile, "r");
      if (!fcd)
      {
        LOG_ERROR("Error: could not Open LOG File\r\n");
        LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - Error: could not Open LOG File\r\n", __FILE__, __FUNCTION__, __LINE__);
        fcd.close();
        return;
      }
      // Read file pointer
      long file_pointer = 0 ;
      // set file pointer
      fcd.seek(file_pointer);

      // Clear previous Data
      memset(sddata_buff, 0, strlen(sddata_buff));

      // Read Line from File
      char temp_sdbuf[520] = "";
      int bytesReadfrmFile = 0;

      while ((bytesReadfrmFile < (MQTT_BUF_SIZE - MQTT_BUF_SKIP_SIZE)) && (readLinefromFile(fcd, temp_sdbuf, 500) != ALTEEM_ERROR) /*(fgets(temp_sdbuf, 500, fcd) != NULL)*/)
      {
        // Increase Byte Read Counter
        
        bytesReadfrmFile += strlen(temp_sdbuf);
        strcat(sddata_buff, temp_sdbuf);
      }

      if (bytesReadfrmFile > 0)
      {
       
        

        bool ret = MQTT_Publish(Topic_PubLogData, sddata_buff, 0);
        if (ret != ALTEEM_SUCCESS) // if (ret < 0)
        {
          LOG_ERROR("Publish WIFI READ From SD Card Failed %d\r\n", ret);
          fcd.close();
          return;
        }
        else
        {
          LOG_INFO("Published WIFI READ From SD Card\r\n");
        }

          fcd.close();
       
        
      }
      else
      {
    
        fcd.close();
      }


}
/* Callback Function for OTA Request */
void OtaReqRxCallback(char *payload)
{
  LOG_INFO("OTA Request Recvd  :%s\r\n", payload);
  LOG_INTOSD("OTA Request Recvd  :%s\r\n", payload);

  /* Msg */
  //{"OT":"OTAT","link":"https://raw.githubusercontent.com/rndalteem/TI_OTA/b7fe90d134dcb03f5ca02a9fceeeb6684c617801/OTA_ROUTER/IOT_ROUTER_NEW_PCB.ota"}

  JsonDocument otafile;
  deserializeJson(otafile, payload);

  const char *ota = otafile["OT"];
  const char *OTA_FILE_link = otafile["link"];

  Serial.print("OT VALUE : ");
  Serial.println(ota);
  Serial.print("Link     : ");
  Serial.println(OTA_FILE_link);

  if ((strcmp(ota, "OTAT") == 0) && link)
  {
    IsMQTTConBrokerConfig = false;
    flag_esp_ota = true;
    // strcpy(httpLink, link);
    // HTTP_DwldFileIntoSDCard();
    performOTAUpdate(OTA_FILE_link);

  }
  /* Check for OTA for Router */
  else if (strstr(payload, "OTAR"))
  {
    /* Hold MQTT Thread */
    // OtaHoldMQTT = true;

    /* To Disable Adding New Messages into Queue */
    IsMQTTConBrokerConfig = false;

    /* Download Image */
    /*  Fetch Link */
    char *link = strstr(payload, "link");
    if (link)
    {
      link = strstr(link, ":") + 2;
      memset(httpLink, 0, 500);
      strncpy(httpLink, link, (strstr(link, "\"") - (link)));
    }

    OTAImageType = OTA_ROUTER;
    /* Download Image Into SD Card */
    HTTP_DwldFileIntoSDCard();
  }
  /* Check for OTA for Coord */
  else if (strstr(payload, "OTAC"))
  {
    /* To Disable Adding New Messages into Queue */
    IsMQTTConBrokerConfig = false;

    /* Download Image */
    /*  Fetch Link */
    char *link = strstr(payload, "link");
    if (link)
    {
      link = strstr(link, ":") + 2;
      memset(httpLink, 0, 500);
      strncpy(httpLink, link, (strstr(link, "\"") - (link)));
    }

    OTAImageType = OTA_COORDINATOR;
    /* Download Image Into SD Card */
    HTTP_DwldFileIntoSDCard();
  }
}

/* Start ESP OTA Update Process */
void Start_ESPOTA()
{
  LOG_INFO("Starting OTA Upadate\r\n");
  String url = String(g_OtaTarFileURL);
  String loc = String(g_OtaTarFileLoc);
  const char *locChar = loc.c_str();
  LOG_INTOSD("Starting OTA Upadate\r\n");
  
  esp32FOTA.forceUpdate("://raw.githubusercontent.com", 443, locChar, false); // check signature: true

  LOG_INTOSD("OTA ERROR \r\n");
}

// Set time via NTP, as required for x.509 validation
void setClock()
{
  configTime(5.5 * 3600, 0, "time.google.com", "time.nist.gov", "utcnist.colorado.edu");
  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2)
  {
    delay(500);
    Serial.print(".");
    yield();
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

/* Init RTC With Default Time
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool Init_RTC()
{
  /* Set Default Time */

  LOG_INFO("\n\rSetting Default Time\n\r");

#ifndef EXT_RTC
  setClock();
#else // External RTC

  if (!rtc.begin())
  {
    LOG_ERROR("Couldn't find RTC");
    LOG_INTOSD("Couldn't find RTC");
  }
  else
  {
  }

#endif

  return ALTEEM_SUCCESS;
}

/* Sync Time of RTC With NTP
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool RTC_Sync()
{
  if (setTimeByNTP() == ALTEEM_SUCCESS)
  {
    /* Commneted for Sync time fron COnfig Msg */
    flag_TimeSyncDone = 1;
    LOG_INFO("\n\rSetting Default Time Done\n\r");
    LOG_INTOSD("\n\rRTC_Sync Done\n\r");
    return ALTEEM_SUCCESS;
  }
  return ALTEEM_ERROR;
}

/* Read TimeValue from RTC
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool RTC_ReadTime()
{
#ifndef EXT_RTC
  if (flag_TimeSyncDone != false)
  {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
      Serial.println("Failed to obtain time");
      return false;
    }

    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

    timeinfo.tm_year = timeinfo.tm_year + 1900;
    memset(timestamp, 0, sizeof(timestamp));
    sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d", timeinfo.tm_year, timeinfo.tm_mon, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return ALTEEM_SUCCESS;
  }
#else
  DateTime now = rtc.now();

  memset(timestamp, 0, sizeof(timestamp));
  sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  return ALTEEM_SUCCESS;

#endif
  return ALTEEM_SUCCESS;
}

void Mqtt_Reconnect()
{
  Serial.println(" Insde mqtt reconnect function ");
  mqttClient.disconnect(true);
}

/* Initialize SD Card
   Initializes FatsFs Driver Also
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool Init_SDCard()
{
  flag_SDInit = ALTEEM_ERROR;

  pinMode(15, INPUT_PULLUP);
  pinMode(2, INPUT_PULLUP);

  if (!SD_MMC.begin("/sdcard", true, false, 1000))
  {
    LOG_ERROR("Card Mount Failed\r\n");
    return ALTEEM_ERROR;
  }

  uint8_t cardType = SD_MMC.cardType();

  if (cardType == CARD_NONE)
  {
    LOG_ERROR("No SD card attached\r\n");
    // flag_SDInit = ALTEEM_ERROR;
    return ALTEEM_ERROR;
  }

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC)
  {
    LOG_INFO("MMC\r\n");
  }
  else if (cardType == CARD_SD)
  {
    LOG_INFO("SDSC\r\n");
  }
  else if (cardType == CARD_SDHC)
  {
    LOG_INFO("SDHC\r\n");
  }
  else
  {
    LOG_INFO("UNKNOWN\r\n");
  }

  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  LOG_INFO("SD Card Size: %lluMB\r\n", cardSize);
  flag_SDInit = ALTEEM_SUCCESS;
  return ALTEEM_SUCCESS;
}

/* Read Wifi Credentials from SD Card
   Returns Nos of Creds on Success
           -1 on Error
*/
int ReadWifiCred_FromSD()
{
  if (flag_SDInit != ALTEEM_ERROR)
  {
    File fpr;
    fpr = SD_MMC.open(wififile);
    if (!fpr)
    {
      LOG_ERROR("Error: could not Wifi Cred File\r\n");
      fpr = SD_MMC.open(wififile, FILE_WRITE);
      fpr.close();
      return -1;
    }
    char buf[200] = "";
    unsigned int bytesRead; // = fread(buf, 1, 30, fpr);
    char i = 0;
    while ((readLinefromFile(fpr, buf, 200) != ALTEEM_ERROR) && (i<30))
    {
      /* Increase Byte Read Counter */
      bytesRead += strlen(buf);
      if (strstr(buf, ","))
      {
        LOG_INFO("\n\rWifi Credentials in Buffer : %s\r\n", buf);
        strncpy(WifiCred[i].ssid, buf, (strstr(buf, ",") - (buf)));
        strcpy(WifiCred[i].password, strstr(buf, ",") + 1);

        /* If Password Contains '\r' OR '\n' then Remove it */
        if (strstr(WifiCred[i].password, "\r"))
        {
          WifiCred[i].password[strcspn(WifiCred[i].password, "\r")] = '\0';
        }
        if (strstr(WifiCred[i].password, "\n"))
        {
          WifiCred[i].password[strcspn(WifiCred[i].password, "\n")] = '\0';
        }
        LOG_INFO("\r\nWiFi Cread - SSID %s PASS %s\r\n", WifiCred[i].ssid, WifiCred[i].password);

        i++;
        memset(buf, 0, 200);
      }
    }

    if (bytesRead == 0)
    {
      LOG_ERROR("Wifi Cred Read Error!!\r\n");
      LOG_INTOSD("Wifi Cred Read Error!!\r\n");
      fpr.close(); // fclose(fpr);
      return -1;
    }

    fpr.close();

    return i;
  }
  else
    return -1;
}

/* Read Line from SD Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool readLinefromFile(File file, char *readdata, int MaxLen)
{
  char ch;
  int ReadCount = 0;
  while (file.available())
  {
    ch = file.read();

    if (ch == '\n')
    {
      *readdata = ch;
      readdata++;
      *readdata = '\0';
      return ALTEEM_SUCCESS;
    }
    else
    { // Everything's OK, append to the string
      *readdata = ch;
      readdata++;
      ReadCount++;

      if (ReadCount >= MaxLen)
      {
        return ALTEEM_ERROR;
      }
    }
  }
  return ALTEEM_ERROR;
}

/* Save Data into SD Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool SaveData_IntoSD(char *data)
{
  if (flag_SDInit == ALTEEM_SUCCESS)
  {
    bool IsSerialData = false;
    File fps;
    if (strstr(data, "DIS")) /* Check if Serial Data */
    {
      /* Check and Wait for Mutex */
      xSemaphoreTake(SerialDataGetMutex, portMAX_DELAY); // pthread_mutex_lock(&SerialDataGetMutex);

      IsSerialData = true;
      fps = SD_MMC.open(serialdatafile, FILE_APPEND); // fps = fopen(serialdatafile, "a+");
      if (!fps)
      {
        LOG_ERROR("SerialData File Open for Append Failed,Creating New File \r\n");
        LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - SerialData File Open for Append Failed,Creating New File \r\n", __FILE__, __FUNCTION__, __LINE__);
        fps = SD_MMC.open(serialdatafile, FILE_WRITE); // fps = fopen(serialdatafile, "w+");
        if (!fps)
        {
          LOG_ERROR("Creating New File For SerialData Failed\r\n");
        }
      }
    }
    else
    {
      /* Check and Wait for Mutex */
      xSemaphoreTake(ProxyDataGetMutex, portMAX_DELAY); // pthread_mutex_lock(&ProxyDataGetMutex);
      IsSerialData = false;
      fps = SD_MMC.open(proxydatafile, FILE_APPEND); // fps = fopen(proxydatafile, "a+");
      if (!fps)
      {
        LOG_ERROR("ProxyData File Open for Append Failed,Creating New File \r\n");
        LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - ProxyData File Open for Append Failed,Creating New File \r\n", __FILE__, __FUNCTION__, __LINE__);

        fps = SD_MMC.open(proxydatafile, FILE_WRITE); // fps = fopen(proxydatafile, "w+");
        if (!fps)
        {
          LOG_ERROR("Creating New File For ProxyData Failed\r\n");
        }
      }
    }

    if (fps.println(data))
    {
      // LOG_INFO("Message appended");
    }
    else
    {
      LOG_ERROR("File Append failed");
    }
    fps.flush();
    fps.close();

    if (IsSerialData == true)
    {
      UpdateFileSize(SERIALDATA);
      xSemaphoreGive(SerialDataGetMutex);
    }
    else
    {
      UpdateFileSize(PROXYDATA);
      xSemaphoreGive(ProxyDataGetMutex);
    }
    return ALTEEM_SUCCESS;
  }
  else
  {
    LOG_ERROR("Save Data into SDCard Failed,SD Card not Initialized\r\n");
    return ALTEEM_ERROR;
  }
}

/* Read Current File Pointer from SD
   Return ALTEEM_SUCCESS
          or ALTEEM_ERROR on Error
*/
bool UpdateFileSize(uint8_t FileType)
{
  if (flag_SDInit == ALTEEM_SUCCESS)
  {
    File fpr;
    if (FileType == PROXYDATA)
    {
      fpr = SD_MMC.open(proxydatafile); // fpr = fopen(proxydatafile, "r");
    }
    else if (FileType == SERIALDATA)
    {
      fpr = SD_MMC.open(serialdatafile); // fpr = fopen(serialdatafile,"r");
    }
    else if (FileType == LOGDATA)
    {
      fpr = SD_MMC.open(logfile); // fpr = fopen(logfile,"r");
    }

    if (!fpr)
    {
      LOG_INFO("Error: could not Open File to Get Size\r\n");
      if (FileType == PROXYDATA)
      {
        fpr = SD_MMC.open(proxydatafile, FILE_WRITE);
        fpr.close();
        SaveFilePointer_IntoSD(PROXYDATA, 0);
      }
      else if (FileType == SERIALDATA)
      {
        fpr = SD_MMC.open(serialdatafile, FILE_WRITE);
        fpr.close();
        SaveFilePointer_IntoSD(SERIALDATA, 0);
      }
      else if (FileType == LOGDATA)
      {
        fpr = SD_MMC.open(logfile, FILE_WRITE);
        fpr.close();
        SaveFilePointer_IntoSD(LOGDATA, 0);
      }
      return 0;
    }

    long size = fpr.size();

    if (FileType == PROXYDATA)
    {
      FileSize_ProxyData = size;
      LOG_INFO("FileSize_ProxyData : %d\r\n", FileSize_ProxyData);
    }
    else if (FileType == SERIALDATA)
    {
      FileSize_SerialData = size;
      LOG_INFO("FileSize_SerialData : %d\r\n", FileSize_SerialData);
    }
    else if (FileType == LOGDATA)
    {
      FileSize_LogData = size;
      LOG_INFO("FileSize_LogData : %d\r\n", FileSize_LogData);
    }

    fpr.close();

    return ALTEEM_SUCCESS;
  }
  else
    return ALTEEM_ERROR;
}

/* Save File Pointer on SD Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool SaveFilePointer_IntoSD(uint8_t FileType, long data)
{
  if (flag_SDInit == ALTEEM_SUCCESS)
  {
    File fp;
    if (FileType == PROXYDATA)
    {
      fp = SD_MMC.open(proxyreadpointerfile, FILE_WRITE); // fp = fopen(proxyreadpointerfile, "w");
      FilePointer_ProxyData = data;
      LOG_INFO("FilePointer_ProxyData : %d\r\n", FilePointer_ProxyData);
    }
    else if (FileType == SERIALDATA)
    {
      fp = SD_MMC.open(serialreadpointerfile, FILE_WRITE); // fp = fopen(serialreadpointerfile, "w");
      FilePointer_SerialData = data;
      LOG_INFO("FilePointer_SerialData : %d\r\n", FilePointer_SerialData);
    }
    else if (FileType == LOGDATA)
    {
      fp = SD_MMC.open(logreadpointerfile, FILE_WRITE); // fp = fopen(logreadpointerfile, "w");
      FilePointer_LogData = data;
      LOG_INFO("FilePointer_LogData : %d\r\n", FilePointer_LogData);
    }

    if (!fp)
    {
      LOG_ERROR("Error: could not Open FP File\r\n");
      fp.close();

      return ALTEEM_ERROR;
    }
    char buf[30] = "";
    sprintf(buf, "%d\n", data);

    if (fp.print(buf))
    {
    }
    else
    {
      LOG_ERROR("could not write FP File\r\n");
    }

    fp.close();

    return ALTEEM_SUCCESS;
  }
  return ALTEEM_ERROR;
}

/* Function for Handling Data from Memory Card */
void SD_HandleData()
{
  /* Check if MQTT Connected ,MQTT Sending Is Not In Process and MQTT Queue is Free */
  if (IsMQTTConBrokerConfig && mqttClient.connected() /*&& (IsMQTTSendBusy == false)*/ && (isMqttQueueFree() == ALTEEM_SUCCESS))
  {
    /* Check if Data Present into Memory Card */
    if ((FileSize_ProxyData > FilePointer_ProxyData)  && ( FilePointer_ProxyData >=0)/* &&  mqtt_service_flag == 1)*/)
    {
      /* Publish Data in Bunch */
      /* Check and Wait for Mutex */
      xSemaphoreTake(ProxyDataGetMutex, portMAX_DELAY); // pthread_mutex_lock(&ProxyDataGetMutex);
      File fcd;
      fcd = SD_MMC.open(proxydatafile); // fcd = fopen(proxydatafile,"r");
      if (!fcd)
      {
        LOG_ERROR("Error: could not Open DATA File\r\n");
        LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - Error: could not Open DATA File\r\n", __FILE__, __FUNCTION__, __LINE__);
        fcd.close();
        xSemaphoreGive(ProxyDataGetMutex);
        return;
      }
      // Read file pointer
      long file_pointer = FilePointer_ProxyData; // ReadFilePointer_FromSD(PROXYDATA);

      // set file pointer
      fcd.seek(file_pointer); // fseek( fcd, file_pointer, SEEK_SET );

      // Clear previous Data
      memset(sddata_buff, 0, strlen(sddata_buff));

      // ready Buffer for Sending
      strcpy(sddata_buff, "{\"DATA\":[");

      // Read Line from File
      char temp_sdbuf[520] = "";
      int bytesReadfrmFile = 0;

      while ((bytesReadfrmFile < (MQTT_BUF_SIZE - MQTT_BUF_SKIP_SIZE)) && (readLinefromFile(fcd, temp_sdbuf, 500) != ALTEEM_ERROR) /* (fgets(temp_sdbuf, 500, fcd) != NULL) */)
      {
        // Increase Byte Read Counter
        bytesReadfrmFile += strlen(temp_sdbuf);

        // Merge Data with MainBuffer
        temp_sdbuf[strlen(temp_sdbuf) - 1] = '\0';

        strcat(sddata_buff, temp_sdbuf);

        strcat(sddata_buff, ",");
      }

      if (bytesReadfrmFile > 0)
      {
        // remove ',' at last and add "]}"
        sddata_buff[strlen(sddata_buff) - 1] = '\0';
        strcat(sddata_buff, "]}\n");

        // send to mqtt for sending
        char Temp_TopicBuf[100] = "";
        sprintf(Temp_TopicBuf, "%s/F", Topic_PubProxyData);

        bool ret = MQTT_Publish(Temp_TopicBuf, sddata_buff, 2);
        if (ret != ALTEEM_SUCCESS) // if (ret < 0)
        {
          LOG_ERROR("Publish Zigbee ProxyData Message From SD Card Failed %d\r\n", ret);
          fcd.close();
          xSemaphoreGive(ProxyDataGetMutex);
          return;
        }
        else
        {
          LOG_INFO("Published Zigbee ProxyData Message From SD Card\r\n");
        }

        // Save Pointer
        // check if eof
        if (fcd.available()) // if (!feof(fcd))
        {
          file_pointer = fcd.position(); // ftell(fcd);
          
          if ( file_pointer>=0)
          {
            SaveFilePointer_IntoSD(PROXYDATA, file_pointer);
          }
          else 
          {

            FilePointer_ProxyData += bytesReadfrmFile;
            SaveFilePointer_IntoSD(PROXYDATA, FilePointer_ProxyData);
          }
          
          fcd.close();
        }
        else
        {
          LOG_INFO("End Of FIle Reached!!\r\n");

          // clear content of file
          fcd.close();

          fcd = SD_MMC.open(proxydatafile, FILE_WRITE);
          fcd.close();

          file_pointer = 0;
          SaveFilePointer_IntoSD(PROXYDATA, 0);
          FileSize_ProxyData = 0;
        }
      }
      else
      {
        fcd.close();
      }
      xSemaphoreGive(ProxyDataGetMutex);
    }
    /* Check if Serial Data Present into Memory Card */
    else if ((FileSize_SerialData > FilePointer_SerialData) && (FilePointer_SerialData >=0) /* &&  mqtt_service_flag == 1)*/)
    {
      /* Publish Data in Bunch */
      xSemaphoreTake(SerialDataGetMutex, portMAX_DELAY);
      // steps
      // open file
      File fcd;
      fcd = SD_MMC.open(serialdatafile); // fcd = fopen(serialdatafile, "r");
      if (!fcd)
      {
        LOG_ERROR("Error: could not Open SERIAL File\r\n");
        LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - Error: could not Open SERIAL File\r\n", __FILE__, __FUNCTION__, __LINE__);
        fcd.close();
        xSemaphoreGive(SerialDataGetMutex);
        return;
      }
      // Read file pointer
      long file_pointer = FilePointer_SerialData; // ReadFilePointer_FromSD(SERIALDATA);

      // set file pointer
      fcd.seek(file_pointer); // fseek( fcd, file_pointer, SEEK_SET );

      // Clear previous Data
      memset(sddata_buff, 0, strlen(sddata_buff));

      // ready Buffer for Sending
      strcpy(sddata_buff, "[");

      // Read Line from File
      char temp_sdbuf[520] = "";
      int bytesReadfrmFile = 0;

      while ((bytesReadfrmFile < (MQTT_BUF_SIZE - MQTT_BUF_SKIP_SIZE)) && (readLinefromFile(fcd, temp_sdbuf, 500) != ALTEEM_ERROR) /*(fgets(temp_sdbuf, 500, fcd) != NULL)*/)
      {
        // Increase Byte Read Counter
        bytesReadfrmFile += strlen(temp_sdbuf);

        // Merge Data with MainBuffer
        temp_sdbuf[strlen(temp_sdbuf) - 1] = '\0';

        strcat(sddata_buff, temp_sdbuf);

        strcat(sddata_buff, ",");
      }

      if (bytesReadfrmFile > 0)
      {
        // remove ',' at last and add "]}"
        sddata_buff[strlen(sddata_buff) - 1] = '\0';
        strcat(sddata_buff, "]\n");

        // send to mqtt for sending
        char Temp_TopicBuf[100] = "";
        sprintf(Temp_TopicBuf, "%s/F", Topic_PubSerialData);

        bool ret = MQTT_Publish(Temp_TopicBuf, sddata_buff, 2);
        if (ret != ALTEEM_SUCCESS) // if (ret < 0)
        {
          LOG_ERROR("Publish Zigbee SerialData Message From SD Card Failed %d\r\n", ret);
          fcd.close();
          xSemaphoreGive(SerialDataGetMutex);
          return;
        }
        else
        {
          LOG_INFO("Published Zigbee SerialData Message From SD Card\r\n");
        }

        // Save Pointer
        // check if eof
        if (fcd.available()) // if (!feof(fcd))
        {
          file_pointer = fcd.position(); // ftell(fcd);
          if ( file_pointer >=0 )
          {
                SaveFilePointer_IntoSD(SERIALDATA, file_pointer);
          }
          else 
          {
            FilePointer_SerialData += bytesReadfrmFile;
            SaveFilePointer_IntoSD(SERIALDATA, FilePointer_SerialData);
          }
         
          fcd.close();
        }
        else
        {
          LOG_INFO("End Of FIle Reached!!\r\n");

          // clear content of file
          fcd.close();

          fcd = SD_MMC.open(serialdatafile, FILE_WRITE);
          fcd.close();

          file_pointer = 0;
          SaveFilePointer_IntoSD(SERIALDATA, 0);
          FileSize_SerialData = 0;
        }
      }
      else
      {
        fcd.close();
      }
      xSemaphoreGive(SerialDataGetMutex);
    }
    /* Check if Log Data Request Received Then Send Log Data from Memory Card */
    // else if(flag_LogReqRx && (FileSize_LogData > ReadFilePointer_FromSD(LOGDATA)))
    else if (flag_LogReqRx && (FileSize_LogData > FilePointer_LogData) && ( FilePointer_LogData >=0))
    {
      /* Publish Data in Bunch */

      /* Wait For Mutex */
      xSemaphoreTake(LogGetMutex, portMAX_DELAY);

      File fcd;
      fcd = SD_MMC.open(logfile); // fcd = fopen(logfile, "r");
      if (!fcd)
      {
        LOG_ERROR("Error: could not Open LOG File\r\n");
        LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - Error: could not Open LOG File\r\n", __FILE__, __FUNCTION__, __LINE__);
        xSemaphoreGive(LogGetMutex);
        fcd.close();
        return;
      }
      // Read file pointer
      long file_pointer = FilePointer_LogData; // ReadFilePointer_FromSD(LOGDATA);

      // set file pointer
      fcd.seek(file_pointer);

      // Clear previous Data
      memset(sddata_buff, 0, strlen(sddata_buff));

      // Read Line from File
      char temp_sdbuf[520] = "";
      int bytesReadfrmFile = 0;

      while ((bytesReadfrmFile < (MQTT_BUF_SIZE - MQTT_BUF_SKIP_SIZE)) && (readLinefromFile(fcd, temp_sdbuf, 500) != ALTEEM_ERROR) /*(fgets(temp_sdbuf, 500, fcd) != NULL)*/)
      {
        // Increase Byte Read Counter
        
        bytesReadfrmFile += strlen(temp_sdbuf);
        strcat(sddata_buff, temp_sdbuf);
      }

      if (bytesReadfrmFile > 0)
      {
        // send to mqtt for sending

        Serial.print(" ByteRead : ");
        Serial.println(bytesReadfrmFile);
        char Temp_TopicBuf[100] = "";
        sprintf(Temp_TopicBuf, "%s/F", Topic_PubLogData);

        bool ret = MQTT_Publish(Temp_TopicBuf, sddata_buff, 0);
        if (ret != ALTEEM_SUCCESS) // if (ret < 0)
        {
          LOG_ERROR("Publish Zigbee LogData Message From SD Card Failed %d\r\n", ret);
          xSemaphoreGive(LogGetMutex);
          fcd.close();
          return;
        }
        else
        {
          LOG_INFO("Published Zigbee LogData Message From SD Card\r\n");
        }

        if (fcd.available()) // if (!feof(fcd))
        {

          file_pointer = fcd.position(); // ftell(fcd);
          if ( file_pointer >=0)
          {           
            SaveFilePointer_IntoSD(LOGDATA, file_pointer);
          
          }
          else {

            FilePointer_LogData += bytesReadfrmFile;
            SaveFilePointer_IntoSD(LOGDATA, FilePointer_LogData);
            
          }
          
          fcd.close();

        }
        else
        {
          LOG_INFO("End Of FIle Reached!!\r\n");

          fcd.close();

          // Set File Pointer to 0
          file_pointer = 0;
          SaveFilePointer_IntoSD(LOGDATA, 0);

          /* All Data Sent So Stop Sending */
          flag_LogReqRx = 0;
        }
      }
      else
      {
        // Serial.println(" ")
        fcd.close();
      }
      xSemaphoreGive(LogGetMutex);
    }
  }
}

/* Read Current File Pointer from SD
  Return Negative Value On Error
         Or File Pointer Value
*/
long ReadFilePointer_FromSD(uint8_t FileType)
{
  if (flag_SDInit == ALTEEM_SUCCESS)
  {
    File fpr;
    if (FileType == PROXYDATA)
    {
      fpr = SD_MMC.open(proxyreadpointerfile); // fpr = fopen(proxyreadpointerfile, "r");
    }
    else if (FileType == SERIALDATA)
    {
      fpr = SD_MMC.open(serialreadpointerfile); // fpr = fopen(serialreadpointerfile, "r");
    }
    else if (FileType == LOGDATA)
    {
      fpr = SD_MMC.open(logreadpointerfile); // fpr = fopen(logreadpointerfile, "r");
    }

    if (!fpr)
    {
      LOG_INFO("Error: could not Open FP File\r\n");
      LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - Error: could not Open FP File FileType:%s\r\n", __FILE__, __FUNCTION__, __LINE__, ((FileType == PROXYDATA) ? ("Proxy") : ((FileType == SERIALDATA) ? ("Serial") : ("Log"))));

      if (FileType == PROXYDATA)
      {
        fpr = SD_MMC.open(proxyreadpointerfile, FILE_WRITE);
        fpr.close();

        SaveFilePointer_IntoSD(PROXYDATA, 0);
      }
      else if (FileType == SERIALDATA)
      {
        fpr = SD_MMC.open(serialreadpointerfile, FILE_WRITE);
        fpr.close();

        SaveFilePointer_IntoSD(SERIALDATA, 0);
      }
      else if (FileType == LOGDATA)
      {
        fpr = SD_MMC.open(logreadpointerfile, FILE_WRITE);
        fpr.close();

        SaveFilePointer_IntoSD(LOGDATA, 0);
      }
      return 0;
    }
    char buf[30] = "";
    unsigned int bytesRead = readLinefromFile(fpr, buf, 30); // fread(buf, 1, 30, fpr);

    if (strlen(buf) == 0) //(bytesRead == 0)
    {
      LOG_INFO("FP File Read Error!!\r\n");
      // LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - FP File Read Error!!\r\n", __FILE__, __FUNCTION__, __LINE__);
       LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d, File Type : %s - FP File Read Error!!\r\n", __FILE__, __FUNCTION__, __LINE__ , ((FileType == PROXYDATA) ? ("Proxy") : ((FileType == SERIALDATA) ? ("Serial") : ("Log"))));
      fpr.close();
    }

    long value = atol(buf);

    fpr.close();

    return value;
  }
  else
    return -1;
}

/* Initialize Logging into File on Memory Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool Init_LOG()
{
  LogGetMutex = xSemaphoreCreateMutex();
  if (LogGetMutex != NULL)
  {
    /* The semaphore was created successfully and
      can be used. */
  }
  else
  {
    LOG_ERROR("LogGetMutex Mutex Init Failed\n\r");
    LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - LogGetMutex Mutex Init Failed\n\r", __FILE__, __FUNCTION__, __LINE__);
  }
    LOG_INFO("Device Started FW V:3 12.05.2025(BUF,CMD,OTA WITH SD,Mqtt service check OFF)\r\n");
  LOG_INTOSD("Device Started FW V:3 12.05.2025(BUF,CMD,OTA WITH SD,Mqtt service check OFF)\r\n");
  return ALTEEM_SUCCESS;
}

/* Clear LOG File from Memory Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool Clear_LogFile()
{
  LOG_INFO("Clearing Log File\r\n");
  File fpr = SD_MMC.open(logfile, FILE_WRITE);
  fpr.close();
  FileSize_LogData = 0;
  SaveFilePointer_IntoSD(LOGDATA, 0);
  LOG_INTOSD("Cleared Log File\r\n");
  return true;
}


bool Clear_ProxyFile()
{
  LOG_INFO("Clearing Proxy File\r\n");
  File fpr = SD_MMC.open(proxydatafile, FILE_WRITE);
  fpr.close();
  FileSize_ProxyData = 0;
  SaveFilePointer_IntoSD(PROXYDATA, 0);
  LOG_INTOSD("Cleared Proxy File\r\n");
  return true;
}

bool Clear_SerialFile()
{
  LOG_INFO("Clearing Serial File\r\n");
  File fpr = SD_MMC.open(serialdatafile, FILE_WRITE);
  fpr.close();
  FileSize_SerialData = 0;
  SaveFilePointer_IntoSD(SERIALDATA, 0);
  LOG_INTOSD("Cleared Serial File\r\n");
  return true;
}

/* Set date and time using NTP
    Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool setTimeByNTP()
{
  struct tm t;

  configTime(5.5 * 3600L, 0, "time.google.com", "time.nist.gov", "utcnist.colorado.edu");

  if (!getLocalTime(&t))
  {
    LOG_ERROR("getLocalTime Error");
    LOG_INTOSD("getLocalTime Error");
    return ALTEEM_ERROR;
  }
  LOG_INFO("getLocalTime : %d:%d:%d %d:%d:%d\r\n", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min, t.tm_sec);
  LOG_INTOSD("getLocalTime : %d:%d:%d %d:%d:%d\r\n", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min, t.tm_sec);
  rtc.adjust(DateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec));
  return ALTEEM_SUCCESS;
}

/* Save Broker Config Data into SD Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool SaveBrokerConfig_IntoSD(char *data)
{
  if (flag_SDInit == ALTEEM_SUCCESS)
  {
    File fps;
    fps = SD_MMC.open(brokerconfigfile, FILE_WRITE);
    if (!fps)
    {
      LOG_ERROR("Cant Open brokerconfigfile for writing\r\n");
      return ALTEEM_ERROR;
    }
    if (fps.println(data))
    {
    }
    else
    {
      LOG_ERROR("File Append failed");
    }
    fps.flush();
    fps.close();
    return ALTEEM_SUCCESS;
  }
  else
  {
    LOG_ERROR("Cant Save brokerconfigfile,SD Card not Initialized\r\n");
    return ALTEEM_ERROR;
  }
}

/* Read Broker Credentials from SD Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool ReadBrokerConfig_FromSD(char *buf)
{
  if (flag_SDInit != ALTEEM_ERROR)
  {
    File fpr;
    fpr = SD_MMC.open(brokerconfigfile);
    if (!fpr)
    {
      LOG_ERROR("Could Not Open brokerconfigfile\r\n");
      fpr = SD_MMC.open(brokerconfigfile, FILE_WRITE);
      fpr.close();
      return ALTEEM_ERROR;
    }

    unsigned int bytesRead = readLinefromFile(fpr, buf, 512); // fread(buf, 1, 30, fpr);

    if (strlen(buf) == 0) //(bytesRead == 0)
    {
      LOG_INFO("brokerconfigfile File Read Error!!\r\n");
      LOG_INTOSD("FileName : %s,Function : %s,LineNo. : %d - brokerconfigfile File Read Error!!\r\n", __FILE__, __FUNCTION__, __LINE__);
      fpr.close();
      return ALTEEM_ERROR;
    }

    fpr.close();

    return ALTEEM_SUCCESS;
  }
  else
    return ALTEEM_ERROR;
}

/* Check if Broker Config Available in File,Parse it
 */
void CheckBrokerConfigfrmFile()
{
  /* Read BrokerConfig from File if availavle or request from AlteemIOT Site */
  char payload[512] = "";
  LOG_INFO("Reading Broker Config : \r\n");
  if (ReadBrokerConfig_FromSD(payload) == ALTEEM_SUCCESS)
  {
    LOG_INFO("Broker Config Available in File : %s\r\n", payload);
    /* Config Available into File */

    /* Check for Valid Responce*/
    strcpy(payload_sd, payload);

    char *sdpoint = strstr(payload_sd, "\"TI\"");
    *sdpoint = 0; 



    if (strstr(payload, "DN"))
    {
      char *p = strstr(payload, ":") + 2;
      char *index = strstr(p, ":") + 1;
      char server_ad_t[100] = "";
      int port_t = 0;
      
      
      /* Check for IP Address or Web Address */
      if (*index == '0') // Web Address
      {
        char *index_ad = strstr(index, ":") + 2;
        strncpy(server_ad_t, index_ad, strstr(index_ad, "\"") - index_ad);
        port_t = atoi(strstr(index_ad, ":") + 1);
        Connect_Mqtt_Using_IP = false;
        LOG_INFO("Broker Addr :%s port :%d\r\n", server_ad_t, port_t);
        strcpy(MQTT_HOST, server_ad_t);
        MQTT_PORT = port_t;
      }
      else if (*index == '1') // IP Address
      {
        char *index_ad = strstr(index, ":") + 2;
        char ip_ad[100] = "";
        strncpy(ip_ad, index_ad, strstr(index_ad, "\"") - index_ad);
        port_t = atoi(strstr(index_ad, ":") + 1);
        Connect_Mqtt_Using_IP = true;
        LOG_INFO("Broker IP :%s port :%d\r\n", ip_ad, port_t);

        strcpy(MQTT_HOST, ip_ad);
        MQTT_PORT = port_t;
      }
    }
    else
    {
      LOG_INFO("Invalid Broker Config Available in File\r\n");
    }
  }
  else
  {
    LOG_INFO("Broker Config Not Available in File\r\n");
  }
}

/* Clear BrokerConfig File from Memory Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool Clear_BrokerConfigFile()
{
  LOG_INFO("Clearing Broker Config File\r\n");
  File fpr = SD_MMC.open(brokerconfigfile, FILE_WRITE);
  fpr.close();
  LOG_INTOSD("Cleared Broker Config File\r\n");
  return true;
}

/* Application Task Function Handling */
void HTTPTaskFunc(void *parameter)
{
  char mqttpayload[100] = "";
  LOG_INFO("HTTPTaskFunc\r\n");
  ClientSecure *Client = new ClientSecure;

  if (Client)
  {
    /* We're downloading from a secure URL, but we don't want to validate the root cert. */
    Client->setInsecure();
    HTTPClient https;
    LOG_INFO("[HTTPS] begin link : %s\n", httpLink);
    if (https.begin(*Client, httpLink))
    {
      /* Collect Header to Find File Size */
      const char *get_headers[] = {"Content-Length", "Content-type"};
      https.collectHeaders(get_headers, 2);

      LOG_INFO("[HTTPS] GET...\n");
      // start connection and send HTTP header
      int httpCode = https.GET();
      // httpCode will be negative on error
      if (httpCode > 0)
      {

            memset(mqttpayload, 0, sizeof(mqttpayload));
            sprintf(mqttpayload, "HTTP GET Request Successful, Code: %d\n", httpCode);
            mqttClient.publish(Topic_PubOtaData, 2, false, mqttpayload,strlen(mqttpayload));
        /* Open File for Writing */
        File fps;
        if (OTAImageType == OTA_ROUTER)
        {
          fps = SD_MMC.open(otaRouterfile, FILE_WRITE);
          if (!fps)
          {
            LOG_INFO("File Open Failed");
          }
        }
        else if ( OTAImageType == OTA_COORDINATOR)
        {
          fps = SD_MMC.open(otaCoordfile, FILE_WRITE);
          if (!fps)
          {
            LOG_INFO("File Open Failed");
          }
        }

        else 
        {

          fps = SD_MMC.open(otaEspfile, FILE_WRITE);

          if(!fps)
          {
            LOG_INFO("FILE OPEN FAILED");
          }
        }

        int contentLength = https.header("Content-Length").toInt();
        String contentType = https.header("Content-type");

        LOG_INFO("contentLength : %i", contentLength);

        // HTTP header has been send and Server response header has been handled
        LOG_INFO("[HTTPS] GET... code: %d\n", httpCode);

        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
        {
            memset(mqttpayload, 0, sizeof(mqttpayload));
            sprintf(mqttpayload, "OTA %s File Downloading Start , Size: %d\n", (( OTAImageType == OTA_COORDINATOR) ? (" Coordinator") : (" Router")) ,contentLength);
            mqttClient.publish(Topic_PubOtaData, 2, false, mqttpayload,strlen(mqttpayload));
          uint8_t buff[512] = {0};
          uint32_t remaining = contentLength, received = 0;

          bool flag_run = true;
          uint16_t timeout_failures = 0;
          // read all data from server
          while (flag_run && (remaining > 0))
          {
            // read up to buffer size
            received = Client->readBytes(buff, ((remaining > sizeof(buff)) ? sizeof(buff) : remaining));

            // write it to file
            fps.write(buff, received);

            if (remaining > 0)
            {
              remaining -= received;
            }

            if (received == 0)
            {
              timeout_failures++;
              if (timeout_failures >= 300)
              {
                flag_run = 0;
              }
              vTaskDelay(100 / portTICK_PERIOD_MS);
            }
            yield();
          }

          Client->stop();
          fps.close();
          if (OTAImageType == OTA_ROUTER)
          {
            fps = SD_MMC.open(otaRouterfile);
          }
          else if( OTAImageType == OTA_COORDINATOR)
          {
            fps = SD_MMC.open(otaCoordfile);
          }

          else 
          {
            fps = SD_MMC.open(otaEspfile);
          }


          LOG_INFO("File Size is ");
          uint32_t sizeoffile = fps.size();
          LOG_INFO("%d\r\n", sizeoffile);
          fps.close();

          if (remaining == 0)
          {
            LOG_INFO("[HTTPS]File Download Successfull\r\n");
            LOG_INTOSD("[HTTPS]File Download Successfull\r\n");
            mqttClient.publish(Topic_PubOtaData, 2, false, "[HTTPS]File Download Successfull",strlen("[HTTPS]File Download Successfull"));

            // if(flag_esp_ota)
            // {
            //   performOTAFromSD(otaEspfile);
            // // }
            // // else
            // // {
              XModem_Tx_Image();
            // }
            
          }
          else
          {
            LOG_INFO("[HTTPS]File Download Failed\r\n");
            LOG_INTOSD("[HTTPS]File Download Failed\r\n");
             mqttClient.publish(Topic_PubOtaData, 2, false, "[HTTPS]File Download Failed",strlen("[HTTPS]File Download Failed"));
          }
        }
      }
      else
      {
        LOG_INFO("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        LOG_INTOSD("[HTTPS] GET... failed\r\n");
         mqttClient.publish(Topic_PubOtaData, 2, false, "[HTTPS] GET... failed",strlen("[HTTPS] GET... failed"));
      }
      https.end();
    }
    else
    {
      LOG_INFO("[HTTPS] Unable to connect\n");
      LOG_INTOSD("[HTTPS] Unable to connect\r\n");
    }
    delete Client;
  }
  else
  {
    LOG_INFO("Unable to create client");
    LOG_INTOSD("Unable to create client\r\n");
  }
  LOG_INFO("[HTTPS]Exiting Task\r\n");

  Mqtt_Reconnect();

  vTaskDelete(NULL);
}


void performOTAFromSD(const char* file ) {
    File updateFile = SD_MMC.open(file);
    if (!updateFile) { 
        Serial.println("Failed to open firmware file!");
        return;
    }

    size_t fileSize = updateFile.size();
    Serial.printf("Firmware Size: %u bytes\n", fileSize);
    
    if (!Update.begin(fileSize)) {
        Serial.println("Not enough space for OTA.");
        return;
    }

    Serial.println("ESP32 OTA START ");
    LOG_INTOSD("ESP32 OTA START\r\n");

    size_t written = Update.writeStream(updateFile);
    if (written == fileSize) {
        Serial.println("OTA Update Successful!");
        LOG_INTOSD("OTA Update Successful!\r\n");

        mqttClient.publish(Topic_PubOtaData, 1, false, "OTA Update Successful! Restarting ESP32...", strlen("OTA Update Successful! Restarting ESP32..."));
        if (Update.end(true)) {
            Serial.println("Restarting ESP32...");
            LOG_INTOSD("Restarting ESP32\r\n");
            ESP.restart();
        }
    } else {
        Serial.println("OTA Update Failed.");
        LOG_INTOSD("OTA UPDATE FAILED");
        mqttClient.publish(Topic_PubOtaData, 1 , false, "OTA Update Failed", strlen("OTA Update Failed"));
    }
    updateFile.close();
}

//================================================================  OTA


 void performOTAUpdate(const char *firmwareUrl)
{
    // Serial.println(" OTA Task Start");

    xTaskCreate(otaDownloadTask, "otaDownloadTask", 4096*3, (void*)firmwareUrl, 4, NULL);
}
bool fireware(const char* fileUrl) {
    // esp_task_wdt_delete(NULL);
    // HTTPClient http;
    // http.setTimeout(15000); 
    // http.begin(fileUrl);

    Serial.print("OTA Link : " );
    Serial.println(fileUrl);

    char downloadlink[100];
    memset(downloadlink, 0, sizeof(downloadlink));
    strcpy(downloadlink, fileUrl);
    Serial.print("Download Link : ");
    Serial.println(downloadlink);

    ClientSecure *Client = new ClientSecure;

   if ( Client)

   {
   
    Client->setInsecure();
    HTTPClient https;
    LOG_INFO("[HTTPS] begin link : %s\n", downloadlink);
    https.begin(*Client, downloadlink);
    const char *get_headers[] = {"Content-Length", "Content-type"};
    https.collectHeaders(get_headers, 2);

   

    int httpCode = https.GET();

    if (httpCode > 0) {
        Serial.printf("HTTP GET request successful, code: %d\n", httpCode);

        

        if (httpCode == HTTP_CODE_OK) {
            long fileSize = https.getSize();
            Serial.printf("File size: %d bytes\n", fileSize);

            char mqttpayload[100];
            sprintf(mqttpayload, "HTTP GET Request Successful, Code: %d\n", httpCode);
         // MQTT_Publish(Topic_PubOtaData, mqttpayload, 1);
           mqttClient.publish(Topic_PubOtaData, 2, false, mqttpayload,strlen(mqttpayload));
 
            // String ota_ = "OTA FILE Size " + String(fileSize) + " Start Downloading"; 
            // mqttClient.publish(Topic_PubOtaData, 1, false, ota_.c_str(), strlen(ota_.c_str()));

            // char mqttpayload[60];
            sprintf(mqttpayload, "OTA File Size : %ld \nFile Downloading  Start ",fileSize );
            // MQTT_Publish(Topic_PubOtaData, mqttpayload, 1);

            mqttClient.publish(Topic_PubOtaData, 2, false, mqttpayload,strlen(mqttpayload));
            

            if (!SD_MMC.begin("/sdcard", true, false, 1000)) {
                Serial.println("Card Mount Failed\r\n");
                return false;
            }


            File file = SD_MMC.open(otaEspfile, FILE_WRITE);
            if (!file) {
                Serial.println("Failed to open file for writing");
                return false;
            }

            WiFiClient* stream = https.getStreamPtr();
            uint8_t buffer[1024];
            long bytesRead = 0;
            unsigned long lastPrintTime = millis();

            while (https.connected() && (fileSize > 0 )) {
                size_t size = stream->available();
                if (size) {
                    int bytes = stream->readBytes(buffer, ((size > sizeof(buffer)) ? sizeof(buffer) : size));
                    file.write(buffer, bytes);

                    if (fileSize > 0) {
                        fileSize -= bytes;
                    }

                    bytesRead += bytes;
                    unsigned long currentTime = millis();
                    if (currentTime - lastPrintTime > 1000) { // Print every second
                        Serial.printf("Downloaded %d bytes\n", bytesRead);
                        // checkMemory(); // Check free heap memory
                        lastPrintTime = currentTime;
                    }

                    vTaskDelay(1 / portTICK_PERIOD_MS);
                }
            }

            Serial.println("Download complete");
            mqttClient.publish(Topic_PubOtaData, 1, false,"OTA File Download Complete", strlen("OTA File Download Complete"));
            file.close();
            return true;
        } else {
            Serial.printf("HTTP response not OK, code: %d\n", httpCode);

            char mqttpayload[100];
            sprintf(mqttpayload, "HTTP Response Not OK, Code: %d\n", httpCode);
            mqttClient.publish(Topic_PubOtaData, 2, false, mqttpayload,strlen(mqttpayload));
        }
    } else {
        Serial.printf("HTTP GET request failed,  Error : ");

        Serial.println(httpCode);

        //"OTA Update Failed", strlen("OTA Update Failed")

          char mqttPayload[100]; // Adjust the size as needed
          sprintf(mqttPayload, " HTTP GET request failed,   Error : %d ", httpCode);
          mqttClient.publish(Topic_PubOtaData, 2, false, mqttPayload,strlen(mqttPayload));
          // MQTT_Publish(Topic_PubOtaData, mqttPayload, 1);

    }
  }
  
  else
  {
    LOG_INFO("Unable to create client");
    LOG_INTOSD("Unable to create client\r\n");
  }


    // http.end();
    return false;
}


void otaDownloadTask(void* parameter) {
    const char* fileUrl = (const char*)parameter;
    if (fireware(fileUrl)) {
        Serial.println("FIRMWARE DOWNLOAD");
        performOTAFromSD(otaEspfile);
    } else {
        Serial.println("Firmware download failed. OTA will not be performed.");
    }
    vTaskDelete(NULL); // Delete the task when done
}


/* For OTA Update
   Download Image from HTTPS and Save into Memory Card
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool HTTP_DwldFileIntoSDCard()
{
  xTaskCreate(HTTPTaskFunc, "HTTPTaskFunc", 8192, NULL, 3, NULL);
  LOG_INFO("Starting HTTP Task\r\n");
  return true;
}

/* Application Task Function for Xmodem */
void XMODEMTaskFunc(void *parameter)
{
  flag_XModemTXBusy = true;

  uint32_t sizeoffile;
  File fps;
  if (OTAImageType == OTA_ROUTER)
  {
    fps = SD_MMC.open(otaRouterfile); // fps = fopen(serialdatafile, "a+");
    if (!fps)
    {
      LOG_INFO("otaRouterfile File Open Failed");
    }
    sizeoffile = fps.size();
  }
  else
  {
    fps = SD_MMC.open(otaCoordfile);
    if (!fps)
    {
      LOG_INFO("otaCoordfile File Open Failed");
    }
    sizeoffile = fps.size();
  }

  char txbuf[100];
  if (OTAImageType == OTA_ROUTER)
  {
    sprintf(txbuf, "{\"OTAR\":0,\"SZ\":%d}\n", sizeoffile);
  }
  else
  {
    sprintf(txbuf, "{\"OTAC\":0,\"SZ\":%d}\n", sizeoffile);
  }
  Serial2.printf(txbuf);

  vTaskDelay(1000 / portTICK_PERIOD_MS);

  /* Transmit File From SD Card via XModem */
  int status = 0;
  if (OTAImageType == OTA_ROUTER)
  {
    status = xmodemTransmitFilefrmSD(otaRouterfile, sizeoffile);
  }
  else
  {
    status = xmodemTransmitFilefrmSD(otaCoordfile, sizeoffile);
  }

  if (status < 0)
  {
    LOG_ERROR("XModem Write Failed %d\r\n", status);
    LOG_INTOSD("XModem Write Failed \r\n");
  }
  else
  {
    LOG_INFO("XModem Write Success\r\n");
    LOG_INTOSD("XModem Write Success \r\n");
  }

  flag_XModemTXBusy = false;

  vTaskDelete(NULL);
}

/* Transmit Image from Using XModem
   Starts Thread
*/
void XModem_Tx_Image()
{
  xTaskCreate(XMODEMTaskFunc, "XMODEMTaskFunc", 4096, NULL, 3, NULL);
  LOG_INFO("Starting XModem Task\r\n");
}
