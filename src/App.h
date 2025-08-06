#include<stdbool.h>
#include<stdint.h>
#include "FS.h"

#define MQTT_BUF_SIZE 2000
#define MQTT_BUF_SKIP_SIZE 500

#define BROKER_CONFIG_WAITIME 100

#define MQTT_RECONNECT_TIME (120*2)

#define ALTEEM_ERROR 1
#define ALTEEM_SUCCESS  0

#define EXT_RTC 1

#define MQTTTASK_BIT  (1UL << 0UL) // zero shift for bit0

/* OnBoard LED Pin */
//#define LED 2
#define WIFI_LED    32
#define SRV_LED     27
#define TIM_LED     33
#define MQTT_BUSY   34

enum
{
  BROKER_CONFIG_NOT_RX,
  BROKER_CONFIG_RX,
  BROKER_CONFIG_INVALID
};

/* BROKER DETAILS for INITIAL CONFIG */
extern char MQTT_HOST [100];// = "device.alteemiot.com";
extern int MQTT_PORT;//  = 88;
extern bool Connect_Mqtt_Using_IP;
/* Buffer for Saving Time Value */
static char timestamp[25];

/* If Broker Configuration Received */
extern char flag_BrokerConfigRx;

extern bool flag_EnCounting;//, flag_TimeSyncDone;

extern bool flag_LogReqRx;

/* Counter if Broker Config Not Rxed */
extern uint16_t counter_BrokerResp;

/* Counter for Wifi Reconnect if MQTT Disconnected  */
extern uint16_t counter_WifiReconnect;

/* If Connected with Broker Received from Configuration */
extern bool IsMQTTConBrokerConfig;

/* If MQTT is Busy Sending Msg */
extern bool IsMQTTBusy;

/* If Wifi Connection Process is Busy */
extern bool IsWifiConnectBusy;

/* If OTA Update Req Rxed */
extern bool flag_OtaUpdateStart;
extern bool mqtt_service_flag;

/* If UNREG Config Rxed */
extern bool flag_UNAUTH;
extern bool mqtt_current_status;
extern char mqtt_reason[100];

/* Topics to Be Subscribed */
extern char Topic_SubBrokerConfig[100];// = "/AT/CN/WF/";
extern char Topic_SubZigbeeConfig[100];// = "/AT/CN/ZB/";
extern char Topic_SubLogData[100];// = "/AT/LG/";
extern char Topic_SubOtaData[100];// = "/AT/OT/";
extern char Topic_SubService[100];// = "/AT/SV/PG/";

/* Topics For Publish */
extern char Topic_PubLogData[100];// = "/AR/LG/";
extern char Topic_PubBrokerConfig[100];// = "/AR/CN/WF/";
extern char Topic_PubZigbeeConfig[100];// = "/AR/CN/ZB/";
extern char Topic_PubProxyData[100];// = "/AR/DT/ZB/";
extern char Topic_PubSerialData[100];// = "/AR/DS/ZB/";
extern char Topic_PubLiveSerialData[100];// = "/AR/LS/ZB/";
extern char Topic_PubLiveProxyData[100];// = "/AR/LT/ZB/";
extern char Topic_PubMonitorData[100];// = "/AR/MN/ZB/";
extern char Topic_PubErrorData[100];// = "/AR/ER/ZB/";
extern char Topic_PubService[100];// = "/AR/SV/PG/";


extern char Topic_PubZigbeeUartConfig[100];// = "/AR/CN/ZB/UT";   // Added By Jay.
/* Wifi File name on SD Card */
const char wififile[] = "/WIFI.txt";

/* OTA File For Router */
const char otaRouterfile[] = "/OTAR.bin";

/* OTA File For Coordinator */
const char otaCoordfile[] = "/OTAC.bin";



///* File Names for Saving Data into Memory */
const char proxydatafile[] = "/PROXY.txt";
const char serialdatafile[] = "/SERIAL.txt";
/* File for Log Data Saving */
const char logfile[] = "/LOG.txt";

const char proxyreadpointerfile[] = "/FPPRO.txt";
const char serialreadpointerfile[] = "/FPSER.txt";
const char logreadpointerfile[] = "/FPLOG.txt";

const char brokerconfigfile[] = "/BROKERCONFIG.txt";

extern uint8_t NoOfCreds;

/* Struct for Wifi Creds */
typedef struct
{
  char ssid[32];
  char password[64];
} WifiCred_t;

extern WifiCred_t WifiCred[30];  //Changes For No. of Wifi ssid And Password. 

