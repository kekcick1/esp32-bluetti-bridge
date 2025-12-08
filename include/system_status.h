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
    
    // Bluetti основні дані
    uint8_t batteryLevel = 0; // Відсоток батареї Bluetti
    int acPower = 0;
    int dcPower = 0;
    int inputPower = 0;
    bool acOutputState = false;
    bool dcOutputState = false;
    
    // Bluetti розширені дані
    int dcInputPower = 0;      // DC вхідна потужність (W)
    int acInputPower = 0;      // AC вхідна потужність (W)
    uint16_t batteryVoltage = 0;  // Напруга батареї (×10, тобто 537 = 53.7V)
    uint16_t batteryRaw = 0;   // Сире значення батареї (×10)
    uint16_t temperature = 0;  // Температура (можливо ×10)
    uint16_t maxDcLimit = 0;  // Максимальний DC ліміт
    char modelName[5] = "";   // Модель пристрою (EB3A)
    uint16_t registers[40];    // Всі 40 регістрів для аналізу
    uint8_t chargingSpeed = 0; // Швидкість зарядки: 0=Standard, 1=Silent, 2=Turbo
    
    unsigned long lastBluettiUpdate = 0;
    unsigned long uptime = 0;
    int wifiRssi = 0;
};

#endif
