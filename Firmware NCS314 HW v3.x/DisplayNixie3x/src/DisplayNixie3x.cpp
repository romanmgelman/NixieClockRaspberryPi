//============================================================================
// Name        : DisplayNixie3x.cpp
// Author      : GRA&AFCH & Roman Gelman
// Version     : v1.0
// Copyright   : Free
// Description : Display time on shields NCS314 v3.x
// Date		   : 16.05.2026
//============================================================================

#define _VERSION "3.5 FINAL - NCS314 v3.x (buttons work permanently)"

#include <iostream>
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <ctime>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <cstdint>
#include <sys/time.h>

using namespace std;

#define LEpin 3
#define SHDNpin 2
#define UP_BUTTON_PIN 1
#define DOWN_BUTTON_PIN 4

#define DEBOUNCE_DELAY 80
#define REPEAT_DELAY   400
#define REPEAT_SPEED   150
#define TOTAL_DELAY    17

bool use12hour = true;
bool dotState = false;

uint16_t SymbolArray[10] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512};

char _stringToDisplay[8];

// Our own persistent software clock
tm currentTime;

void initPin(int pin) {
    pinMode(pin, INPUT);
    pullUpDnControl(pin, PUD_UP);
}

// Set system time (so other programs see the correct time too)
void setSystemTime(tm* t) {
    time_t newTime = mktime(t);
    struct timeval tv = {newTime, 0};
    settimeofday(&tv, NULL);
}

void dotBlink() {
    static unsigned int lastTimeBlink = millis();
    if ((millis() - lastTimeBlink) >= 1000) {
        lastTimeBlink = millis();
        dotState = !dotState;
    }
}

void signal_handler(int sig_received) {
    printf("Received Signal %d; Exiting cleanly.\n", sig_received);
    digitalWrite(SHDNpin, LOW);
    exit(sig_received);
}

// ====================== EXACT WORKING v3 doIndication ======================
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit)  ((value) |= (1UL << (bit)))
#define bitClear(value, bit)((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) (bitvalue ? bitSet(value, bit) : bitClear(value, bit))

void doIndication() {
    unsigned long Var32 = 0;
    unsigned long New32_L = 0;
    unsigned long New32_H = 0;
    unsigned char buff[8];
    long digits = atoi(_stringToDisplay);

    // REG 1
    Var32 = 0;
    Var32 |= (unsigned long)(SymbolArray[digits % 10]) << 20; digits /= 10;
    Var32 |= (unsigned long)(SymbolArray[digits % 10]) << 10; digits /= 10;
    Var32 |= (unsigned long)(SymbolArray[digits % 10]);       digits /= 10;
    if (dotState) Var32 |= 0x40000000; else Var32 &= ~0x40000000;
    if (dotState) Var32 |= 0x80000000; else Var32 &= ~0x80000000;

    for (int i = 1; i <= 32; i++) {
        i = i + 32;
        int newindex = 16 * (3 - (ceil((float)i / 4) * 4 - i)) + ceil((float)i / 4);
        i = i - 32;
        if (newindex <= 32) bitWrite(New32_L, newindex - 1, bitRead(Var32, i - 1));
        else                bitWrite(New32_H, newindex - 32 - 1, bitRead(Var32, i - 1));
    }

    // REG 0
    Var32 = 0;
    Var32 |= (unsigned long)(SymbolArray[digits % 10]) << 20; digits /= 10;
    Var32 |= (unsigned long)(SymbolArray[digits % 10]) << 10; digits /= 10;
    Var32 |= (unsigned long)SymbolArray[digits % 10];
    if (dotState) Var32 |= 0x40000000; else Var32 &= ~0x40000000;
    if (dotState) Var32 |= 0x80000000; else Var32 &= ~0x80000000;

    for (int i = 1; i <= 32; i++) {
        int newindex = 16 * (3 - (ceil((float)i / 4) * 4 - i)) + ceil((float)i / 4);
        if (newindex <= 32) bitWrite(New32_L, newindex - 1, bitRead(Var32, i - 1));
        else                bitWrite(New32_H, newindex - 32 - 1, bitRead(Var32, i - 1));
    }

    buff[0] = New32_H >> 24; buff[1] = New32_H >> 16; buff[2] = New32_H >> 8; buff[3] = New32_H;
    buff[4] = New32_L >> 24; buff[5] = New32_L >> 16; buff[6] = New32_L >> 8; buff[7] = New32_L;

    wiringPiSPIDataRW(0, buff, 8);
    digitalWrite(LEpin, HIGH);
    digitalWrite(LEpin, LOW);
}
// =========================================================================

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Nixie Clock %s starting...\n", _VERSION);
    printf("UP button   → +1 hour   (hold for auto-repeat)\n");
    printf("DOWN button → +1 minute (hold for auto-repeat)\n\n");

    wiringPiSetup();

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "24hour")) use12hour = false;
    }

    if (use12hour) puts("→ 12-hour mode");
    else           puts("→ 24-hour mode");

    // === Initialize our internal clock from real system time ===
    time_t now = time(NULL);
    currentTime = *localtime(&now);

    pinMode(SHDNpin, OUTPUT);
    digitalWrite(SHDNpin, HIGH);

    if (wiringPiSPISetupMode(0, 2000000, 3)) {
        puts("SPI OK");
    } else {
        puts("SPI FAILED");
        return 1;
    }

    unsigned long buttonDelay = millis();
    bool upWasPressed = false;
    bool downWasPressed = false;

    unsigned long lastSecondTick = millis();

    while (true) {
        // Recover button pins every loop (strongest possible recovery)
        initPin(UP_BUTTON_PIN);
        initPin(DOWN_BUTTON_PIN);

        // === Advance our internal clock manually (prevents NTP fighting us) ===
        if ((millis() - lastSecondTick) >= 1000) {
            currentTime.tm_sec++;
            mktime(&currentTime);          // normalize time
            lastSecondTick = millis();
        }

        // Format time for display
        char format[8];
        strcpy(format, use12hour ? "%I%M%S" : "%H%M%S");
        strftime(_stringToDisplay, 8, format, &currentTime);

        dotBlink();

        // === BUTTON HANDLING ===
        bool upPressed   = (digitalRead(UP_BUTTON_PIN)   == 0);
        bool downPressed = (digitalRead(DOWN_BUTTON_PIN) == 0);

        unsigned long nowMs = millis();

        // UP button = +1 hour
        if (upPressed) {
            if (!upWasPressed || (nowMs - buttonDelay) > (upWasPressed ? REPEAT_SPEED : REPEAT_DELAY)) {
                currentTime.tm_hour++;
                mktime(&currentTime);
                setSystemTime(&currentTime);
                buttonDelay = nowMs;
                upWasPressed = true;
            }
        } else {
            upWasPressed = false;
        }

        // DOWN button = +1 minute
        if (downPressed) {
            if (!downWasPressed || (nowMs - buttonDelay) > (downWasPressed ? REPEAT_SPEED : REPEAT_DELAY)) {
                currentTime.tm_min++;
                mktime(&currentTime);
                setSystemTime(&currentTime);
                buttonDelay = nowMs;
                downWasPressed = true;
            }
        } else {
            downWasPressed = false;
        }

        pinMode(LEpin, OUTPUT);
        doIndication();

        delay(TOTAL_DELAY);
    }
    return 0;
}
