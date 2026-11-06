#include "MAX30102.h"

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

  // 2. TÌM ĐỈNH VÀ ĐÁY ĐỂ TÍNH AC (Peak-to-Peak)
  float irMin = 999999.0, irMax = -999999.0;
  float redMin = 999999.0, redMax = -999999.0;

  for (int i = 0; i < WINDOW_SIZE; i++) {
    // Loại bỏ DC để lấy tín hiệu động
    float irAC_val = irBuffer[i] - irDC;
    float redAC_val = redBuffer[i] - redDC;

    if (irAC_val > irMax) irMax = irAC_val;
    if (irAC_val < irMin) irMin = irAC_val;

    if (redAC_val > redMax) redMax = redAC_val;
    if (redAC_val < redMin) redMin = redAC_val;
  }

  // Biên độ xung mạch chính là khoảng cách từ đỉnh đến đáy
  float irAC = irMax - irMin;
  float redAC = redMax - redMin;

  // Bỏ qua nếu nhiễu quá nhỏ (chưa đặt tay chặt)
  if (irAC < 10.0 || redAC < 10.0) return; 

  // 3. TÍNH TỶ SỐ QUANG HỌC R
  float R = (redAC / redDC) / (irAC / irDC);

  // 4. HIỆU CHỈNH PHƯƠNG TRÌNH MAXIM (CALIBRATION)
  // Hệ số gốc của Maxim: A = -45.060, B = 30.354, C = 94.845
  float CALIB_A = -45.060;
  float CALIB_B = 30.354;
  
  // TĂNG HỆ SỐ C NẾU KẾT QUẢ BỊ THẤP. Ví dụ: Đổi từ 94.845 lên 105.0
  float CALIB_C = 105.0; 

  float rawSpO2 = (CALIB_A * R * R) + (CALIB_B * R) + CALIB_C;

  // 5. LỌC VÀ KHÓA KẾT QUẢ
  if (rawSpO2 > 100.0) rawSpO2 = 100.0;
  
  if (rawSpO2 > 70.0) {
    if (smoothedSpO2 == 0) {
      smoothedSpO2 = rawSpO2;
    } else {
      smoothedSpO2 = (smoothedSpO2 * 0.8) + (rawSpO2 * 0.2); 
    }
    currentSpO2 = (int32_t)smoothedSpO2;
  }
}