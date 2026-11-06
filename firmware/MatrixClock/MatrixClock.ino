/*********************************************************************************************************
  HACK LABS MatrixClock                                                                                 
  ********************************************************************************************************
  Author: HACK Lab                                                                                     
  YouTube ID: HACK Lab, welcome to subscribe: https://www.youtube.com/channel/UCxFY1FcIYK9d7riTvIh6eiA                                                          
  Version 2.4
  HACK_CLOCK is a free download and may be used, modified, evaluated and
  distributed without charge provided the user adheres to version three of the GNU
  General Public License (GPL) and does not remove the copyright notice or this
  text.  The GPL V3 text is available on the gnu.org web site
  Author: HACK Lab                                                                                        
  Bilibili ID: HACK Lab  welcome to subscribe: https://space.bilibili.com/395145107                                                                             
  WeChat Official Account: HACK Lab, the only release point for open-source resources, regularly sharing open-source hardware and valuable technical articles, welcome to follow.                        
  Version 2.4                                                                                           
  HACK_CLOCK is a fully open-source hardware project, free to download and use, subject to the GPL V3 open-source license, the license text is available on the gnu.org website.    
*********************************************************************************************************/

#include <SPI.h>
#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <time.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include "WebPage.h"

#define SDA        5      // Pin sda (I2C)
#define SCL        4      // Pin scl (I2C)
#define CS         15     // Pin cs  (SPI)
#define anzMAX     4        // number of dot matrix modules
#define WIFI_CHECK_INTERVAL 30  // seconds between WiFi status checks
#define FIRMWARE_VERSION "v5.13"   // shown on the settings page
//#define REVERSE_HORIZONTAL
//#define REVERSE_VERTICAL
//***************************Web Settings Page**********************************************************************
// configurable settings, stored in EEPROM, editable via http://<clock-ip>/
struct ClockSettings {
    uint32_t magic;          // 0xA5A5A5A5 if settings are valid
    char ssid[32];
    char pass[64];
    char ntpServer[64];
    int8_t tzOffset;         // hours, e.g. 8 = UTC+8
    uint8_t brightness;      // 0-15
    uint8_t orient;          // 0 = auto (pin 16), 1 = normal, 2 = flipped
    uint8_t scrollDate;      // 0 = off, 1 = on
    uint8_t scrollDir;       // 0 = Up-Down, 1 = Down-Up
    uint8_t resync;          // 0 = off, 1 = on
    uint8_t resyncHour;      // 0-23
    uint8_t resyncMin;       // 0-59
    uint8_t dstRule;         // 0 = off, 1 = Israel, 2 = EU, 3 = US
    uint8_t colonAnim;       // colon animation: 0=solid, 1=blink 1Hz, 2=blink slow, 3=double flash, 4=alternate dots
    uint8_t marquee;         // 0 = off, 1 = marquee border sweep
    uint8_t pulse;           // 0 = off, 1 = brightness pulse
    uint16_t ntpInterval;    // periodic NTP sync interval in minutes, 0 = off
    uint8_t weather;         // 0 = off, 1 = on
    char weatherCity[40];    // city name for weather lookup
    uint16_t weatherInterval; // weather update interval in minutes
    int32_t weatherLat;      // cached latitude  * 1e6 (geocoded from city)
    int32_t weatherLon;      // cached longitude * 1e6
    uint8_t weatherSpeed;    // weather message scroll speed: 1=slow, 2=normal, 3=fast, 4=very fast
    uint16_t scrollDateInt;  // minutes between date scrolls (default 1 = every minute)
    uint8_t rainWarn;        // 0 = off, 1 = show RAIN WARNING when precipitation >= threshold
    uint8_t heatWarn;        // 0 = off, 1 = show HEAT WARNING when temperature >= threshold
    uint8_t rainThresh;      // legacy whole-mm rain threshold (kept for older firmware)
    uint8_t heatThresh;      // legacy whole-°C heat threshold (kept for older firmware)
    uint8_t digitAnim;       // digit change animation: 0=off, 1=roll, 2=flip, 3=wipe, 4=fade, 5=drop
    uint8_t weatherCityShow; // 0 = hide city name in weather message, 1 = show it
    uint8_t warnAnim;        // 0 = off, 1 = show a short rain/heat animation before the warning text
    uint8_t shabbat;         // 0 = off, 1 = announce Shabbat candle lighting / end times
    uint8_t shabbatLead;     // minutes before candle lighting to start announcing (10-240, default 60)
    char webPass[32];        // optional HTTP Basic auth password for the web UI (empty = no auth)
    uint8_t shabbatFreq;     // repeat Shabbat message every N minutes (5-60, default 10)
    uint8_t shabbatAnim;     // 0 = text only, 1 = candle / Star of David animation
    uint8_t holidayGreetings; // 0 = off, 1 = show New Year / Christmas / Hebrew holiday greetings
    uint16_t rainThresh10;   // rain warning threshold in 0.1 mm (12 = 1.2 mm)
    uint16_t heatThresh10;   // heat warning threshold in 0.1 °C (355 = 35.5 °C)
    uint16_t threshFormat;   // 0xA55A when the tenths fields above are valid
    uint8_t weatherSource;   // 0 = Open-Meteo, 1 = IMS + Open-Meteo, 2 = IMS only
    uint16_t imsStation;     // IMS station ID, 0 = nearest to Weather City
    uint16_t imsFormat;      // 0xBEEF when the IMS fields above are valid
};
ClockSettings settings = { 0xA5A5A5A5, "", "", "il.pool.ntp.org", 2, 3, 0, 1, 0, 1, 0, 20, 0, 0, 0, 0, 0, 1, "", 30, 0, 0, 2, 1, 1, 1, 2, 31, 1, 1, 1, 1, 60, "", 10, 1, 1, 20, 310, 0xA55A, 0, 0, 0xBEEF };
const ClockSettings settings_defaults = { 0xA5A5A5A5, "", "", "il.pool.ntp.org", 2, 3, 0, 1, 0, 1, 0, 20, 0, 0, 0, 0, 0, 1, "", 30, 0, 0, 2, 1, 1, 1, 2, 31, 1, 1, 1, 1, 60, "", 10, 1, 1, 20, 310, 0xA55A, 0, 0, 0xBEEF };
bool rtcInitialized = false;
bool apActive = false;              // AP fallback mode active (settings page accessible)
bool apShutdownPending = false;     // stopAP ran; softAP shutdown deferred to a quieter moment
unsigned long apShutdownSince = 0;  // millis() when the softAP shutdown was scheduled
bool apUpdateMode = false;          // user requested the setup AP for a firmware update
unsigned long apUpdatePending = 0;  // millis() when the AP switch should happen
unsigned long apUpdateSince = 0;    // millis() when AP update mode started

unsigned short maxPosX = anzMAX * 8 - 1;            
unsigned short LEDarr[anzMAX][8];                   
unsigned short helpArrMAX[anzMAX * 8];              
unsigned short helpArrPos[anzMAX * 8];              
int z_PosX = 0;                            
int d_PosX = 0;                            
bool showMsg = false;                                // a message is scrolling across the display
char msg[120] = "";
uint8_t msgStep = 2;                                 // current message scroll speed (px per 50ms tick)
#define MSG_QUEUE_SIZE 3
char msgQueue[MSG_QUEUE_SIZE][120];
uint8_t msgQueueStep[MSG_QUEUE_SIZE];
uint8_t msgQueueCount = 0;
unsigned long apMsgUntil = 0;                        // AP-mode message keeps repeating until this time
bool shabbatValid = false;
uint16_t shabbatCandleMin = 0;
uint16_t shabbatHavdalahMin = 0;

#define HOLIDAY_MAX 24
struct HolidayEntry {
    uint8_t day;
    uint8_t type;
};
HolidayEntry holidayList[HOLIDAY_MAX];
uint8_t holidayCount = 0;
uint16_t holidayFetchMonth = 0;
bool holidayFetchOk = false;
unsigned long holidayLastFetch = 0;
uint32_t holidayGreetedKey = 0;

enum {
    ALERT_ANIM_NONE = 0,
    ALERT_ANIM_RAIN,
    ALERT_ANIM_HEAT,
    ALERT_ANIM_CANDLES,
    ALERT_ANIM_SHABBAT_END,
    ALERT_ANIM_FIREWORKS,
    ALERT_ANIM_CHRISTMAS,
    ALERT_ANIM_SHOFAR,
    ALERT_ANIM_MENORAH,
    ALERT_ANIM_TREE,
    ALERT_ANIM_MASK,
    ALERT_ANIM_MATZAH,
    ALERT_ANIM_TORAH,
    ALERT_ANIM_SUKKAH,
    ALERT_ANIM_FLAG,
    ALERT_ANIM_MEMORIAL,
    ALERT_ANIM_BONFIRE
};
enum {
    HOLIDAY_NONE = 0,
    HOLIDAY_NEW_YEAR,
    HOLIDAY_CHRISTMAS,
    HOLIDAY_ROSH_HASHANA,
    HOLIDAY_YOM_KIPPUR,
    HOLIDAY_SUKKOT,
    HOLIDAY_SIMCHAT_TORAH,
    HOLIDAY_HANUKKAH,
    HOLIDAY_TU_BISHVAT,
    HOLIDAY_PURIM,
    HOLIDAY_PESACH,
    HOLIDAY_SHAVUOT,
    HOLIDAY_TISHA_BAV,
    HOLIDAY_LAG_BAOMER,
    HOLIDAY_YOM_HASHOAH,
    HOLIDAY_YOM_HAZIKARON,
    HOLIDAY_YOM_HAATZMAUT,
    HOLIDAY_YERUSHALAYIM,
    HOLIDAY_SIGD,
    HOLIDAY_HERZL,
    HOLIDAY_JABOTINSKY,
    HOLIDAY_BEN_GURION,
    HOLIDAY_RABIN,
    HOLIDAY_SWORDS_IRON,
    HOLIDAY_ALIYAH
};
bool alertAnimActive = false;
uint8_t alertAnimType = ALERT_ANIM_NONE;
unsigned long alertAnimStartMs = 0;
uint8_t alertAnimSpeed = 2;
char alertAnimMsg[120] = "";
const unsigned long ALERT_ANIM_MS = 2500UL;   // 2.5 s at the 50 ms frame rate
bool weatherAlertActive = false;              // rain/heat warning owns the display
bool weatherAlertMsgActive = false;           // the warning text itself is scrolling
bool weatherDataValid = false;                // last weather fetch succeeded
float weatherTempC = 0;
float weatherPrecipRate = 0;                  // current rain rate, mm/h
float weatherRainPeak = 0;                    // peak rain rate in the next 2 h, mm/h
int weatherHumidity = 0;
int weatherCode = 0;
unsigned long weatherUpdatedMs = 0;
char weatherDataCity[sizeof(settings.weatherCity)] = "";
bool imsRainValid = false;
float imsRainRate = 0;                       // local station rain rate, mm/h
float imsTempC = 0;
int imsHumidity = 0;
bool imsTempValid = false;
bool imsHumValid = false;
unsigned long imsUpdatedMs = 0;
char imsObsTime[24] = "";
uint16_t imsStationId = 0;
char imsStationName[28] = "";
float imsStationDistanceKm = 0;
bool f_tckr1s = false;
bool f_tckr50ms = false;
bool kk;
unsigned long epoch = 0;
unsigned int localPort = 2390;                      // local port to listen for UDP packets
const int NTP_PACKET_SIZE = 48;                     // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE];                 // buffer to hold incoming and outgoing packets
IPAddress timeServerIP;                            

const unsigned char DS3231_ADDRESS = 0x68;
const unsigned char secondREG = 0x00;
const unsigned char minuteREG = 0x01;
const unsigned char hourREG = 0x02;
const unsigned char WTREG = 0x03;                   //weekday
const unsigned char dateREG = 0x04;
const unsigned char monthREG = 0x05;
const unsigned char yearREG = 0x06;
const unsigned char alarm_min1secREG = 0x07;
const unsigned char alarm_min1minREG = 0x08;
const unsigned char alarm_min1hrREG = 0x09;
const unsigned char alarm_min1dateREG = 0x0A;
const unsigned char alarm_min2minREG = 0x0B;
const unsigned char alarm_min2hrREG = 0x0C;
const unsigned char alarm_min2dateREG = 0x0D;
const unsigned char controlREG = 0x0E;
const unsigned char statusREG = 0x0F;
const unsigned char ageoffsetREG = 0x10;
const unsigned char tempMSBREG = 0x11;
const unsigned char tempLSBREG = 0x12;
const unsigned char _24_hour_format = 0;
const unsigned char _12_hour_format = 1;
const unsigned char AM = 0;
const unsigned char PM = 1;

struct DateTime {
    unsigned short sek1, sek2, sek12, min1, min2, min12, std1, std2, std12;
    unsigned short tag1, tag2, tag12, mon1, mon2, mon12, jahr1, jahr2, jahr12, WT;// Tage=day, Monate=month, Jahre=year (German)
} MEZ;


// The object for the Ticker
Ticker tckr;
// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;


//months
char M_arr[12][5] = { { ' ', 'J', 'A', 'N', ' ' }, { ' ', 'F', 'E', 'B', ' ' },
        { ' ', 'M', 'A', 'R', ' ' }, { ' ', 'A', 'P', 'R', ' ' }, { ' ', 'M', 'A',
                'Y', ' ' }, { ' ', 'J', 'U', 'N', ' ' }, { ' ', 'J', 'U', 'L', ' ' }, {
                ' ', 'A', 'U', 'G', ' ' }, { ' ', 'S', 'E', 'P', ' ' }, { ' ', 'O', 'C',
                'T', ' ' }, { ' ', 'N', 'O', 'V', ' ' }, { ' ', 'D', 'E', 'C', ' ' } };
//days
char WT_arr[7][4] = { { 'S', 'U', 'N', ' ' }, { 'M', 'O', 'N', ' ' }, { 'T', 'U', 'E', ' ' }, {
        'W', 'E', 'D', ' ' }, { 'T', 'H', 'U', ' ' }, { 'F', 'R', 'I', ' ' }, { 'S', 'A', 'T', ' ' } };

