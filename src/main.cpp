#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <MAX30105.h>

// Include các module nội bộ
#include "audio.h"
#include "sd_logger.h"
#include "web_manage.h"
#include "MAX30102.h"

// ==========================================
// THÔNG TIN WIFI & PINS
// ==========================================
const char* ssid = "P203";
const char* password = "28112005";

// I2C cho OLED
#define OLED_SDA 1
#define OLED_SCL 2

// I2C cho MAX30102
#define MAX_SDA 8
#define MAX_SCL 9

// Chân đo pin
#define BAT_ADC_PIN 3

// ==========================================
// GLOBALS & OBJECTS
// ==========================================
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);
MAX30105 particleSensor;

QueueHandle_t audioQueue;
SemaphoreHandle_t sdMutex; 
bool isOnline = false;

// --- Biến Trạng Thái ---
bool isMeasured = false;
bool lastFingerOn = false;
int fingerOffCounter = 30;
const int FINGER_OFF_THRESHOLD = 20;
bool inWarmup = false;

unsigned long measureStartTime = 0;
unsigned long lastDisplayUpdate = 0; 
unsigned long lastWsUpdate = 0; 

// --- Heart Rate Variables ---
float dynamicThreshold = 30.0; 
float smoothedBPM = 0; 
bool isPeak = false; 
bool firstPeakDetected = false; 
long dcFilterIR = 0;
int lastAcSignal = 0;
int beatAvg = 0;
unsigned long lastBeat = 0; 

int sampleIdx = 0;
bool bufferFull = false;

// --- OLED Graph ---
int xPos = 0;
const int GRAPH_BOTTOM = 63;
const int GRAPH_TOP = 44;
uint32_t baselineIR = 0;
bool baselineLocked = false;

// --- Battery Variables ---
float batVoltage = 0;
int batPercentage = 0;
bool isCharging = false;

// ==========================================
// HÀM VẼ PIN & SẠC
// ==========================================
void drawBattery() {
  uint32_t adc_mv = analogReadMilliVolts(BAT_ADC_PIN); 
  // Áp thực tế = ADC * 2 (phân áp 100k/100k)
  batVoltage = (adc_mv / 1000.0) * 2.0; 

  // Nâng ngưỡng vọt áp lên 4.22V để tránh báo tia sét ảo khi pin đầy (4.20V)
  if (batVoltage >= 4.22) {
    batPercentage = 100;
    isCharging = true; 
  } else {
    isCharging = false;
    if (batVoltage >= 4.10) batPercentage = 100;
    else if (batVoltage >= 3.95) batPercentage = 80 + 20 * (batVoltage - 3.95) / 0.15;
    else if (batVoltage >= 3.80) batPercentage = 60 + 20 * (batVoltage - 3.80) / 0.15;
    else if (batVoltage >= 3.65) batPercentage = 40 + 20 * (batVoltage - 3.65) / 0.15;
    else if (batVoltage >= 3.45) batPercentage = 20 + 20 * (batVoltage - 3.45) / 0.20;
    else if (batVoltage >= 3.20) batPercentage = 20 * (batVoltage - 3.20) / 0.25;
    else batPercentage = 0;
  }

  // Xóa vùng góc phải trên cùng (để số % cập nhật không bị đè nhòe)
  display.fillRect(85, 0, 43, 10, SH110X_BLACK);

  // In số % pin
  display.setTextSize(1);
  if (batPercentage == 100) display.setCursor(90, 0);
  else if (batPercentage >= 10) display.setCursor(96, 0);
  else display.setCursor(102, 0);
  display.print(batPercentage);
  display.print("%");

  // Vẽ khung viên pin nhỏ
  int x = 114, y = 0;
  display.drawRect(x, y, 12, 7, SH110X_WHITE);
  display.fillRect(x + 12, y + 2, 2, 3, SH110X_WHITE); 
  
  if (isCharging) {
    // Vẽ tia sét nếu áp vọt
    display.drawLine(x + 5, y + 1, x + 3, y + 4, SH110X_WHITE);
    display.drawLine(x + 3, y + 4, x + 7, y + 4, SH110X_WHITE);
    display.drawLine(x + 7, y + 4, x + 5, y + 7, SH110X_WHITE);
  } else {
    // Tô vạch pin
    int fillWidth = (batPercentage * 8) / 100;
    if (fillWidth > 0) display.fillRect(x + 2, y + 2, fillWidth, 3, SH110X_WHITE);
  }
}

// ==========================================
// OLED FUNCTIONS
// ==========================================
void showReadyScreen() {
  display.clearDisplay(); 
  display.setTextSize(2); display.setCursor(35, 15); display.println("READY");
  display.setTextSize(1); display.setCursor(25, 40); display.println("Place finger..."); 
  drawBattery();
  display.display();
}

