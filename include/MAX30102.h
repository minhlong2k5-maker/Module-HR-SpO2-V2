#ifndef MAX30102_CUSTOM_H
#define MAX30102_CUSTOM_H

#include <Arduino.h>

#define WINDOW_SIZE 200

// Chỉ KHAI BÁO (dùng extern), không khởi tạo giá trị ở đây
extern uint32_t irBuffer[WINDOW_SIZE];
extern uint32_t redBuffer[WINDOW_SIZE];
extern float smoothedSpO2;
extern int32_t currentSpO2;

void calculateSpO2Window();

#endif // MAX30102_CUSTOM_H