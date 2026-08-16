/*********************************************************************************************************
  HACK LABS MatrixClock                                                                                 
  ********************************************************************************************************
  Author: HACK Lab                                                                                     
  YouTube ID: HACK Lab, welcome to subscribe: https://www.youtube.com/channel/UCxFY1FcIYK9d7riTvIh6eiA                                                          
  Version 2.1 
  HACK_CLOCK is a free download and may be used, modified, evaluated and
  distributed without charge provided the user adheres to version three of the GNU
  General Public License (GPL) and does not remove the copyright notice or this
  text.  The GPL V3 text is available on the gnu.org web site
  Author: HACK Lab                                                                                        
  Bilibili ID: HACK Lab  welcome to subscribe: https://space.bilibili.com/395145107                                                                             
  WeChat Official Account: HACK Lab, the only release point for open-source resources, regularly sharing open-source hardware and valuable technical articles, welcome to follow.                        
  Version 2.1                                                                                           
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

#define SDA        5      // Pin sda (I2C)
#define SCL        4      // Pin scl (I2C)
#define CS         15     // Pin cs  (SPI)
#define anzMAX     4        // number of dot matrix modules
#define WIFI_CHECK_INTERVAL 30  // seconds between WiFi status checks
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
    uint8_t rainThresh;      // rain warning threshold in mm (default 2)
    uint8_t heatThresh;      // heat warning threshold in °C (default 35)
};
ClockSettings settings = { 0xA5A5A5A5, "", "", "ch.pool.ntp.org", 8, 3, 0, 1, 0, 1, 0, 20, 0, 0, 0, 0, 0, 0, "", 30, 0, 0, 2, 1, 1, 1, 2, 35 };
const ClockSettings settings_defaults = { 0xA5A5A5A5, "", "", "ch.pool.ntp.org", 8, 3, 0, 1, 0, 1, 0, 20, 0, 0, 0, 0, 0, 0, "", 30, 0, 0, 2, 1, 1, 1, 2, 35 };
bool rtcInitialized = false;
bool apActive = false;              // AP fallback mode active (settings page accessible)

unsigned short maxPosX = anzMAX * 8 - 1;            
unsigned short LEDarr[anzMAX][8];                   
unsigned short helpArrMAX[anzMAX * 8];              
unsigned short helpArrPos[anzMAX * 8];              
int z_PosX = 0;                            
int d_PosX = 0;                            
bool showMsg = false;                                // a message is scrolling across the display
char msg[80] = "";
bool msgPending = false;                             // message queued, starts when the display is idle
char msgPendingText[80] = "";
uint8_t msgStep = 2;                                 // current message scroll speed (px per 50ms tick)
uint8_t msgPendingStep = 2;                          // scroll speed of the queued message
bool f_tckr1s = false;
bool f_tckr50ms = false;
bool kk;
unsigned long epoch = 0;
unsigned int localPort = 2390;                      // local port to listen for UDP packets
const int NTP_PACKET_SIZE = 48;                     // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE];                 // buffer to hold incoming and outgoing packets
IPAddress timeServerIP;                            
tm *tt, ttm;


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
bool autoConfig()
{
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleepMode(WIFI_MODEM_SLEEP);   // modem sleep to save power; lwIP keeps the link alive

    if (strlen(settings.ssid) > 0)
    {
        WiFi.persistent(false);   // settings are the source of truth, don't touch flash config
        Serial.printf("Connecting to SSID:%s PSW:%s\r\n", settings.ssid, settings.pass);
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

        WiFi.disconnect();
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
          Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
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
          Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
          WiFi.setAutoConnect(true);  // enable auto connect
          
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
    settings.colonAnim = settings.colonAnim > 4 ? 0 : settings.colonAnim;   // new fields: 0xFF from older firmware = off
    settings.marquee = 0;   // effects removed from the settings menu, keep disabled
    settings.pulse = 0;
    if (settings.ntpInterval > 720)   // field added later / garbage from old EEPROM -> off
        settings.ntpInterval = 0;
    if (settings.weather > 1)   // new fields: 0xFF from older firmware = off
        settings.weather = 0;
    if (settings.weatherInterval > 240 || settings.weatherInterval == 0)
        settings.weatherInterval = 30;
    if (settings.weatherSpeed < 1 || settings.weatherSpeed > 4)   // new field / garbage from old EEPROM -> normal
        settings.weatherSpeed = 2;
    if (settings.scrollDateInt < 1 || settings.scrollDateInt > 120)   // new field -> every minute
        settings.scrollDateInt = 1;
    if (settings.rainWarn > 1)   // new field / garbage from old EEPROM -> on
        settings.rainWarn = 1;
    if (settings.heatWarn > 1)   // new field / garbage from old EEPROM -> on
        settings.heatWarn = 1;
    if (settings.rainThresh < 1 || settings.rainThresh > 25)   // new field -> 2mm
        settings.rainThresh = 2;
    if (settings.heatThresh < 20 || settings.heatThresh > 50)   // new field -> 35C
        settings.heatThresh = 35;
    settings.weatherCity[sizeof(settings.weatherCity) - 1] = '\0';
    for (unsigned i = 0; i < sizeof(settings.weatherCity); i++)   // blank EEPROM (0xFF) from older firmware -> empty
        if ((unsigned char) settings.weatherCity[i] > 0x7E)
        {
            settings.weatherCity[i] = '\0';
            break;
        }
}
//**************************************************************************************************
//***************************Web Settings Page**********************************************************************
ESP8266WebServer server(80);
//**************************************************************************************************
void startAP()   // start fallback AP so the settings page stays reachable without router WiFi
{
    if (apActive) return;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("MatrixClock-Setup", "12345678");
    apActive = true;
    Serial.println("AP started: MatrixClock-Setup / password 12345678 / http://192.168.4.1");
}
//**************************************************************************************************
void stopAP()    // stop fallback AP once the router connection works again
{
    if (!apActive) return;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apActive = false;
    Serial.println("AP stopped");
}
//**************************************************************************************************
void startMsg(const char* text, uint8_t step = 2)   // scroll a message across the display; step = px per 50ms tick; queues if busy
{
    if (showMsg)
    {
        strncpy(msgPendingText, text, sizeof(msgPendingText) - 1);
        msgPendingText[sizeof(msgPendingText) - 1] = 0;
        msgPendingStep = step;
        msgPending = true;
    }
    else
    {
        strncpy(msg, text, sizeof(msg) - 1);
        msg[sizeof(msg) - 1] = 0;
        msgStep = step;
        showMsg = true;
        z_PosX = 0;
        d_PosX = -8;
    }
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
void sendChunked(int code, const char* contentType, const String& body)
    // ESP8266 lwIP can silently drop a TCP segment when a response is handed to the socket as one
    // large write() and never retransmit it -- which is why the settings page used to need several
    // reloads to fully load. Sending the same bytes as a series of small, explicit sendContent()
    // calls (each well under one TCP segment) with a yield() between them avoids that failure mode,
    // regardless of how long the page or the stored settings values happen to be.
{
    const size_t CHUNK = 512;
    server.setContentLength(body.length());
    server.send(code, contentType, "");
    size_t sentLen = 0;
    while (sentLen < body.length())
    {
        size_t n = min(CHUNK, body.length() - sentLen);
        server.sendContent(body.substring(sentLen, sentLen + n));
        sentLen += n;
        yield();
    }
}
//**************************************************************************************************
void handleRoot()
{
    // Cosmetic + mobile-optimized settings page. The transport now goes through sendChunked()
    // (see above), so this page is no longer squeezed under a single-TCP-segment byte budget --
    // it can be as nicely styled as we want without reintroducing the old reload bug.
    String html = "<!DOCTYPE html><html><head>"
        "<meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<meta name=theme-color content='#0a0d0b'>"
        "<title>MatrixClock</title>"
        "<style>"
        ":root{--bg:#0a0d0b;--panel:#121614;--panel2:#171c19;--line:#232a26;--accent:#22e07a;--accentd:#1a9c58;--text:#e8f0ec;--sub:#7c8c84}"
        "*{box-sizing:border-box}"
        "html,body{margin:0}"
        "body{background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;-webkit-tap-highlight-color:transparent}"
        ".wrap{max-width:480px;margin:0 auto;padding:20px 16px 8px}"
        "h1{font-size:19px;text-align:center;margin:4px 0 20px;color:var(--accent);letter-spacing:.5px;text-shadow:0 0 16px rgba(34,224,122,.35)}"
        "fieldset{border:1px solid var(--line);border-radius:14px;background:var(--panel);padding:2px 14px 4px;margin:0 0 16px}"
        "legend{padding:0 8px;font-size:11px;font-weight:600;letter-spacing:.08em;text-transform:uppercase;color:var(--accentd)}"
        ".row{display:block;padding:10px 0;border-bottom:1px solid var(--line)}"
        ".row:last-child{border-bottom:none}"
        ".row>span{display:block;font-size:12.5px;color:var(--sub);margin-bottom:6px}"
        "input[type=text],input[type=password],input[type=number],select{width:100%;padding:11px 12px;font-size:16px;color:var(--text);background:var(--panel2);border:1px solid var(--line);border-radius:9px;outline:none}"
        "input:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(34,224,122,.15)}"
        ".pwrap{position:relative}"
        ".pwrap input{padding-right:52px}"
        ".pwrap button{position:absolute;right:4px;top:4px;bottom:4px;width:44px;background:none;border:none;color:var(--sub);font-size:11px;text-transform:uppercase;letter-spacing:.05em}"
        ".row>.toggle{display:flex;align-items:center;justify-content:space-between;padding:2px 0}"
        ".toggle .k{font-size:13.5px;color:var(--text)}"
        ".sw{position:relative;width:46px;height:27px;flex:none;display:inline-block}"
        ".sw input{position:absolute;opacity:0;width:100%;height:100%;margin:0;cursor:pointer}"
        ".sw i{position:absolute;inset:0;background:#2b332e;border-radius:27px;transition:.15s;pointer-events:none}"
        ".sw i:before{content:'';position:absolute;width:21px;height:21px;left:3px;top:3px;background:#e8f0ec;border-radius:50%;transition:.15s}"
        ".sw input:checked+i{background:var(--accentd)}"
        ".sw input:checked+i:before{transform:translateX(19px);background:#fff}"
        ".savebar{position:sticky;bottom:0;padding:14px 0 calc(14px + env(safe-area-inset-bottom));background:linear-gradient(180deg,transparent,var(--bg) 35%)}"
        "button#sb{width:100%;padding:15px;font-size:15.5px;font-weight:600;background:var(--accent);color:#04150b;border:none;border-radius:12px;box-shadow:0 4px 18px rgba(34,224,122,.3)}"
        "button#sb:active{transform:scale(.98)}"
        "button#sb.busy{opacity:.6}"
        ".hint{font-size:12.5px;color:var(--sub);text-align:center;padding:16px 0}"
        "</style></head><body>"
        "<div class=wrap>"
        "<h1>&#9201; MatrixClock Settings</h1>"
        "<form id=f method=POST action=/save>"
        "<div id=fields><p class=hint>Loading settings...</p></div>"
        "<div class=savebar><button type=submit id=sb>Save Settings</button></div>"
        "</form>"
        "</div>"
        "<script>"
        "function E(s){return String(s).replace(/&/g,'&amp;').replace(/\"/g,'&quot;').replace(/</g,'&lt;');}"
        "var GROUPS=[['Network',['ssid','pass','ntp']],['Time &amp; Sync',['tz','dst','resync','resyncHour','resyncMin','ntpInterval']],['Display',['bright','orient','colonAnim']],['Date Scroll',['scrollDate','scrollDateInt','scrollDir']],['Weather',['weather','weatherCity','weatherInterval','weatherSpeed','rainWarn','rainThresh','heatWarn','heatThresh']]];"
        "var BOUNDS={bright:[0,15],scrollDateInt:[1,120],resyncHour:[0,23],resyncMin:[0,59],ntpInterval:[0,720],weatherInterval:[1,240],rainThresh:[1,25],heatThresh:[20,50]};"
        "function isToggle(x){var o=x[2];return o&&o.length==2&&o[0][0]=='0'&&o[0][1]=='Off'&&o[1][0]=='1'&&o[1][1]=='On';}"
        "function field(x){"
        "if(isToggle(x)){return '<label class=row><span class=toggle><span class=k>'+E(x[0])+'</span><span class=sw><input type=checkbox name=\"'+x[1]+'\" value=1'+(x[3]=='1'?' checked':'')+'><i></i></span></span></label>';}"
        "if(x[2]){var h='<div class=row><span>'+E(x[0])+'</span><select name=\"'+x[1]+'\">';"
        "for(var k=0;k<x[2].length;k++)h+='<option value=\"'+x[2][k][0]+'\"'+(x[2][k][0]==x[3]?' selected':'')+'>'+E(x[2][k][1])+'</option>';"
        "return h+'</select></div>';}"
        "if(x[1]=='pass'){return '<div class=row><span>'+E(x[0])+'</span><div class=pwrap><input type=password id=pw name=\"'+x[1]+'\" value=\"'+E(x[3])+'\" autocomplete=new-password autocapitalize=off spellcheck=false><button type=button id=pwt>Show</button></div></div>';}"
        "var b=BOUNDS[x[1]],extra;"
        "if(b)extra=' type=number inputmode=numeric min='+b[0]+' max='+b[1];"
        "else if(x[1]=='tz')extra=' type=number inputmode=numeric';"
        "else extra=' type=text autocomplete=off autocapitalize=off spellcheck=false';"
        "return '<div class=row><span>'+E(x[0])+'</span><input name=\"'+x[1]+'\" value=\"'+E(x[3])+'\"'+extra+'></div>';"
        "}"
        "fetch('/v').then(function(r){return r.json();}).then(function(d){"
        "var used={};GROUPS.forEach(function(g){g[1].forEach(function(k){used[k]=1;});});"
        "var byKey={};d.f.forEach(function(x){byKey[x[1]]=x;});"
        "var html='';"
        "GROUPS.forEach(function(g){var rows='';g[1].forEach(function(k){if(byKey[k])rows+=field(byKey[k]);});if(rows)html+='<fieldset><legend>'+g[0]+'</legend>'+rows+'</fieldset>';});"
        "var other='';d.f.forEach(function(x){if(!used[x[1]])other+=field(x);});"
        "if(other)html+='<fieldset><legend>Other</legend>'+other+'</fieldset>';"
        "document.getElementById('fields').innerHTML=html;"
        "var pwt=document.getElementById('pwt');"
        "if(pwt)pwt.onclick=function(){var p=document.getElementById('pw');if(p.type=='password'){p.type='text';pwt.textContent='Hide';}else{p.type='password';pwt.textContent='Show';}};"
        "}).catch(function(){document.getElementById('fields').innerHTML='<p class=hint>Could not load settings -- pull to refresh.</p>';});"
        "document.getElementById('f').addEventListener('submit',function(){var b=document.getElementById('sb');b.classList.add('busy');b.textContent='Saving...';});"
        "</script></body></html>";
    sendChunked(200, "text/html", html);
}
//**************************************************************************************************
void handleValues()
{
    String j = "{\"f\":[";
    j += "[\"SSID\",\"ssid\",null,\"" + jsonEsc(settings.ssid) + "\"],";
    j += "[\"WiFi Password\",\"pass\",null,\"" + jsonEsc(settings.pass) + "\"],";
    j += "[\"NTP Server\",\"ntp\",null,\"" + jsonEsc(settings.ntpServer) + "\"],";
    j += "[\"Time Zone\",\"tz\",null,\"" + String(settings.tzOffset) + "\"],";
    j += "[\"Daylight Saving Time\",\"dst\",[[\"0\",\"Off\"],[\"1\",\"IL\"],[\"2\",\"EU\"],[\"3\",\"US\"]],\"" + String(settings.dstRule) + "\"],";
    j += "[\"Brightness\",\"bright\",null,\"" + String(settings.brightness) + "\"],";
    j += "[\"Orientation\",\"orient\",[[\"0\",\"Auto\"],[\"1\",\"Norm\"],[\"2\",\"Flip\"]],\"" + String(settings.orient) + "\"],";
    j += "[\"Scroll Date\",\"scrollDate\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.scrollDate) + "\"],";
    j += "[\"Date Interval (min)\",\"scrollDateInt\",null,\"" + String(settings.scrollDateInt) + "\"],";
    j += "[\"Seconds Scroll Direction\",\"scrollDir\",[[\"0\",\"Up-Down\"],[\"1\",\"Down-Up\"]],\"" + String(settings.scrollDir) + "\"],";
    j += "[\"Colon Animation\",\"colonAnim\",[[\"0\",\"Solid\"],[\"1\",\"Blink\"],[\"2\",\"Slow\"],[\"3\",\"Dbl\"],[\"4\",\"Alt\"]],\"" + String(settings.colonAnim) + "\"],";
    j += "[\"NTP Resync\",\"resync\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.resync) + "\"],";
    j += "[\"Resync Hour\",\"resyncHour\",null,\"" + String(settings.resyncHour) + "\"],";
    j += "[\"Resync Minute\",\"resyncMin\",null,\"" + String(settings.resyncMin) + "\"],";
    j += "[\"NTP Interval (min)\",\"ntpInterval\",null,\"" + String(settings.ntpInterval) + "\"],";
    j += "[\"Weather\",\"weather\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.weather) + "\"],";
    j += "[\"Weather City\",\"weatherCity\",null,\"" + jsonEsc(settings.weatherCity) + "\"],";
    j += "[\"Weather Interval (min)\",\"weatherInterval\",null,\"" + String(settings.weatherInterval) + "\"],";
    j += "[\"Weather Speed\",\"weatherSpeed\",[[\"1\",\"Slow\"],[\"2\",\"Norm\"],[\"3\",\"Fast\"],[\"4\",\"VFast\"]],\"" + String(settings.weatherSpeed) + "\"],";
    j += "[\"Rain Warning\",\"rainWarn\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.rainWarn) + "\"],";
    j += "[\"Rain Threshold (mm, default 2)\",\"rainThresh\",null,\"" + String(settings.rainThresh) + "\"],";
    j += "[\"Heat Warning\",\"heatWarn\",[[\"0\",\"Off\"],[\"1\",\"On\"]],\"" + String(settings.heatWarn) + "\"],";
    j += "[\"Heat Threshold (°C, default 35)\",\"heatThresh\",null,\"" + String(settings.heatThresh) + "\"]";
    j += "]}";
    sendChunked(200, "application/json", j);
}
//**************************************************************************************************
void handleSave()
{
    strncpy(settings.ssid, server.arg("ssid").c_str(), sizeof(settings.ssid) - 1);
    settings.ssid[sizeof(settings.ssid) - 1] = '\0';
    strncpy(settings.pass, server.arg("pass").c_str(), sizeof(settings.pass) - 1);
    settings.pass[sizeof(settings.pass) - 1] = '\0';
    strncpy(settings.ntpServer, server.arg("ntp").c_str(), sizeof(settings.ntpServer) - 1);
    settings.ntpServer[sizeof(settings.ntpServer) - 1] = '\0';
    settings.tzOffset = (int8_t) server.arg("tz").toInt();
    settings.brightness = (uint8_t) constrain(server.arg("bright").toInt(), 0, 15);
    settings.orient = (uint8_t) constrain(server.arg("orient").toInt(), 0, 2);
    settings.scrollDate = (uint8_t) (server.arg("scrollDate").toInt() ? 1 : 0);
    settings.scrollDateInt = (uint16_t) constrain(server.arg("scrollDateInt").toInt(), 1, 120);
    settings.scrollDir = (uint8_t) (server.arg("scrollDir").toInt() ? 1 : 0);
    settings.colonAnim = (uint8_t) constrain(server.arg("colonAnim").toInt(), 0, 4);
    settings.marquee = 0;   // removed from settings menu, keep disabled
    settings.pulse = 0;
    settings.resync = (uint8_t) (server.arg("resync").toInt() ? 1 : 0);
    settings.resyncHour = (uint8_t) constrain(server.arg("resyncHour").toInt(), 0, 23);
    settings.resyncMin = (uint8_t) constrain(server.arg("resyncMin").toInt(), 0, 59);
    settings.ntpInterval = (uint16_t) constrain(server.arg("ntpInterval").toInt(), 0, 720);
    settings.weather = (uint8_t) (server.arg("weather").toInt() ? 1 : 0);
    strncpy(settings.weatherCity, server.arg("weatherCity").c_str(), sizeof(settings.weatherCity) - 1);
    settings.weatherCity[sizeof(settings.weatherCity) - 1] = '\0';
    settings.weatherInterval = (uint16_t) constrain(server.arg("weatherInterval").toInt(), 1, 240);
    settings.weatherSpeed = (uint8_t) constrain(server.arg("weatherSpeed").toInt(), 1, 4);
    settings.rainWarn = (uint8_t) (server.arg("rainWarn").toInt() ? 1 : 0);
    settings.heatWarn = (uint8_t) (server.arg("heatWarn").toInt() ? 1 : 0);
    settings.rainThresh = (uint8_t) constrain(server.arg("rainThresh").toInt(), 1, 25);
    settings.heatThresh = (uint8_t) constrain(server.arg("heatThresh").toInt(), 20, 50);
    settings.dstRule = (uint8_t) constrain(server.arg("dst").toInt(), 0, 3);
    settings_save();
    Serial.printf("Settings saved, SSID:%s PSW:%s\r\n", settings.ssid, settings.pass);
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
    delay(500);
    ESP.restart();
}
//**************************************************************************************************
//***************************Weather (Open-Meteo, free, no API key)**********************************************************************
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
    int code = http.GET();
    if (code != 200) { http.end(); return false; }
    out = http.getString();
    http.end();
    return true;
}
//**************************************************************************************************
bool fetchWeather(double& temp, int& hum, double& precip)   // resolve the city once, then read temperature + humidity + precipitation
{
    static char geocodedCity[sizeof(settings.weatherCity)] = "";
    if (strcmp(geocodedCity, settings.weatherCity) != 0)
    {
        String url = String("http://geocoding-api.open-meteo.com/v1/search?name=") + urlEncode(settings.weatherCity) + "&count=1&language=en";
        String resp;
        if (!httpGet(url, resp)) { Serial.println("Weather: geocode request failed"); return false; }
        double lat = jsonDoubleAfter(resp, "\"latitude\":");
        double lon = jsonDoubleAfter(resp, "\"longitude\":");
        if (lat == 0 && lon == 0) { Serial.println("Weather: city not found"); return false; }
        settings.weatherLat = (int32_t) (lat * 1000000);
        settings.weatherLon = (int32_t) (lon * 1000000);
        strncpy(geocodedCity, settings.weatherCity, sizeof(geocodedCity) - 1);
        geocodedCity[sizeof(geocodedCity) - 1] = 0;
        settings_save();
        Serial.printf("Weather: geocoded %s -> lat %d lon %d\r\n", settings.weatherCity, settings.weatherLat, settings.weatherLon);
    }
    char latbuf[16];
    char lonbuf[16];
    snprintf(latbuf, sizeof latbuf, "%.5f", settings.weatherLat / 1000000.0);
    snprintf(lonbuf, sizeof lonbuf, "%.5f", settings.weatherLon / 1000000.0);
    String url = String("http://api.open-meteo.com/v1/forecast?latitude=") + latbuf + "&longitude=" + lonbuf + "&current=temperature_2m,relative_humidity_2m,precipitation";
    String resp;
    if (!httpGet(url, resp)) { Serial.println("Weather: forecast request failed"); return false; }
    temp = jsonDoubleAfter(resp, "\"temperature_2m\":");
    hum = (int) jsonDoubleAfter(resp, "\"relative_humidity_2m\":");
    precip = jsonDoubleAfter(resp, "\"precipitation\":");
    Serial.printf("Weather: %.1fC %d%% %.1fmm\r\n", temp, hum, precip);
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
    WiFi.disconnect();
    delay(200);
    if (!apActive)     // while the fallback AP is up, stay in AP_STA mode so the AP stays reachable
    {
        WiFi.mode(WIFI_STA);
        delay(100);
    }
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
    delay(1000);                 // wait to see if a reply is available
    int cb = udp.parsePacket();
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
    unsigned long t4 = millis();        // client receive time
    // parse the server timestamps: T2 = server receive time (bytes 32-39), T3 = server transmit time (bytes 40-47)
    uint64_t t2 = ntpTimestampMs(packetBuffer + 32);
    uint64_t t3 = ntpTimestampMs(packetBuffer + 40);
    // round-trip time in real ms = client elapsed time minus the server's own processing time
    unsigned long rtt = (unsigned long)(t4 - t1) - (t3 > t2 ? (unsigned long)(t3 - t2) : 0);
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
    if (cb == 48)
        return (tt);
    else
        return (NULL);
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
    if (Monat > 12)
        Monat = 0;
    Tag = rtc_tag();//day
    if (Tag > 31)
        Tag = 0;
    WoTag = rtc_wochentag();
    if (WoTag == 7)
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

void char22Arr(unsigned short ch, int PosX, short PosY) { //characters into arr
    int i, j, k, l, m, o1, o2, o3, o4;  //in LEDarr
    PosX++;
    k = ch - 32;                        //ASCII position in font
    if ((k >= 0) && (k < 96))           //character found in font?
    {
        o4 = font2[k][0];                 //character width
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
                        l = font2[k][j + 1];
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

//***********************************Marquee Border Helper**********************************************************
void mqPerimeter(unsigned int k, int &px, int &py)   // walk the 32x8 border perimeter, k = 0..76
{
    if (k < 32)          { px = (int)k;      py = 0; }              // top edge, left -> right
    else if (k < 39)     { px = 31;          py = (int)k - 31; }    // right edge, down
    else if (k < 71)     { px = 70 - (int)k; py = 7; }              // bottom edge, right -> left
    else                 { px = 0;           py = 77 - (int)k; }    // left edge, up
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
//
//The setup function is called once at startup of the sketch
void setup() {
    // Add your initialization code here
    pinMode(16, INPUT);
    pinMode(CS, OUTPUT);
    digitalWrite(CS, HIGH);
    Serial.begin(115200);
    settings_load();              // load saved settings from EEPROM
    Serial.printf("Settings loaded: SSID:%s PSW:%s NTP:%s TZ:%d BR:%d ORIENT:%d DST:%d COLON:%d MARQ:%d PULSE:%d NTPINT:%d WEATHER:%d WCITY:%s WINT:%d WSPEED:%d SDINT:%d RWARN:%d HWARN:%d RTHR:%d HTHR:%d\r\n", settings.ssid, settings.pass, settings.ntpServer, settings.tzOffset, settings.brightness, settings.orient, settings.dstRule, settings.colonAnim, settings.marquee, settings.pulse, settings.ntpInterval, settings.weather, settings.weatherCity, settings.weatherInterval, settings.weatherSpeed, settings.scrollDateInt, settings.rainWarn, settings.heatWarn, settings.rainThresh, settings.heatThresh);
    SPI.begin();
    helpArr_init();
    max7219_init();
    max7219_set_brightness(settings.brightness);//0-15
    rtc_init(SDA, SCL);
    clear_Display();
    refresh_display(); //take info into LEDarr
    tckr.attach(0.05, timer50ms);    // every 50 msec
    //////////////////////////////////
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);     // auto reconnect if the AP drops the link
    WiFi.setSleep(true);             // enable modem sleep to save power
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
    server.on("/s", HTTP_GET, []() {   // TEMP: sweep test - serve n bytes of 'A' to find the reliable page-size cutoff
        int n = constrain(server.arg("n").toInt(), 0, 3000);
        String body;
        body.reserve(n);
        for (int i = 0; i < n; i++) body += 'A';
        server.send(200, "text/plain", body);
    });
    server.on("/save", HTTP_POST, handleSave);   // POST only: a later GET/refresh on /save must NOT re-run the save
    server.begin();
    Serial.println("Web server started");
    ///////////////////////////////////
    tm* tt;
    tt = connectNTP();// get network time
    if (tt != NULL)   // if network time was received, write it to the RTC
    {
        rtc_set(tt);
        msgPending = true;      // show "NTP TIME SYNC OK!" after the boot/daily-resync time sync
        strncpy(msgPendingText, "NTP TIME SYNC OK!", sizeof(msgPendingText) - 1);
        msgPendingText[sizeof(msgPendingText) - 1] = 0;
    }
    else
        Serial.println("no time packet received");
}
//**************************************************************************************************
// The loop function is called in an endless loop
void loop() {
    //Add your repeated code here
    unsigned int sek1 = 0, sek2 = 0, min1 = 0, min2 = 0, std1 = 0, std2 = 0;
    unsigned int sek11 = 0, sek12 = 0, sek21 = 0, sek22 = 0;
    unsigned int min11 = 0, min12 = 0, min21 = 0, min22 = 0;
    unsigned int std11 = 0, std12 = 0, std21 = 0, std22 = 0;
    signed int x = 0; //x1,x2;
    signed int y = 0, y1 = 0, y2 = 0, y3=0;
    bool updown = false;
    unsigned int sc1 = 0, sc2 = 0, sc3 = 0, sc4 = 0, sc5 = 0, sc6 = 0;
    bool f_scrollend_y = false;
    unsigned int f_scroll_x = false;

    z_PosX = maxPosX;
    d_PosX = -8;
    //  x=0; x1=0; x2=0;

    refresh_display();
    updown = (settings.scrollDir == 0);   // 0 = up to down, 1 = down to up
    if (updown == false) {
        y2 = -9;
        y1 = 8;
    }
    if (updown == true) { //scroll  up to down
        y2 = 8;
        y1 = -8;
    }
    while (true) {
        yield();  // on the ESP8266, if the loop must run long operations (e.g. floating point math), call yield regularly so the 8266 keeps running in the background without rebooting or dropping the network.
        server.handleClient();   // handle web settings page requests
        static bool wifiReconnectNotify = false;   // set by the 50ms tick when WiFi comes up
        static bool ntpSyncRequested = false;      // set on a genuine reconnect (not the boot connect)
        if (rtcInitialized && settings.resync)   // daily RTC/NTP resync: sync directly and show the status
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
                    startMsg("NTP TIME SYNC OK!");
                }
                else
                {
                    Serial.println("Daily resync failed (no NTP reply)");
                    startMsg("Sync Failed");
                }
            }
        }
        if (rtcInitialized && settings.ntpInterval > 0 && WiFi.status() == WL_CONNECTED)   // periodic NTP sync every ntpInterval minutes
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
                    startMsg("NTP TIME SYNC OK!");
                }
                else
                {
                    Serial.println("Periodic NTP sync failed (no NTP reply)");
                    startMsg("Sync Failed");
                }
            }
        }
        if (rtcInitialized && settings.weather && WiFi.status() == WL_CONNECTED && strlen(settings.weatherCity) > 0)   // periodic weather update
        {
            static unsigned long weatherLast = 0;
            static bool weatherArmed = false;
            if (!weatherArmed)
            {
                weatherLast = millis();
                weatherArmed = true;
            }
            if ((millis() - weatherLast) >= (unsigned long) settings.weatherInterval * 60000UL)
            {
                weatherArmed = false;
                double temp;
                int hum;
                double precip;
                if (fetchWeather(temp, hum, precip))
                {
                    char buf[80];
                    if (settings.rainWarn && precip >= settings.rainThresh)
                        snprintf(buf, sizeof buf, "RAIN WARNING: %.1fmm %s Outside Temp: %.1fC, Humidity: %d%%", precip, settings.weatherCity, temp, hum);
                    else if (settings.heatWarn && temp >= settings.heatThresh)
                        snprintf(buf, sizeof buf, "HEAT WARNING: %.0fC %s Outside Temp: %.1fC, Humidity: %d%%", temp, settings.weatherCity, temp, hum);
                    else
                        snprintf(buf, sizeof buf, "%s Outside Temp: %.1fC, Humidity: %d%%", settings.weatherCity, temp, hum);
                    startMsg(buf, settings.weatherSpeed);
                }
                else
                    startMsg("WEATHER FAIL");
            }
        }
        if (f_tckr1s == true)        // flag 1sek
        {if (settings.orient == 0) kk = !digitalRead(16);      // auto via pin 16
            else if (settings.orient == 1) kk = 0;              // normal
            else kk = 1;                                        // flipped
            static unsigned int wifiCheckCnt = 0;
            static bool reconnectActive = false;
            static unsigned long reconnectStart = 0;
            static bool ntpSyncPending = false;
            static bool ntpSyncArm = false;

            if (wifiReconnectNotify)   // WiFi came up (boot or auto/manual reconnect)
            {
                wifiReconnectNotify = false;
                Serial.println("WiFi connected");
                Serial.println(WiFi.localIP());
                if (ntpSyncRequested)   // genuine reconnection -> show success + resync RTC once (boot already synced in setup, boot "OK!!" shown by autoConfig)
                {
                    startMsg("WI-FI OK");
                    ntpSyncArm = true;
                }
            }
            if (WiFi.status() == WL_CONNECTED)
            {
                wifiCheckCnt = 0;
                reconnectActive = false;
                if (apActive)
                    stopAP();   // router is back, fallback AP no longer needed
            }
            else
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
                }
            }

            if (ntpSyncArm)   // one tick delay so the "WI-FI OK" scroll is not blocked by connectNTP()
            {
                ntpSyncArm = false;
                ntpSyncPending = true;
            }
            if (ntpSyncPending)   // resync RTC once after a successful reconnect
            {
                ntpSyncPending = false;
                tm* tt = connectNTP();
                if (tt != NULL)
                {
                    rtc_set(tt);
                    Serial.println("RTC resynced after WiFi reconnect");
                    startMsg("NTP TIME SYNC OK!");
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
            y = y2;                 //scroll updown
            sc1 = 1;
            sek1++;
            if (sek1 == 10) {
                sc2 = 1;
                sek2++;
                sek1 = 0;
            }
            if (sek2 == 6) {
                min1++;
                sek2 = 0;
                sc3 = 1;
            }
            if (min1 == 10) {
                min2++;
                min1 = 0;
                sc4 = 1;
            }
            if (min2 == 6) {
                std1++;
                min2 = 0;
                sc5 = 1;
            }
            if (std1 == 10) {
                std2++;
                std1 = 0;
                sc6 = 1;
            }
            if ((std2 == 2) && (std1 == 4)) {
                std1 = 0;
                std2 = 0;
                sc6 = 1;
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
            if (settings.scrollDate)   // scroll the date at the chosen interval
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
        } // end 1s
        if (f_tckr50ms == true) {// the following runs every 50ms to control scroll speed
            f_tckr50ms = false;
            static bool wifiPrevUp = false;
            static bool wifiEverConnected = false;
            bool wifiUp = (WiFi.status() == WL_CONNECTED);
            if (wifiUp && !wifiPrevUp)   // WiFi came up (boot or after a drop)
            {
                wifiReconnectNotify = true;
                if (wifiEverConnected)   // genuine reconnection, not the boot connection
                    ntpSyncRequested = true;
            }
            wifiPrevUp = wifiUp;
            if (wifiUp)
                wifiEverConnected = true;
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
                }
            }
            else if (msgPending) {
                msgPending = false;
                startMsg(msgPendingText, msgPendingStep);
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
            if (settings.marquee) {   // marquee border sweep: a comet of LEDs runs around the border
                static unsigned int mqPhase = 0;
                int bx, by;
                for (unsigned int k = 0; k < 77; k++) {   // clear border cells first (digits redraw over them)
                    mqPerimeter(k, bx, by);
                    LEDarr[bx >> 3][by] &= ~(1 << (bx & 7));
                }
                for (unsigned int k = 0; k < 6; k++) {    // 6 LED comet
                    mqPerimeter((mqPhase + k) % 77, bx, by);
                    LEDarr[bx >> 3][by] |= (1 << (bx & 7));
                }
                mqPhase++;
            }
            if (settings.pulse) {   // brightness pulse: breathes between base and base+6
                static unsigned int pulsePhase = 0;
                unsigned int ph = pulsePhase % 100;
                unsigned int tri = (ph < 50) ? ph : (99 - ph);   // triangle 0..49..0
                int br = (int)settings.brightness + (int)(tri * 6 / 49);
                max7219_set_brightness((unsigned short)constrain(br, 0, 15));
                pulsePhase++;
            }
            if (sc1 == 1) {
                if (updown == 1)
                    y--;
                else
                    y++;
               y3 = y;
               if (y3 > 0) {
                y3 = 0;
                }     
               char22Arr(48 + sek12, z_PosX - 27, y3);
               char22Arr(48 + sek11, z_PosX - 27, y + y1);
                if (y == 0) {
                    sc1 = 0;
                    f_scrollend_y = true;
                }
            }
            else
                char22Arr(48 + sek1, z_PosX - 27, 0);

            if (sc2 == 1) {
                char22Arr(48 + sek22, z_PosX - 23, y3);
                char22Arr(48 + sek21, z_PosX - 23, y + y1);
                if (y == 0)
                    sc2 = 0;
            }
            else
              char22Arr(48 + sek2, z_PosX - 23, 0);

            if (sc3 == 1) {
                char2Arr(48 + min12, z_PosX - 18, y);
                char2Arr(48 + min11, z_PosX - 18, y + y1);
                if (y == 0)
                    sc3 = 0;
            }
            else
                char2Arr(48 + min1, z_PosX - 18, 0);

            if (sc4 == 1) {
                char2Arr(48 + min22, z_PosX - 13, y);
                char2Arr(48 + min21, z_PosX - 13, y + y1);
                if (y == 0)
                    sc4 = 0;
            }
            else
                char2Arr(48 + min2, z_PosX - 13, 0);

              {   // colon/separator animation (glyph is 4 columns wide, single dot column)
                  uint8_t cst = 1;   // 0=off, 1=both dots, 2=top dot, 3=bottom dot
                  if (settings.colonAnim == 1) cst = ((millis() / 1000) % 2 == 0) ? 1 : 0;
                  else if (settings.colonAnim == 2) cst = ((millis() / 2000) % 2 == 0) ? 1 : 0;
                  else if (settings.colonAnim == 3) { unsigned long t = millis() % 1000; cst = (t < 150 || (t >= 250 && t < 400)) ? 1 : 0; }
                  else if (settings.colonAnim == 4) cst = ((millis() / 500) % 2 == 0) ? 2 : 3;
                  if (cst == 0) {
                      for (int cc = 0; cc < 4; cc++) {
                          int cxx = z_PosX - 9 - cc;
                          if (cxx >= 0 && cxx < 32)
                              for (int rr = 0; rr < 8; rr++)
                                  LEDarr[cxx >> 3][rr] &= ~(1 << (cxx & 7));
                      }
                  } else {
                      char2Arr(':', z_PosX - 10 + x, 0);
                      if (cst == 2) {   // hide the bottom dot
                          for (int cc = 0; cc < 4; cc++) {
                              int cxx = z_PosX - 9 - cc;
                              if (cxx >= 0 && cxx < 32)
                                  for (int rr = 4; rr < 8; rr++)
                                      LEDarr[cxx >> 3][rr] &= ~(1 << (cxx & 7));
                          }
                      } else if (cst == 3) {   // hide the top dot
                          for (int cc = 0; cc < 4; cc++) {
                              int cxx = z_PosX - 9 - cc;
                              if (cxx >= 0 && cxx < 32)
                                  for (int rr = 0; rr < 4; rr++)
                                      LEDarr[cxx >> 3][rr] &= ~(1 << (cxx & 7));
                          }
                      }
                  }
              }

            if (sc5 == 1) {
                char2Arr(48 + std12, z_PosX - 4, y);
                char2Arr(48 + std11, z_PosX - 4, y + y1);
                if (y == 0)
                    sc5 = 0;
            }
            else
                char2Arr(48 + std1, z_PosX - 4, 0);

            if (sc6 == 1) {
                char2Arr(48 + std22, z_PosX + 1, y);
                char2Arr(48 + std21, z_PosX + 1, y + y1);
                if (y == 0)
                    sc6 = 0;
            }
            else
                char2Arr(48 + std2, z_PosX + 1, 0);

            if (showMsg) {
                int mi, x = d_PosX;
                clearLEDarr();   // erase clock digits + scroll trails so the message scrolls cleanly
                for (mi = 0; msg[mi] != 0; mi++) {
                    int k = (int)(unsigned char) msg[mi] - 32;
                    if (k >= 0 && k < 96)
                        char2Arr(msg[mi], x - 1, 0);
                    x -= (k >= 0 && k < 96) ? font1[k][0] : 4;   // step by the actual glyph width; non-ASCII bytes use a space width
                }
            }
            else {
            char2Arr(' ', d_PosX+5, 0);        //day of the week
            char2Arr(WT_arr[MEZ.WT][0], d_PosX - 1, 0);        //day of the week
            char2Arr(WT_arr[MEZ.WT][1], d_PosX - 7, 0);
            char2Arr(WT_arr[MEZ.WT][2], d_PosX - 13, 0);
            char2Arr(WT_arr[MEZ.WT][3], d_PosX - 19, 0);
            char2Arr(48 + MEZ.tag2, d_PosX - 24, 0);           //day
            char2Arr(48 + MEZ.tag1, d_PosX - 30, 0);
            char2Arr(M_arr[MEZ.mon12 - 1][0], d_PosX - 39, 0); //month
            char2Arr(M_arr[MEZ.mon12 - 1][1], d_PosX - 43, 0);
            char2Arr(M_arr[MEZ.mon12 - 1][2], d_PosX - 49, 0);
            char2Arr(M_arr[MEZ.mon12 - 1][3], d_PosX - 55, 0);
            char2Arr(M_arr[MEZ.mon12 - 1][4], d_PosX - 61, 0);
            char2Arr('2', d_PosX - 68, 0);                     //year
            char2Arr('0', d_PosX - 74, 0);
            char2Arr(48 + MEZ.jahr2, d_PosX - 80, 0);
            char2Arr(48 + MEZ.jahr1, d_PosX - 86, 0);
            }

            refresh_display(); //alle 50ms
            if (f_scrollend_y == true) {
                f_scrollend_y = false;
            }
        } //end 50ms
        if (y == 0) {
            // do something else
        }
    }  //end while(true)
    //this section can not be reached
}
