#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#define SD_CS    10
#define SPI_MOSI 11
#define SPI_MISO 13  // Đổi từ 12 sang 13
#define SPI_SCK  12  // Đổi từ 13 sang 12

bool initSDCard();

#endif