// Font 5x8 in an 8x8 matrix, 0,0 is top right
unsigned short const font1[96][9] = { { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00 },   // 0x20, Space (narrower 4px gap between words)
        { 0x07, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00 },   // 0x21, !
        { 0x07, 0x09, 0x09, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x22, "
        { 0x07, 0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a, 0x00 },   // 0x23, #
        { 0x07, 0x04, 0x0f, 0x14, 0x0e, 0x05, 0x1e, 0x04, 0x00 },   // 0x24, $
        { 0x07, 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13, 0x00 },   // 0x25, %
        { 0x07, 0x04, 0x0a, 0x0a, 0x0a, 0x15, 0x12, 0x0d, 0x00 },   // 0x26, &
        { 0x07, 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x27, '
        { 0x07, 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, 0x00 },   // 0x28, (
        { 0x07, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00 },   // 0x29, )
        { 0x07, 0x04, 0x15, 0x0e, 0x1f, 0x0e, 0x15, 0x04, 0x00 },   // 0x2a, *
        { 0x07, 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00, 0x00 },   // 0x2b, +
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02 },   // 0x2c, ,
        { 0x07, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00 },   // 0x2d, -
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x00 },   // 0x2e, .
        { 0x07, 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00 },   // 0x2f, /
        { 0x07, 0x0F, 0x09, 0x09, 0x09, 0x09, 0x09, 0x0F, 0x00 },   // 0x30, 0
        { 0x07, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00 },   // 0x31, 1
        { 0x07, 0x0F, 0x01, 0x01, 0x0F, 0x08, 0x08, 0x0F, 0x00 },   // 0x32, 2
        { 0x07, 0x0F, 0x01, 0x01, 0x0F, 0x01, 0x01, 0x0F, 0x00 },   // 0x33, 3
        { 0x07, 0x09, 0x09, 0x09, 0x0F, 0x01, 0x01, 0x01, 0x00 },   // 0x34, 4
        { 0x07, 0x0F, 0x08, 0x08, 0x0F, 0x01, 0x01, 0x0F, 0x00 },   // 0x35, 5
        { 0x07, 0x0F, 0x08, 0x08, 0x0F, 0x09, 0x09, 0x0F, 0x00 },   // 0x36, 6
        { 0x07, 0x0F, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00 },   // 0x37, 7
        { 0x07, 0x0F, 0x09, 0x09, 0x0F, 0x09, 0x09, 0x0F, 0x00 },   // 0x38, 8
        { 0x07, 0x0F, 0x09, 0x09, 0x0F, 0x01, 0x01, 0x0F, 0x00 },   // 0x39, 9
        { 0x04, 0x00, 0x01, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00 },   // 0x3a, :
        { 0x07, 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08, 0x00 },   // 0x3b, ;
        { 0x07, 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00 },   // 0x3c, <
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x3d, =
        { 0x07, 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00 },   // 0x3e, >
        { 0x07, 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00 },   // 0x3f, ?
        { 0x07, 0x0e, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0f, 0x00 },   // 0x40, @
        { 0x07, 0x04, 0x0a, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x00 },   // 0x41, A
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e, 0x00 },   // 0x42, B
        { 0x07, 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e, 0x00 },   // 0x43, C
        { 0x07, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E, 0x00 },   // 0x44, D
        { 0x07, 0x1f, 0x10, 0x10, 0x1c, 0x10, 0x10, 0x1f, 0x00 },   // 0x45, E
        { 0x07, 0x1f, 0x10, 0x10, 0x1f, 0x10, 0x10, 0x10, 0x00 },   // 0x46, F
        { 0x07, 0x0e, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0f, 0x00 },   // 0x37, G
        { 0x07, 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, 0x00 },   // 0x48, H
        { 0x07, 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e, 0x00 },   // 0x49, I
        { 0x07, 0x1f, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c, 0x00 },   // 0x4a, J
        { 0x07, 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00 },   // 0x4b, K
        { 0x07, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f, 0x00 },   // 0x4c, L
        { 0x07, 0x11, 0x1b, 0x15, 0x11, 0x11, 0x11, 0x11, 0x00 },   // 0x4d, M
        { 0x07, 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00 },   // 0x4e, N
        { 0x07, 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x4f, O
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10, 0x00 },   // 0x50, P
        { 0x07, 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d, 0x00 },   // 0x51, Q
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11, 0x00 },   // 0x52, R
        { 0x07, 0x0e, 0x11, 0x10, 0x0e, 0x01, 0x11, 0x0e, 0x00 },   // 0x53, S
        { 0x07, 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 },   // 0x54, T
        { 0x07, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x55, U
        { 0x07, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04, 0x00 },   // 0x56, V
        { 0x07, 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11, 0x00 },   // 0x57, W
        { 0x07, 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11, 0x00 },   // 0x58, X
        { 0x07, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04, 0x00 },   // 0x59, Y
        { 0x07, 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f, 0x00 },   // 0x5a, Z
        { 0x07, 0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e, 0x00 },   // 0x5b, [
        { 0x07, 0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01, 0x00 },   // 0x5c, '\'
        { 0x07, 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e, 0x00 },   // 0x5d, ]
        { 0x07, 0x04, 0x0a, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x5e, ^
        { 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00 },   // 0x5f, _
        { 0x07, 0x04, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x60, `
        { 0x07, 0x00, 0x0e, 0x01, 0x0d, 0x13, 0x13, 0x0d, 0x00 },   // 0x61, a
        { 0x07, 0x10, 0x10, 0x10, 0x1c, 0x12, 0x12, 0x1c, 0x00 },   // 0x62, b
        { 0x07, 0x00, 0x00, 0x0E, 0x10, 0x10, 0x10, 0x0E, 0x00 },   // 0x63, c
        { 0x07, 0x01, 0x01, 0x01, 0x07, 0x09, 0x09, 0x07, 0x00 },   // 0x64, d
        { 0x07, 0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0f, 0x00 },   // 0x65, e
        { 0x07, 0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08, 0x00 },   // 0x66, f
        { 0x07, 0x00, 0x0e, 0x11, 0x13, 0x0d, 0x01, 0x01, 0x0e },   // 0x67, g
        { 0x07, 0x10, 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x00 },   // 0x68, h
        { 0x05, 0x00, 0x02, 0x00, 0x06, 0x02, 0x02, 0x07, 0x00 },   // 0x69, i
        { 0x07, 0x00, 0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0c },   // 0x6a, j
        { 0x07, 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00 },   // 0x6b, k
        { 0x05, 0x06, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00 },   // 0x6c, l
        { 0x07, 0x00, 0x00, 0x0a, 0x15, 0x15, 0x11, 0x11, 0x00 },   // 0x6d, m
        { 0x07, 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00 },   // 0x6e, n
        { 0x07, 0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x6f, o
        { 0x07, 0x00, 0x00, 0x1c, 0x12, 0x12, 0x1c, 0x10, 0x10 },   // 0x70, p
        { 0x07, 0x00, 0x00, 0x07, 0x09, 0x09, 0x07, 0x01, 0x01 },   // 0x71, q
        { 0x07, 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00 },   // 0x72, r
        { 0x07, 0x00, 0x00, 0x0f, 0x10, 0x0e, 0x01, 0x1e, 0x00 },   // 0x73, s
        { 0x07, 0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06, 0x00 },   // 0x74, t
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d, 0x00 },   // 0x75, u
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04, 0x00 },   // 0x76, v
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0a, 0x00 },   // 0x77, w
        { 0x07, 0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x00 },   // 0x78, x
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x0f, 0x01, 0x11, 0x0e },   // 0x79, y
        { 0x07, 0x00, 0x00, 0x1f, 0x02, 0x04, 0x08, 0x1f, 0x00 },   // 0x7a, z
        { 0x07, 0x06, 0x08, 0x08, 0x10, 0x08, 0x08, 0x06, 0x00 },   // 0x7b, {
        { 0x07, 0x04, 0x04, 0x04, 0x00, 0x04, 0x04, 0x04, 0x00 },   // 0x7c, |
        { 0x07, 0x0c, 0x02, 0x02, 0x01, 0x02, 0x02, 0x0c, 0x00 },   // 0x7d, }
        { 0x07, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x7e, ~
        { 0x07, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x00 }    // 0x7f, DEL
};

// Font 5x8 in an 8x8 matrix, 0,0 is top right
unsigned short const font2[96][9] = { { 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00 },   // 0x20, Space
        { 0x07, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00 },   // 0x21, !
        { 0x07, 0x09, 0x09, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x22, "
        { 0x07, 0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a, 0x00 },   // 0x23, #
        { 0x07, 0x04, 0x0f, 0x14, 0x0e, 0x05, 0x1e, 0x04, 0x00 },   // 0x24, $
        { 0x07, 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13, 0x00 },   // 0x25, %
        { 0x07, 0x04, 0x0a, 0x0a, 0x0a, 0x15, 0x12, 0x0d, 0x00 },   // 0x26, &
        { 0x07, 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x27, '
        { 0x07, 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, 0x00 },   // 0x28, (
        { 0x07, 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00 },   // 0x29, )
        { 0x07, 0x04, 0x15, 0x0e, 0x1f, 0x0e, 0x15, 0x04, 0x00 },   // 0x2a, *
        { 0x07, 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00, 0x00 },   // 0x2b, +
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02 },   // 0x2c, ,
        { 0x07, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00 },   // 0x2d, -
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x00 },   // 0x2e, .
        { 0x07, 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00 },   // 0x2f, /
        { 0x07, 0x00, 0x00, 0x07, 0x05, 0x05, 0x05, 0x07, 0x00 },   // 0x30, 0
        { 0x07, 0x00, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00 },   // 0x31, 1
        { 0x07, 0x00, 0x00, 0x07, 0x01, 0x07, 0x04, 0x07, 0x00 },   // 0x32, 2
        { 0x07, 0x00, 0x00, 0x07, 0x01, 0x07, 0x01, 0x07, 0x00 },   // 0x33, 3
        { 0x07, 0x00, 0x00, 0x05, 0x05, 0x07, 0x01, 0x01, 0x00 },   // 0x34, 4
        { 0x07, 0x00, 0x00, 0x07, 0x04, 0x07, 0x01, 0x07, 0x00 },   // 0x35, 5
        { 0x07, 0x00, 0x00, 0x07, 0x04, 0x07, 0x05, 0x07, 0x00 },   // 0x36, 6
        { 0x07, 0x00, 0x00, 0x07, 0x01, 0x01, 0x01, 0x01, 0x00 },   // 0x37, 7
        { 0x07, 0x00, 0x00, 0x07, 0x05, 0x07, 0x05, 0x07, 0x00 },   // 0x38, 8
        { 0x07, 0x00, 0x00, 0x07, 0x05, 0x07, 0x01, 0x07, 0x00 },   // 0x39, 9
        { 0x04, 0x00, 0x03, 0x03, 0x00, 0x03, 0x03, 0x00, 0x00 },   // 0x3a, :
        { 0x07, 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08, 0x00 },   // 0x3b, ;
        { 0x07, 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00 },   // 0x3c, <
        { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x3d, =
        { 0x07, 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00 },   // 0x3e, >
        { 0x07, 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00 },   // 0x3f, ?
        { 0x07, 0x0e, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0f, 0x00 },   // 0x40, @
        { 0x07, 0x04, 0x0a, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x00 },   // 0x41, A
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e, 0x00 },   // 0x42, B
        { 0x07, 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e, 0x00 },   // 0x43, C
        { 0x07, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E, 0x00 },   // 0x44, D
        { 0x07, 0x1f, 0x10, 0x10, 0x1c, 0x10, 0x10, 0x1f, 0x00 },   // 0x45, E
        { 0x07, 0x1f, 0x10, 0x10, 0x1f, 0x10, 0x10, 0x10, 0x00 },   // 0x46, F
        { 0x07, 0x0e, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0f, 0x00 },   // 0x37, G
        { 0x07, 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, 0x00 },   // 0x48, H
        { 0x07, 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e, 0x00 },   // 0x49, I
        { 0x07, 0x1f, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c, 0x00 },   // 0x4a, J
        { 0x07, 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00 },   // 0x4b, K
        { 0x07, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f, 0x00 },   // 0x4c, L
        { 0x07, 0x11, 0x1b, 0x15, 0x11, 0x11, 0x11, 0x11, 0x00 },   // 0x4d, M
        { 0x07, 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00 },   // 0x4e, N
        { 0x07, 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x4f, O
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10, 0x00 },   // 0x50, P
        { 0x07, 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d, 0x00 },   // 0x51, Q
        { 0x07, 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11, 0x00 },   // 0x52, R
        { 0x07, 0x0e, 0x11, 0x10, 0x0e, 0x01, 0x11, 0x0e, 0x00 },   // 0x53, S
        { 0x07, 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 },   // 0x54, T
        { 0x07, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x55, U
        { 0x07, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04, 0x00 },   // 0x56, V
        { 0x07, 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11, 0x00 },   // 0x57, W
        { 0x07, 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11, 0x00 },   // 0x58, X
        { 0x07, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04, 0x00 },   // 0x59, Y
        { 0x07, 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f, 0x00 },   // 0x5a, Z
        { 0x07, 0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e, 0x00 },   // 0x5b, [
        { 0x07, 0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01, 0x00 },   // 0x5c, '\'
        { 0x07, 0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e, 0x00 },   // 0x5d, ]
        { 0x07, 0x04, 0x0a, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x5e, ^
        { 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00 },   // 0x5f, _
        { 0x07, 0x04, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x60, `
        { 0x07, 0x00, 0x0e, 0x01, 0x0d, 0x13, 0x13, 0x0d, 0x00 },   // 0x61, a
        { 0x07, 0x10, 0x10, 0x10, 0x1c, 0x12, 0x12, 0x1c, 0x00 },   // 0x62, b
        { 0x07, 0x00, 0x00, 0x0E, 0x10, 0x10, 0x10, 0x0E, 0x00 },   // 0x63, c
        { 0x07, 0x01, 0x01, 0x01, 0x07, 0x09, 0x09, 0x07, 0x00 },   // 0x64, d
        { 0x07, 0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0f, 0x00 },   // 0x65, e
        { 0x07, 0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08, 0x00 },   // 0x66, f
        { 0x07, 0x00, 0x0e, 0x11, 0x13, 0x0d, 0x01, 0x01, 0x0e },   // 0x67, g
        { 0x07, 0x10, 0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x00 },   // 0x68, h
        { 0x05, 0x00, 0x02, 0x00, 0x06, 0x02, 0x02, 0x07, 0x00 },   // 0x69, i
        { 0x07, 0x00, 0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0c },   // 0x6a, j
        { 0x07, 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00 },   // 0x6b, k
        { 0x05, 0x06, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00 },   // 0x6c, l
        { 0x07, 0x00, 0x00, 0x0a, 0x15, 0x15, 0x11, 0x11, 0x00 },   // 0x6d, m
        { 0x07, 0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00 },   // 0x6e, n
        { 0x07, 0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e, 0x00 },   // 0x6f, o
        { 0x07, 0x00, 0x00, 0x1c, 0x12, 0x12, 0x1c, 0x10, 0x10 },   // 0x70, p
        { 0x07, 0x00, 0x00, 0x07, 0x09, 0x09, 0x07, 0x01, 0x01 },   // 0x71, q
        { 0x07, 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00 },   // 0x72, r
        { 0x07, 0x00, 0x00, 0x0f, 0x10, 0x0e, 0x01, 0x1e, 0x00 },   // 0x73, s
        { 0x07, 0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06, 0x00 },   // 0x74, t
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d, 0x00 },   // 0x75, u
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04, 0x00 },   // 0x76, v
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0a, 0x00 },   // 0x77, w
        { 0x07, 0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x00 },   // 0x78, x
        { 0x07, 0x00, 0x00, 0x11, 0x11, 0x0f, 0x01, 0x11, 0x0e },   // 0x79, y
        { 0x07, 0x00, 0x00, 0x1f, 0x02, 0x04, 0x08, 0x1f, 0x00 },   // 0x7a, z
        { 0x07, 0x06, 0x08, 0x08, 0x10, 0x08, 0x08, 0x06, 0x00 },   // 0x7b, {
        { 0x07, 0x04, 0x04, 0x04, 0x00, 0x04, 0x04, 0x04, 0x00 },   // 0x7c, |
        { 0x07, 0x0c, 0x02, 0x02, 0x01, 0x02, 0x02, 0x0c, 0x00 },   // 0x7d, }
        { 0x07, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },   // 0x7e, ~
        { 0x07, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x00 }    // 0x7f, DEL
};
//************************Auto Connect**************************************************************************
const char* passHidden(const char* pass)   // never log the WiFi password in clear text
{
    return (pass && pass[0]) ? "(hidden)" : "";
}
//**************************************************************************************************
bool autoConfig()
{
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);    // keep the web UI/OTA responsive; the clock is mains-powered

    if (strlen(settings.ssid) > 0)
    {
        WiFi.persistent(false);   // settings are the source of truth, don't touch flash config
        Serial.printf("Connecting to SSID:%s PSW:%s\r\n", settings.ssid, passHidden(settings.pass));
        Serial.println("Scanning networks...");
        int n = WiFi.scanNetworks();
        Serial.printf("Found %d networks\r\n", n);
        bool found = false;
        for (int i = 0; i < n; i++)
        {
            Serial.printf("  %d: %s (%d dBm)\r\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
            if (WiFi.SSID(i).equals(settings.ssid)) found = true;
        }
        Serial.printf("Target SSID %s: %s\r\n", settings.ssid, found ? "FOUND" : "NOT FOUND");

        WiFi.disconnect(false, false);   // stop any in-flight attempt, keep stored credentials
        delay(200);
        WiFi.mode(WIFI_STA);
        WiFi.begin(settings.ssid, settings.pass);   // credentials from settings
    }
    else
    {
        WiFi.persistent(true);    // smartConfig path relies on flash persistence
        Serial.println("No SSID in settings, using flash saved config");
        WiFi.mode(WIFI_STA);
        WiFi.begin();                               // connect to the saved WiFi by default
    }

    for (int i = 0; i < 10; i++)
    {
       char2Arr('W', 28, 0);
       char2Arr('i', 22, 0);
       char2Arr('-', 18, 0);
       char2Arr('F', 12, 0);
       char2Arr('i', 6, 0);

       refresh_display(); 
       
       if (WiFi.status() == WL_CONNECTED)
       {
          Serial.println("AutoConfig Success");
          Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
           Serial.printf("PSW:%s\r\n", passHidden(WiFi.psk().c_str()));
          clear_Display();
          char2Arr('O', 25, 0);
          char2Arr('K', 19, 0);
          char2Arr('!', 12, 0);
          char2Arr('!', 6, 0);
          refresh_display(); 

          Serial.println("WiFi connected");
          Serial.println(WiFi.localIP());
          Serial.println("Starting UDP");
          udp.begin(localPort);
          Serial.print("Local port: ");
          Serial.println(udp.localPort());
          return true;
       }
       else{
          Serial.print("AutoConfig Waiting......");
          Serial.println(WiFi.status());
          delay(1000);
       }
    }
    clear_Display();
    char2Arr('E', 25, 0);
    char2Arr('r', 19, 0);
    char2Arr('r', 12, 0);
    char2Arr('!', 6, 0);
    refresh_display(); 
    delay(1000);
    Serial.println("AutoConfig Failed!" );
    return false;
}
//***************************Smart Config**********************************************************************
void smartConfig()
{
    int i = 0;
  
    WiFi.mode(WIFI_STA);
    Serial.println("\r\nWait for Smartconfig");
    WiFi.beginSmartConfig();
    for (i = 0; i < 30; i++)
    {
       Serial.print(".");
       if(WiFi.smartConfigDone())
       {
          clear_Display();
          char2Arr('O', 25, 0);
          char2Arr('K', 19, 0);
          char2Arr('!', 12, 0);
          char2Arr('!', 6, 0);
          refresh_display(); 
          Serial.println("SmartConfig Success");
          Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
           Serial.printf("PSW:%s\r\n", passHidden(WiFi.psk().c_str()));
          WiFi.setAutoConnect(true);  // enable auto connect
          
          strncpy(settings.ssid, WiFi.SSID().c_str(), sizeof(settings.ssid) - 1);   // remember credentials in settings
          settings.ssid[sizeof(settings.ssid) - 1] = '\0';
          strncpy(settings.pass, WiFi.psk().c_str(), sizeof(settings.pass) - 1);
          settings.pass[sizeof(settings.pass) - 1] = '\0';
          settings_save();
          
          Serial.println("WiFi connected");
          Serial.println(WiFi.localIP());
          Serial.println("Starting UDP");
          udp.begin(localPort);
          Serial.print("Local port: ");
          Serial.println(udp.localPort());
          delay(1000); 
          ESP.restart();
          break;
       }
       clear_Display();
       char2Arr('S', 29, 0);// show "S-con" while entering smart config mode
       char2Arr('-', 23, -1);
       char2Arr('c', 17, 0);
       char2Arr('o', 12, 0);
       char2Arr('n', 6, 0);
       refresh_display(); 
       delay(1000);
    }
    if (i > 28)
    {
       clear_Display();
       char2Arr('R', 25, 0);
       char2Arr('T', 19, 0);
       char2Arr('C', 12, 0);
       char2Arr('!', 6, 0);
       refresh_display(); 
       delay(1000);
       Serial.println("SmartConfig Failed!" );
       Serial.println("Clock uses RTC!" );
       WiFi.stopSmartConfig();   // release the radio so the fallback softAP can start
    }
}
//**************************************************************************************************
//***************************Settings Storage (EEPROM)**********************************************************************
void settings_save()
{
    EEPROM.put(0, settings);
    EEPROM.commit();
}
//**************************************************************************************************
void settings_load()
{
    EEPROM.begin(sizeof(ClockSettings) + 8);
    EEPROM.get(0, settings);
    if (settings.magic != 0xA5A5A5A5 || settings.dstRule > 3)   // dstRule > 3 = settings saved by an older firmware
    {
        settings = settings_defaults;
        settings_save();
    }
    bool repaired = false;
    if (settings.colonAnim > 4) { settings.colonAnim = 0; repaired = true; }   // new fields: 0xFF from older firmware = off
    settings.marquee = 0;   // effects removed from the settings menu, keep disabled
    settings.pulse = 0;
    if (settings.ntpInterval > 720) { settings.ntpInterval = 0; repaired = true; }   // field added later / garbage from old EEPROM -> off
    if (settings.weather > 1) { settings.weather = 0; repaired = true; }   // new fields: 0xFF from older firmware = off
    if (settings.weatherInterval > 240 || settings.weatherInterval == 0) { settings.weatherInterval = 30; repaired = true; }
    if (settings.weatherSpeed < 1 || settings.weatherSpeed > 4) { settings.weatherSpeed = 2; repaired = true; }   // new field / garbage from old EEPROM -> normal
    if (settings.scrollDateInt < 1 || settings.scrollDateInt > 120) { settings.scrollDateInt = 1; repaired = true; }   // new field -> every minute
    if (settings.rainWarn > 1) { settings.rainWarn = 1; repaired = true; }   // new field / garbage from old EEPROM -> on
    if (settings.heatWarn > 1) { settings.heatWarn = 1; repaired = true; }   // new field / garbage from old EEPROM -> on
    if (settings.rainThresh < 1 || settings.rainThresh > 25) { settings.rainThresh = 2; repaired = true; }   // legacy field -> 2mm
    if (settings.heatThresh < 20 || settings.heatThresh > 50) { settings.heatThresh = 31; repaired = true; }   // legacy field -> 31C
    if (settings.threshFormat != 0xA55A)   // settings saved by v5.7 or older: copy whole-number thresholds
    {
        settings.rainThresh10 = (uint16_t) settings.rainThresh * 10;
        settings.heatThresh10 = (uint16_t) settings.heatThresh * 10;
        settings.threshFormat = 0xA55A;
        repaired = true;
    }
    if (settings.imsFormat != 0xBEEF)   // settings saved by v5.9 or older
    {
        settings.weatherSource = 0;
        settings.imsStation = 0;
        settings.imsFormat = 0xBEEF;
        repaired = true;
    }
    if (settings.weatherSource > 2) { settings.weatherSource = 0; repaired = true; }
    if (settings.imsStation > 9999) { settings.imsStation = 0; repaired = true; }
    if (settings.rainThresh10 < 1 || settings.rainThresh10 > 250) { settings.rainThresh10 = 20; repaired = true; }   // 0.1..25.0 mm
    if (settings.heatThresh10 < 200 || settings.heatThresh10 > 500) { settings.heatThresh10 = 310; repaired = true; }   // 20.0..50.0 C
    uint8_t rainWhole = (uint8_t) constrain((settings.rainThresh10 + 5) / 10, 1, 25);
    uint8_t heatWhole = (uint8_t) constrain((settings.heatThresh10 + 5) / 10, 20, 50);
    if (settings.rainThresh != rainWhole) { settings.rainThresh = rainWhole; repaired = true; }
    if (settings.heatThresh != heatWhole) { settings.heatThresh = heatWhole; repaired = true; }
    if (settings.digitAnim > 5) { settings.digitAnim = 1; repaired = true; }   // new field / garbage from old EEPROM -> roll
    if (settings.weatherCityShow > 1) { settings.weatherCityShow = 1; repaired = true; }   // new field / garbage from old EEPROM -> show city
    if (settings.warnAnim > 1) { settings.warnAnim = 1; repaired = true; }   // new field / garbage from old EEPROM -> animation on
    if (settings.shabbat > 1) { settings.shabbat = 1; repaired = true; }   // new field / garbage from old EEPROM -> on
    if (settings.shabbatLead < 10 || settings.shabbatLead > 240) { settings.shabbatLead = 60; repaired = true; }   // new field -> 60 min
    if (settings.shabbatFreq != 5 && settings.shabbatFreq != 10 && settings.shabbatFreq != 15
        && settings.shabbatFreq != 20 && settings.shabbatFreq != 30 && settings.shabbatFreq != 60)
    { settings.shabbatFreq = 10; repaired = true; }
    if (settings.shabbatAnim > 1) { settings.shabbatAnim = 1; repaired = true; }
    if (settings.holidayGreetings > 1) { settings.holidayGreetings = 1; repaired = true; }   // new field -> on
    settings.weatherCity[sizeof(settings.weatherCity) - 1] = '\0';
    for (unsigned i = 0; i < sizeof(settings.weatherCity); i++)   // blank EEPROM (0xFF) from older firmware -> empty
        if ((unsigned char) settings.weatherCity[i] > 0x7E)
        {
            settings.weatherCity[i] = '\0';
            repaired = true;
            break;
        }
    settings.webPass[sizeof(settings.webPass) - 1] = '\0';
    for (unsigned i = 0; i < sizeof(settings.webPass); i++)   // blank EEPROM (0xFF) from older firmware -> no auth
        if ((unsigned char) settings.webPass[i] > 0x7E)
        {
            settings.webPass[i] = '\0';
            repaired = true;
            break;
        }
    if (repaired)
        settings_save();   // persist the corrected fields so they don't need repairing on every boot
}
//**************************************************************************************************
//***************************Web Settings Page**********************************************************************
ESP8266WebServer server(80);
unsigned long otaRebootAt = 0;          // >0 while waiting to reboot after a successful update
bool otaUploadStarted = false;
bool otaUploadSuccess = false;
char otaUploadError[96] = "";
Ticker otaWatchdog;
volatile bool otaWatchdogActive = false;
volatile unsigned long otaLastProgressMs = 0;

