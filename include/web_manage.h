#ifndef WEB_MANAGE_H
#define WEB_MANAGE_H

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <SD.h>

extern SemaphoreHandle_t sdMutex;
extern bool isOnline;

void setupWiFi(const char* ssid, const char* password);
void setupWeb();
void wsSendHealthData(int sharpSignal, int beatAvg, int currentSpO2);
void wsCleanup();

#endif