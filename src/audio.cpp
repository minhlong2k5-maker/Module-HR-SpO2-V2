#include "audio.h"

void initI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000, 
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, 
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,    
    .dma_buf_len = 1024,   
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM, &pin_config);
}

void AudioTask(void *pvParameters) {
  char fileName[MAX_AUDIO_FILENAME_LEN];
  while(1) {
    if (xQueueReceive(audioQueue, &fileName, portMAX_DELAY) == pdTRUE) {
      if (xSemaphoreTake(sdMutex, portMAX_DELAY) == pdTRUE) {
        bool hasMore = true;
        
        while(hasMore) {
          File audioFile = SD.open(fileName, FILE_READ);
          if (audioFile) {
            uint32_t dataOffset = 44;
            uint32_t dataLength = audioFile.size() - 44;
            
            uint8_t buf[100];
            audioFile.seek(0);
            audioFile.read(buf, 100);
            
            for (int i = 0; i < 92; i++) {
              if (buf[i] == 'd' && buf[i+1] == 'a' && buf[i+2] == 't' && buf[i+3] == 'a') {
                dataOffset = i + 8; 
                dataLength = buf[i+4] | (buf[i+5] << 8) | (buf[i+6] << 16) | (buf[i+7] << 24);
                break;
              }
            }
            
            audioFile.seek(dataOffset);
            
            uint8_t playBuffer[512]; 
            uint32_t bytesPlayed = 0;
            
            while (audioFile.available() && bytesPlayed < dataLength) {
              size_t bytesToRead = sizeof(playBuffer);
              if (dataLength - bytesPlayed < bytesToRead) bytesToRead = dataLength - bytesPlayed;

              size_t readBytes = audioFile.read(playBuffer, bytesToRead);
              if (readBytes > 0) {
                int16_t* pcmData = (int16_t*)playBuffer;
                int samples = readBytes / 2;
                for (int i = 0; i < samples; i++) {
                  int32_t amplified = pcmData[i] * 1.0; 
                  if (amplified > 32767) amplified = 32767;
                  if (amplified < -32768) amplified = -32768;
                  pcmData[i] = (int16_t)amplified;
                }
                size_t written = 0;
                i2s_write(I2S_NUM, playBuffer, readBytes, &written, portMAX_DELAY);
                bytesPlayed += readBytes;
              }
            }
            audioFile.close();
            
            uint8_t silence[128] = {0}; 
            size_t written = 0;
            i2s_write(I2S_NUM, silence, sizeof(silence), &written, portMAX_DELAY);
          }
          
          if (uxQueueMessagesWaiting(audioQueue) > 0) {
            xQueueReceive(audioQueue, &fileName, 0); 
          } else {
            hasMore = false; 
          }
        }
        xSemaphoreGive(sdMutex);
      }
    }
  }
}

void queueWav(const char* filePath) {
  char temp[MAX_AUDIO_FILENAME_LEN];
  strncpy(temp, filePath, MAX_AUDIO_FILENAME_LEN - 1);
  temp[MAX_AUDIO_FILENAME_LEN - 1] = '\0';
  xQueueSend(audioQueue, &temp, 0);
}

void queueSequence(const char* file1, const char* file2, const char* file3) {
  if (file1 && strlen(file1) > 0) queueWav(file1);
  if (file2 && strlen(file2) > 0) queueWav(file2);
  if (file3 && strlen(file3) > 0) queueWav(file3);
}

void readHealthConclusion(int hr, int spo2) {
  bool hrNormal = (hr >= 60 && hr <= 100);
  bool spo2Normal = (spo2 >= 95);
  if (hrNormal && spo2Normal) queueWav("/c_norm.wav");
  else if (!hrNormal && !spo2Normal) queueWav("/c_dang.wav"); 
  else if (!spo2Normal) queueWav("/c_hypo.wav"); 
  else if (hr > 100) queueWav("/c_tachy.wav"); 
  else if (hr < 60) queueWav("/c_brady.wav"); 
}