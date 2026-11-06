#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <SD.h>

#define I2S_BCLK 6  // Đổi từ 5 sang 6
#define I2S_LRC  5  // Đổi từ 6 sang 5
#define I2S_DOUT 7  // Giữ nguyên
#define I2S_NUM I2S_NUM_0
#define MAX_AUDIO_FILENAME_LEN 32

// Chia sẻ các biến toàn cục với main.cpp
extern QueueHandle_t audioQueue;
extern SemaphoreHandle_t sdMutex;

// Khai báo các hàm
void initI2S();
void AudioTask(void *pvParameters);
void queueWav(const char* filePath);
void queueSequence(const char* file1, const char* file2, const char* file3);
void readHealthConclusion(int hr, int spo2);

#endif