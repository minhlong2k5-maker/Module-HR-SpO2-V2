#include "sd_logger.h"

bool initSDCard() {
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS); 
    if (!SD.begin(SD_CS, SPI, 4000000)) {
        return false;
    }
    return true;
}