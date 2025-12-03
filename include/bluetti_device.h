#ifndef BLUETTI_DEVICE_H
#define BLUETTI_DEVICE_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "system_status.h"

static constexpr char BLUETTI_SERVICE_UUID[] = "0000ff00-0000-1000-8000-00805f9b34fb";
static constexpr char BLUETTI_NOTIFY_UUID[]  = "0000ff01-0000-1000-8000-00805f9b34fb";
static constexpr char BLUETTI_WRITE_UUID[]   = "0000ff02-0000-1000-8000-00805f9b34fb";

class BluettiDevice {
public:
    explicit BluettiDevice(SystemStatus* status);
    bool begin();
    bool connectByMAC(const char* macAddress);
    bool scanAndConnect(const char* macAddress);
    void disconnect();
    void loop();
    bool isConnected() const;

    bool setACOutput(bool state);
    bool setDCOutput(bool state);

    uint8_t getBatteryLevel() const;
    int getACOutputPower() const;
    int getDCOutputPower() const;
    bool getACOutputState() const;
    bool getDCOutputState() const;
    int getInputPower() const;
    
    // Налаштування інтервалу опитування (мс)
    void setUpdateInterval(unsigned long intervalMs);
    unsigned long getUpdateInterval() const;

private:
    NimBLEClient* client;
    NimBLERemoteCharacteristic* notifyCharacteristic;
    NimBLERemoteCharacteristic* writeCharacteristic;
    bool connected;
    unsigned long lastRequest;
    SystemStatus* status;
    bool connecting;
    unsigned long connectStartTime;
    uint8_t connectAttempts;
    bool scanning;
    unsigned long scanStartTime;
    NimBLEScan* scanner;
    uint8_t cachedBattery;
    int cachedAcPower;
    int cachedDcPower;
    int cachedInputPower;
    bool cachedAcState;
    bool cachedDcState;
    unsigned long updateInterval; // Інтервал опитування в мс (за замовчуванням 4000)

    bool setupCharacteristics();
    bool sendCommand(const uint8_t* data, size_t length);
    void requestStatus();
    void handleNotification(uint8_t* data, size_t length);

    static void notificationThunk(NimBLERemoteCharacteristic* characteristic,
                                  uint8_t* data,
                                  size_t length,
                                  bool isNotify);
    static BluettiDevice* instance;
};

#endif
