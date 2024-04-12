/*
Version 1.0.1 - Improve battery life from 45 days to 163 days. Details described in
https://www.e-tinkers.com/2024/01/ntp-clock-project-revisit-and-esp8266-rtc-memory/
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include <sntp.h>

// Reset Reason
#define POWER_UP_RESET 0
#define HARDWARE_RESET 6

// RTC Time constants
#define TIME_ZONE           28800       // GMT + 8
#define DAY_LIGHT_SAVING    0
#define UTC_TEST_TIME       1649289600  // Thu, 07 Apr 2022 00:00:00 +0000

#define DISPLAY_TIME        5000
#define DAY_SLEEP_TIME      300e6       // 300 seconds (5 mins)
// #define NIGHT_SLEEP_TIME    3600e6      // 1 hour
#define NEXT_HOUR       1L          // Every 1 hour

// GPIO pins
#define P1         5   // D1
#define P2         12  // D6
#define P3         13  // D7
#define P4         14  // D5

// Charlieplexing configuration and state matrix
#define PIN_CONFIG 0
#define PIN_STATE  1
#define LEDS       12
const uint8_t mux[LEDS][2][4] = {
//{ {         PIN_CONFIG             }, {        PIN_STATE       } }
//{ {  P1,     P2,     P3,     P4    }, {  P1,   P2,   P3,   P4  } }
  { { OUTPUT, INPUT,  INPUT,  OUTPUT }, { LOW,  LOW,  LOW,  HIGH } },  // 0
  { { OUTPUT, OUTPUT, INPUT,  INPUT  }, { HIGH, LOW,  LOW,  LOW  } },  // 1
  { { OUTPUT, OUTPUT, INPUT,  INPUT  }, { LOW,  HIGH, LOW,  LOW  } },  // 2
  { { INPUT,  OUTPUT, OUTPUT, INPUT  }, { LOW,  HIGH, LOW,  LOW  } },  // 3
  { { INPUT,  OUTPUT, OUTPUT, INPUT  }, { LOW,  LOW,  HIGH, LOW  } },  // 4
  { { INPUT,  INPUT,  OUTPUT, OUTPUT }, { LOW,  LOW,  HIGH, LOW  } },  // 5
  { { INPUT,  INPUT,  OUTPUT, OUTPUT }, { LOW,  LOW,  LOW,  HIGH } },  // 6
  { { OUTPUT, INPUT,  OUTPUT, INPUT  }, { HIGH, LOW,  LOW,  LOW  } },  // 7
  { { OUTPUT, INPUT,  OUTPUT, INPUT  }, { LOW,  LOW,  HIGH, LOW  } },  // 8
  { { INPUT,  OUTPUT, INPUT,  OUTPUT }, { LOW,  HIGH, LOW,  LOW  } },  // 9
  { { INPUT,  OUTPUT, INPUT,  OUTPUT }, { LOW,  LOW,  LOW,  HIGH } },  // 10
  { { OUTPUT, INPUT,  INPUT,  OUTPUT }, { HIGH, LOW,  LOW,  LOW  } }   // 11
};

// network configuration - change based on your local settings
// refer to https://www.e-tinkers.com/2022/04/esp8266-ntp-clock-with-ntp-update-and-charlieplexing/ on
// how to obtain channel and bssid
IPAddress ip(192, 168, 0, 120);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(1,1,1,1);
const char* ssid = "your-wifi-ssid";
const char* password = "your-wifi-password";
const int channel = 2;                                         // this must match your wifi router channel
const uint8_t bssid[] = {0x7c, 0x8b, 0xca, 0x31, 0x61, 0x91};  // this must match your wifi router mac address
const char* ntpServer = "sg.pool.ntp.org";                     // use pool.ntp.org or other local NTP server

// state machine variables
enum States {BEGIN, LED_ON, LED_OFF, END};
uint8_t flashState = BEGIN;
uint8_t cycle = 0;
unsigned long onTimer = 0;
unsigned long offTimer = 0;
unsigned long intervalTimer = 0;
unsigned long displayStart = 0;

int32_t nextNtpSync{0};

time_t now;
struct tm* t;

void turnOnLED(uint8_t led) {

  pinMode(P1, mux[led][PIN_CONFIG][0]);
  pinMode(P2, mux[led][PIN_CONFIG][1]);
  pinMode(P3, mux[led][PIN_CONFIG][2]);
  pinMode(P4, mux[led][PIN_CONFIG][3]);

  digitalWrite(P1, mux[led][PIN_STATE][0]);
  digitalWrite(P2, mux[led][PIN_STATE][1]);
  digitalWrite(P3, mux[led][PIN_STATE][2]);
  digitalWrite(P4, mux[led][PIN_STATE][3]);

}

void turnOffLED() {

    pinMode(P1, INPUT);
    pinMode(P2, INPUT);
    pinMode(P3, INPUT);
    pinMode(P4, INPUT);

}

void flashLED(uint8_t theLED, int flashes) {

    switch(flashState) {
      case BEGIN:
          onTimer = millis();
          flashState = LED_ON;
          break;
      case LED_ON:
          {
              turnOnLED(theLED);
              if (flashes == 0) {
                if (millis() - onTimer > 450) {
                    flashState = LED_OFF;
                }
              }
              else {
                if (millis() - onTimer > 50) {
                    flashState = LED_OFF;
                    offTimer = millis();
                }
              }
          }
          break;
      case LED_OFF:
          {
              turnOffLED();
              if (flashes == 0) {
                if (millis() - offTimer > 50) {
                    intervalTimer = millis();
                    flashState = END;
                }
              }
              else {
                if (millis() - offTimer > (500UL - 50 * flashes)/flashes) {
                  if (++cycle < flashes) {
                      flashState = BEGIN;
                  }
                  else {
                      flashState = END;
                      intervalTimer = millis();
                  }
                }
              }
          }
          break;
      case END:
          if (millis() - intervalTimer > 1000UL) {
              flashState = BEGIN;
              cycle = 0;
          }
          break;
      default:
          break;
    }

}

void testLED() {

    for(int l = 0; l < LEDS; l++) {
        turnOnLED( l );
        delay( DISPLAY_TIME / LEDS );
    }

}

void turnOnWiFi() {

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.config(ip, gateway, subnet, dns);
    WiFi.begin(ssid, password, channel, bssid);
    while (WiFi.status() != WL_CONNECTED) {
        delay(10);
    }

}

void updateNTP() {

    configTime(DAY_LIGHT_SAVING, TIME_ZONE, ntpServer);
    while (time(nullptr) < UTC_TEST_TIME)
        yield();

    now = time(nullptr);
    t = localtime(&now);
    nextNtpSync = (t->tm_hour + NEXT_HOUR) % 24;
    system_rtc_mem_write(64, &nextNtpSync, sizeof(nextNtpSync));  // write nextNtpSync value to rtc memory
    // Serial.printf("Sync with NTP, next Sync %d now=%02d:%02d\n", nextNtpSync, t->tm_hour, t->tm_min);
}

void turnOffWiFi() {

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();

}

void setup() {

    // Serial.begin(115200);
    // delay(3000);

    struct rst_info *rstInfo = system_get_rst_info();
    system_rtc_mem_read(64, &nextNtpSync, sizeof(nextNtpSync));  // get nextNtpSync value from rtc memory
    system_rtc_mem_read(68, &now, sizeof(now));                  // read back the savedTimestamp

    sntp_set_timezone(TIME_ZONE/3600);
    t = localtime(&now);
    // if reset is caused by first power up (reason=0) or current hour is equal to the update hour
    if ((rstInfo->reason == POWER_UP_RESET) | (rstInfo->reason == HARDWARE_RESET) | (nextNtpSync == t->tm_hour)) {
        turnOnWiFi();
        updateNTP();
        turnOffWiFi();
    }

    displayStart = millis();

}

void loop() {

    // testLED();

    uint8_t hour = t->tm_hour % 12;
    uint8_t fiveMinuteInterval = t->tm_min / 5;
    uint8_t flashes = t->tm_min % 5;

    turnOnLED(hour);
    delay(1);
    flashLED(fiveMinuteInterval, flashes);
    delay(1);

    if (millis() - displayStart > DISPLAY_TIME) {
        now = now + (time_t) (DAY_SLEEP_TIME/1e6 + DISPLAY_TIME/1000);
        system_rtc_mem_write(68, &now, sizeof(now));
        ESP.deepSleep(DAY_SLEEP_TIME);
    }

}