void otaWatchdogTick()   // reset if the upload parser waits forever on a half-open socket
{
    if (otaWatchdogActive && (millis() - otaLastProgressMs > 45000UL))
        ESP.restart();
}
//**************************************************************************************************
bool webAuthOk()   // optional HTTP Basic auth: enabled only when a web password is configured
{
    if (settings.webPass[0] == 0) return true;
    return server.authenticate("admin", settings.webPass);
}
//**************************************************************************************************
void startMsg(const char* text, uint8_t step = 2);   // forward decl, defined below
void startAP()   // start fallback AP so the settings page stays reachable without router WiFi
{
    if (apActive) return;
    WiFi.setAutoReconnect(false);   // stop the SDK's background retry loop
    WiFi.mode(WIFI_OFF);            // clean radio state (also clears any SmartConfig session)
    delay(300);
    WiFi.mode(WIFI_AP_STA);         // keep the station interface available for WiFi scanning
    bool ok = WiFi.softAP("MatrixClock-Setup", "12345678");
    if (!ok)
    {
        delay(400);
        ok = WiFi.softAP("MatrixClock-Setup", "12345678");
    }
    if (!ok)
    {
        Serial.println("AP start FAILED (softAP returned false)");
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        return;    }
    apActive = true;
    apMsgUntil = millis() + 30000UL;   // keep showing the setup hint for 30 seconds
    startMsg("SETUP MODE AP 192.168.4.1");
    Serial.printf("AP started: MatrixClock-Setup / password 12345678 / http://%s\r\n",
                  WiFi.softAPIP().toString().c_str());
}
//**************************************************************************************************
void stopAP()    // stop fallback AP once the router connection works again
{
    if (!apActive) return;
    apActive = false;
    WiFi.setAutoReconnect(true);    // normal reconnect behavior once the router is back
    // Shut the softAP down a few seconds later: switching the opmode in the same
    // instant the station connection completes can crash the SDK (observed: reset
    // right after "WiFi connected" with no "AP stopped" printed).
    apShutdownPending = true;
    apShutdownSince = millis();
    Serial.println("AP stop scheduled");
}
//**************************************************************************************************
void enterAPUpdateMode()   // switch to the setup AP so a weak router link can't break the upload
{
    apUpdateMode = true;
    apUpdateSince = millis();
    startAP();   // robust AP-only start with retry and softAP result check
    if (!apActive)
    {
        Serial.println("AP update mode failed, rebooting");
        delay(1500);
        ESP.restart();
        return;
    }
    apMsgUntil = millis() + 3600000UL;   // keep the hint message for an hour
    server.close();
    server.begin();   // rebind the web server to the AP interface
    startMsg("AP UPDATE 192.168.4.1");
    Serial.printf("AP update mode: IP=%s\r\n", WiFi.softAPIP().toString().c_str());
}
//**************************************************************************************************
void startMsgNow(const char* text, uint8_t step)
{
    strncpy(msg, text, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = 0;
    msgStep = step;
    showMsg = true;
    z_PosX = 0;
    d_PosX = -8;
}

void startNextQueuedMsg()
{
    if (msgQueueCount == 0) return;
    strncpy(msg, msgQueue[0], sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = 0;
    msgStep = msgQueueStep[0];
    for (uint8_t i = 1; i < msgQueueCount; i++)
    {
        strncpy(msgQueue[i - 1], msgQueue[i], sizeof(msgQueue[i]) - 1);
        msgQueueStep[i - 1] = msgQueueStep[i];
    }
    msgQueueCount--;
    showMsg = true;
    z_PosX = 0;
    d_PosX = -8;
}

void startMsg(const char* text, uint8_t step)   // scroll a message; queue up to 3 so weather/Shabbat are not lost
{
    if (!showMsg && msgQueueCount == 0)
    {
        startMsgNow(text, step);
        return;
    }
    if (msgQueueCount >= MSG_QUEUE_SIZE)   // drop the oldest queued message
    {
        for (uint8_t i = 1; i < MSG_QUEUE_SIZE; i++)
        {
            strncpy(msgQueue[i - 1], msgQueue[i], sizeof(msgQueue[i]) - 1);
            msgQueueStep[i - 1] = msgQueueStep[i];
        }
        msgQueueCount = MSG_QUEUE_SIZE - 1;
    }
    strncpy(msgQueue[msgQueueCount], text, sizeof(msgQueue[msgQueueCount]) - 1);
    msgQueue[msgQueueCount][sizeof(msgQueue[msgQueueCount]) - 1] = 0;
    msgQueueStep[msgQueueCount] = step;
    msgQueueCount++;
}
//**************************************************************************************************
String htmlEscape(const char* s)   // escape special chars so saved values can't break the HTML form
{
    String out;
    for (const char* p = s; *p; p++)
    {
        if (*p == '&') out += "&amp;";
        else if (*p == '<') out += "&lt;";
        else if (*p == '>') out += "&gt;";
        else if (*p == '"') out += "&quot;";
        else out += *p;
    }
    return out;
}
//**************************************************************************************************
String jsonEsc(const char* s)   // escape special chars so saved values can't break the /v JSON
{
    String out;
    for (const char* p = s; *p; p++)
    {
        if (*p == '"') out += "\\\"";
        else if (*p == '\\') out += "\\\\";
        else out += *p;
    }
    return out;
}
//**************************************************************************************************
String tenthsStr(uint16_t v)   // 12 -> "1.2" (used by the settings page and logs)
{
    char b[10];
    snprintf(b, sizeof b, "%u.%u", (unsigned) (v / 10), (unsigned) (v % 10));
    return String(b);
}
String imsStationOptions();   // defined with the IMS station table below
//**************************************************************************************************
void sendChunked(int code, const char* contentType, const String& body)
    // ESP8266 lwIP can silently drop a TCP segment when a response is handed to the socket as one
    // large write() and never retransmit it -- which is why the settings page used to need several
    // reloads to fully load. Sending the same bytes as a series of small, explicit writes (each
    // well under one TCP segment) with a yield() between them avoids that failure mode.
    // The writes are zero-copy: they point straight into the existing body buffer. The previous
    // version built a fresh substring() String per chunk, and when that allocation failed under
    // heap fragmentation it silently sent nothing while the loop still advanced -- leaving holes
    // in the middle of the page (the "settings page sometimes incomplete" bug).
{
    const size_t CHUNK = 512;
    const char* p = body.c_str();
    size_t total = body.length();
    server.setContentLength(total);
    server.send(code, contentType, "");
    unsigned long started = millis();
    size_t sentLen = 0;
    while (sentLen < total)
    {
        size_t n = min(CHUNK, total - sentLen);
        size_t written = server.client().write(p + sentLen, n);
        if (written == 0)   // socket stalled or gone: back off and retry the same bytes
        {
            if (!server.client().connected()) return;   // browser vanished, nothing more to send
            if (millis() - started > 15000UL) { server.client().stop(); return; }   // never block the whole clock on one page
            delay(2);
            continue;
        }
        sentLen += written;
        yield();
    }
}
//**************************************************************************************************
void sendChunked_P(int code, const char* contentType, const char* body)
    // Same as sendChunked(), but the body stays in flash (PROGMEM) instead of using heap.
{
    const size_t CHUNK = 512;
    size_t total = strlen_P(body);
    server.setContentLength(total);
    server.send(code, contentType, "");
    unsigned long started = millis();
    size_t sentLen = 0;
    char buf[CHUNK];
    while (sentLen < total)
    {
        size_t n = min(CHUNK, total - sentLen);
        memcpy_P(buf, body + sentLen, n);
        size_t offset = 0;
        while (offset < n)
        {
            size_t written = server.client().write((const uint8_t*) (buf + offset), n - offset);
            if (written == 0)
            {
                if (!server.client().connected()) return;
                if (millis() - started > 15000UL) { server.client().stop(); return; }
                delay(2);
                continue;
            }
            offset += written;
        }
        sentLen += n;
        yield();
    }
}
//**************************************************************************************************

void handleRoot()
{
    // Cosmetic + mobile-optimized settings page. The page is stored in flash (PROGMEM)
    // and sent in small chunks, so it does not consume the ESP8266 heap.
    if (!webAuthOk()) { server.requestAuthentication(); return; }
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.keepAlive(false);   // free the socket after the page; avoids ESP8266 keep-alive stalls
    sendChunked_P(200, "text/html", ROOT_PAGE);
}
//**************************************************************************************************
void handleValues()
{
    if (!webAuthOk()) { server.requestAuthentication(); return; }
    server.keepAlive(false);
    String j;
    j.reserve(5200);
    j = "{\"f\":[";
    j += "[\"SSID\",\"ssid\",null,\"" + jsonEsc(settings.ssid) + "\"],";
    j += "[\"WiFi Password\",\"pass\",null,\"\"],";   // never return the saved password; leave blank to keep it
    j += "[\"Web Login Password\",\"webPass\",null,\"" + String(settings.webPass[0] ? "********" : "") + "\"],";
    j += "[\"NTP Server\",\"ntp\",null,\"" + jsonEsc(settings.ntpServer) + "\"],";
    j += "[\"Time Zone\",\"tz\",null,\"" + String(settings.tzOffset) + "\"],";
    j += "[\"Daylight Saving Time\",\"dst\",[[\"0\",\"Off\"],[\"1\",\"IL\"],[\"2\",\"EU\"],[\"3\",\"US\"]],\"" + String(settings.dstRule) + "\"],";
    j += "[\"Brightness\",\"bright\",null,\"" + String(settings.brightness) + "\"],";
    j += "[\"Orientation\",\"orient\",[[\"0\",\"Auto\"],[\"1\",\"Norm\"],[\"2\",\"Flip\"]],\"" + String(settings.orient) + "\"],";
    j += "[\"Scroll Date\",\"scrollDate\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.scrollDate) + "\"],";
    j += "[\"Date Interval (min)\",\"scrollDateInt\",null,\"" + String(settings.scrollDateInt) + "\"],";
    j += "[\"Animation Direction\",\"scrollDir\",[[\"0\",\"Up-Down\"],[\"1\",\"Down-Up\"]],\"" + String(settings.scrollDir) + "\"],";
    j += "[\"Colon Animation\",\"colonAnim\",[[\"0\",\"Solid\"],[\"1\",\"Blink\"],[\"2\",\"Slow\"],[\"3\",\"Dbl\"],[\"4\",\"Alt\"]],\"" + String(settings.colonAnim) + "\"],";
    j += "[\"Digit Animation\",\"digitAnim\",[[\"0\",\"Off\"],[\"1\",\"Roll\"],[\"2\",\"Flip\"],[\"3\",\"Wipe\"],[\"4\",\"Fade\"],[\"5\",\"Drop\"]],\"" + String(settings.digitAnim) + "\"],";
    j += "[\"NTP Resync\",\"resync\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.resync) + "\"],";
    j += "[\"Resync Hour\",\"resyncHour\",null,\"" + String(settings.resyncHour) + "\"],";
    j += "[\"Resync Minute\",\"resyncMin\",null,\"" + String(settings.resyncMin) + "\"],";
    j += "[\"NTP Interval (min)\",\"ntpInterval\",null,\"" + String(settings.ntpInterval) + "\"],";
    j += "[\"Weather Messages\",\"weather\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.weather) + "\"],";
    j += "[\"Weather City\",\"weatherCity\",null,\"" + jsonEsc(settings.weatherCity) + "\"],";
    j += "[\"Show Weather City\",\"weatherCityShow\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.weatherCityShow) + "\"],";
    j += "[\"Weather Interval (min)\",\"weatherInterval\",null,\"" + String(settings.weatherInterval) + "\"],";
    j += "[\"Weather Speed\",\"weatherSpeed\",[[\"1\",\"Slow\"],[\"2\",\"Norm\"],[\"3\",\"Fast\"],[\"4\",\"VFast\"]],\"" + String(settings.weatherSpeed) + "\"],";
    j += "[\"Rain Warning\",\"rainWarn\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.rainWarn) + "\"],";
    j += "[\"Rain Threshold (mm/h, e.g. 1.2)\",\"rainThresh\",null,\"" + tenthsStr(settings.rainThresh10) + "\"],";
    j += "[\"Weather Source\",\"weatherSource\",[[\"0\",\"Open-Meteo\"],[\"1\",\"IMS + Open-Meteo\"],[\"2\",\"IMS only\"]],\"" + String(settings.weatherSource) + "\"],";
    j += "[\"IMS Station\",\"imsStation\"," + imsStationOptions() + ",\"" + String(settings.imsStation) + "\"],";
    j += "[\"Heat Warning\",\"heatWarn\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.heatWarn) + "\"],";
    j += "[\"Heat Threshold (°C, e.g. 35.5)\",\"heatThresh\",null,\"" + tenthsStr(settings.heatThresh10) + "\"],";
    j += "[\"Warning Animation\",\"warnAnim\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.warnAnim) + "\"],";
    j += "[\"Shabbat Times\",\"shabbat\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.shabbat) + "\"],";
    j += "[\"Shabbat Message Frequency\",\"shabbatFreq\",[[\"5\",\"5 min\"],[\"10\",\"10 min\"],[\"15\",\"15 min\"],[\"20\",\"20 min\"],[\"30\",\"30 min\"],[\"60\",\"60 min\"]],\"" + String(settings.shabbatFreq) + "\"],";
    j += "[\"Shabbat Lead (min)\",\"shabbatLead\",[[\"10\",\"10 min\"],[\"15\",\"15 min\"],[\"20\",\"20 min\"],[\"30\",\"30 min\"],[\"45\",\"45 min\"],[\"60\",\"1 hour\"],[\"90\",\"1.5 h\"],[\"120\",\"2 h\"],[\"180\",\"3 h\"],[\"240\",\"4 h\"]],\"" + String(settings.shabbatLead) + "\"],";
    j += "[\"Shabbat Animation\",\"shabbatAnim\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.shabbatAnim) + "\"],";
    j += "[\"Holiday Greetings\",\"holidayGreetings\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.holidayGreetings) + "\"]";
    j += "]}";
    sendChunked(200, "application/json", j);
}
//**************************************************************************************************
void handleWeatherPage()
{
    if (!webAuthOk()) { server.requestAuthentication(); return; }
    server.keepAlive(false);

    if (server.arg("format") == "json")
    {
        double rainLimit = settings.rainThresh10 / 10.0;
        bool dataForCity = weatherDataValid && strcmp(weatherDataCity, settings.weatherCity) == 0;
        bool localFresh = settings.weatherSource != 0 && imsRainValid && (millis() - imsUpdatedMs < 2700000UL);
        bool warning = false;
        if (settings.rainWarn)
        {
            if (settings.weatherSource != 2 && dataForCity
                && (weatherPrecipRate >= rainLimit || weatherRainPeak >= rainLimit)) warning = true;
            if (localFresh && imsRainRate >= rainLimit) warning = true;
        }
        unsigned long omAge = dataForCity ? (millis() - weatherUpdatedMs) / 1000UL : 0;
        unsigned long imsAge = imsRainValid ? (millis() - imsUpdatedMs) / 1000UL : 0;
        String j;
        j.reserve(800);
        j = "{\"ok\":1,\"city\":\"" + jsonEsc(settings.weatherCity) + "\"";
        j += ",\"source\":" + String(settings.weatherSource);
        j += ",\"threshold\":" + String(settings.rainThresh10 / 10.0, 1);
        j += ",\"rainWarn\":" + String(settings.rainWarn);
        j += ",\"warning\":" + String(warning ? 1 : 0);
        j += ",\"omValid\":" + String(dataForCity ? 1 : 0);
        j += ",\"omNow\":" + String(weatherPrecipRate, 1);
        j += ",\"omPeak\":" + String(weatherRainPeak, 1);
        j += ",\"omTemp\":" + String(weatherTempC, 1);
        j += ",\"omHum\":" + String(weatherHumidity);
        j += ",\"omAge\":" + String(omAge);
        j += ",\"imsEnabled\":" + String(settings.weatherSource != 0 ? 1 : 0);
        j += ",\"imsValid\":" + String(imsRainValid ? 1 : 0);
        j += ",\"imsStation\":\"" + jsonEsc(imsStationName) + "\"";
        j += ",\"imsId\":" + String(imsStationId);
        j += ",\"imsDistance\":" + String(imsStationDistanceKm, 1);
        j += ",\"imsRate\":" + String(imsRainRate, 1);
        j += ",\"imsTemp\":" + String(imsTempC, 1);
        j += ",\"imsHum\":" + String(imsHumidity);
        j += ",\"imsTime\":\"" + jsonEsc(imsObsTime) + "\"";
        j += ",\"imsAge\":" + String(imsAge);
        j += "}";
        server.send(200, "application/json", j);
        return;
    }

    double rainLimit = settings.rainThresh10 / 10.0;
    bool dataForCity = weatherDataValid && strcmp(weatherDataCity, settings.weatherCity) == 0;
    bool localFresh = settings.weatherSource != 0 && imsRainValid && (millis() - imsUpdatedMs < 2700000UL);
    bool warning = false;
    if (settings.rainWarn)
    {
        if (settings.weatherSource != 2 && dataForCity
            && (weatherPrecipRate >= rainLimit || weatherRainPeak >= rainLimit)) warning = true;
        if (localFresh && imsRainRate >= rainLimit) warning = true;
    }
    unsigned long age = dataForCity ? (millis() - weatherUpdatedMs) / 1000UL : 0;
    unsigned long localAge = imsRainValid ? (millis() - imsUpdatedMs) / 1000UL : 0;

    String page;
    page.reserve(3200);
    page += "<!DOCTYPE html><html><head><meta charset=utf-8>"
            "<meta name=viewport content='width=device-width,initial-scale=1'>"
            "<meta name=theme-color content='#0a0d0b'>"
            "<meta http-equiv=refresh content=30>"
            "<title>MatrixClock Weather</title><style>"
            ":root{--bg:#0a0d0b;--panel:#121614;--line:#232a26;--accent:#22e07a;--text:#e8f0ec;--sub:#7c8c84}"
            "*{box-sizing:border-box}body{margin:0;background:radial-gradient(900px 420px at 50% -10%,#10241a 0,var(--bg) 62%);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;min-height:100vh;padding:22px 16px}"
            ".wrap{max-width:440px;margin:0 auto}h1{font-size:20px;text-align:center;margin:4px 0 6px;color:var(--accent)}"
            ".city{text-align:center;color:var(--sub);font-size:13px;margin:0 0 18px}"
            "table{width:100%;border-collapse:collapse;background:linear-gradient(180deg,#171c19,#121614);border:1px solid var(--line);border-radius:14px;overflow:hidden;box-shadow:0 6px 20px rgba(0,0,0,.18)}"
            "td{padding:12px 14px;border-bottom:1px solid var(--line);font-size:14px}tr:last-child td{border-bottom:none}"
            "td:first-child{color:var(--sub)}td:last-child{text-align:right;font-weight:600}"
            ".yes{color:var(--accent)}.no{color:var(--sub)}.big{font-size:16px}"
            ".note{font-size:12px;line-height:1.6;color:var(--sub);margin:16px 0}.note a{color:var(--accent);text-decoration:none}"
            "</style></head><body><div class=wrap><h1>Rain / Weather</h1><div class=city>";
    page += settings.weatherCity[0] ? settings.weatherCity : "No weather city configured";
    page += "</div><table>";
    page += "<tr><td>Weather source</td><td>";
    page += settings.weatherSource == 0 ? "Open-Meteo" : (settings.weatherSource == 1 ? "IMS + Open-Meteo" : "IMS only");
    page += "</td></tr>";

    if (settings.weatherSource == 2)
        page += "<tr><td>Open-Meteo</td><td class=no>Disabled (IMS only)</td></tr>";
    else if (!settings.weather || settings.weatherCity[0] == 0)
        page += "<tr><td>Status</td><td class=no>Weather is off</td></tr>";
    else if (!dataForCity)
    {
        page += "<tr><td>Status</td><td class=no>Waiting for first check</td></tr>";
        page += "<tr><td>First check</td><td>15 seconds after boot</td></tr>";
    }
    else
    {
        char row[80];
        page += "<tr><td>Rain now</td><td class=big>";
        page += String(weatherPrecipRate, 1);
        page += " mm/h</td></tr><tr><td>Next 2h peak</td><td class=big>";
        page += String(weatherRainPeak, 1);
        page += " mm/h</td></tr><tr><td>Your threshold</td><td>";
        page += tenthsStr(settings.rainThresh10);
        page += " mm/h</td></tr><tr><td>Rain warning</td><td class=";
        if (!settings.rainWarn) page += "no>Off";
        else if (warning) page += "yes>WOULD TRIGGER";
        else page += "no>Not triggered";
        page += "</td></tr><tr><td>Temperature</td><td>";
        page += String(weatherTempC, 1);
        page += " &deg;C</td></tr><tr><td>Humidity</td><td>";
        page += String(weatherHumidity);
        page += " %</td></tr><tr><td>Weather code</td><td>";
        page += String(weatherCode);
        page += "</td></tr><tr><td>Last check</td><td>";
        snprintf(row, sizeof row, "%lu s ago", age);
        page += row;
        page += "</td></tr>";
    }
    if (settings.weatherSource != 0)
    {
        char row[100];
        page += "<tr><td>Local station</td><td>";
        page += imsStationName[0] ? imsStationName : "IMS";
        if (imsStationId)
        {
            page += " #";
            page += String(imsStationId);
        }
        if (settings.imsStation == 0 && imsStationDistanceKm > 0)
        {
            page += " (";
            page += String(imsStationDistanceKm, 1);
            page += " km)";
        }
        page += "</td></tr>";
        if (imsRainValid)
        {
            page += "<tr><td>Local rain now</td><td class=big>";
            page += String(imsRainRate, 1);
            page += " mm/h</td></tr><tr><td>Local observation</td><td>";
            page += imsObsTime;
            snprintf(row, sizeof row, " (%lu s ago)", localAge);
            page += row;
            page += "</td></tr>";
        }
        else
            page += "<tr><td>Local rain now</td><td class=no>No data yet</td></tr>";
    }
    page += "</table><div class=note>";
    if (settings.weatherSource != 2 && settings.weather)
    {
        page += "The clock checks every ";
        page += String(settings.weatherInterval);
        page += " min. It warns when the current rain rate or the peak rate "
                "forecast for the next 2 hours reaches your threshold. This page "
                "refreshes every 30 seconds.<br><br>"
                "Open-Meteo data is model forecast output, not a local rain gauge. "
                "Outside central Europe and North America the 15-minute values are "
                "interpolated from hourly model data.";
    }
    if (settings.weatherSource != 0)
    {
        page += "<br><br>Local rain uses the IMS 10-minute XML station: "
                "nearest to your Weather City when the station ID is 0, or the "
                "station ID you set. IMS data can lag by about 30 minutes. "
                "<a href='https://www.ims.gov.il/sites/default/files/ims_data/xml_files/imslasthour.xml' "
                "target='_blank'>View IMS XML</a>.";
        if (settings.weatherSource == 2)
            page += "<br><br>IMS-only mode uses current station observations. "
                    "It has no forecast, so rain warnings use the current local rate only.";
    }
    page += "<a href='/'>Back to settings</a></div></div></body></html>";
    sendChunked(200, "text/html", page);
}
//**************************************************************************************************
void handleStatus()
{
    if (!webAuthOk()) { server.requestAuthentication(); return; }
    server.keepAlive(false);
    String j;
    j.reserve(260);
    j = "{\"version\":\"" FIRMWARE_VERSION "\"";
    j += ",\"build\":\"" __DATE__ " " __TIME__ "\"";
    j += ",\"heap\":" + String(ESP.getFreeHeap());
    j += ",\"rssi\":" + String(WiFi.RSSI());
    j += ",\"wifi\":" + String((int) WiFi.status());
    j += ",\"uptime\":" + String(millis() / 1000UL);
    j += ",\"sketch\":" + String(ESP.getSketchSize());
    j += ",\"freeSketch\":" + String(ESP.getFreeSketchSpace());
    j += ",\"ota\":" + String(Update.isRunning() ? 1 : 0);
    j += "}";
    server.send(200, "application/json", j);
}
//**************************************************************************************************
void handleScan()
{
    if (!webAuthOk()) { server.requestAuthentication(); return; }
    int n = WiFi.scanNetworks(false, true);   // include hidden SSIDs
    String j;
    j.reserve(1600);
    j = "{\"networks\":[";
    if (n < 0)
        j += "],\"error\":\"scan failed\"}";
    else
    {
        for (int i = 0; i < n; i++)
        {
            if (i) j += ",";
            j += "{\"ssid\":\"" + jsonEsc(WiFi.SSID(i).c_str()) + "\"";
            j += ",\"rssi\":" + String(WiFi.RSSI(i));
            j += ",\"enc\":" + String((int) WiFi.encryptionType(i));
            j += "}";
        }
        j += "]}";
    }
    WiFi.scanDelete();
    server.keepAlive(false);
    server.send(200, "application/json", j);
}
//**************************************************************************************************
void handleReset()
{
    if (!webAuthOk()) { server.requestAuthentication(); return; }
    settings = settings_defaults;
    // Explicit defaults requested for these fields:
    strncpy(settings.ntpServer, "il.pool.ntp.org", sizeof(settings.ntpServer) - 1);
    settings.ntpServer[sizeof(settings.ntpServer) - 1] = '\0';
    settings.rainWarn = 1;
    settings.heatWarn = 1;
    settings.rainThresh = 2;
    settings.heatThresh = 31;
    settings.rainThresh10 = 20;
    settings.heatThresh10 = 310;
    settings.threshFormat = 0xA55A;
    settings.weatherSource = 0;
    settings.imsStation = 0;
    settings.imsFormat = 0xBEEF;
    settings_save();
    Serial.printf("Settings reset: NTP=%s rain=%d/%smm heat=%d/%sC\r\n",
                  settings.ntpServer, settings.rainWarn, tenthsStr(settings.rainThresh10).c_str(),
                  settings.heatWarn, tenthsStr(settings.heatThresh10).c_str());
    server.keepAlive(false);
    server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Settings reset. Rebooting...\"}");
    otaRebootAt = millis() + 2000UL;
}
//**************************************************************************************************
void handleSave()
{
    if (!webAuthOk()) { server.requestAuthentication(); return; }
    strncpy(settings.ssid, server.arg("ssid").c_str(), sizeof(settings.ssid) - 1);
    settings.ssid[sizeof(settings.ssid) - 1] = '\0';
    if (server.arg("pass").length() > 0)   // blank field = keep the saved password
    {
        strncpy(settings.pass, server.arg("pass").c_str(), sizeof(settings.pass) - 1);
        settings.pass[sizeof(settings.pass) - 1] = '\0';
    }
    if (server.hasArg("webPass"))
    {
        String wp = server.arg("webPass");
        if (wp != "********")   // masked value = keep the current password; blank = disable login
        {
            strncpy(settings.webPass, wp.c_str(), sizeof(settings.webPass) - 1);
            settings.webPass[sizeof(settings.webPass) - 1] = '\0';
        }
    }
    strncpy(settings.ntpServer, server.arg("ntp").c_str(), sizeof(settings.ntpServer) - 1);
    settings.ntpServer[sizeof(settings.ntpServer) - 1] = '\0';
    settings.tzOffset = (int8_t) constrain(server.arg("tz").toInt(), -12, 14);
    settings.brightness = (uint8_t) constrain(server.arg("bright").toInt(), 0, 15);
    settings.orient = (uint8_t) constrain(server.arg("orient").toInt(), 0, 2);
    settings.scrollDate = (uint8_t) (server.arg("scrollDate").toInt() ? 1 : 0);
    settings.scrollDateInt = (uint16_t) constrain(server.arg("scrollDateInt").toInt(), 1, 120);
    settings.scrollDir = (uint8_t) (server.arg("scrollDir").toInt() ? 1 : 0);
    settings.colonAnim = (uint8_t) constrain(server.arg("colonAnim").toInt(), 0, 4);
    if (server.hasArg("digitAnim")) settings.digitAnim = (uint8_t) constrain(server.arg("digitAnim").toInt(), 0, 5);
    settings.marquee = 0;   // removed from settings menu, keep disabled
    settings.pulse = 0;
    settings.resync = (uint8_t) (server.arg("resync").toInt() ? 1 : 0);
    settings.resyncHour = (uint8_t) constrain(server.arg("resyncHour").toInt(), 0, 23);
    settings.resyncMin = (uint8_t) constrain(server.arg("resyncMin").toInt(), 0, 59);
    settings.ntpInterval = (uint16_t) constrain(server.arg("ntpInterval").toInt(), 0, 720);
    settings.weather = (uint8_t) (server.arg("weather").toInt() ? 1 : 0);
    strncpy(settings.weatherCity, server.arg("weatherCity").c_str(), sizeof(settings.weatherCity) - 1);
    settings.weatherCity[sizeof(settings.weatherCity) - 1] = '\0';
    if (server.hasArg("weatherCityShow")) settings.weatherCityShow = (uint8_t) (server.arg("weatherCityShow").toInt() ? 1 : 0);
    settings.weatherInterval = (uint16_t) constrain(server.arg("weatherInterval").toInt(), 1, 240);
    settings.weatherSpeed = (uint8_t) constrain(server.arg("weatherSpeed").toInt(), 1, 4);
    settings.rainWarn = (uint8_t) (server.arg("rainWarn").toInt() ? 1 : 0);
    settings.heatWarn = (uint8_t) (server.arg("heatWarn").toInt() ? 1 : 0);
    String rainArg = server.arg("rainThresh");
    rainArg.replace(',', '.');   // accept 1,2 as well as 1.2
    float rainVal = rainArg.toFloat();
    if (rainVal > 0)
    {
        if (rainVal < 0.1f) rainVal = 0.1f;
        if (rainVal > 25.0f) rainVal = 25.0f;
        settings.rainThresh10 = (uint16_t) (rainVal * 10.0f + 0.5f);
    }
    String heatArg = server.arg("heatThresh");
    heatArg.replace(',', '.');
    float heatVal = heatArg.toFloat();
    if (heatVal > 0)
    {
        if (heatVal < 20.0f) heatVal = 20.0f;
        if (heatVal > 50.0f) heatVal = 50.0f;
        settings.heatThresh10 = (uint16_t) (heatVal * 10.0f + 0.5f);
    }
    settings.rainThresh = (uint8_t) constrain((settings.rainThresh10 + 5) / 10, 1, 25);   // keep legacy field in sync
    settings.heatThresh = (uint8_t) constrain((settings.heatThresh10 + 5) / 10, 20, 50);
    settings.threshFormat = 0xA55A;
    if (server.hasArg("weatherSource")) settings.weatherSource = (uint8_t) constrain(server.arg("weatherSource").toInt(), 0, 2);
    if (server.hasArg("imsStation")) settings.imsStation = (uint16_t) constrain(server.arg("imsStation").toInt(), 0, 9999);
    settings.imsFormat = 0xBEEF;
    if (server.hasArg("warnAnim")) settings.warnAnim = (uint8_t) (server.arg("warnAnim").toInt() ? 1 : 0);
    if (server.hasArg("shabbat")) settings.shabbat = (uint8_t) (server.arg("shabbat").toInt() ? 1 : 0);
    if (server.hasArg("shabbatFreq")) settings.shabbatFreq = (uint8_t) constrain(server.arg("shabbatFreq").toInt(), 5, 60);
    if (server.hasArg("shabbatLead")) settings.shabbatLead = (uint8_t) constrain(server.arg("shabbatLead").toInt(), 10, 240);
    if (server.hasArg("shabbatAnim")) settings.shabbatAnim = (uint8_t) (server.arg("shabbatAnim").toInt() ? 1 : 0);
    settings.holidayGreetings = (uint8_t) (server.arg("holidayGreetings").toInt() ? 1 : 0);
    settings.dstRule = (uint8_t) constrain(server.arg("dst").toInt(), 0, 3);
    settings_save();
    Serial.printf("Settings saved, SSID:%s PSW:%s WPAUTH:%s\r\n", settings.ssid, passHidden(settings.pass), settings.webPass[0] ? "set" : "none");
    sendChunked(200, "text/html", "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<meta name=theme-color content='#0a0d0b'>"
        "<meta http-equiv=\"refresh\" content=\"15; url=/\"><title>Saved</title><style>"
        "body{margin:0;background:#0a0d0b;color:#e8f0ec;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;text-align:center;padding:72px 24px;min-height:100vh;box-sizing:border-box}"
        ".ok{font-size:44px;color:#22e07a;text-shadow:0 0 20px rgba(34,224,122,.45)}"
        "h2{color:#22e07a;font-size:20px;margin:14px 0 6px}"
        "p{color:#7c8c84;font-size:14px}"
        "</style></head><body>"
        "<div class=ok>&#10003;</div>"
        "<h2>Settings saved</h2><p>Restarting the clock in a few seconds...</p>"
        "</body></html>");
    server.client().stop();   // close TCP cleanly so the browser gets the full response
    delay(2000);
    ESP.restart();
}
//**************************************************************************************************
//***************************Web Firmware Update**********************************************************************
void handleUpdatePage()
{
    if (!webAuthOk()) { server.requestAuthentication(); return; }
    static const char UPDATE_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>
<meta name=theme-color content='#0a0d0b'><title>MatrixClock Update</title><style>
:root{--bg:#0a0d0b;--panel:#121614;--panel2:#171c19;--line:#232a26;--accent:#22e07a;--accentd:#1a9c58;--text:#e8f0ec;--sub:#7c8c84}
*{box-sizing:border-box}html,body{margin:0}body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;-webkit-tap-highlight-color:transparent}
.wrap{max-width:480px;margin:0 auto;padding:24px 16px}h1{font-size:20px;text-align:center;color:var(--accent);margin:4px 0 20px;text-shadow:0 0 16px rgba(34,224,122,.35)}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:18px}
p{font-size:13.5px;line-height:1.5;color:var(--sub);margin:0 0 16px}
input[type=file]{width:100%;padding:12px;font-size:14px;color:var(--text);background:var(--panel2);border:1px solid var(--line);border-radius:9px;margin-bottom:14px}
button{width:100%;padding:15px;font-size:15.5px;font-weight:600;background:var(--accent);color:#04150b;border:none;border-radius:11px}
button:disabled{opacity:.55}.bar{height:16px;background:var(--panel2);border:1px solid var(--line);border-radius:8px;overflow:hidden;margin:18px 0 8px}
.fill{height:100%;width:0;background:linear-gradient(90deg,var(--accentd),var(--accent));transition:width .2s}
.fill.err{background:#e05252}.status{font-size:13.5px;color:var(--sub);min-height:20px}
.status.ok{color:var(--accent)}.status.err{color:#ff6b6b}a{color:var(--accent);text-decoration:none}
.back{text-align:center;margin-top:18px;font-size:13px}
</style></head><body><div class=wrap>
<h1>&#8679; Firmware Update</h1>
<div class=card>
<p>Select the MatrixClock <b>.bin</b> firmware file. Keep the clock powered during the update.</p>
<form id=f><input id=fw type=file accept='.bin' required><button id=btn type=submit>Upload &amp; Update</button></form>
<div class=bar><div id=fill class=fill></div></div>
<div id=st class=status>Ready.</div>
<p class=back><a href='/apupdate'>Update over direct Wi-Fi (AP)</a> &middot; <a href='/reboot'>Reboot</a><br><a href='/'>Back to settings</a></p>
</div></div>
<script>
var f=document.getElementById('f'),fw=document.getElementById('fw'),btn=document.getElementById('btn'),fill=document.getElementById('fill'),st=document.getElementById('st');
f.addEventListener('submit',function(e){
 e.preventDefault();
 if(!fw.files.length){st.className='status err';st.textContent='Choose a .bin file first.';return;}
 var fd=new FormData();fd.append('firmware',fw.files[0]);
 var xhr=new XMLHttpRequest();
 xhr.open('POST','/update',true);xhr.timeout=120000;
 btn.disabled=true;fw.disabled=true;fill.style.width='0%';fill.className='fill';st.className='status';st.textContent='Starting upload...';
 xhr.upload.onprogress=function(ev){if(ev.lengthComputable){var pct=Math.min(100,Math.round(ev.loaded/ev.total*100));fill.style.width=pct+'%';st.textContent=pct<100?'Uploading... '+pct+'%':'Writing firmware...';}};
 xhr.onload=function(){
  var r={ok:false,msg:'Invalid response from clock'};
  try{r=JSON.parse(xhr.responseText);}catch(err){}
  if(r.ok){fill.style.width='100%';fill.className='fill';st.className='status ok';st.textContent=r.msg;setTimeout(function(){location.href='/';},15000);}
  else{fill.className='fill err';st.className='status err';st.textContent='Update failed: '+r.msg;btn.disabled=false;fw.disabled=false;}
 };
 xhr.onerror=function(){fill.className='fill err';st.className='status err';st.textContent='Connection lost. Check the clock and try again.';btn.disabled=false;fw.disabled=false;};
 xhr.ontimeout=function(){fill.className='fill err';st.className='status err';st.textContent='Update timed out.';btn.disabled=false;fw.disabled=false;};
 xhr.send(fd);
});
</script></body></html>
)HTML";
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.keepAlive(false);
    sendChunked_P(200, "text/html", UPDATE_PAGE);
}

void handleAPUpdate()
{
    if (!webAuthOk()) { server.requestAuthentication(); return; }
    if (Update.isRunning()) { server.send(503, "text/plain", "Update in progress"); return; }
    server.keepAlive(false);
    server.send(200, "text/html", "<!DOCTYPE html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>AP Update</title></head><body style='margin:0;background:#0a0d0b;color:#e8f0ec;"
        "font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Arial,sans-serif;padding:48px 20px;text-align:center'>"
        "<h2 style='color:#22e07a'>Switching to setup Wi-Fi...</h2>"
        "<p style='color:#7c8c84;font-size:15px;line-height:1.6'>Connect your phone or computer to the Wi-Fi network<br>"
        "<b style='color:#e8f0ec'>MatrixClock-Setup</b> with password <b style='color:#e8f0ec'>12345678</b><br>"
        "then open <b style='color:#22e07a'>http://192.168.4.1/update</b></p>"
        "</body></html>");
    apUpdatePending = millis() + 1500UL;   // switch after the response is flushed
}

void handleReboot()
{
    if (!webAuthOk()) { server.requestAuthentication(); return; }
    if (Update.isRunning()) { server.send(503, "text/plain", "Update in progress"); return; }
    server.keepAlive(false);
    server.send(200, "text/plain", "Rebooting...");
    otaRebootAt = millis() + 800UL;
}

void handleUpdateUpload()
{
    HTTPUpload& upload = server.upload();
    otaLastProgressMs = millis();
    if (upload.status == UPLOAD_FILE_START)
    {
        otaUploadStarted = false;
        otaUploadSuccess = false;
        otaUploadError[0] = 0;
        otaWatchdogActive = true;
        if (!webAuthOk())
        {
            snprintf(otaUploadError, sizeof otaUploadError, "Unauthorized");
            otaWatchdogActive = false;
            return;
        }
        WiFiUDP::stopAll();                    // free the NTP socket/heap during the update
        WiFi.setSleepMode(WIFI_NONE_SLEEP);    // no modem sleep while streaming firmware
        clear_Display();                       // blank the matrix: lower current during flash writes
        Serial.printf("OTA: receiving %s\r\n", upload.filename.c_str());
        if (upload.filename.length() == 0)
            snprintf(otaUploadError, sizeof otaUploadError, "No firmware file selected");
        else
        {
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (Update.begin(maxSketchSpace, U_FLASH))
                otaUploadStarted = true;
            else
            {
                Update.printError(Serial);
                snprintf(otaUploadError, sizeof otaUploadError, "Not enough flash space or update could not start");
            }
        }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (!otaUploadStarted) return;
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            Update.printError(Serial);
            snprintf(otaUploadError, sizeof otaUploadError, "Flash write failed");
            Update.end();
            otaUploadStarted = false;
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (otaUploadStarted)
        {
            if (Update.end(true))
                otaUploadSuccess = true;
            else
            {
                Update.printError(Serial);
                snprintf(otaUploadError, sizeof otaUploadError, "Update verification failed");
            }
            otaUploadStarted = false;
        }
        Serial.printf("OTA: finished, success=%d, bytes=%u\r\n", otaUploadSuccess, (unsigned) upload.totalSize);
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        Update.end();
        otaUploadStarted = false;
        otaWatchdogActive = false;
        snprintf(otaUploadError, sizeof otaUploadError, "Upload aborted");
        Serial.println("OTA: aborted");
    }
    yield();   // let the WiFi stack and watchdog run between flash-write chunks
}

void handleUpdateDone()
{
    otaWatchdogActive = false;
    if (!webAuthOk())
    {
        if (Update.isRunning()) Update.end();
        server.keepAlive(false);
        server.send(401, "application/json", "{\"ok\":false,\"msg\":\"Unauthorized\"}");
        return;
    }
    if (otaUploadSuccess)
    {
        Serial.println("OTA: success, rebooting");
        startMsg("UPDATE OK");
        otaRebootAt = millis() + 5000UL;
        server.keepAlive(false);
        server.send(200, "application/json",
                    "{\"ok\":true,\"msg\":\"Firmware updated successfully. Rebooting...\"}");
    }
    else
    {
        if (Update.isRunning())
            Update.end();   // make sure a failed update can be retried without rebooting
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
        udp.begin(localPort);   // restore the NTP socket after a failed/aborted update
        if (otaUploadError[0] == 0)
            snprintf(otaUploadError, sizeof otaUploadError, "Update failed");
        Serial.printf("OTA: failed: %s\r\n", otaUploadError);
        startMsg("UPDATE FAIL");
        String j = "{\"ok\":false,\"msg\":\"";
        j += jsonEsc(otaUploadError);
        j += "\"}";
        server.keepAlive(false);
        server.send(200, "application/json", j);
    }
}
//**************************************************************************************************
//***************************Weather (Open-Meteo, free, no API key)**********************************************************************
#define IMS_XML_URL "https://ims.gov.il/sites/default/files/ims_data/xml_files/imslasthour.xml"
#define IMS_XML_MARGIN 25000UL   // byte window before the cached station offset

struct ImsStationInfo {
    uint16_t id;
    int32_t lat;       // degrees * 1e6
    int32_t lon;       // degrees * 1e6
    uint32_t offset;   // approximate byte offset in imslasthour.xml
    const char* name;
};
static const ImsStationInfo imsStations[] PROGMEM = {
    { 78, 32846600, 35112300, 96251UL, "AFEQ" },
    { 16, 32596001, 35277000, 18199UL, "AFULA NIR HAEMEQ" },
    { 123, 32915000, 35513500, 128230UL, "AMMIAD" },
    { 29, 31250000, 35186001, 40941UL, "ARAD" },
    { 21, 32105600, 35211400, 23763UL, "ARIEL" },
    { 124, 31834200, 34637400, 130587UL, "ASHDOD PORT" },
    { 208, 31639400, 34521500, 153310UL, "ASHQELON PORT" },
    { 271, 30787600, 34771000, 188784UL, "AVDAT" },
    { 2, 32817001, 35762001, 1016UL, "AVNE ETAN" },
    { 353, 33023998, 35574001, 207448UL, "AYYELET HASHAHAR" },
    { 411, 31264200, 34804500, 223172UL, "BEER SHEVA BGU" },
    { 75, 31725000, 34976002, 91075UL, "BEIT JIMAL" },
    { 58, 31271999, 34389000, 71870UL, "BESOR FARM" },
    { 54, 32007000, 34813999, 68951UL, "BET DAGAN" },
    { 228, 31805200, 35501000, 167486UL, "BET HAARAVA" },
    { 6, 32880000, 35650400, 3804UL, "BET ZAYDA" },
    { 499, 33227700, 35635000, 226107UL, "DAFNA" },
    { 99, 32862000, 35374000, 112592UL, "DEIR HANNA" },
    { 79, 31503600, 34648000, 99090UL, "DOROT" },
    { 206, 32467998, 35488998, 147566UL, "EDEN FARM" },
    { 64, 29553000, 34952000, 77726UL, "ELAT" },
    { 73, 33064999, 35216999, 85912UL, "ELON" },
    { 107, 32388000, 34938000, 117786UL, "EN HAHORESH" },
    { 67, 32603001, 35096001, 83106UL, "EN HASHOFET" },
    { 44, 32681000, 34959000, 60439UL, "EN KARMEL" },
    { 205, 32884400, 35301500, 144768UL, "ESHHAR" },
    { 338, 30791100, 34471500, 196627UL, "EZUZ" },
    { 263, 32556400, 35072500, 184032UL, "GALED" },
    { 227, 32905200, 35748700, 164640UL, "GAMLA" },
    { 236, 31629999, 34791000, 175959UL, "GAT" },
    { 30, 31997000, 35451000, 43721UL, "GILGAL" },
    { 46, 32473000, 34882000, 66143UL, "HADERA PORT" },
    { 121, 31791000, 34805000, 125361UL, "HAFEZ HAYYIM" },
    { 41, 32803400, 35054800, 52172UL, "HAIFA REFINERIES" },
    { 43, 32773600, 35022300, 57502UL, "HAIFA TECHNION" },
    { 42, 32761002, 35021000, 54636UL, "HAIFA UNIVERSITY" },
    { 275, 32130000, 34832400, 194371UL, "HAKFAR HAYAROK" },
    { 24, 31944900, 35149900, 32450UL, "HAR HARASHA" },
    { 269, 32956001, 35328999, 186425UL, "HARASHIM" },
    { 33, 30778999, 35238998, 46565UL, "HAZEVA" },
    { 90, 32159800, 35351300, 107040UL, "ITAMAR" },
    { 23, 31781000, 35222000, 29556UL, "JERUSALEM CENTRE" },
    { 22, 31770400, 35197300, 26567UL, "JERUSALEM GIVAT RAM" },
    { 202, 33172001, 35612999, 141910UL, "KEFAR BLUM" },
    { 241, 33240400, 35569600, 178344UL, "KEFAR GILADI" },
    { 233, 32883300, 35579200, 173134UL, "KEFAR NAHUM" },
    { 350, 31381200, 34872900, 205043UL, "LAHAV" },
    { 218, 31774000, 35296001, 158987UL, "MAALE ADUMMIM" },
    { 224, 32481000, 35415000, 161823UL, "MAALE GILBOA" },
    { 355, 32683300, 35600800, 209879UL, "MASSADA" },
    { 10, 33129002, 35805000, 9525UL, "MEROM GOLAN PICMAN" },
    { 210, 31588100, 35391600, 156153UL, "METZOKE DRAGOT" },
    { 259, 31834100, 34961600, 181177UL, "NAHSHON" },
    { 82, 31659000, 34680000, 101918UL, "NEGBA" },
    { 232, 30048000, 35023300, 170308UL, "NEOT SMADAR" },
    { 25, 31690000, 34973999, 35285UL, "NETIV HALAMED HE" },
    { 349, 31205000, 34922700, 202243UL, "NEVATIM" },
    { 186, 32708000, 35178001, 136243UL, "NEWE YAAR" },
    { 274, 31731900, 34634800, 191575UL, "NIZZAN" },
    { 207, 30365999, 35147999, 150465UL, "PARAN" },
    { 20, 32174999, 35096001, 20929UL, "QARNE SHOMERON" },
    { 74, 31816600, 34724400, 88690UL, "QEVUZAT YAVNE" },
    { 106, 33080600, 35107900, 114964UL, "ROSH HANIQRA" },
    { 77, 31664000, 35123001, 93449UL, "ROSH ZURIM" },
    { 98, 30870001, 34794998, 109780UL, "SEDE BOQER" },
    { 366, 32438999, 35511002, 212277UL, "SEDE ELIYYAHU" },
    { 65, 31031000, 35391998, 80267UL, "SEDOM" },
    { 28, 31357000, 35066002, 38158UL, "SHANI" },
    { 343, 32983600, 35092300, 199411UL, "SHAVE ZIYYON" },
    { 13, 32705002, 35405998, 15269UL, "TAVOR KADOORIE" },
    { 178, 32058000, 34758800, 133404UL, "TEL AVIV COAST" },
    { 11, 32698002, 35509998, 12426UL, "YAVNEEL" },
    { 36, 29885000, 35077000, 49219UL, "YOTVATA" },
    { 62, 32979999, 35507000, 74832UL, "ZEFAT HAR KENAAN" },
    { 8, 32702000, 35584000, 6642UL, "ZEMAH" },
    { 45, 32572400, 34954600, 63308UL, "ZIKHRON YAAQOV" },
    { 112, 31070800, 34851300, 120210UL, "ZOMET HANEGEV" },
    { 188, 31787800, 35125800, 139114UL, "ZOVA" },
};
#define IMS_STATION_COUNT (sizeof(imsStations) / sizeof(imsStations[0]))
//**************************************************************************************************
String imsStationOptions()
{
    String out;
    out.reserve(2400);
    out = "[[\"0\",\"Auto (nearest)\"]";
    char name[28];
    for (uint16_t i = 0; i < IMS_STATION_COUNT; i++)
    {
        const char* nm = (const char*) pgm_read_ptr(&imsStations[i].name);
        strncpy_P(name, nm, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        out += ",[\"";
        out += String(pgm_read_word(&imsStations[i].id));
        out += "\",\"";
        out += name;
        out += "\"]";
    }
    out += "]";
    return out;
}
String urlEncode(const char* s)
{
    String out;
    while (*s)
    {
        if (isalnum((unsigned char) *s) || *s == '-' || *s == '_' || *s == '.' || *s == '~')
            out += *s;
        else
        {
            char hex[4];
            snprintf(hex, sizeof hex, "%%%02X", (unsigned char) *s);
            out += hex;
        }
        s++;
    }
    return out;
}
//**************************************************************************************************
double jsonDoubleAfter(const String& s, const char* key)   // parse the number following the LAST "key" (values are after current_units strings)
{
    int i = s.lastIndexOf(key);
    if (i < 0) return 0;
    return atof(s.c_str() + i + strlen(key));
}
//**************************************************************************************************
bool httpGet(const String& url, String& out)   // plain-HTTP GET (Open-Meteo supports port 80, no TLS needed)
{
    WiFiClient client;
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    http.setTimeout(4000);   // don't let a stalled weather/geocode request block the web UI forever
    int code = http.GET();
    if (code != 200) { http.end(); return false; }
    out = http.getString();
    http.end();
    return true;
}
//**************************************************************************************************
bool geocodeCity()   // resolve settings.weatherCity to cached latitude/longitude
{
    if (settings.weatherCity[0] == 0) return false;
    String url = String("http://geocoding-api.open-meteo.com/v1/search?name=") + urlEncode(settings.weatherCity) + "&count=1&language=en";
    String resp;
    if (!httpGet(url, resp)) { Serial.println("Weather: geocode request failed"); return false; }
    double lat = jsonDoubleAfter(resp, "\"latitude\":");
    double lon = jsonDoubleAfter(resp, "\"longitude\":");
    if (lat == 0 && lon == 0) { Serial.println("Weather: city not found"); return false; }
    settings.weatherLat = (int32_t) (lat * 1000000);
    settings.weatherLon = (int32_t) (lon * 1000000);
    settings_save();
    Serial.printf("Weather: geocoded %s -> lat %d lon %d\r\n", settings.weatherCity, settings.weatherLat, settings.weatherLon);
    return true;
}

bool fetchWeather(double& temp, int& hum, double& precip, double& rainNext, int& wmoCode)   // resolve the city once, then read current conditions + next-hour rain
{
    static char geocodedCity[sizeof(settings.weatherCity)] = "";
    if (strcmp(geocodedCity, settings.weatherCity) != 0)
    {
        if (!geocodeCity()) return false;
        strncpy(geocodedCity, settings.weatherCity, sizeof(geocodedCity) - 1);
        geocodedCity[sizeof(geocodedCity) - 1] = 0;
    }
    char latbuf[16];
    char lonbuf[16];
    snprintf(latbuf, sizeof latbuf, "%.5f", settings.weatherLat / 1000000.0);
    snprintf(lonbuf, sizeof lonbuf, "%.5f", settings.weatherLon / 1000000.0);
    String url = String("http://api.open-meteo.com/v1/forecast?latitude=") + latbuf + "&longitude=" + lonbuf
               + "&current=temperature_2m,relative_humidity_2m,precipitation,weather_code"
               + "&minutely_15=precipitation,weather_code&forecast_minutely_15=8";
    String resp;
    if (!httpGet(url, resp)) { Serial.println("Weather: forecast request failed"); return false; }
    temp = jsonDoubleAfter(resp, "\"temperature_2m\":");
    hum = (int) jsonDoubleAfter(resp, "\"relative_humidity_2m\":");
    int curSec = resp.indexOf("\"current\":");
    int precipKey = (curSec >= 0) ? resp.indexOf("\"precipitation\":", curSec) : -1;
    double precipRaw = (precipKey >= 0) ? atof(resp.c_str() + precipKey + (int) strlen("\"precipitation\":")) : 0;
    int interval = (int) jsonDoubleAfter(resp, "\"interval\":");   // current value is a 15-min sum
    if (interval <= 0) interval = 900;
    precip = precipRaw * 3600.0 / interval;   // normalize to mm/h so it matches the threshold
    int wmoKey = (curSec >= 0) ? resp.indexOf("\"weather_code\":", curSec) : -1;
    wmoCode = (wmoKey >= 0) ? (int) atof(resp.c_str() + wmoKey + (int) strlen("\"weather_code\":")) : 0;
    // Peak rain rate over the next two hours. The 15-min values are sums, so
    // multiply by 4 to get mm/h; this also gives a rolling 2-hour window.
    rainNext = 0;
    int mp = resp.indexOf("\"minutely_15\":");
    if (mp >= 0)
    {
        mp = resp.indexOf("\"precipitation\":[", mp);
        if (mp >= 0)
        {
            mp += (int) strlen("\"precipitation\":[");
            int end = resp.indexOf(']', mp);
            for (int i = 0; i < 8 && mp < end; i++)
            {
                double v = atof(resp.c_str() + mp) * 4.0;
                if (v > rainNext) rainNext = v;
                int comma = resp.indexOf(',', mp);
                if (comma < 0 || comma >= end) break;
                mp = comma + 1;
            }
        }
    }
    Serial.printf("Weather: %.1fC %d%% now %.1fmm/h peak2h %.1fmm/h code %d\r\n", temp, hum, precip, rainNext, wmoCode);
    weatherTempC = (float) temp;
    weatherHumidity = hum;
    weatherPrecipRate = (float) precip;
    weatherRainPeak = (float) rainNext;
    weatherCode = wmoCode;
    weatherUpdatedMs = millis();
    strncpy(weatherDataCity, settings.weatherCity, sizeof(weatherDataCity) - 1);
    weatherDataCity[sizeof(weatherDataCity) - 1] = 0;
    weatherDataValid = true;
    return true;
}
//**************************************************************************************************
bool imsStationById(uint16_t id, uint32_t& offset, char* name, size_t nameLen, float& dist)
{
    for (uint16_t i = 0; i < IMS_STATION_COUNT; i++)
    {
        if (pgm_read_word(&imsStations[i].id) == id)
        {
            offset = pgm_read_dword(&imsStations[i].offset);
            const char* nm = (const char*) pgm_read_ptr(&imsStations[i].name);
            strncpy_P(name, nm, nameLen - 1);
            name[nameLen - 1] = 0;
            dist = 0;
            return true;
        }
    }
    return false;
}
//**************************************************************************************************
bool imsNearestStation(uint16_t& id, uint32_t& offset, char* name, size_t nameLen, float& dist)
{
    if (settings.weatherLat == 0 && settings.weatherLon == 0) return false;
    float lat0 = settings.weatherLat / 1000000.0f;
    float lon0 = settings.weatherLon / 1000000.0f;
    float best = 1e9f;
    int bestIdx = -1;
    for (uint16_t i = 0; i < IMS_STATION_COUNT; i++)
    {
        float lat = (int32_t) pgm_read_dword(&imsStations[i].lat) / 1000000.0f;
        float lon = (int32_t) pgm_read_dword(&imsStations[i].lon) / 1000000.0f;
        float dlat = (lat - lat0) * 111.0f;
        float dlon = (lon - lon0) * 111.0f * cosf(lat0 * 0.0174532925f);
        float d = dlat * dlat + dlon * dlon;
        if (d < best)
        {
            best = d;
            bestIdx = i;
        }
    }
    if (bestIdx < 0) return false;
    id = pgm_read_word(&imsStations[bestIdx].id);
    offset = pgm_read_dword(&imsStations[bestIdx].offset);
    const char* nm = (const char*) pgm_read_ptr(&imsStations[bestIdx].name);
    strncpy_P(name, nm, nameLen - 1);
    name[nameLen - 1] = 0;
    dist = sqrtf(best);
    return true;
}
//**************************************************************************************************
bool fetchImsXmlRain()   // read a byte range from the IMS 10-minute XML and extract the selected station
{
    if (settings.weatherSource == 0 || WiFi.status() != WL_CONNECTED) return false;

    uint16_t stationId = settings.imsStation;
    uint32_t stationOffset = 0;
    char stationName[28] = "";
    float stationDistance = 0;
    bool found = false;
    if (stationId == 0)
    {
        static char imsGeocodedCity[sizeof(settings.weatherCity)] = "";
        if (settings.weatherCity[0] && strcmp(imsGeocodedCity, settings.weatherCity) != 0)
        {
            if (geocodeCity())
            {
                strncpy(imsGeocodedCity, settings.weatherCity, sizeof(imsGeocodedCity) - 1);
                imsGeocodedCity[sizeof(imsGeocodedCity) - 1] = 0;
            }
        }
        found = imsNearestStation(stationId, stationOffset, stationName, sizeof(stationName), stationDistance);
    }
    else
        found = imsStationById(stationId, stationOffset, stationName, sizeof(stationName), stationDistance);
    if (!found)
    {
        Serial.println("IMS XML: no matching station (set a station ID or Weather City)");
        return false;
    }

    unsigned long rangeStart = stationOffset > IMS_XML_MARGIN ? stationOffset - IMS_XML_MARGIN : 0;

    WiFiClientSecure client;
    client.setInsecure();          // IMS uses a public certificate; skip the large cert store
    client.setBufferSizes(1024, 512);

    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, IMS_XML_URL))
    {
        Serial.println("IMS XML: begin failed");
        return false;
    }
    http.addHeader("Range", String("bytes=") + String(rangeStart) + "-");
    http.addHeader("Accept-Encoding", "identity");
    int code = http.GET();
    if (code != 206 && code != 200)
    {
        Serial.printf("IMS XML: HTTP %d\r\n", code);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    if (!stream) { http.end(); return false; }

    char line[128];
    size_t lineLen = 0;
    bool inObs = false;
    bool target = false;
    uint8_t obsCount = 0;
    float lastRain = 0;
    float lastTemp = 0;
    int lastHum = 0;
    bool gotTemp = false;
    bool gotHum = false;
    char lastTime[24] = "";
    bool got = false;
    bool finished = false;
    unsigned long started = millis();

    while (!finished && millis() - started < 12000UL)
    {
        while (stream->available())
        {
            int c = stream->read();
            if (c < 0) break;
            if (c == '\r') continue;
            if (c == '\n')
            {
                line[lineLen] = 0;
                if (strstr(line, "<Observation>"))
                {
                    inObs = true;
                    target = false;
                }
                if (inObs)
                {
                    char* p;
                    if ((p = strstr(line, "<stn_num>")) != NULL)
                        target = (atoi(p + 9) == stationId);
                    if (target)
                    {
                        if ((p = strstr(line, "<time_obs>")) != NULL)
                        {
                            p += 10;
                            size_t i = 0;
                            while (p[i] && p[i] != '<' && i < sizeof(lastTime) - 1)
                            {
                                lastTime[i] = p[i];
                                i++;
                            }
                            lastTime[i] = 0;
                        }
                        else if (strstr(line, "<Rain/>") != NULL)
                            lastRain = 0;
                        else if ((p = strstr(line, "<Rain>")) != NULL)
                            lastRain = atof(p + 6);
                        else if ((p = strstr(line, "<TD>")) != NULL)
                        {
                            lastTemp = atof(p + 4);
                            gotTemp = true;
                        }
                        else if ((p = strstr(line, "<RH>")) != NULL)
                        {
                            lastHum = atoi(p + 4);
                            gotHum = true;
                        }
                    }
                    if (strstr(line, "</Observation>"))
                    {
                        if (target)
                        {
                            obsCount++;
                            got = true;
                            if (obsCount >= 6)
                            {
                                finished = true;
                                break;
                            }
                        }
                        inObs = false;
                    }
                }
                lineLen = 0;
            }
            else if (lineLen < sizeof(line) - 1)
                line[lineLen++] = (char) c;
        }
        if (finished) break;
        if (!stream->connected() && !stream->available()) break;
        delay(1);
    }
    http.end();

    if (!got)
    {
        Serial.println("IMS XML: station not found");
        return false;
    }
    imsRainRate = lastRain * 6.0f;   // XML is a 10-minute sum -> mm/h
    imsTempC = lastTemp;
    imsHumidity = lastHum;
    imsTempValid = gotTemp;
    imsHumValid = gotHum;
    imsUpdatedMs = millis();
    imsStationId = stationId;
    strncpy(imsStationName, stationName, sizeof(imsStationName) - 1);
    imsStationName[sizeof(imsStationName) - 1] = 0;
    imsStationDistanceKm = stationDistance;
    strncpy(imsObsTime, lastTime, sizeof(imsObsTime) - 1);
    imsObsTime[sizeof(imsObsTime) - 1] = 0;
    imsRainValid = true;
    Serial.printf("IMS XML: %s #%d %.1f mm/10min = %.1f mm/h at %s\r\n",
                  imsStationName, imsStationId, lastRain, imsRainRate, imsObsTime);
    return true;
}
//**************************************************************************************************
//***********************WiFi Reconnect Logic**********************************************************************
// starts a reconnect attempt (non-blocking, result is checked in loop())
void wifi_reconnect_start()
{
    Serial.println("WiFi disconnected, starting reconnect...");
    if (strlen(settings.ssid) > 0)
        Serial.printf("Using settings SSID:%s\r\n", settings.ssid);
    else
        Serial.println("Using saved flash config");
    WiFi.disconnect(false, false);   // stop any in-flight attempt, keep stored credentials
    delay(200);
    if (!apActive)     // while the fallback AP is up, stay in AP_STA mode so the AP stays reachable
    {
        WiFi.mode(WIFI_STA);
        delay(100);
    }
    WiFi.setAutoReconnect(true);   // re-enable the SDK reconnect so the connection attempt can complete
    if (strlen(settings.ssid) > 0)
    {
        WiFi.persistent(false);
        WiFi.begin(settings.ssid, settings.pass);   // credentials from settings
    }
    else
    {
        WiFi.persistent(true);
        WiFi.begin();                               // use config saved by smartConfig
    }
}
//**************************************************************************************************
//***************************Daylight Saving Time**********************************************************************
int dow(int y, int m, int d)   // day of week, 0 = Sunday (Sakamoto)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
int daysInMonth(int y, int m)
{
    static const int dim[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dim[m - 1];
}
int firstSunday(int y, int m)
{
    return 1 + ((7 - dow(y, m, 1)) % 7);
}
int lastSunday(int y, int m)
{
    return daysInMonth(y, m) - dow(y, m, daysInMonth(y, m));
}
bool dst_is_active(int y, int m, int d, int h, uint8_t rule)   // h = hour of the UTC time
{
    int startM = 0, startD = 0, startH = 0, endM = 0, endD = 0, endH = 0;
    switch (rule)
    {
        case 1:   // Israel: Fri before last Sun Mar 02:00 IST (= 00:00 UTC) until last Sun Oct 02:00 IDT (= 23:00 UTC Sat)
            startM = 3;  startD = lastSunday(y, 3) - 2; startH = 0;
            endM = 10;   endD = lastSunday(y, 10) - 1;  endH = 23;
            break;
        case 2:   // EU: last Sun Mar 01:00 UTC until last Sun Oct 01:00 UTC
            startM = 3;  startD = lastSunday(y, 3); startH = 1;
            endM = 10;   endD = lastSunday(y, 10); endH = 1;
            break;
        case 3:   // US: second Sun Mar 07:00 UTC until first Sun Nov 06:00 UTC
            startM = 3;  startD = firstSunday(y, 3) + 7; startH = 7;
            endM = 11;   endD = firstSunday(y, 11); endH = 6;
            break;
        default:
            return false;
    }
    long now = ((long) m * 100 + d) * 100 + h;
    long start = ((long) startM * 100 + startD) * 100 + startH;
    long end = ((long) endM * 100 + endD) * 100 + endH;
    return now >= start && now < end;
}
//**************************************************************************************************
//***************************Shabbat Times (Hebcal)**********************************************************************
bool hebcalTime(const String& s, int from, uint16_t& utcMin, uint16_t& year, uint8_t& month, uint8_t& day)
    // pull "YYYY-MM-DDTHH:MM..." from the item whose "category" starts at "from".
    // Hebcal stores "date" BEFORE "category", so search backwards, not forwards.
{
    int p = s.lastIndexOf("\"date\":\"", from);
    if (p < 0) return false;
    p += 8;
    year = (uint16_t) s.substring(p, p + 4).toInt();
    month = (uint8_t) s.substring(p + 5, p + 7).toInt();
    day = (uint8_t) s.substring(p + 8, p + 10).toInt();
    int t = s.indexOf('T', p);
    if (t < 0) return false;
    utcMin = (uint16_t) (s.substring(t + 1, t + 3).toInt() * 60 + s.substring(t + 4, t + 6).toInt());
    return true;
}

uint16_t shabbatToLocal(uint16_t utcMin, uint16_t year, uint8_t month, uint8_t day)
    // Hebcal returns UTC; shift to the clock's timezone + DST rule
{
    int dstH = dst_is_active((int) year, month, day, utcMin / 60, settings.dstRule) ? 1 : 0;
    int32_t m = (int32_t) utcMin + (int32_t) (settings.tzOffset + dstH) * 60;
    while (m < 0) m += 1440;
    while (m >= 1440) m -= 1440;
    return (uint16_t) m;
}

bool fetchShabbatTimes()
{
    if (settings.weatherLat == 0 && settings.weatherLon == 0)
        return false;   // city has not been geocoded yet
    char latbuf[16];
    char lonbuf[16];
    snprintf(latbuf, sizeof latbuf, "%.5f", settings.weatherLat / 1000000.0);
    snprintf(lonbuf, sizeof lonbuf, "%.5f", settings.weatherLon / 1000000.0);
    String url = String("http://www.hebcal.com/shabbat?cfg=json&geo=pos&latitude=") + latbuf
               + "&longitude=" + lonbuf + "&tzid=UTC";
    String resp;
    if (!httpGet(url, resp)) return false;
    int iC = resp.indexOf("\"category\":\"candles\"");
    int iH = resp.indexOf("\"category\":\"havdalah\"");
    if (iC < 0 || iH < 0) return false;
    uint16_t utcMin, year;
    uint8_t month, day;
    if (!hebcalTime(resp, iC, utcMin, year, month, day)) return false;
    shabbatCandleMin = shabbatToLocal(utcMin, year, month, day);
    if (!hebcalTime(resp, iH, utcMin, year, month, day)) return false;
    shabbatHavdalahMin = shabbatToLocal(utcMin, year, month, day);
    return true;
}

void shabbatTryFetch()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Shabbat: waiting for WiFi");
        return;
    }
    static char shabbatCity[sizeof(settings.weatherCity)] = "";
    bool cityChanged = strcmp(shabbatCity, settings.weatherCity) != 0;
    if ((settings.weatherLat == 0 && settings.weatherLon == 0) || cityChanged)
    {
        if (!geocodeCity())
        {
            Serial.println("Shabbat: no coordinates (set a valid Weather City)");
            return;
        }
        strncpy(shabbatCity, settings.weatherCity, sizeof(shabbatCity) - 1);
        shabbatCity[sizeof(shabbatCity) - 1] = 0;
    }
    Serial.println("Shabbat: fetching times...");
    if (fetchShabbatTimes())
    {
        shabbatValid = true;
        Serial.printf("Shabbat: candles %02d:%02d, havdalah %02d:%02d\r\n",
                      shabbatCandleMin / 60, shabbatCandleMin % 60,
                      shabbatHavdalahMin / 60, shabbatHavdalahMin % 60);
    }
    else
    {
        shabbatValid = false;
        Serial.println("Shabbat: fetch failed");
    }
}

uint8_t holidayTypeFromTitle(const String& t)
{
    // Selected holidays only. Other civic/minor days are intentionally ignored.
    if (t == "Yom Kippur") return HOLIDAY_YOM_KIPPUR;
    if (t.startsWith("Rosh Hashana") && t.indexOf("II") < 0 && t.indexOf("LaBehemot") < 0) return HOLIDAY_ROSH_HASHANA;
    if (t == "Sukkot I") return HOLIDAY_SUKKOT;
    if (t == "Shmini Atzeret" || t == "Simchat Torah") return HOLIDAY_SIMCHAT_TORAH;
    if (t == "Chanukah: 1 Candle") return HOLIDAY_HANUKKAH;
    if (t == "Tu BiShvat") return HOLIDAY_TU_BISHVAT;
    if (t == "Purim") return HOLIDAY_PURIM;
    if (t == "Pesach I") return HOLIDAY_PESACH;
    if (t == "Shavuot") return HOLIDAY_SHAVUOT;
    if (t.startsWith("Tish") && t.indexOf("Erev") < 0) return HOLIDAY_TISHA_BAV;
    if (t == "Lag BaOmer") return HOLIDAY_LAG_BAOMER;
    if (t == "Yom HaShoah") return HOLIDAY_YOM_HASHOAH;
    if (t == "Yom HaZikaron") return HOLIDAY_YOM_HAZIKARON;
    if (t.startsWith("Yom HaAtzma")) return HOLIDAY_YOM_HAATZMAUT;
    if (t == "Yom Yerushalayim" || t == "Jerusalem Day") return HOLIDAY_YERUSHALAYIM;
    if (t.startsWith("Swords of Iron")) return HOLIDAY_SWORDS_IRON;
    return HOLIDAY_NONE;
}

bool fetchHolidayMonth(uint16_t year, uint8_t month)
{
    char url[176];
    snprintf(url, sizeof url,
             "http://www.hebcal.com/hebcal?cfg=json&v=1&year=%u&month=%u"
             "&maj=on&mod=on&min=on&nx=off&mf=off&ss=off&i=on",
             year, month);
    String resp;
    if (!httpGet(url, resp))
    {
        Serial.println("Holidays: fetch failed");
        return false;
    }

    holidayCount = 0;
    int p = 0;
    while (holidayCount < HOLIDAY_MAX)
    {
        int t = resp.indexOf("\"title\":\"", p);
        if (t < 0) break;
        t += 9;
        int te = resp.indexOf('"', t);
        if (te < 0) break;
        String title = resp.substring(t, te);
        int d = resp.indexOf("\"date\":\"", te);
        if (d < 0) break;
        d += 8;
        int y = resp.substring(d, d + 4).toInt();
        int m = resp.substring(d + 5, d + 7).toInt();
        int day = resp.substring(d + 8, d + 10).toInt();
        p = d + 10;
        if (y != (int) year || m != (int) month || day < 1 || day > 31) continue;

        uint8_t type = holidayTypeFromTitle(title);
        if (type == HOLIDAY_NONE) continue;
        bool exists = false;
        for (uint8_t i = 0; i < holidayCount; i++)
            if (holidayList[i].day == (uint8_t) day) { exists = true; break; }
        if (!exists)
        {
            holidayList[holidayCount].day = (uint8_t) day;
            holidayList[holidayCount].type = type;
            holidayCount++;
        }
    }
    Serial.printf("Holidays: %u entries for %04u-%02u\r\n", holidayCount, year, month);
    return true;
}

uint8_t holidayTypeForDay(uint8_t day)
{
    for (uint8_t i = 0; i < holidayCount; i++)
        if (holidayList[i].day == day) return holidayList[i].type;
    return HOLIDAY_NONE;
}

const char* holidayGreeting(uint8_t type)
{
    switch (type)
    {
        case HOLIDAY_NEW_YEAR: return "HAPPY NEW YEAR";
        case HOLIDAY_CHRISTMAS: return "MERRY CHRISTMAS";
        case HOLIDAY_ROSH_HASHANA: return "SHANA TOVA";
        case HOLIDAY_YOM_KIPPUR: return "G'MAR CHATIMA TOVA";
        case HOLIDAY_SUKKOT: return "CHAG SUKKOT SAMEACH";
        case HOLIDAY_SIMCHAT_TORAH: return "SIMCHAT TORAH SAMEACH";
        case HOLIDAY_HANUKKAH: return "HAPPY HANUKKAH";
        case HOLIDAY_TU_BISHVAT: return "TU BISHVAT SAMEACH";
        case HOLIDAY_PURIM: return "HAPPY PURIM";
        case HOLIDAY_PESACH: return "CHAG PESACH SAMEACH";
        case HOLIDAY_SHAVUOT: return "CHAG SHAVUOT SAMEACH";
        case HOLIDAY_TISHA_BAV: return "TISHA B'AV";
        case HOLIDAY_LAG_BAOMER: return "LAG BAOMER SAMEACH";
        case HOLIDAY_YOM_HASHOAH: return "YOM HASHOAH";
        case HOLIDAY_YOM_HAZIKARON: return "YOM HAZIKARON";
        case HOLIDAY_YOM_HAATZMAUT: return "YOM HAATZMAUT SAMEACH";
        case HOLIDAY_YERUSHALAYIM: return "YOM YERUSHALAYIM SAMEACH";
        case HOLIDAY_SIGD: return "SIGD SAMEACH";
        case HOLIDAY_HERZL: return "HERZL DAY";
        case HOLIDAY_JABOTINSKY: return "JABOTINSKY DAY";
        case HOLIDAY_BEN_GURION: return "BEN-GURION DAY";
        case HOLIDAY_RABIN: return "RABIN MEMORIAL DAY";
        case HOLIDAY_SWORDS_IRON: return "REMEMBER";
        case HOLIDAY_ALIYAH: return "YOM HAALIYAH";
        default: return "";
    }
}

uint8_t holidayAnimation(uint8_t type)
{
    switch (type)
    {
        case HOLIDAY_NEW_YEAR: return ALERT_ANIM_FIREWORKS;
        case HOLIDAY_CHRISTMAS: return ALERT_ANIM_CHRISTMAS;
        case HOLIDAY_ROSH_HASHANA:
        case HOLIDAY_YOM_KIPPUR: return ALERT_ANIM_SHOFAR;
        case HOLIDAY_SUKKOT: return ALERT_ANIM_SUKKAH;
        case HOLIDAY_SIMCHAT_TORAH:
        case HOLIDAY_SHAVUOT:
        case HOLIDAY_SIGD: return ALERT_ANIM_TORAH;
        case HOLIDAY_HANUKKAH: return ALERT_ANIM_MENORAH;
        case HOLIDAY_TU_BISHVAT: return ALERT_ANIM_TREE;
        case HOLIDAY_PURIM: return ALERT_ANIM_MASK;
        case HOLIDAY_PESACH: return ALERT_ANIM_MATZAH;
        case HOLIDAY_TISHA_BAV:
        case HOLIDAY_YOM_HASHOAH:
        case HOLIDAY_YOM_HAZIKARON:
        case HOLIDAY_RABIN:
        case HOLIDAY_SWORDS_IRON: return ALERT_ANIM_MEMORIAL;
        case HOLIDAY_LAG_BAOMER: return ALERT_ANIM_BONFIRE;
        case HOLIDAY_YOM_HAATZMAUT:
        case HOLIDAY_YERUSHALAYIM:
        case HOLIDAY_HERZL:
        case HOLIDAY_JABOTINSKY:
        case HOLIDAY_BEN_GURION:
        case HOLIDAY_ALIYAH: return ALERT_ANIM_FLAG;
        default: return ALERT_ANIM_NONE;
    }
}

void holidayTick()   // once per minute: fetch this month's holidays and greet on the day
{
    if (!settings.holidayGreetings || !rtcInitialized) return;
    if (MEZ.jahr12 < 2024) return;   // wait for a valid RTC/NTP date, avoid a false New Year on 2000-01-01
    if (Update.isRunning() || otaRebootAt != 0) return;   // don't block or interrupt a firmware update

    uint32_t todayKey = ((uint32_t) MEZ.jahr12 * 10000UL)
                      + ((uint32_t) MEZ.mon12 * 100UL)
                      + MEZ.tag12;
    uint8_t type = HOLIDAY_NONE;

    if (MEZ.mon12 == 1 && MEZ.tag12 == 1)
        type = HOLIDAY_NEW_YEAR;
    else if (MEZ.mon12 == 12 && MEZ.tag12 == 25)
        type = HOLIDAY_CHRISTMAS;
    else
    {
        uint16_t monthKey = (uint16_t) (MEZ.jahr12 * 12 + MEZ.mon12);
        if (monthKey != holidayFetchMonth)
        {
            holidayFetchMonth = monthKey;
            holidayFetchOk = false;
            holidayCount = 0;
        }
        if (!holidayFetchOk && WiFi.status() == WL_CONNECTED
            && (holidayLastFetch == 0 || millis() - holidayLastFetch > 5UL * 60000UL))
        {
            holidayLastFetch = millis();
            holidayFetchOk = fetchHolidayMonth(MEZ.jahr12, MEZ.mon12);
        }
        if (holidayFetchOk)
            type = holidayTypeForDay(MEZ.tag12);
    }

    if (type == HOLIDAY_NONE || holidayGreetedKey == todayKey) return;
    holidayGreetedKey = todayKey;

    const char* text = holidayGreeting(type);
    uint8_t anim = holidayAnimation(type);
    Serial.printf("Holiday: %s\r\n", text);
    if (anim != ALERT_ANIM_NONE)
        startAlertAnimation(anim, text, 2);
    else
        startMsg(text, 2);
}

void shabbatMinuteTick(uint16_t nowMin)   // called once per minute from the 1s block
{
    if (!settings.shabbat) return;
    static unsigned long lastShabbatFetch = 0;
    static uint16_t lastSundayKey = 0xFFFF;
    static uint16_t lastCandleKey = 0xFFFF;
    static uint16_t lastEndKey = 0xFFFF;
    static uint16_t leadMsgDate = 0xFFFF;   // first Shabbat-time message inside the lead window
    static bool leadMsgSent = false;
    static uint16_t endMsgDate = 0xFFFF;    // first Shabbat-end message on Saturday
    static bool endMsgSent = false;
    uint16_t dateKey = ((uint16_t) MEZ.mon12 << 8) | MEZ.tag12;

    // On Sunday, fetch the new week's times once (retries continue below if the fetch failed).
    if (MEZ.WT == 0 && lastSundayKey != dateKey)
    {
        lastSundayKey = dateKey;
        shabbatValid = false;
        lastShabbatFetch = 0;
    }
    if (!shabbatValid)
    {
        if (lastShabbatFetch == 0 || millis() - lastShabbatFetch > 2UL * 60000UL)
        {
            lastShabbatFetch = millis();
            shabbatTryFetch();
        }
        return;
    }

    char buf[40];
    if (MEZ.WT == 5)
    {
        int untilCandle = (int) shabbatCandleMin - (int) nowMin;
        if (untilCandle <= 0 && untilCandle >= -5 && lastCandleKey != dateKey)
        {
            lastCandleKey = dateKey;
            if (settings.shabbatAnim)
                startAlertAnimation(ALERT_ANIM_CANDLES, "SHABBAT SHALOM", 2);
            else
                startMsg("SHABBAT SHALOM", 2);
        }
        else if (untilCandle > 0 && untilCandle <= settings.shabbatLead)
        {
            if (leadMsgDate != dateKey)
            {
                leadMsgDate = dateKey;
                leadMsgSent = false;
            }
            // Always show the time on the first tick inside the lead window, then repeat
            // at the configured interval. This covers booting/fetching late in the window.
            if (!leadMsgSent || untilCandle % settings.shabbatFreq == 0 || untilCandle == settings.shabbatLead)
            {
                leadMsgSent = true;
                snprintf(buf, sizeof buf, "SHABBAT CANDLES %02d:%02d", shabbatCandleMin / 60, shabbatCandleMin % 60);
                startMsg(buf, 2);
            }
        }
    }
    else if (MEZ.WT == 6)
    {
        int untilEnd = (int) shabbatHavdalahMin - (int) nowMin;
        if (untilEnd <= 0 && untilEnd >= -5 && lastEndKey != dateKey)
        {
            lastEndKey = dateKey;
            if (settings.shabbatAnim)
                startAlertAnimation(ALERT_ANIM_SHABBAT_END, "Have A Nice Week ;-)", 2);
            else
                startMsg("Have A Nice Week ;-)", 2);
        }
        else if (untilEnd > 0)
        {
            if (endMsgDate != dateKey)
            {
                endMsgDate = dateKey;
                endMsgSent = false;
            }
            // Same as Friday: don't miss the first end-time message if the clock starts late.
            if (!endMsgSent || untilEnd % settings.shabbatFreq == 0 || untilEnd == settings.shabbatLead)
            {
                endMsgSent = true;
                snprintf(buf, sizeof buf, "SHABBAT ENDS %02d:%02d", shabbatHavdalahMin / 60, shabbatHavdalahMin % 60);
                startMsg(buf, 2);
            }
        }
    }
}
//**************************************************************************************************
uint64_t ntpTimestampMs(const byte* p)
    // parse an 8-byte NTP timestamp (32-bit seconds + 32-bit fraction) into milliseconds since 1900
{
    uint64_t secs = ((uint64_t)p[0] << 24) | ((uint64_t)p[1] << 16) | ((uint64_t)p[2] << 8) | p[3];
    uint64_t frac = ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) | ((uint64_t)p[6] << 8) | p[7];
    return secs * 1000ULL + (frac * 1000ULL >> 32);
}
//**************************************************************************************************
tm* connectNTP() { // if response from NTP was successful return *tm else return a null pointer
    WiFi.hostByName(settings.ntpServer, timeServerIP);
    Serial.println(timeServerIP);
    Serial.println("sending NTP packet...");
    // set all bytes in the buffer to 0
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    // Initialize values needed to form NTP request
    // (see URL above for details on the packets)
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;
    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    udp.beginPacket(timeServerIP, 123); //NTP requests are to port 123
    udp.write(packetBuffer, NTP_PACKET_SIZE);
    unsigned long t1 = millis();        // client send time
    udp.endPacket();
    int cb = 0;
    while (millis() - t1 < 3000UL)      // wait up to 3s for a reply, keep the web server responsive while waiting
    {
        yield();
        server.handleClient();
        cb = udp.parsePacket();
        if (cb > 0) break;
    }
    unsigned long t4 = millis();        // client receive time
    if (cb < 48) {
        Serial.printf("NTP: expected at least 48 bytes, got %d\r\n", cb);
        return NULL;
    }
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
    // validate the reply: LI=3 = server not synchronized, stratum 0 = Kiss-o'-Death, version 1-4
    uint8_t li = (packetBuffer[0] >> 6) & 0x03;
    uint8_t version = (packetBuffer[0] >> 3) & 0x07;
    uint8_t stratum = packetBuffer[1];
    if (li == 3 || version < 1 || version > 4 || stratum == 0) {
        Serial.printf("NTP: invalid reply LI=%d version=%d stratum=%d\r\n", li, version, stratum);
        return NULL;
    }
    if (li != 0)   // LI=1/2 announce a pending leap second, the time is still valid
        Serial.printf("NTP: leap second announced (LI=%d)\r\n", li);
    // parse the server timestamps: T2 = server receive time (bytes 32-39), T3 = server transmit time (bytes 40-47)
    uint64_t t2 = ntpTimestampMs(packetBuffer + 32);
    uint64_t t3 = ntpTimestampMs(packetBuffer + 40);
    // round-trip time in real ms = client elapsed time minus the server's own processing time
    unsigned long elapsed = (unsigned long)(t4 - t1);
    unsigned long serverProc = (t3 > t2) ? (unsigned long)(t3 - t2) : 0;
    unsigned long rtt = (serverProc < elapsed) ? (elapsed - serverProc) : elapsed;
    unsigned long transit = rtt / 2;    // one-way network delay (assumes symmetric paths)
    // true server time when the reply arrived, rounded to the nearest second
    const unsigned long seventyYears = 2208988800UL;
    unsigned long secsSince1900 = (unsigned long)((t3 + transit + 500) / 1000);
    // now convert NTP time into everyday time:
    // subtract seventy years:
    int dstOffsetSec = 0;
    if (settings.dstRule > 0)
    {
        time_t utcEpoch = (time_t) (secsSince1900 - seventyYears);
        tm* ut = gmtime(&utcEpoch);
        if (ut != NULL && dst_is_active(ut->tm_year + 1900, ut->tm_mon + 1, ut->tm_mday, ut->tm_hour, settings.dstRule))
            dstOffsetSec = 3600;
        Serial.printf("DST rule:%d active:%s\r\n", settings.dstRule, dstOffsetSec ? "yes" : "no");
    }
    epoch = secsSince1900 - seventyYears + 3600 * settings.tzOffset + dstOffsetSec; // RTT-corrected, no fixed offset
    //epoch=epoch-3600*6; // difference -6h = -6* 3600 sec)
    Serial.printf("NTP RTT:%lums transit:%lums offset:%lds\r\n", rtt, transit, (long)epoch - (long)(secsSince1900 - seventyYears));
    time_t t;
    t = epoch;
    tm* tt;
    tt = localtime(&t);
    Serial.println(epoch);
    Serial.println(asctime(tt));
    return tt;
}
//**************************************************************************************************
void rtc_init(unsigned char sda, unsigned char scl) {
    Wire.begin(sda, scl);
    rtc_Write(controlREG, 0x00);
}
//**************************************************************************************************
// BCD Code
//**************************************************************************************************
unsigned char dec2bcd(unsigned char x) { //value 0...99
    unsigned char z, e, r;
    e = x % 10;
    z = x / 10;
    z = z << 4;
    r = e | z;
    return (r);
}
unsigned char bcd2dec(unsigned char x) { //value 0...99
    int z, e;
    e = x & 0x0F;
    z = x & 0xF0;
    z = z >> 4;
    z = z * 10;
    return (z + e);
}
//**************************************************************************************************
// RTC I2C Code
//**************************************************************************************************
unsigned char rtc_Read(unsigned char regaddress) {
    Wire.beginTransmission(DS3231_ADDRESS);
    Wire.write(regaddress);
    Wire.endTransmission();
    Wire.requestFrom((unsigned char) DS3231_ADDRESS, (unsigned char) 1);
    return (Wire.read());
}
void rtc_Write(unsigned char regaddress, unsigned char value) {
    Wire.beginTransmission(DS3231_ADDRESS);
    Wire.write(regaddress);
    Wire.write(value);
    Wire.endTransmission();
}
//**********************RTC read/write functions (some variable names are German)****************************************************************************
unsigned char rtc_sekunde() {
    return (bcd2dec(rtc_Read(secondREG)));
}
unsigned char rtc_minute() {
    return (bcd2dec(rtc_Read(minuteREG)));
}
unsigned char rtc_stunde() {
    return (bcd2dec(rtc_Read(hourREG)));
}
unsigned char rtc_wochentag() {
    return (bcd2dec(rtc_Read(WTREG)));
}
unsigned char rtc_tag() {
    return (bcd2dec(rtc_Read(dateREG)));
}
unsigned char rtc_monat() {
    return (bcd2dec(rtc_Read(monthREG)));
}
unsigned char rtc_jahr() {
    return (bcd2dec(rtc_Read(yearREG)));
}
void rtc_sekunde(unsigned char sek) {
    rtc_Write(secondREG, (dec2bcd(sek)));
}
void rtc_minute(unsigned char min) {
    rtc_Write(minuteREG, (dec2bcd(min)));
}
void rtc_stunde(unsigned char std) {
    rtc_Write(hourREG, (dec2bcd(std)));
}
void rtc_wochentag(unsigned char wt) {
    rtc_Write(WTREG, (dec2bcd(wt)));
}
void rtc_tag(unsigned char tag) {
    rtc_Write(dateREG, (dec2bcd(tag)));
}
void rtc_monat(unsigned char mon) {
    rtc_Write(monthREG, (dec2bcd(mon)));
}
void rtc_jahr(unsigned char jahr) {
    rtc_Write(yearREG, (dec2bcd(jahr)));
}
//******************************Set RTC********************************************************************
void rtc_set(tm* tt) {
    rtc_sekunde((unsigned char) tt->tm_sec);
    rtc_minute((unsigned char) tt->tm_min);
    rtc_stunde((unsigned char) tt->tm_hour);
    rtc_tag((unsigned char) tt->tm_mday);
    rtc_monat((unsigned char) tt->tm_mon + 1);
    rtc_jahr((unsigned char) tt->tm_year - 100);
    if (tt->tm_wday == 0)
    {
      rtc_wochentag(7);
    }
    else
    rtc_wochentag((unsigned char) tt->tm_wday);
}
//*****************************Get RTC Temperature*********************************************************************
float rtc_temp() {
    float t = 0.0;
    unsigned char lowByte = 0;
    signed char highByte = 0;
    lowByte = rtc_Read(tempLSBREG);
    highByte = rtc_Read(tempMSBREG);
    lowByte >>= 6;
    lowByte &= 0x03;
    t = ((float) lowByte);
    t *= 0.25;
    t += highByte;
    return (t); // return temp value
}
//**************************************************************************************************
void rtc2mez() {
 
    unsigned short Jahr, Tag, Monat, WoTag, Stunde, Minute, Sekunde;

    Jahr = rtc_jahr();//year
    if (Jahr > 99)
        Jahr = 0;
    Monat = rtc_monat();//month
    if (Monat < 1 || Monat > 12)
        Monat = 1;
    Tag = rtc_tag();//day
    if (Tag < 1 || Tag > 31)
        Tag = 1;
    WoTag = rtc_wochentag();
    if (WoTag == 7)
        WoTag = 0;
    if (WoTag > 6)
        WoTag = 0;
    Stunde = rtc_stunde();//hour
    if (Stunde > 23)
        Stunde = 0;
    Minute = rtc_minute();//minute
    if (Minute > 59)
        Minute = 0;
    Sekunde = rtc_sekunde();//second
    if (Sekunde > 59)
        Sekunde = 0;
    
    MEZ.WT = WoTag;          //So=0, Mo=1, Di=2 ...
    MEZ.sek1 = Sekunde % 10;
    MEZ.sek2 = Sekunde / 10;
    MEZ.sek12 = Sekunde;
    MEZ.min1 = Minute % 10;
    MEZ.min2 = Minute / 10;
    MEZ.min12 = Minute;
    MEZ.std1 = Stunde % 10;
    MEZ.std2 = Stunde / 10;
    MEZ.std12 = Stunde;
    MEZ.tag12 = Tag;
    MEZ.tag1 = Tag % 10;
    MEZ.tag2 = Tag / 10;
    MEZ.mon12 = Monat;
    MEZ.mon1 = Monat % 10;
    MEZ.mon2 = Monat / 10;
    MEZ.jahr12 = Jahr;
    MEZ.jahr1 = Jahr % 10;
    MEZ.jahr2 = Jahr / 10;
}

