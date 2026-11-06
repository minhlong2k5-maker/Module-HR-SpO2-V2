#include "MAX30102.h"
#include <math.h>

// KHỞI TẠO BIẾN THỰC SỰ ở đây (không có chữ extern)
uint32_t irBuffer[WINDOW_SIZE];
uint32_t redBuffer[WINDOW_SIZE];
float smoothedSpO2 = 0.0;
int32_t currentSpO2 = 0;

void calculateSpO2Window() {
  float irDC = 0, redDC = 0;
  
  // 1. TÍNH THÀNH PHẦN TĨNH (DC)
  for (int i = 0; i < WINDOW_SIZE; i++) {
    irDC += irBuffer[i]; 
    redDC += redBuffer[i];
  }
  irDC /= WINDOW_SIZE; 
  redDC /= WINDOW_SIZE;

  if (irDC < 10000 || redDC < 10000) return;

  // 2. TÍNH THÀNH PHẦN ĐỘNG (AC) BẰNG RMS
  float irSqSum = 0, redSqSum = 0;

  for (int i = 0; i < WINDOW_SIZE; i++) {
    float irAC_val = irBuffer[i] - irDC;
    float redAC_val = redBuffer[i] - redDC;

    irSqSum += (irAC_val * irAC_val);
    redSqSum += (redAC_val * redAC_val);
  }

  float irAC = sqrt(irSqSum / WINDOW_SIZE);
  float redAC = sqrt(redSqSum / WINDOW_SIZE);

  if (irAC < 3.0 || redAC < 3.0) return; 

  // 3. TÍNH TỶ SỐ QUANG HỌC R
  float R = (redAC / redDC) / (irAC / irDC);

  // 4. HIỆU CHỈNH CALIBRATION
  float CALIB_A = -45.060;
  float CALIB_B = 30.354;
  float CALIB_C = 105.0; 

  float rawSpO2 = (CALIB_A * R * R) + (CALIB_B * R) + CALIB_C;

  // 5. LỌC VÀ KHÓA KẾT QUẢ
  if (rawSpO2 > 100.0) rawSpO2 = 100.0;
  
  if (rawSpO2 > 70.0) {
    if (smoothedSpO2 == 0.0) {
      smoothedSpO2 = rawSpO2;
    } else {
      smoothedSpO2 = (smoothedSpO2 * 0.8) + (rawSpO2 * 0.2); 
    }
    currentSpO2 = (int32_t)smoothedSpO2;
  }
}