#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "bluetti_device.h"
#include "system_status.h"

class MQTTHandler {
public:
    MQTTHandler(BluettiDevice* device, SystemStatus* status);
    void configure(const char* server, uint16_t port, const char* user = nullptr, const char* pass = nullptr);
    void loop(bool wifiReady);
    bool isConnected();
    void republishDiscovery(); // Публічний метод для повторної публікації Discovery

private:
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    BluettiDevice* bluetti;
    SystemStatus* status;

    String serverHost;
    uint16_t serverPort;
    String username;
    String password;
    unsigned long lastPublish;
    unsigned long lastMqttAttempt;
    bool mqttConnecting;

    bool ensureConnection();
    void onMessage(char* topic, byte* payload, unsigned int length);
    void publishStatus();
    void publishDiscovery();

    static void callbackThunk(char* topic, byte* payload, unsigned int length);
    static MQTTHandler* instance;
};

#endif
