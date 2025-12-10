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
    bool setChargingSpeed(uint8_t speed); // 0=Standard, 1=Silent, 2=Turbo
    bool setEcoMode(bool state);
    bool setPowerLifting(bool state);
    bool setLedMode(uint8_t mode);      // 1=Low, 2=High, 3=SOS, 4=Off
    bool setEcoShutdown(uint8_t hours); // 1..4 години
    bool powerOff();

    uint8_t getBatteryLevel() const;
    int getACOutputPower() const;
    int getDCOutputPower() const;
    bool getACOutputState() const;
    bool getDCOutputState() const;
    int getInputPower() const;
    float getTemperature() const;
    float getBatteryVoltage() const;
    uint8_t getChargingSpeed() const; // Отримати поточний режим зарядки
    bool getEcoMode() const;
    bool getPowerLifting() const;
    uint8_t getLedMode() const;
    uint8_t getEcoShutdown() const;
    
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
    uint8_t cachedInputPower;
    bool cachedAcState;
    bool cachedDcState;
    unsigned long updateInterval; // Інтервал опитування в мс (за замовчуванням 4000)
    uint8_t lastRequestedPage; // Останній запитаний page (0x00 або 0x0B)
    uint16_t lastSingleRegisterRequested = 0; // Останній одноразовий запитаний регістр
    uint8_t featurePollIndex = 0; // Черга опитування додаткових функцій
    uint16_t lastWriteRegister = 0; // Останній записуваний регістр (для діагностики помилок)
    bool ecoWriteBlocked = false; // Якщо пристрій відхилив ECO регістр, більше не пишемо
    bool ledFallbackTried = false; // Якщо 0x0BDA відхилено, пробуємо 0x0BBA один раз
    bool waitingForResponse = false; // Чи очікуємо відповідь від Bluetti
    unsigned long requestStartTime = 0; // Час надсилання останнього запиту

    bool setupCharacteristics();
    bool sendCommand(const uint8_t* data, size_t length);
    bool writeSingleRegister(uint16_t reg, uint16_t value);
    void requestRegister(uint16_t reg);
    void pollFeatureState();
    void requestStatus();
    void handleNotification(uint8_t* data, size_t length);

    static void notificationThunk(NimBLERemoteCharacteristic* characteristic,
                                  uint8_t* data,
                                  size_t length,
                                  bool isNotify);
    static BluettiDevice* instance;
};

#endif
