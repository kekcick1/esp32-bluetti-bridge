#ifndef SYSTEM_STATUS_H
#define SYSTEM_STATUS_H

#include <Arduino.h>
#include <WiFi.h>

struct SystemStatus {
    bool wifiConnected = false;
    IPAddress wifiIp;
    bool mqttConnected = false;
    bool bluettiEnabled = true;
    bool bluettiConnected = false;
    float esp32Voltage = 0.0f;
    float esp32BatteryVoltage = 0.0f; // Остання відома напруга батареї (без USB)
    bool esp32UsbPowered = false; // Чи підключений USB
    uint8_t esp32BatteryPercent = 0; // Відсоток батареї ESP32
    uint8_t batteryLevel = 0; // Відсоток батареї Bluetti
    int acPower = 0;
    int dcPower = 0;
    int inputPower = 0;
    bool acOutputState = false;
    bool dcOutputState = false;
    unsigned long lastBluettiUpdate = 0;
    unsigned long uptime = 0;
    int wifiRssi = 0;
};

#endif
