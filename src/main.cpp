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
// THÔNG TIN WIFI & PINS MỚI
// ==========================================
const char* ssid = "P203";
const char* password = "28112005";

// I2C cho OLED
#define OLED_SDA 1
#define OLED_SCL 2

// I2C cho MAX30102
#define MAX_SDA 8
#define MAX_SCL 9

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

// ==========================================
// OLED FUNCTIONS
// ==========================================
void showReadyScreen() {
  display.clearDisplay(); display.setTextSize(2);
  display.setCursor(35, 15); display.println("READY");
  display.setTextSize(1);
  display.setCursor(25, 40); display.println("Place finger..."); display.display();
}

void showDoneScreen() {
  display.clearDisplay();
  bool isValid = (beatAvg > 0 && currentSpO2 > 80);
  display.setTextSize(1); display.setCursor(0, 0);
  display.print(isValid ? "Status: DONE!" : "Status: FAILED!");
  display.setTextSize(2); display.setCursor(0, 14);
  display.print("HR: "); if (beatAvg > 0) display.print(beatAvg); else display.print("--");
  display.setTextSize(1); display.print(" BPM");
  display.setTextSize(2); display.setCursor(0, 32);
  display.print("SpO2:"); if (currentSpO2 > 0) display.print(currentSpO2); else display.print("--");
  display.print("%");
  display.drawLine(0, 50, 127, 50, SH110X_WHITE);
  display.setTextSize(1); display.setCursor(25, 54); display.print("REMOVE FINGER"); display.display();
}

void triggerAdjustError(bool isFingerOn) {
  display.clearDisplay(); display.setTextSize(2);
  display.setCursor(25, 20); display.print("ADJUST");
  display.setCursor(25, 40); display.print("FINGER!"); display.display();
  queueWav("/adjust.wav"); 
  measureStartTime = millis(); particleSensor.clearFIFO(); 
  
  sampleIdx = 0; bufferFull = false; smoothedSpO2 = 0; inWarmup = true;
  dcFilterIR = particleSensor.getIR(); lastAcSignal = 0; dynamicThreshold = 30.0; smoothedBPM = 0; firstPeakDetected = false;
  baselineLocked = false; lastFingerOn = isFingerOn; 
}

// ==========================================
void setup() {
  Serial.begin(115200); delay(1000);
  
  audioQueue = xQueueCreate(15, MAX_AUDIO_FILENAME_LEN * sizeof(char));
  sdMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(AudioTask, "AudioTask", 4096, NULL, 1, NULL, 0);

  // 1. Khởi tạo bus I2C thứ nhất (Wire) cho OLED trên chân 1 và 2
  Wire.begin(OLED_SDA, OLED_SCL); 
  Wire.setClock(400000); 
  display.begin(0x3C, true); 
  display.clearDisplay(); 
  display.setTextColor(SH110X_WHITE);
  display.setCursor(10, 30); 
  display.print("Booting..."); 
  display.display();

  initSDCard();
  
  // 2. Khởi tạo bus I2C thứ hai (Wire1) cho MAX30102 trên chân 8 và 9
  Wire1.begin(MAX_SDA, MAX_SCL);
  Wire1.setClock(400000);
  // LƯU Ý QUAN TRỌNG: Truyền Wire1 vào hàm begin của cảm biến
  particleSensor.begin(Wire1, I2C_SPEED_FAST);
  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x00); 
  particleSensor.setPulseAmplitudeIR(0x1F); 
  particleSensor.setSampleRate(100);

  initI2S(); 
  setupWiFi(ssid, password);
  
  if (isOnline) {
    setupWeb();
    display.clearDisplay(); display.setCursor(0, 10); 
    display.println("Web Ready:"); 
    display.println(WiFi.localIP().toString()); 
    display.display(); 
    delay(2000);
  } else {
    display.clearDisplay(); display.setCursor(0, 10); 
    display.println("OFFLINE MODE"); 
    display.display(); 
    delay(2000);
  }
  showReadyScreen(); 
  queueWav("/ready.wav");
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
    display.clearDisplay(); display.setTextSize(1); display.setCursor(20, 30); display.print("Please wait..."); display.display();
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
      display.setCursor(0, 0); display.print("Status: STABILIZING");
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

      if (currentMillis - lastDisplayUpdate > 50) {
        lastDisplayUpdate = currentMillis;
        display.fillRect(0, 0, 128, 43, SH110X_BLACK);
        display.setTextSize(1); display.setCursor(0, 0); display.print(((millis() / 500) % 2 == 0) ? "Status: MEASURING..." : "Status: MEASURING");
        display.setTextSize(2); display.setCursor(0, 10); display.print("HR: "); if (beatAvg > 0) { display.print(beatAvg); display.setTextSize(1); display.print(" BPM"); } else { display.print("--"); }
        display.setTextSize(2); display.setCursor(0, 26); display.print("SpO2:"); if (currentSpO2 > 0) { display.print(currentSpO2); display.print("%"); } else { display.print("--%"); }
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