//***************************MAX7219 Init Values**********************************************************************
const unsigned short InitArr[7][2] = { { 0x0C, 0x00 },    // display off
        { 0x00, 0xFF },    // no LEDtest
        { 0x09, 0x00 },    // BCD off
        { 0x0F, 0x00 },    // normal operation
        { 0x0B, 0x07 },    // start display
        { 0x0A, 0x04 },    // brightness
        { 0x0C, 0x01 }     // display on
};
//*********************************MAX7219 Init*****************************************************************
void max7219_init()  //all MAX7219 init
{
    unsigned short i, j;
    for (i = 0; i < 7; i++) {
        digitalWrite(CS, LOW);
        delayMicroseconds(1);
        for (j = 0; j < anzMAX; j++) {
            SPI.write(InitArr[i][0]);  //register
            SPI.write(InitArr[i][1]);  //value
        }
        digitalWrite(CS, HIGH);
    }
}
//*********************************Set Brightness*****************************************************************
void max7219_set_brightness(unsigned short br)  //brightness MAX7219
{
    unsigned short j;
    if (br < 16) {
        digitalWrite(CS, LOW);
        delayMicroseconds(1);
        for (j = 0; j < anzMAX; j++) {
            SPI.write(0x0A);  //register
            SPI.write(br);    //value
        }
        digitalWrite(CS, HIGH);
    }
}
//**************************************************************************************************
void helpArr_init(void)  //helperarray init
{
    unsigned short i, j, k;
    j = 0;
    k = 0;
    for (i = 0; i < anzMAX * 8; i++) {
        helpArrPos[i] = (1 << j);   //bitmask
        helpArrMAX[i] = k;
        j++;
        if (j > 7) {
            j = 0;
            k++;
        }
    }
}
//***********************************Clear LED Array (RAM)***************************************************************
void clearLEDarr()   //clear all matrix rows in the LED array, used for a clean message scroll
{
    for (unsigned short i = 0; i < anzMAX; i++)
        for (unsigned short j = 0; j < 8; j++)
            LEDarr[i][j] = 0;
}
//***********************************Clear Display***************************************************************
void clear_Display()   //clear all
{
    unsigned short i, j;
    for (i = 0; i < 8; i++)     //8 rows
    {
        digitalWrite(CS, LOW);
        delayMicroseconds(1);
        for (j = anzMAX; j > 0; j--) {
            LEDarr[j - 1][i] = 0;       //LEDarr clear
            SPI.write(i + 1);           //current row
            SPI.write(LEDarr[j - 1][i]);
        }
        digitalWrite(CS, HIGH);
    }
}
//*************************************Rotate 90 Degrees********************************************************************
void rotate_90() // for Generic displays
{
    for (uint8_t k = anzMAX; k > 0; k--) {

        uint8_t i, j, m, imask, jmask;
        uint8_t tmp[8]={0,0,0,0,0,0,0,0};
        for (  i = 0, imask = 0x01; i < 8; i++, imask <<= 1) {
          for (j = 0, jmask = 0x01; j < 8; j++, jmask <<= 1) {
            if (LEDarr[k-1][i] & jmask) {
              tmp[j] |= imask;
            }
          }
        }
        for(m=0; m<8; m++){
            LEDarr[k-1][m]=tmp[m];
        }
    }
}
//*****************************Refresh Display*********************************************************************
void refresh_display() //take info into LEDarr
{
    unsigned short i, j;

#ifdef ROTATE_90
    rotate_90();
#endif

    for (i = 0; i < 8; i++)     //8 rows
    {
        digitalWrite(CS, LOW);
        delayMicroseconds(1);
        for (j = 1; j <= anzMAX; j++) 
        {
            SPI.write(i + 1);  //current row
            //if(kk==1){kk=0;
            if(kk){
              SPI.setBitOrder(LSBFIRST);      // bitorder for reverse columns
              SPI.write(LEDarr[4-j][7-i]);//SPI.write(LEDarr[j - 1][7-i]);
              SPI.setBitOrder(MSBFIRST);      // reset bitorder to normal order
            }
            else {
              
            


            
#ifdef REVERSE_HORIZONTAL    // reverse horizontal direction 
            SPI.setBitOrder(LSBFIRST);      // bitorder for reverse columns
#endif

#ifdef REVERSE_VERTICAL     // reverse vertical direction
            SPI.write(LEDarr[4-j][7-i]);//SPI.write(LEDarr[j - 1][7-i]);
#else
            SPI.write(LEDarr[j - 1][i]);
#endif

#ifdef REVERSE_HORIZONTAL
            SPI.setBitOrder(MSBFIRST);      // reset bitorder to normal order
#endif
        }}
        digitalWrite(CS, HIGH);
    }
}
//**************************************************************************************************
void char2Arr(unsigned short ch, int PosX, short PosY) { //characters into arr
    int i, j, k, l, m, o1, o2, o3, o4;  //in LEDarr
    PosX++;
    k = ch - 32;                        //ASCII position in font
    if ((k >= 0) && (k < 96))           //character found in font?
    {
        o4 = font1[k][0];                 //character width
        o3 = 1 << (o4 - 2);
        for (i = 0; i < o4; i++) {
            if (((PosX - i <= maxPosX) && (PosX - i >= 0))
                    && ((PosY > -8) && (PosY < 8))) //within matrix?
            {
                o1 = helpArrPos[PosX - i];
                o2 = helpArrMAX[PosX - i];
                for (j = 0; j < 8; j++) {
                    if (((PosY >= 0) && (PosY <= j)) || ((PosY < 0) && (j < PosY + 8))) //scroll vertical
                    {
                        l = font1[k][j + 1];
                        m = (l & (o3 >> i));  //e.g. o4=7  0zzzzz0, o4=4  0zz0
                        if (m > 0)
                            LEDarr[o2][j - PosY] = LEDarr[o2][j - PosY] | (o1);  //set point
                        else
                            LEDarr[o2][j - PosY] = LEDarr[o2][j - PosY] & (~o1); //clear point
                    }
                }
            }
        }
    }
}

