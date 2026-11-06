#include "web_manage.h"
#include "webpage.h"

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
File uploadFile;

void setupWiFi(const char* ssid, const char* password) {
  WiFi.mode(WIFI_STA); 
  WiFi.begin(ssid, password);
  int retries = 0; 
  while (WiFi.status() != WL_CONNECTED && retries < 20) { 
    delay(500); 
    retries++; 
  }
  if (WiFi.status() == WL_CONNECTED) {
    isOnline = true; 
    MDNS.begin("hust-health");
  } else {
    isOnline = false;
  }
}

void setupWeb() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/html", index_html); });
  
  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = ""; 
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    File root = SD.open("/"); 
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) { 
        html += "<div class='file-item'><span>📄 /" + String(file.name()) + "</span><button class='btn-danger' onclick=\"deleteFile('/" + String(file.name()) + "')\">Xóa</button></div>"; 
      }
      file = root.openNextFile();
    }
    xSemaphoreGive(sdMutex); 
    request->send(200, "text/html", html);
  });
  
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("file")) {
      xSemaphoreTake(sdMutex, portMAX_DELAY);
      if (SD.exists(request->getParam("file")->value())) SD.remove(request->getParam("file")->value());
      xSemaphoreGive(sdMutex);
    }
    request->send(200, "text/plain", "OK");
  });
  
  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    File root = SD.open("/"); File file = root.openNextFile();
    while (file) { if (!file.isDirectory()) SD.remove(String("/") + file.name()); file = root.openNextFile(); }
    xSemaphoreGive(sdMutex); request->send(200, "text/plain", "OK");
  });
  
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", "OK"); }, 
  [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      if (!filename.startsWith("/")) filename = "/" + filename;
      xSemaphoreTake(sdMutex, portMAX_DELAY); 
      if (SD.exists(filename)) SD.remove(filename); 
      uploadFile = SD.open(filename, FILE_WRITE);
    }
    if (uploadFile) uploadFile.write(data, len);
    if (final) { 
        if (uploadFile) uploadFile.close(); 
        xSemaphoreGive(sdMutex); 
    }
  });

  ws.onEvent([](AsyncWebSocket *s, AsyncWebSocketClient *c, AwsEventType t, void *a, uint8_t *d, size_t l){}); 
  server.addHandler(&ws); 
  server.begin();
}

void wsSendHealthData(int sharpSignal, int beatAvg, int currentSpO2) {
  if (ws.count() > 0 && ws.availableForWriteAll()) {
    char jsonBuf[64];
    snprintf(jsonBuf, sizeof(jsonBuf), "{\"ir\":%d,\"bpm\":%d,\"spo2\":%d}", sharpSignal, beatAvg, currentSpO2);
    ws.textAll(jsonBuf);
  }
}

void wsCleanup() {
    ws.cleanupClients();
}