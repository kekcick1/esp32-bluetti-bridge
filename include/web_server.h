#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "bluetti_device.h"
#include "system_status.h"

class WebServerManager {
public:
    WebServerManager(BluettiDevice* device, SystemStatus* status);
    void begin();
    void handleClient(); // Заглушка для сумісності
    bool isBluettiEnabled() const;
    void setBluettiEnabled(bool enabled);

private:
    AsyncWebServer server;
    BluettiDevice* bluetti;
    SystemStatus* status;

    String buildHtml();
    String buildConfigHtml();
    String buildUpdateHtml();
    void handleSaveConfig(AsyncWebServerRequest *request);
    void handleUpdateProgress(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void handleSetACOutput(AsyncWebServerRequest *request);
    void handleSetDCOutput(AsyncWebServerRequest *request);
};

#endif