void showDoneScreen() {
  display.clearDisplay();
  bool isValid = (beatAvg > 0 && currentSpO2 > 80);
  
  drawBattery(); // Cập nhật pin

  display.setTextSize(1); display.setCursor(0, 0);
  display.print(isValid ? "ST: DONE!" : "ST: FAILED!");
  display.setTextSize(2); display.setCursor(0, 14);
  display.print("HR: "); if (beatAvg > 0) display.print(beatAvg); else display.print("--");
  display.setTextSize(1); display.print(" BPM");
  display.setTextSize(2); display.setCursor(0, 32);
  display.print("SpO2:"); if (currentSpO2 > 0) display.print(currentSpO2); else display.print("--");
  display.print("%");
  display.drawLine(0, 50, 127, 50, SH110X_WHITE);
  display.setTextSize(1); display.setCursor(25, 54); display.print("REMOVE FINGER"); 
  display.display();
}

void triggerAdjustError(bool isFingerOn) {
  display.clearDisplay(); 
  drawBattery(); // Cập nhật pin
  
  display.setTextSize(2);
  display.setCursor(25, 20); display.print("ADJUST");
  display.setCursor(25, 40); display.print("FINGER!"); display.display();
  queueWav("/adjust.wav"); 
  measureStartTime = millis(); particleSensor.clearFIFO(); 
  
  sampleIdx = 0; bufferFull = false; smoothedSpO2 = 0; inWarmup = true;
  dcFilterIR = particleSensor.getIR(); lastAcSignal = 0; dynamicThreshold = 30.0; smoothedBPM = 0; firstPeakDetected = false;
  baselineLocked = false; lastFingerOn = isFingerOn; 
}