//***********************************Digit Change Animations*********************************************************
enum {
    DIGIT_ANIM_OFF = 0,
    DIGIT_ANIM_ROLL,
    DIGIT_ANIM_FLIP,
    DIGIT_ANIM_WIPE,
    DIGIT_ANIM_FADE,
    DIGIT_ANIM_DROP
};

struct DigitAnimState {
    uint8_t oldVal;
    uint8_t newVal;
    uint8_t frame;
    uint8_t total;
    bool active;
};
DigitAnimState digitAnim[6];

uint8_t digitAnimFrames(uint8_t mode)   // number of 50 ms frames for one transition
{
    switch (mode) {
        case DIGIT_ANIM_ROLL: return 6;   // 300 ms
        case DIGIT_ANIM_FLIP: return 4;   // 200 ms
        case DIGIT_ANIM_WIPE: return 5;   // 250 ms
        case DIGIT_ANIM_FADE: return 6;   // 300 ms
        case DIGIT_ANIM_DROP: return 6;   // 300 ms
        default: return 1;
    }
}

void digitAnimStart(uint8_t idx, uint8_t oldVal, uint8_t newVal)
{
    if (idx >= 6) return;
    if (settings.digitAnim == DIGIT_ANIM_OFF || oldVal == newVal) {
        digitAnim[idx].active = false;
        return;
    }
    digitAnim[idx].oldVal = oldVal;
    digitAnim[idx].newVal = newVal;
    digitAnim[idx].frame = 0;
    digitAnim[idx].total = digitAnimFrames(settings.digitAnim);
    digitAnim[idx].active = true;
}

