#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "bluetti_device.h"
#include "system_status.h"

constexpr uint8_t BUTTON_1 = 35; // Ліва кнопка
constexpr uint8_t BUTTON_2 = 0;  // Права кнопка (Boot)
// Додаткові кнопки біля екрану
// На LILYGO T-Display можуть бути на різних пінах
// Спробуємо найбільш ймовірні: 36, 39 (input-only) або 32, 33 (з pull-up)
constexpr uint8_t BUTTON_3 = 36; // Додаткова кнопка 1 (input-only, без pull-up)
constexpr uint8_t BUTTON_4 = 39; // Додаткова кнопка 2 (input-only, без pull-up)
// Альтернативні піни (якщо 36/39 не працюють): 32, 33, 25, 26, 27

enum class MenuScreen {
    STATUS = 0,
    WIFI = 1,
    ESP_INFO = 2,
    BLUETTI = 3,
    COUNT
};

class DisplayManager {
public:
    DisplayManager(BluettiDevice* device, SystemStatus* status);
    void begin();
    void loop();
    void showConnecting(const char* message);
    void showError(const char* message);
    void showMessage(const char* line1, const char* line2 = nullptr, uint16_t color = TFT_WHITE);
    void setDisplayTimeout(uint32_t ms);
    bool isDisplayOn() const;

private:
    TFT_eSPI tft;
    BluettiDevice* bluetti;
    SystemStatus* status;
    MenuScreen currentScreen;

    bool displayOn;
    bool button1Latched;
    bool button2Latched;
    bool button3Latched;
    bool button4Latched;
    unsigned long lastInteraction;
    unsigned long lastRender;
    unsigned long displayTimeout;
    unsigned long lastButton1Press;
    unsigned long lastButton2Press;
    unsigned long lastButton3Press;
    unsigned long lastButton4Press;

    void handleButtons();
    void processButton(uint8_t pin, bool& latched, unsigned long& lastPress, 
                       int8_t direction, const char* name, 
                       unsigned long now, unsigned long debounceMs);
    void render();
    void drawStatusScreen();
    void drawWiFiScreen();
    void drawEsp32Screen();
    void drawBluettiScreen();
    void drawFooter(const char* label);
    void drawBatteryIcon(int x, int y, int level);
    void changeScreen(int8_t delta);
    void wakeDisplay();
    void turnDisplayOff();
};

#endif