/* All Application Related Flags */
extern bool flag_SDInit;               //If SD Card init succeed

enum
{
  PROXYDATA,
  SERIALDATA,
  LOGDATA
};

/* Variables for File pointer Values */
extern long FilePointer_ProxyData;
extern long FilePointer_SerialData;
extern long FilePointer_LogData;

/* Variables for File Sze Values */
extern long FileSize_ProxyData;
extern long FileSize_SerialData;
extern long FileSize_LogData;

extern char buffer[1024];
extern char log_buf[1300];
extern char buffer_[1024];
extern char buffer_er[1024];

// Servers for Syncing Time
static const char * SNTP_internalServersList[] = {
  "time.google.com",
  "time.nist.gov",
  "utcnist.colorado.edu",
  "utcnist2.colorado.edu",
  "nist-time-server.eoni.com",
};

//  declare a event grounp handler variable
extern EventGroupHandle_t  xEventGroup;

extern SemaphoreHandle_t MQTTBUSYSem;


enum
{
    OTA_ROUTER,
    OTA_COORDINATOR
};

/* OTA Image type if for Router or CoOrdinator */
extern char OTAImageType;

/* Link of File to Download From
 * Used in OTA Updates
*/
extern char httpLink[500];

/* Application Task Function Handling */
void UART_HandleData();

/* Starts Thread Handling Application */
void StartApp();

/* Print Debig Data on Uart
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool LOG_INFO(const char *pcFormat, ...);

/* Print Debig Data on Uart
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool LOG_ERROR(const char *pcFormat, ...);

/* Read UDID of Device
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool Read_UDID();


/* Insert Into LOG File of Memory Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool LOG_INTOSD(const char *pcFormat, ...);


/* Callback Function for Broker Config From device.alteemiot.com */
void BrokerConfigRxCallback(char* payload);

/* Read TimeValue from RTC
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool RTC_ReadTime();

// Set time via NTP, as required for x.509 validation
void setClock();

/* Init RTC With Default Time
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool Init_RTC();


/* Sync Time of RTC With NTP
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool RTC_Sync();

void Mqtt_Reconnect();

/* Initialize SD Card
   Initializes FatsFs Driver Also
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool Init_SDCard();

/* Read Wifi Credentials from SD Card
   Returns Nos of Creds on Success
           -1 on Error
*/
int ReadWifiCred_FromSD();


/* Read Line from SD Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool readLinefromFile(File filem, char *readdata, int MaxLen);

/* Save Data into SD Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool SaveData_IntoSD(char *data);

/* Read Current File Pointer from SD
   Return ALTEEM_SUCCESS
          or ALTEEM_ERROR on Error
*/
bool UpdateFileSize(uint8_t FileType);

/* Save File Pointer on SD Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool SaveFilePointer_IntoSD(uint8_t FileType, long data);

/* Function for Handling Data from Memory Card */
void SD_HandleData();

/* Read Current File Pointer from SD
  Return Negative Value On Error
         Or File Pointer Value
*/
long ReadFilePointer_FromSD(uint8_t FileType);

/* Initialize Logging into File on Memory Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool Init_LOG();

/* Clear LOG File from Memory Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool Clear_LogFile();
bool Clear_SerialFile();
bool Clear_ProxyFile();

/* Callback Function for Zigbee Config */
void ZigbeeConfigRxCallback(char* payload);

/* Callback Function for Log Request */
void LogReqRxCallback(char* payload);

/* Callback Function for OTA Request */
void OtaReqRxCallback(char* payload);

/* Start ESP OTA Update Process */
void Start_ESPOTA();


// Set date and time using NTP
bool setTimeByNTP();


/* Save Broker Config Data into SD Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool SaveBrokerConfig_IntoSD(char *data);

/* Read Broker Credentials from SD Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool ReadBrokerConfig_FromSD(char *buf);

/* Check if Broker Config Available in File,Parse it
*/
void CheckBrokerConfigfrmFile();


/* Clear BrokerConfig File from Memory Card
   Return ALTEEM_ERROR On Error
          ALTEEM_SUCCESS On Success
*/
bool Clear_BrokerConfigFile();

/* For OTA Update
   Download Image from HTTPS and Save into Memory Card
   Return ALTEEM_SUCCESS on Success
          ALTEEM_EERROR on Error
*/
bool HTTP_DwldFileIntoSDCard();

/* Transmit Image from Using XModem
   Starts Thread
*/
void XModem_Tx_Image();