void digitAnimCancelAll()
{
    for (uint8_t i = 0; i < 6; i++)
        digitAnim[i].active = false;
}

void glyphRows(uint8_t ch, bool small, int8_t dx, int8_t dy, uint8_t rows[8])
    // Render one font glyph into an 8x8 cell. rows[y] bits 0..6 map to
    // screen columns (baseX - 5) .. (baseX + 1), matching char2Arr().
{
    for (uint8_t y = 0; y < 8; y++) rows[y] = 0;
    int k = (int) ch - 32;
    if (k < 0 || k >= 96) return;
    const unsigned short *f = small ? font2[k] : font1[k];
    uint8_t w = (uint8_t) f[0];
    if (w < 2) return;
    if (w > 8) w = 8;
    uint16_t firstBit = 1u << (w - 2);
    for (uint8_t i = 0; i < w; i++) {
        int8_t c = (int8_t) (6 - i + dx);
        if (c < 0 || c > 7) continue;
        uint8_t colBit = 1 << c;
        uint16_t rowBit = firstBit >> i;
        for (uint8_t y = 0; y < 8; y++) {
            int8_t sy = (int8_t) (y - dy);
            if (sy < 0 || sy > 7) continue;
            if (f[sy + 1] & rowBit) rows[y] |= colBit;
        }
    }
}

void blitRows(const uint8_t rows[8], int x0)
{
    for (uint8_t y = 0; y < 8; y++) {
        uint8_t bits = rows[y];
        if (!bits) continue;
        for (uint8_t c = 0; c < 8; c++) {
            if (!(bits & (1 << c))) continue;
            int xx = x0 + c;
            if (xx < 0 || xx > (int) maxPosX) continue;
            LEDarr[xx >> 3][y] |= (1 << (xx & 7));
        }
    }
}

void drawDigitStatic(uint8_t val, int baseX, bool small)
{
    uint8_t rows[8];
    glyphRows((uint8_t) (48 + val), small, 0, 0, rows);
    blitRows(rows, baseX - 5);
}