// ==========================================
// SETUP 
// ==========================================
void setup() {
  Serial.begin(115200); delay(1000);
  
  // Khởi tạo độ phân giải chuẩn cho chân đọc pin
  analogReadResolution(12);

  audioQueue = xQueueCreate(15, MAX_AUDIO_FILENAME_LEN * sizeof(char));
  sdMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(AudioTask, "AudioTask", 4096, NULL, 1, NULL, 0);

  // Khởi tạo I2C cho OLED (Chân 1, 2)
  Wire.begin(OLED_SDA, OLED_SCL); 
  Wire.setClock(400000); 
  display.begin(0x3C, true); display.clearDisplay(); display.setTextColor(SH110X_WHITE);
  display.setCursor(10, 30); display.print("Booting..."); display.display();

  initSDCard();
  
  // Khởi tạo I2C cho MAX30102 (Chân 8, 9)
  Wire1.begin(MAX_SDA, MAX_SCL);
  Wire1.setClock(400000);
  particleSensor.begin(Wire1, I2C_SPEED_FAST);
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x00); 
  particleSensor.setPulseAmplitudeIR(0x1F); 
  particleSensor.setSampleRate(100);

  initI2S(); 
  setupWiFi(ssid, password);
  
  if (isOnline) {
    setupWeb();
    display.clearDisplay(); display.setCursor(0, 10); display.println("Web Ready:"); display.println(WiFi.localIP().toString()); display.display(); delay(2000);
  } else {
    display.clearDisplay(); display.setCursor(0, 10); display.println("OFFLINE MODE"); display.display(); delay(2000);
  }
  showReadyScreen(); queueWav("/ready.wav");
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  uint32_t irValue = particleSensor.getIR();
  uint32_t redValue = particleSensor.getRed();

  bool currentFingerPresent = (irValue > 10000); 
  if (!currentFingerPresent) { if (fingerOffCounter < FINGER_OFF_THRESHOLD + 1) fingerOffCounter++; } 
  else { fingerOffCounter = 0; }
  bool fingerOn = (fingerOffCounter < FINGER_OFF_THRESHOLD);

  if (fingerOn && !lastFingerOn) {
    display.clearDisplay(); 
    drawBattery();
    display.setTextSize(1); display.setCursor(20, 30); display.print("Please wait..."); display.display();
    queueWav("/measur.wav");
    particleSensor.clearFIFO();
    
    isMeasured = false; xPos = 0; currentSpO2 = 0; beatAvg = 0; smoothedSpO2 = 0;
    sampleIdx = 0; bufferFull = false; inWarmup = true;
    dcFilterIR = particleSensor.getIR(); lastAcSignal = 0; dynamicThreshold = 30.0; smoothedBPM = 0; firstPeakDetected = false; baselineLocked = false; 
    measureStartTime = millis(); particleSensor.setPulseAmplitudeRed(0x20); 
  }

  if (!fingerOn && lastFingerOn && !isMeasured) { showReadyScreen(); }

  if (fingerOn && !isMeasured) {
    unsigned long timeElapsed = millis() - measureStartTime;

    if (timeElapsed < 2500) {
      inWarmup = true; baselineLocked = false; 
      dcFilterIR = (dcFilterIR * 15 + irValue) / 16; 
      lastAcSignal = irValue - dcFilterIR;
      
      display.fillRect(0, 0, 128, 64, SH110X_BLACK); 
      drawBattery(); // Vẽ pin
      
      display.setCursor(0, 0); display.print("ST: STABILIZING");
      display.setTextSize(2); display.setCursor(0, 14); display.print("HR: --"); display.setCursor(0, 32); display.print("SpO2:--");
      display.setTextSize(1); display.setCursor(25, 54); display.print("Please wait..."); display.display();
    }
    else {
      if (inWarmup) { display.clearDisplay(); inWarmup = false; }

      if (!baselineLocked) {
        if (dcFilterIR < 10000) { triggerAdjustError(fingerOn); return; }
        baselineIR = dcFilterIR; baselineLocked = true;
      }
      if (abs((long)irValue - (long)baselineIR) > (baselineIR * 0.15)) { triggerAdjustError(fingerOn); return; }

      dcFilterIR = (dcFilterIR * 15 + irValue) / 16;
      int acSignal = irValue - dcFilterIR;
      int sharpSignal = (acSignal + lastAcSignal) / 2;
      lastAcSignal = acSignal;

      int yPlot = map(sharpSignal, -120, 120, GRAPH_BOTTOM, GRAPH_TOP);
      yPlot = constrain(yPlot, GRAPH_TOP, GRAPH_BOTTOM);
      display.drawLine(xPos, GRAPH_TOP, xPos, GRAPH_BOTTOM, SH110X_BLACK);
      display.drawLine(xPos + 1, GRAPH_TOP, xPos + 1, GRAPH_BOTTOM, SH110X_BLACK);
      display.drawLine(xPos, GRAPH_BOTTOM, xPos, yPlot, SH110X_WHITE);
      xPos++; if(xPos >= 128) xPos = 0;

      unsigned long currentMillis = millis();
      unsigned long delta = currentMillis - lastBeat;
      if (sharpSignal > dynamicThreshold && !isPeak && delta > 300) {
        isPeak = true; dynamicThreshold = sharpSignal * 0.6;
        if (!firstPeakDetected) { firstPeakDetected = true; lastBeat = currentMillis; } 
        else if (delta > 0) {
          float bpm = 60000.0 / delta; lastBeat = currentMillis; 
          if (bpm <= 180 && bpm >= 40) { 
            if (smoothedBPM == 0) smoothedBPM = bpm; else smoothedBPM = (smoothedBPM * 0.75) + (bpm * 0.25);
            beatAvg = (int)smoothedBPM; 
          }
        }
      } else if (sharpSignal < -10) { isPeak = false; }
      dynamicThreshold = max(dynamicThreshold * 0.98, 10.0);

      irBuffer[sampleIdx] = irValue;
      redBuffer[sampleIdx] = redValue;
      sampleIdx++;

      if (sampleIdx >= WINDOW_SIZE) {
        calculateSpO2Window(); 
        for (int i = 50; i < WINDOW_SIZE; i++) {
          irBuffer[i - 50] = irBuffer[i];
          redBuffer[i - 50] = redBuffer[i];
        }
        sampleIdx = 150; 
        bufferFull = true;
      }

      if (isOnline && (currentMillis - lastWsUpdate > 50)) {
        lastWsUpdate = currentMillis;
        wsCleanup();
        wsSendHealthData(sharpSignal, beatAvg, currentSpO2);
      }

      // ==== PHẦN SỬA LỖI MẤT HIỂN THỊ HR / SPO2 ====
      if (currentMillis - lastDisplayUpdate > 50) {
        lastDisplayUpdate = currentMillis;
        
        // 1. Chỉ xóa đúng phần chữ ở nửa trên (chừa lại đồ thị bên dưới)
        display.fillRect(0, 0, 128, 43, SH110X_BLACK);
        
        // 2. Vẽ viên pin và phần trăm
        drawBattery();
        
        // 3. Viết lại thông số đo lường (rút gọn thành "ST: " để không lấn pin)
        display.setTextSize(1); display.setCursor(0, 0); 
        display.print(((millis() / 500) % 2 == 0) ? "ST: MEASURE..." : "ST: MEASURE");
        
        display.setTextSize(2); display.setCursor(0, 10); display.print("HR: "); 
        if (beatAvg > 0) { display.print(beatAvg); display.setTextSize(1); display.print(" BPM"); } 
        else { display.print("--"); }
        
        display.setTextSize(2); display.setCursor(0, 26); display.print("SpO2:"); 
        if (currentSpO2 > 0) { display.print(currentSpO2); display.print("%"); } 
        else { display.print("--%"); }
        
        display.display();
      }

      if ((bufferFull && beatAvg > 0 && currentSpO2 > 80 && timeElapsed > 15000) || (timeElapsed >= 23000)) {
        isMeasured = true; particleSensor.setPulseAmplitudeRed(0x00);
        if (beatAvg > 0 && currentSpO2 > 80) {
          showDoneScreen(); 
          queueSequence("/hr_is.wav", ("/" + String(beatAvg) + ".wav").c_str(), "/bpm.wav");
          queueSequence("/spo2_is.wav", ("/" + String(currentSpO2) + ".wav").c_str(), "/percent.wav");
          readHealthConclusion(beatAvg, currentSpO2); 
        } else {
          showDoneScreen(); queueWav("/fail.wav"); 
        }
      }
    }
  }

  lastFingerOn = fingerOn; 
}