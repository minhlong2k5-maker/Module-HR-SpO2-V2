#ifndef SENSOR_MAX30102_H
#define SENSOR_MAX30102_H

#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"

// Các mức trạng thái của ngón tay
#define FINGER_NONE 0
#define FINGER_BAD  1
#define FINGER_GOOD 2

void initMAX30102();
int checkFingerStatus();
void startMeasuring();
void processMAX30102();
void getCurrentReadings(int &currentSpO2, int &currentHR);
void getFinalResults(int &finalSpO2, int &finalHR);

#endif