void drawDigitTransition(uint8_t idx, int baseX, bool small)
{
    DigitAnimState &a = digitAnim[idx];
    uint8_t oldRows[8], newRows[8], out[8];
    uint8_t mode = settings.digitAnim;
    uint16_t p = (a.total > 1) ? ((uint32_t) a.frame * 256 / (a.total - 1)) : 256;
    uint8_t f = a.frame;

    glyphRows((uint8_t) (48 + a.oldVal), small, 0, 0, oldRows);
    glyphRows((uint8_t) (48 + a.newVal), small, 0, 0, newRows);

    if (mode == DIGIT_ANIM_ROLL) {
        uint16_t off = (p * 8) >> 8;
        int8_t oldDy = (settings.scrollDir == 0) ? -(int8_t) off : (int8_t) off;
        int8_t newDy = (settings.scrollDir == 0) ? (int8_t) (8 - off) : (int8_t) (off - 8);
        glyphRows((uint8_t) (48 + a.oldVal), small, 0, oldDy, oldRows);
        glyphRows((uint8_t) (48 + a.newVal), small, 0, newDy, newRows);
        for (uint8_t y = 0; y < 8; y++) out[y] = oldRows[y] | newRows[y];
    } else if (mode == DIGIT_ANIM_FLIP) {
        bool topFirst = (settings.scrollDir == 0);
        if (f == 0)
            for (uint8_t y = 0; y < 8; y++) out[y] = oldRows[y];
        else if (f + 1 >= a.total)
            for (uint8_t y = 0; y < 8; y++) out[y] = newRows[y];
        else if (f == 1) {
            for (uint8_t y = 0; y < 8; y++)
                out[y] = topFirst ? ((y < 4) ? oldRows[y] : 0) : ((y >= 4) ? oldRows[y] : 0);
        } else {
            for (uint8_t y = 0; y < 8; y++)
                out[y] = topFirst ? ((y < 4) ? newRows[y] : oldRows[y])
                                  : ((y < 4) ? oldRows[y] : newRows[y]);
        }
    } else if (mode == DIGIT_ANIM_WIPE) {
        uint8_t reveal = (uint8_t) ((p * 8) >> 8);
        bool topDown = (settings.scrollDir == 0);
        for (uint8_t y = 0; y < 8; y++) {
            bool useNew = topDown ? (y < reveal) : (y >= 8 - reveal);
            out[y] = useNew ? newRows[y] : oldRows[y];
        }
    } else if (mode == DIGIT_ANIM_FADE) {
        static const uint8_t bayer4[4][4] = {
            { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
        };
        for (uint8_t y = 0; y < 8; y++) {
            uint8_t row = 0;
            for (uint8_t c = 0; c < 8; c++) {
                uint8_t bv = bayer4[y & 3][c & 3];
                if (settings.scrollDir != 0) bv = 15 - bv;
                bool useNew = p > (uint16_t) (bv * 16 + 8);
                uint8_t bit = (useNew ? newRows[y] : oldRows[y]) & (1 << c);
                row |= bit;
            }
            out[y] = row;
        }
    } else if (mode == DIGIT_ANIM_DROP) {
        static const int8_t dropNewDn[6] = { -8, -5, -2, 0, 1, 0 };
        static const int8_t dropOldDn[6] = { 0, 2, 5, 8, 8, 8 };
        static const int8_t dropNewUp[6] = { 8, 5, 2, 0, -1, 0 };
        static const int8_t dropOldUp[6] = { 0, -2, -5, -8, -8, -8 };
        uint8_t i = f < 6 ? f : 5;
        int8_t newDy = (settings.scrollDir == 0) ? dropNewDn[i] : dropNewUp[i];
        int8_t oldDy = (settings.scrollDir == 0) ? dropOldDn[i] : dropOldUp[i];
        glyphRows((uint8_t) (48 + a.oldVal), small, 0, oldDy, oldRows);
        glyphRows((uint8_t) (48 + a.newVal), small, 0, newDy, newRows);
        for (uint8_t y = 0; y < 8; y++) out[y] = oldRows[y] | newRows[y];
    } else {
        for (uint8_t y = 0; y < 8; y++) out[y] = newRows[y];
    }

    blitRows(out, baseX - 5);
    if (++a.frame >= a.total) a.active = false;
}

void drawDigitCell(uint8_t idx, uint8_t val, int baseX, bool small)
{
    if (settings.digitAnim == DIGIT_ANIM_OFF || !digitAnim[idx].active) {
        drawDigitStatic(val, baseX, small);
        return;
    }
    drawDigitTransition(idx, baseX, small);
}

void drawColon(int baseX)   // separator; baseX is the same position char2Arr(':') uses
{
    uint8_t cst = 1;   // 0=off, 1=both dots, 2=top dot, 3=bottom dot
    if (settings.colonAnim == 1) cst = ((millis() / 1000) % 2 == 0) ? 1 : 0;
    else if (settings.colonAnim == 2) cst = ((millis() / 2000) % 2 == 0) ? 1 : 0;
    else if (settings.colonAnim == 3) { unsigned long t = millis() % 1000; cst = (t < 150 || (t >= 250 && t < 400)) ? 1 : 0; }
    else if (settings.colonAnim == 4) cst = ((millis() / 500) % 2 == 0) ? 2 : 3;
    if (cst == 0) return;
    uint8_t rows[8];
    glyphRows(':', false, 0, 0, rows);
    if (cst == 2) {          // hide the bottom dot
        for (uint8_t y = 4; y < 8; y++) rows[y] = 0;
    } else if (cst == 3) {   // hide the top dot
        for (uint8_t y = 0; y < 4; y++) rows[y] = 0;
    }
    blitRows(rows, baseX - 5);
}

void drawDate(int posX)
{
    char2Arr(' ', posX + 5, 0);        //day of the week
    char2Arr(WT_arr[MEZ.WT][0], posX - 1, 0);
    char2Arr(WT_arr[MEZ.WT][1], posX - 7, 0);
    char2Arr(WT_arr[MEZ.WT][2], posX - 13, 0);
    char2Arr(WT_arr[MEZ.WT][3], posX - 19, 0);
    char2Arr(48 + MEZ.tag2, posX - 24, 0);           //day
    char2Arr(48 + MEZ.tag1, posX - 30, 0);
    char2Arr(M_arr[MEZ.mon12 - 1][0], posX - 39, 0); //month
    char2Arr(M_arr[MEZ.mon12 - 1][1], posX - 43, 0);
    char2Arr(M_arr[MEZ.mon12 - 1][2], posX - 49, 0);
    char2Arr(M_arr[MEZ.mon12 - 1][3], posX - 55, 0);
    char2Arr(M_arr[MEZ.mon12 - 1][4], posX - 61, 0);
    char2Arr('2', posX - 68, 0);                     //year
    char2Arr('0', posX - 74, 0);
    char2Arr(48 + MEZ.jahr2, posX - 80, 0);
    char2Arr(48 + MEZ.jahr1, posX - 86, 0);
}

void setLedPixel(int x, int y)
{
    if (x < 0 || x > (int) maxPosX || y < 0 || y > 7) return;
    LEDarr[x >> 3][y] |= (1 << (x & 7));
}

void drawAlertAnimation(uint8_t type, uint16_t frame)
{
    if (type == ALERT_ANIM_RAIN) {
        // Rain cloud across the top.
        for (int x = 12; x <= 19; x++) setLedPixel(x, 0);
        for (int x = 10; x <= 21; x++) setLedPixel(x, 1);
        for (int x = 8; x <= 23; x++) setLedPixel(x, 2);

        // Falling drops with staggered phases and 2-pixel trails.
        static const uint8_t dropX[8] = { 3, 7, 11, 15, 19, 23, 27, 30 };
        for (uint8_t i = 0; i < 8; i++) {
            int y = 3 + (int) ((frame + i * 3) / 2) % 5;
            setLedPixel(dropX[i], y);
            if (y + 1 < 8) setLedPixel(dropX[i], y + 1);
        }
    } else if (type == ALERT_ANIM_HEAT) {
        // 5x5 sun with a pulsing set of rays.
        static const uint8_t sun[5] = { 0b01110, 0b11111, 0b11111, 0b11111, 0b01110 };
        for (uint8_t row = 0; row < 5; row++)
            for (uint8_t col = 0; col < 5; col++)
                if (sun[row] & (1 << (4 - col))) setLedPixel(14 + col, 1 + row);

        uint8_t pulse = (frame / 5) & 1;
        setLedPixel(16, 0);
        setLedPixel(16, 6);
        setLedPixel(12 + pulse, 3);
        setLedPixel(20 - pulse, 3);
        setLedPixel(13, 1);
        setLedPixel(19, 1);
        setLedPixel(13, 5);
        setLedPixel(19, 5);

        // Shimmering heat waves below and beside the sun.
        for (int x = 0; x < 32; x++) {
            if (x >= 12 && x <= 20) continue;
            if (((x + (int) (frame / 2)) % 3) == 0) setLedPixel(x, 7);
            if (((x + (int) (frame / 2) + 1) % 3) == 0) setLedPixel(x, 6);
        }
    } else if (type == ALERT_ANIM_CANDLES) {
        // Two Shabbat candles with flickering flames.
        static const uint8_t candleX[2] = { 10, 21 };
        for (uint8_t i = 0; i < 2; i++) {
            int cx = candleX[i];
            for (int y = 3; y <= 6; y++) setLedPixel(cx, y);
            setLedPixel(cx - 1, 7);
            setLedPixel(cx, 7);
            setLedPixel(cx + 1, 7);
            setLedPixel(cx, 1);
            setLedPixel(cx, 2);
            uint8_t flicker = (frame / 2 + i * 2) % 3;
            if (flicker == 0) setLedPixel(cx - 1, 2);
            else if (flicker == 1) setLedPixel(cx + 1, 2);
            else { setLedPixel(cx - 1, 1); setLedPixel(cx + 1, 1); }
        }
    } else if (type == ALERT_ANIM_SHABBAT_END) {
        // Magen David with twinkling stars.
        static const uint8_t star[7] = {
            0b0001000, 0b0010100, 0b0100010, 0b1111111,
            0b0100010, 0b0010100, 0b0001000
        };
        for (uint8_t row = 0; row < 7; row++)
            for (uint8_t col = 0; col < 7; col++)
                if (star[row] & (1 << (6 - col))) setLedPixel(12 + col, row);
        static const uint8_t twinkleX[4] = { 3, 7, 24, 29 };
        for (uint8_t i = 0; i < 4; i++) {
            int ty = (int) ((frame / 3 + i * 2) % 8);
            if (((frame / 2) + i) % 2 == 0) setLedPixel(twinkleX[i], ty);
        }
    } else if (type == ALERT_ANIM_FIREWORKS) {
        // Three expanding bursts with drifting sparkles (New Year).
        static const uint8_t burstX[3] = { 8, 23, 16 };
        static const uint8_t burstY[3] = { 3, 4, 2 };
        static const int8_t dirX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
        static const int8_t dirY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        for (uint8_t i = 0; i < 3; i++) {
            int r = 1 + (int) ((frame / 3 + i * 2) % 3);
            for (uint8_t d = 0; d < 8; d++)
                setLedPixel(burstX[i] + dirX[d] * r, burstY[i] + dirY[d] * r);
        }
        for (int x = 0; x < 32; x++)
            if (((x * 5 + (int) (frame / 2)) % 13) == 0)
                setLedPixel(x, (x * 3 + (int) (frame / 3)) % 8);
    } else if (type == ALERT_ANIM_CHRISTMAS) {
        // Christmas tree with a star and twinkling lights.
        const int cx = 16;
        for (int row = 0; row < 5; row++)
            for (int x = cx - row; x <= cx + row; x++)
                setLedPixel(x, row + 1);
        setLedPixel(cx, 0);
        setLedPixel(cx - 1, 0);
        setLedPixel(cx + 1, 0);
        setLedPixel(cx, 6);
        for (int x = cx - 1; x <= cx + 1; x++) setLedPixel(x, 7);
        static const int8_t lx[5] = { -3, -1, 1, 3, 0 };
        static const uint8_t ly[5] = { 3, 2, 3, 2, 4 };
        for (uint8_t i = 0; i < 5; i++)
            if (((frame / 4) + i) % 2 == 0)
                setLedPixel(cx + lx[i], ly[i]);
    } else if (type == ALERT_ANIM_SHOFAR) {
        // Curved shofar with sound waves (Rosh Hashana / Yom Kippur).
        for (int x = 4; x <= 21; x++) {
            int y = 6 - (x - 4) / 5;
            setLedPixel(x, y);
            setLedPixel(x, y - 1);
        }
        for (int x = 22; x <= 27; x++) {
            int top = 1 - (x - 22) / 4;
            int bot = 4 + (x - 22) / 4;
            for (int y = top; y <= bot; y++) setLedPixel(x, y);
        }
        static const uint8_t waveX[3] = { 29, 30, 31 };
        uint8_t phase = (uint8_t) ((frame / 4) % 3);
        setLedPixel(waveX[phase], 1 + phase);
        setLedPixel(waveX[phase], 4 - phase);
        setLedPixel(waveX[(phase + 1) % 3], 2);
    } else if (type == ALERT_ANIM_MENORAH) {
        // Seven-branch menorah with flickering flames (Hanukkah).
        for (int x = 11; x <= 21; x++) setLedPixel(x, 7);
        for (int y = 4; y <= 6; y++) setLedPixel(16, y);
        for (int x = 7; x <= 25; x++) setLedPixel(x, 4);
        static const uint8_t candleX[7] = { 7, 10, 13, 16, 19, 22, 25 };
        for (uint8_t i = 0; i < 7; i++) {
            int x = candleX[i];
            setLedPixel(x, 3);
            if (((frame / 4 + i) % 3) != 0) {
                setLedPixel(x, 2);
                setLedPixel(x - 1 + (int) ((frame / 3 + i) % 2), 1);
            }
        }
        setLedPixel(16, 2);
        setLedPixel(16, 1);
    } else if (type == ALERT_ANIM_TREE) {
        // Rounded tree with blinking fruit (Tu BiShvat).
        static const uint8_t tree[5] = { 0b0011100, 0b0111110, 0b1111111, 0b0111110, 0b0011100 };
        for (uint8_t row = 0; row < 5; row++)
            for (uint8_t col = 0; col < 7; col++)
                if (tree[row] & (1 << (6 - col))) setLedPixel(12 + col, 1 + row);
        for (int y = 6; y <= 7; y++) { setLedPixel(15, y); setLedPixel(16, y); }
        static const uint8_t fx[6] = { 11, 13, 15, 17, 19, 20 };
        static const uint8_t fy[6] = { 2, 4, 3, 2, 4, 3 };
        for (uint8_t i = 0; i < 6; i++)
            if (((frame / 4) + i) % 2 == 0) setLedPixel(fx[i], fy[i]);
    } else if (type == ALERT_ANIM_MASK) {
        // Carnival mask with a blink (Purim).
        for (int x = 9; x <= 23; x++) { setLedPixel(x, 1); setLedPixel(x, 6); }
        for (int y = 2; y <= 5; y++) { setLedPixel(9, y); setLedPixel(23, y); }
        for (int x = 12; x <= 14; x++) {
            if ((frame / 6) % 2 == 0) setLedPixel(x, 3);
            else setLedPixel(x, 4);
        }
        for (int x = 18; x <= 20; x++) {
            if ((frame / 6) % 2 == 0) setLedPixel(x, 3);
            else setLedPixel(x, 4);
        }
        for (int x = 14; x <= 18; x++) setLedPixel(x, 5);
        setLedPixel(7, 2); setLedPixel(6, 1); setLedPixel(7, 5); setLedPixel(6, 6);
        setLedPixel(25, 2); setLedPixel(26, 1); setLedPixel(25, 5); setLedPixel(26, 6);
    } else if (type == ALERT_ANIM_MATZAH) {
        // Square matzah with holes (Pesach).
        for (int x = 8; x <= 23; x++) { setLedPixel(x, 1); setLedPixel(x, 7); }
        for (int y = 1; y <= 7; y++) { setLedPixel(8, y); setLedPixel(23, y); }
        static const uint8_t hx[8] = { 11, 15, 19, 12, 17, 21, 14, 18 };
        static const uint8_t hy[8] = { 3, 2, 3, 5, 5, 4, 6, 2 };
        for (uint8_t i = 0; i < 8; i++)
            if (((frame / 5) + i) % 3 != 0) setLedPixel(hx[i], hy[i]);
    } else if (type == ALERT_ANIM_TORAH) {
        // Torah scroll with a crown (Shavuot / Simchat Torah / Sigd).
        for (int y = 1; y <= 7; y++) { setLedPixel(8, y); setLedPixel(23, y); }
        for (int x = 9; x <= 22; x++) { setLedPixel(x, 2); setLedPixel(x, 6); }
        for (int x = 11; x <= 20; x++)
            if (((x + (int) (frame / 4)) % 3) != 0) setLedPixel(x, 4);
        setLedPixel(15, 0); setLedPixel(16, 0); setLedPixel(17, 0);
        setLedPixel(16, 1);
        setLedPixel(7, 1); setLedPixel(7, 7); setLedPixel(24, 1); setLedPixel(24, 7);
    } else if (type == ALERT_ANIM_SUKKAH) {
        // Sukkah with branches, door, lulav and etrog (Sukkot).
        for (int x = 6; x <= 25; x++) setLedPixel(x, 2 - (x & 1));
        for (int y = 3; y <= 7; y++) { setLedPixel(8, y); setLedPixel(23, y); }
        for (int x = 8; x <= 23; x++) setLedPixel(x, 7);
        for (int y = 5; y <= 7; y++) { setLedPixel(14, y); setLedPixel(17, y); }
        setLedPixel(15, 5); setLedPixel(16, 5);
        for (int y = 2; y <= 6; y++) setLedPixel(26, y);
        setLedPixel(27, 1); setLedPixel(27, 2); setLedPixel(27, 3);
        setLedPixel(28, 6); setLedPixel(29, 6);
        if ((frame / 4) % 2) setLedPixel(27, 4);
    } else if (type == ALERT_ANIM_FLAG) {
        // Israeli flag with Magen David (Yom HaAtzmaut / Yerushalayim).
        for (int y = 0; y <= 7; y++) setLedPixel(2, y);
        for (int x = 3; x <= 30; x++) { setLedPixel(x, 1); setLedPixel(x, 6); }
        static const uint8_t star[7] = {
            0b0001000, 0b0010100, 0b0100010, 0b1111111,
            0b0100010, 0b0010100, 0b0001000
        };
        for (uint8_t row = 0; row < 7; row++)
            for (uint8_t col = 0; col < 7; col++)
                if (star[row] & (1 << (6 - col))) setLedPixel(12 + col, row);
    } else if (type == ALERT_ANIM_MEMORIAL) {
        // Memorial candle with small stars (Yom HaShoah / Yom HaZikaron).
        for (int x = 15; x <= 17; x++) setLedPixel(x, 7);
        for (int y = 4; y <= 7; y++) { setLedPixel(15, y); setLedPixel(17, y); }
        setLedPixel(16, 3);
        setLedPixel(16, 2);
        if ((frame / 2) % 3 == 0) setLedPixel(15, 2);
        else if ((frame / 2) % 3 == 1) setLedPixel(17, 2);
        else { setLedPixel(15, 1); setLedPixel(17, 1); }
        setLedPixel(6, 2); setLedPixel(5, 3); setLedPixel(7, 3); setLedPixel(6, 4);
        setLedPixel(26, 2); setLedPixel(25, 3); setLedPixel(27, 3); setLedPixel(26, 4);
    } else if (type == ALERT_ANIM_BONFIRE) {
        // Bonfire with logs and sparks (Lag BaOmer).
        for (int x = 9; x <= 22; x++) setLedPixel(x, 7);
        for (int x = 11; x <= 20; x++) setLedPixel(x, 6);
        for (int i = 0; i < 6; i++) {
            int y = 5 - i;
            int half = i / 2;
            int dx = (int) ((frame / 3 + i) % 2);
            for (int x = 16 - half; x <= 16 + half; x++) setLedPixel(x + dx, y);
        }
        static const uint8_t sparkX[4] = { 10, 14, 19, 23 };
        for (uint8_t i = 0; i < 4; i++)
            if (((frame / 3) + i) % 2 == 0)
                setLedPixel(sparkX[i], (frame / 4 + i * 2) % 5);
    }
}

void startAlertAnimation(uint8_t type, const char* text, uint8_t speed)
{
    // A rain/heat warning must not be covered by a Shabbat or holiday greeting.
    // Queue the greeting text instead; it will play after the warning.
    if (weatherAlertActive && type != ALERT_ANIM_RAIN && type != ALERT_ANIM_HEAT)
    {
        startMsg(text, speed);
        return;
    }

    alertAnimType = type;
    alertAnimStartMs = millis();
    alertAnimActive = true;
    alertAnimSpeed = speed;
    strncpy(alertAnimMsg, text, sizeof(alertAnimMsg) - 1);
    alertAnimMsg[sizeof(alertAnimMsg) - 1] = 0;

    // The warning animation has priority over a running clock message/date scroll.
    showMsg = false;
    msgQueueCount = 0;
    z_PosX = maxPosX;
    d_PosX = -8;
    digitAnimCancelAll();
}

//**************************************************************************************************
void timer50ms() {
    static unsigned int cnt50ms = 0;
    f_tckr50ms = true;
    cnt50ms++;
    if (cnt50ms == 20) {
        f_tckr1s = true; // 1 sec
        cnt50ms = 0;
    }
}
//**************************************************************************************************
void handleSerialCommand()
{
    static char line[24];
    static uint8_t len = 0;
    while (Serial.available())
    {
        char c = (char) Serial.read();
        if (c == '\r' || c == '\n')
        {
            line[len] = 0;
            if (len)
            {
                if (strcmp(line, "ap") == 0)
                {
                    Serial.println("Serial: starting setup AP...");
                    apUpdatePending = millis() + 100UL;
                }
                else if (strcmp(line, "reboot") == 0)
                {
                    Serial.println("Serial: rebooting...");
                    otaRebootAt = millis() + 500UL;
                }
                else if (strcmp(line, "status") == 0)
                {
                    Serial.printf("Serial: %s heap=%u rssi=%d wifi=%d\r\n",
                                  FIRMWARE_VERSION, ESP.getFreeHeap(), WiFi.RSSI(), (int) WiFi.status());
                }
                else if (strncmp(line, "wifi ", 5) == 0)
                {
                    char* arg = line + 5;
                    char* sep = strchr(arg, '|');
                    if (sep)
                    {
                        *sep = 0;
                        strncpy(settings.ssid, arg, sizeof(settings.ssid) - 1);
                        settings.ssid[sizeof(settings.ssid) - 1] = 0;
                        strncpy(settings.pass, sep + 1, sizeof(settings.pass) - 1);
                        settings.pass[sizeof(settings.pass) - 1] = 0;
                        settings_save();
                        Serial.println("Serial: WiFi credentials saved, rebooting");
                        otaRebootAt = millis() + 800UL;
                    }
                    else
                        Serial.println("Serial: use wifi SSID|PASSWORD");
                }
                else
                    Serial.println("Serial commands: ap, reboot, status, wifi SSID|PASSWORD");
            }
            len = 0;
        }
        else if (len < sizeof(line) - 1)
            line[len++] = c;
    }
}
//**************************************************************************************************
//
//The setup function is called once at startup of the sketch
void setup() {
    // Add your initialization code here
    pinMode(16, INPUT_PULLDOWN_16);
    pinMode(CS, OUTPUT);
    digitalWrite(CS, HIGH);
    Serial.begin(115200);
    settings_load();              // load saved settings from EEPROM
    Serial.printf("Settings loaded: SSID:%s PSW:%s NTP:%s TZ:%d BR:%d ORIENT:%d DST:%d COLON:%d DIGANIM:%d MARQ:%d PULSE:%d NTPINT:%d WEATHER:%d WCITY:%s WCITYSHOW:%d WINT:%d WSRC:%d/%d WSPEED:%d SDINT:%d RWARN:%d HWARN:%d RTHR:%d.%d HTHR:%d.%d WARNANIM:%d SHABBAT:%d SLEAD:%d SFREQ:%d SANIM:%d HOLIDAY:%d WPAUTH:%s\r\n", settings.ssid, passHidden(settings.pass), settings.ntpServer, settings.tzOffset, settings.brightness, settings.orient, settings.dstRule, settings.colonAnim, settings.digitAnim, settings.marquee, settings.pulse, settings.ntpInterval, settings.weather, settings.weatherCity, settings.weatherCityShow, settings.weatherInterval, settings.weatherSource, settings.imsStation, settings.weatherSpeed, settings.scrollDateInt, settings.rainWarn, settings.heatWarn, settings.rainThresh10 / 10, settings.rainThresh10 % 10, settings.heatThresh10 / 10, settings.heatThresh10 % 10, settings.warnAnim, settings.shabbat, settings.shabbatLead, settings.shabbatFreq, settings.shabbatAnim, settings.holidayGreetings, settings.webPass[0] ? "set" : "none");
    if (!settings.weather)
        Serial.println("Weather: disabled in settings");
    else if (strlen(settings.weatherCity) == 0)
        Serial.println("Weather: enabled but city is empty");
    SPI.begin();
    helpArr_init();
    max7219_init();
    max7219_set_brightness(settings.brightness);//0-15
    rtc_init(SDA, SCL);
    clear_Display();
    refresh_display(); //take info into LEDarr
    tckr.attach(0.05, timer50ms);    // every 50 msec
    otaWatchdog.attach(1.0, otaWatchdogTick);   // revoke a permanently stalled OTA upload
    //////////////////////////////////
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);     // auto reconnect if the AP drops the link
    WiFi.setSleep(false);            // keep the web UI/OTA responsive; the clock is mains-powered
    if (!autoConfig())// if WiFi can't be configured, enter smart config mode
    {
       if (strlen(settings.ssid) > 0)
       {
           startAP();   // settings configured but connection failed -> settings page via AP
       }
       else
       {
           smartConfig();
           if (WiFi.status() != WL_CONNECTED)
               startAP();
       }
    }
    ///////////////////////////////////
    server.on("/", handleRoot);
    server.on("/v", HTTP_GET, handleValues);   // settings values JSON (single-segment response, like /)
    server.on("/status", HTTP_GET, handleStatus);   // live heap/WiFi/OTA diagnostics
    server.on("/weather", HTTP_GET, handleWeatherPage);   // last rain/weather values vs threshold
    server.on("/scan", HTTP_GET, handleScan);       // WiFi network scan for the settings page
    server.on("/reset", HTTP_POST, handleReset);    // restore factory defaults
    server.on("/save", HTTP_POST, handleSave);   // POST only: a later GET/refresh on /save must NOT re-run the save
    server.on("/update", HTTP_GET, handleUpdatePage);
    server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    server.on("/apupdate", HTTP_GET, handleAPUpdate);
    server.on("/reboot", HTTP_GET, handleReboot);
    server.begin();
    Serial.println("Web server started");
    ///////////////////////////////////
    udp.begin(localPort);   // always needed for NTP; in AP-mode boots autoConfig()/smartConfig() never ran their udp.begin()
    Serial.print("Local port: ");
    Serial.println(udp.localPort());
    tm* tt;
    if (WiFi.status() == WL_CONNECTED)   // only sync when connected; in AP mode there is no router to reach
    {
        tt = connectNTP();// get network time
        if (tt != NULL)   // if network time was received, write it to the RTC
        {
            rtc_set(tt);
            startMsg("NTP Clock Sync. Successful.");
        }
        else
            Serial.println("no time packet received");
    }
}
//**************************************************************************************************
// The loop function is called in an endless loop
void loop() {
    //Add your repeated code here
    unsigned int sek1 = 0, sek2 = 0, min1 = 0, min2 = 0, std1 = 0, std2 = 0;
    unsigned int sek11 = 0, sek12 = 0, sek21 = 0, sek22 = 0;
    unsigned int min11 = 0, min12 = 0, min21 = 0, min22 = 0;
    unsigned int std11 = 0, std12 = 0, std21 = 0, std22 = 0;
    unsigned int f_scroll_x = false;

    z_PosX = maxPosX;
    d_PosX = -8;

    refresh_display();
    while (true) {
        yield();  // on the ESP8266, if the loop must run long operations (e.g. floating point math), call yield regularly so the 8266 keeps running in the background without rebooting or dropping the network.
        handleSerialCommand();
        server.handleClient();   // handle web settings page requests
        if (otaRebootAt != 0 && (long) (millis() - otaRebootAt) >= 0)
        {
            Serial.println("OTA: rebooting");
            ESP.restart();
        }
        if (apUpdatePending != 0 && (long) (millis() - apUpdatePending) >= 0)
        {
            apUpdatePending = 0;
            enterAPUpdateMode();
        }
        if (apUpdateMode)
        {
            static uint8_t lastAPClients = 0;
            uint8_t apClients = WiFi.softAPgetStationNum();
            if (apClients != lastAPClients)
            {
                lastAPClients = apClients;
                apUpdateSince = millis();
            }
            if (!Update.isRunning() && apClients == 0 && millis() - apUpdateSince > 600000UL)
            {
                Serial.println("AP update mode idle 10 min, rebooting");
                ESP.restart();
            }
        }
        bool otaBusy = Update.isRunning() || otaRebootAt != 0;   // do not start blocking NTP/weather work during a firmware update
        static bool wifiReconnectNotify = false;   // set by the 50ms tick when WiFi comes up
        static bool ntpSyncRequested = false;      // set on a genuine reconnect or when leaving AP mode (not the boot connect)
        if (!otaBusy && rtcInitialized && settings.resync && WiFi.status() == WL_CONNECTED)   // daily RTC/NTP resync: sync directly and show the status
        {
            static bool resyncRan = false;
            if (!(MEZ.std12 == settings.resyncHour && MEZ.min12 == settings.resyncMin))
                resyncRan = false;   // re-arm once we leave the resync minute (next day)
            if (MEZ.sek12 == 0 && !resyncRan && MEZ.std12 == settings.resyncHour && MEZ.min12 == settings.resyncMin)
            {
                resyncRan = true;
                tm* tt = connectNTP();
                if (tt != NULL)
                {
                    rtc_set(tt);
                    Serial.println("Daily resync OK");
                    startMsg("NTP Clock Sync. Successful.");
                }
                else
                {
                    Serial.println("Daily resync failed (no NTP reply)");
                    startMsg("Sync Failed");
                }
            }
        }
        if (!otaBusy && rtcInitialized && settings.ntpInterval > 0 && WiFi.status() == WL_CONNECTED)   // periodic NTP sync every ntpInterval minutes
        {
            static unsigned long intervalLastSync = 0;
            static bool intervalArmed = false;
            if (!intervalArmed)
            {
                intervalLastSync = millis();
                intervalArmed = true;
            }
            if ((millis() - intervalLastSync) >= (unsigned long) settings.ntpInterval * 60000UL)
            {
                intervalArmed = false;
                tm* tt = connectNTP();
                if (tt != NULL)
                {
                    rtc_set(tt);
                    Serial.println("Periodic NTP sync OK");
                    startMsg("NTP Clock Sync. Successful.");
                }
                else
                {
                    Serial.println("Periodic NTP sync failed (no NTP reply)");
                    startMsg("Sync Failed");
                }
            }
        }
        if (!otaBusy && rtcInitialized && settings.weatherSource != 2 && settings.weather && WiFi.status() == WL_CONNECTED && strlen(settings.weatherCity) > 0)   // periodic Open-Meteo update
        {
            static unsigned long weatherLast = 0;
            static bool weatherArmed = false;
            static bool weatherFirst = true;
            if (!weatherArmed)
            {
                weatherLast = millis();
                weatherArmed = true;
                if (weatherFirst)
                    Serial.printf("Weather: active, first check in 15s (city %s, then every %d min)\r\n",
                                  settings.weatherCity, settings.weatherInterval);
                else
                    Serial.printf("Weather: active, next check in %d min (city %s)\r\n",
                                  settings.weatherInterval, settings.weatherCity);
            }
            // Make the first weather check after boot quick, so warnings can be tested
            // without waiting for the full weather interval.
            unsigned long weatherWait = weatherFirst ? 15000UL
                                                     : (unsigned long) settings.weatherInterval * 60000UL;
            if ((millis() - weatherLast) >= weatherWait)
            {
                weatherArmed = false;
                weatherFirst = false;
                Serial.printf("Weather: checking %s...\r\n", settings.weatherCity);
                double temp;
                int hum;
                double precip;
                double rainNext;
                int wmoCode;
                if (fetchWeather(temp, hum, precip, rainNext, wmoCode))
                {
                    double rainLimit = settings.rainThresh10 / 10.0;
                    double heatLimit = settings.heatThresh10 / 10.0;
                    bool localFresh = settings.weatherSource && imsRainValid && (millis() - imsUpdatedMs < 2700000UL);
                    double nowRate = localFresh ? imsRainRate : precip;
                    double nowTemp = (localFresh && imsTempValid) ? imsTempC : temp;
                    int nowHum = (localFresh && imsHumValid) ? imsHumidity : hum;
                    Serial.printf("Weather check: %.1fC (heat %s, limit %.1f), now %.1fmm/h%s peak2h %.1fmm/h (rain %s, limit %.1f, code %d)\r\n",
                                  temp, settings.heatWarn ? "on" : "off", heatLimit,
                                  nowRate, localFresh ? " local" : "", rainNext, settings.rainWarn ? "on" : "off", rainLimit, wmoCode);
                    char buf[160];
                    uint8_t alertType = ALERT_ANIM_NONE;
                    bool rainNow = settings.rainWarn && nowRate >= rainLimit;
                    bool rainSoon = settings.rainWarn && rainNext >= rainLimit;
                    if (rainNow || rainSoon)
                    {
                        alertType = ALERT_ANIM_RAIN;
                        // Show the current rate (local IMS when available) plus the
                        // peak rate forecast for the next two hours.
                        if (settings.weatherCityShow)
                            snprintf(buf, sizeof buf, "RAIN WARNING: NOW %.1fmm/h NEXT 2H PEAK %.1fmm/h %s Outside Temp: %.1fC, Humidity: %d%%",
                                     nowRate, rainNext, settings.weatherCity, nowTemp, nowHum);
                        else
                            snprintf(buf, sizeof buf, "RAIN WARNING: NOW %.1fmm/h NEXT 2H PEAK %.1fmm/h Outside Temp: %.1fC, Humidity: %d%%",
                                     nowRate, rainNext, nowTemp, nowHum);
                    }
                    else if (settings.heatWarn && temp >= heatLimit)
                    {
                        alertType = ALERT_ANIM_HEAT;
                        if (settings.weatherCityShow)
                            snprintf(buf, sizeof buf, "HEAT WARNING: %.1fC %s Outside Temp: %.1fC, Humidity: %d%%", heatLimit, settings.weatherCity, temp, hum);
                        else
                            snprintf(buf, sizeof buf, "HEAT WARNING: %.1fC Outside Temp: %.1fC, Humidity: %d%%", heatLimit, temp, hum);
                    }
                    else
                    {
                        if (settings.weatherCityShow)
                            snprintf(buf, sizeof buf, "%s Outside Temp: %.1fC, Humidity: %d%%", settings.weatherCity, temp, hum);
                        else
                            snprintf(buf, sizeof buf, "Outside Temp: %.1fC, Humidity: %d%%", temp, hum);
                    }
                    if (alertType != ALERT_ANIM_NONE && settings.warnAnim)
                    {
                        weatherAlertActive = true;
                        weatherAlertMsgActive = false;
                        startAlertAnimation(alertType, buf, settings.weatherSpeed);
                        f_scroll_x = false;   // discard any date scroll; the alert has priority
                    }
                    else if (alertType != ALERT_ANIM_NONE)
                    {
                        // Warn without animation, but still give the warning priority over a
                        // running Shabbat/holiday message or animation.
                        weatherAlertActive = true;
                        weatherAlertMsgActive = true;
                        alertAnimActive = false;
                        showMsg = false;
                        msgQueueCount = 0;
                        startMsg(buf, settings.weatherSpeed);
                        f_scroll_x = false;
                    }
                    else
                        startMsg(buf, settings.weatherSpeed);
                }
                else
                    startMsg("WEATHER FAIL");
            }
        }
        if (!otaBusy && rtcInitialized && settings.weatherSource && WiFi.status() == WL_CONNECTED)   // IMS local rain from the public 10-min XML
        {
            static unsigned long imsLast = 0;
            static bool imsArmed = false;
            static bool imsFirst = true;
            static bool imsWasWet = false;
            static unsigned long imsLastWarn = 0;
            static unsigned long imsLastMsg = 0;
            static unsigned long imsLastHeat = 0;
            if (!imsArmed)
            {
                imsLast = millis();
                imsArmed = true;
                Serial.printf("IMS: local station %s check in %s (then every 10 min)\r\n",
                              settings.imsStation == 0 ? "nearest" : "selected",
                              imsFirst ? "20s" : "10 min");
            }
            unsigned long imsWait = imsFirst ? 20000UL : 600000UL;
            if ((millis() - imsLast) >= imsWait)
            {
                imsArmed = false;
                imsFirst = false;
                Serial.println("IMS: checking local station...");
                if (fetchImsXmlRain())
                {
                    double limit = settings.rainThresh10 / 10.0;
                    bool wet = imsRainRate >= limit;
                    if (settings.rainWarn && wet && (!imsWasWet || (millis() - imsLastWarn) >= 1200000UL))
                    {
                        char buf[160];
                        if (!imsTempValid || !imsHumValid)
                            snprintf(buf, sizeof buf, "RAIN WARNING: LOCAL %.1fmm/h", imsRainRate);
                        else if (settings.weatherCityShow)
                            snprintf(buf, sizeof buf, "RAIN WARNING: LOCAL %.1fmm/h %s Outside Temp: %.1fC, Humidity: %d%%",
                                     imsRainRate, settings.weatherCity, imsTempC, imsHumidity);
                        else
                            snprintf(buf, sizeof buf, "RAIN WARNING: LOCAL %.1fmm/h Outside Temp: %.1fC, Humidity: %d%%",
                                     imsRainRate, imsTempC, imsHumidity);
                        weatherAlertActive = true;
                        if (settings.warnAnim)
                        {
                            weatherAlertMsgActive = false;
                            startAlertAnimation(ALERT_ANIM_RAIN, buf, settings.weatherSpeed);
                        }
                        else
                        {
                            weatherAlertMsgActive = true;
                            alertAnimActive = false;
                            showMsg = false;
                            msgQueueCount = 0;
                            startMsg(buf, settings.weatherSpeed);
                        }
                        f_scroll_x = false;
                        imsLastWarn = millis();
                    }
                    if (settings.weatherSource == 2)
                    {
                        double heatLimit = settings.heatThresh10 / 10.0;
                        if (settings.heatWarn && imsTempValid && imsTempC >= heatLimit
                            && (imsLastHeat == 0 || (millis() - imsLastHeat) >= 1200000UL))
                        {
                            char buf[160];
                            if (settings.weatherCityShow)
                                snprintf(buf, sizeof buf, "HEAT WARNING: LOCAL %.1fC %s Humidity: %d%%",
                                         imsTempC, settings.weatherCity, imsHumValid ? imsHumidity : 0);
                            else
                                snprintf(buf, sizeof buf, "HEAT WARNING: LOCAL %.1fC Humidity: %d%%",
                                         imsTempC, imsHumValid ? imsHumidity : 0);
                            weatherAlertActive = true;
                            if (settings.warnAnim)
                            {
                                weatherAlertMsgActive = false;
                                startAlertAnimation(ALERT_ANIM_HEAT, buf, settings.weatherSpeed);
                            }
                            else
                            {
                                weatherAlertMsgActive = true;
                                alertAnimActive = false;
                                showMsg = false;
                                msgQueueCount = 0;
                                startMsg(buf, settings.weatherSpeed);
                            }
                            f_scroll_x = false;
                            imsLastHeat = millis();
                        }
                        else if (settings.weather
                                 && (imsLastMsg == 0 || (millis() - imsLastMsg) >= (unsigned long) settings.weatherInterval * 60000UL))
                        {
                            char buf[160];
                            if (settings.weatherCityShow)
                                snprintf(buf, sizeof buf, "%s Outside Temp: %.1fC, Humidity: %d%%",
                                         settings.weatherCity, imsTempValid ? imsTempC : 0.0f, imsHumValid ? imsHumidity : 0);
                            else
                                snprintf(buf, sizeof buf, "Outside Temp: %.1fC, Humidity: %d%%",
                                         imsTempValid ? imsTempC : 0.0f, imsHumValid ? imsHumidity : 0);
                            startMsg(buf, settings.weatherSpeed);
                            imsLastMsg = millis();
                        }
                    }
                    imsWasWet = wet;
                }
            }
        }
        if (f_tckr1s == true)        // flag 1sek
        {if (settings.orient == 0) kk = !digitalRead(16);      // auto via pin 16
            else if (settings.orient == 1) kk = 0;              // normal
            else kk = 1;                                        // flipped
            static unsigned int wifiCheckCnt = 0;
            static bool reconnectActive = false;
            static unsigned long reconnectStart = 0;
            static uint8_t reconnectFailures = 0;
            static bool ntpSyncPending = false;
            static bool ntpSyncArm = false;

            if (!apUpdateMode && WiFi.status() == WL_CONNECTED)
            {
                wifiCheckCnt = 0;
                reconnectActive = false;
                reconnectFailures = 0;
                if (apActive)
                {
                    stopAP();   // router is back, fallback AP no longer needed
                    ntpSyncRequested = true;   // leaving AP mode -> resync the RTC once
                }
            }
            if (!apUpdateMode && wifiReconnectNotify)   // WiFi came up (boot or auto/manual reconnect)
            {
                wifiReconnectNotify = false;
                Serial.println("WiFi connected");
                Serial.println(WiFi.localIP());
                if (ntpSyncRequested)   // genuine reconnection, or leaving AP mode -> resync the RTC once
                {
                    startMsg("Wi-Fi Connected");
                    ntpSyncArm = true;
                    ntpSyncRequested = false;
                }
            }
            else if (!apUpdateMode && !apActive)
            {
                if (!reconnectActive)
                {
                    if (++wifiCheckCnt >= WIFI_CHECK_INTERVAL)   // check WiFi every 30s
                    {
                        wifiCheckCnt = 0;
                        wifi_reconnect_start();
                        reconnectActive = true;
                        reconnectStart = millis();
                    }
                }
                else if (millis() - reconnectStart > 10000)   // give up after 10s
                {
                    Serial.println("WiFi reconnect failed, will retry later");
                    reconnectActive = false;
                    wifiCheckCnt = 0;
                    reconnectFailures++;
                    if (!apActive && !apUpdateMode && reconnectFailures >= 3)
                    {
                        Serial.println("WiFi failed repeatedly, starting setup AP");
                        reconnectFailures = 0;
                        startAP();
                    }
                    if (apActive)
                    {
                        WiFi.setAutoReconnect(false);   // stop the SDK's background retry so the fallback AP stays responsive
                        WiFi.disconnect(false, false);
                    }
                }
            }

            if (!apUpdateMode && apShutdownPending && millis() - apShutdownSince > 8000)   // defer the opmode switch until the link has settled
            {
                apShutdownPending = false;
                if (WiFi.status() == WL_CONNECTED)
                {
                    WiFi.softAPdisconnect(true);
                    WiFi.mode(WIFI_STA);
                    Serial.println("AP stopped");
                }
                else
                {
                    apActive = true;   // router dropped again during the grace period: keep the fallback AP
                    apMsgUntil = millis() + 30000UL;
                    Serial.println("WiFi dropped again, keeping fallback AP");
                }
            }

            if (ntpSyncArm)   // one tick delay so the "Wi-Fi Connected" scroll is not blocked by connectNTP()
            {
                ntpSyncArm = false;
                ntpSyncPending = true;
            }
            if (!apUpdateMode && ntpSyncPending && !otaBusy)   // resync RTC once after a successful reconnect
            {
                ntpSyncPending = false;
                tm* tt = connectNTP();
                if (tt != NULL)
                {
                    rtc_set(tt);
                    Serial.println("RTC resynced after WiFi reconnect");
                    startMsg("NTP Clock Sync. Successful.");
                }
                else
                    Serial.println("no time packet received");
            }
            rtc2mez();
            rtcInitialized = true;
            sek1 = MEZ.sek1;
            sek2 = MEZ.sek2;
            min1 = MEZ.min1;
            min2 = MEZ.min2;
            std1 = MEZ.std1;
            std2 = MEZ.std2;
            sek1++;
            if (sek1 == 10) {
                sek2++;
                sek1 = 0;
            }
            if (sek2 == 6) {
                min1++;
                sek2 = 0;
            }
            if (min1 == 10) {
                min2++;
                min1 = 0;
            }
            if (min2 == 6) {
                std1++;
                min2 = 0;
            }
            if (std1 == 10) {
                std2++;
                std1 = 0;
            }
            if ((std2 == 2) && (std1 == 4)) {
                std1 = 0;
                std2 = 0;
            }

            sek11 = sek12;
            sek12 = sek1;
            sek21 = sek22;
            sek22 = sek2;
            min11 = min12;
            min12 = min1;
            min21 = min22;
            min22 = min2;
            std11 = std12;
            std12 = std1;
            std21 = std22;
            std22 = std2;
            f_tckr1s = false;
            {
                static uint16_t lastMinute = 0xFFFF;
                uint16_t nowMin = (uint16_t) (MEZ.std12 * 60 + MEZ.min12);
                if (nowMin != lastMinute)
                {
                    lastMinute = nowMin;
                    shabbatMinuteTick(nowMin);
                    holidayTick();
                }
            }
            if (settings.scrollDate && !alertAnimActive)   // scroll the date at the chosen interval
            {
                static unsigned short lastScrollMin = 0xFFFF;
                unsigned short minuteOfDay = MEZ.std12 * 60 + MEZ.min12;
                if (minuteOfDay != lastScrollMin)
                {
                    lastScrollMin = minuteOfDay;
                    if (minuteOfDay % settings.scrollDateInt == 0)
                        f_scroll_x = true;
                }
            }
            if (!showMsg && !f_scroll_x && !alertAnimActive)   // start transitions for the digits that changed
            {
                uint8_t newVals[6] = { (uint8_t) sek1, (uint8_t) sek2, (uint8_t) min1,
                                       (uint8_t) min2, (uint8_t) std1, (uint8_t) std2 };
                uint8_t oldVals[6] = { (uint8_t) sek11, (uint8_t) sek21, (uint8_t) min11,
                                       (uint8_t) min21, (uint8_t) std11, (uint8_t) std21 };
                uint8_t changed[6] = { (uint8_t) (newVals[0] != oldVals[0]),
                                       (uint8_t) (newVals[1] != oldVals[1]),
                                       (uint8_t) (newVals[2] != oldVals[2]),
                                       (uint8_t) (newVals[3] != oldVals[3]),
                                       (uint8_t) (newVals[4] != oldVals[4]),
                                       (uint8_t) (newVals[5] != oldVals[5]) };
                for (uint8_t i = 0; i < 6; i++) {
                    if (changed[i]) digitAnimStart(i, oldVals[i], newVals[i]);
                    else if (digitAnim[i].active) digitAnim[i].active = false;
                }
            }
            else
                digitAnimCancelAll();
        } // end 1s
        if (f_tckr50ms == true) {// the following runs every 50ms to control scroll speed
            f_tckr50ms = false;
            if (Update.isRunning() && otaRebootAt == 0)
                continue;   // keep the display/SPI quiet while firmware is being written
            if (alertAnimActive)   // show the rain/heat icon first, then scroll the warning text
            {
                unsigned long elapsed = millis() - alertAnimStartMs;
                if (elapsed < ALERT_ANIM_MS)
                {
                    clearLEDarr();
                    drawAlertAnimation(alertAnimType, (uint16_t) (elapsed / 50));
                    refresh_display();
                    continue;
                }
                bool wasWeatherAlert = (alertAnimType == ALERT_ANIM_RAIN || alertAnimType == ALERT_ANIM_HEAT);
                alertAnimActive = false;
                showMsg = false;
                msgQueueCount = 0;
                startMsg(alertAnimMsg, alertAnimSpeed);
                if (wasWeatherAlert)
                    weatherAlertMsgActive = true;
            }
            // Announce "Wi-Fi Connected" only when a link that had been stably up for a while
            // drops and comes back. A fixed boot-time grace period is not enough: the link often
            // settles with one extra drop/reconnect shortly after boot, which would otherwise be
            // mistaken for a genuine reconnection and announce on every boot.
            static bool wifiPrevUp = false;
            static bool wifiStable = false;      // link continuously up for >= WIFI_STABLE_MS
            static bool stableBeforeDrop = false;
            static unsigned long upSinceMs = 0;
            const unsigned long WIFI_STABLE_MS = 90000UL;
            bool wifiUp = (WiFi.status() == WL_CONNECTED);
            if (wifiUp != wifiPrevUp)   // link state changed
            {
                Serial.printf("WiFi link %s (had been stable: %d)\r\n", wifiUp ? "up" : "down", stableBeforeDrop);
                if (!wifiUp)
                    stableBeforeDrop = wifiStable;   // remember stability at the moment of the drop
                else
                {
                    wifiReconnectNotify = true;
                    if (stableBeforeDrop)   // link had been stably up before the drop -> genuine reconnect
                        ntpSyncRequested = true;
                }
                upSinceMs = millis();
                wifiStable = false;
            }
            else if (wifiUp && !wifiStable && millis() - upSinceMs >= WIFI_STABLE_MS)
                wifiStable = true;
            wifiPrevUp = wifiUp;
            if (showMsg) {
                z_PosX += 2;
                d_PosX += msgStep;
                int msgWidth = 0;
                for (int mi = 0; msg[mi] != 0; mi++) {
                    int k = (int)(unsigned char) msg[mi] - 32;
                    msgWidth += (k >= 0 && k < 96) ? font1[k][0] : 4;   // non-ASCII bytes count as a space width (never index font1 OOB)
                }
                if (d_PosX > 31 + msgWidth + 8) {   // whole message has scrolled past
                    z_PosX = maxPosX;
                    d_PosX = -8;
                    showMsg = false;
                    if (weatherAlertMsgActive)   // warning finished; let Shabbat/holiday messages through again
                    {
                        weatherAlertMsgActive = false;
                        weatherAlertActive = false;
                    }
                    if (apActive && millis() < apMsgUntil)   // keep repeating the AP setup hint for 30 seconds
                        startMsg(apUpdateMode ? "AP UPDATE 192.168.4.1" : "SETUP MODE AP 192.168.4.1");
                }
            }
            else if (msgQueueCount > 0) {
                startNextQueuedMsg();
            }
            else if (f_scroll_x == true) {
                z_PosX++;
                d_PosX++;
                if (d_PosX == 101)
                    z_PosX = 0;
                if (z_PosX == maxPosX) {
                    f_scroll_x = false;
                    d_PosX = -8;
                }
            }
            if (!showMsg)
                clearLEDarr();   // rebuild the frame from scratch: no animation/scroll trails

            if (showMsg) {
                int mi, mx = d_PosX;
                digitAnimCancelAll();
                clearLEDarr();   // erase clock digits + scroll trails so the message scrolls cleanly
                for (mi = 0; msg[mi] != 0; mi++) {
                    int k = (int)(unsigned char) msg[mi] - 32;
                    if (k >= 0 && k < 96)
                        char2Arr(msg[mi], mx - 1, 0);
                    mx -= (k >= 0 && k < 96) ? font1[k][0] : 4;   // step by the actual glyph width; non-ASCII bytes use a space width
                }
            }
            else {
                if (f_scroll_x)
                    digitAnimCancelAll();   // no transitions while the clock is scrolling sideways

                drawDigitCell(0, (uint8_t) sek1, z_PosX - 27, true);   // seconds ones (small font)
                drawDigitCell(1, (uint8_t) sek2, z_PosX - 23, true);   // seconds tens
                drawDigitCell(2, (uint8_t) min1, z_PosX - 18, false);  // minutes ones
                drawDigitCell(3, (uint8_t) min2, z_PosX - 13, false);  // minutes tens
                drawColon(z_PosX - 10);
                drawDigitCell(4, (uint8_t) std1, z_PosX - 4, false);   // hours ones
                drawDigitCell(5, (uint8_t) std2, z_PosX + 1, false);   // hours tens
                drawDate(d_PosX);
            }

            refresh_display(); //alle 50ms
        } //end 50ms
    }  //end while(true)
    //this section can not be reached
}
