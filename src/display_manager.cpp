#include "display_manager.h"
#include <cstring>
#include <WiFi.h>

namespace {
constexpr uint8_t TFT_BL_PIN = 4;
constexpr uint16_t BG_COLOR = TFT_BLACK;
constexpr uint16_t TITLE_COLOR = TFT_CYAN;
constexpr uint16_t TEXT_COLOR = TFT_WHITE;
constexpr uint16_t WARN_COLOR = TFT_YELLOW;
constexpr uint16_t ERROR_COLOR = TFT_RED;
}

DisplayManager::DisplayManager(BluettiDevice* device, SystemStatus* sharedStatus)
    : bluetti(device),
      status(sharedStatus),
      currentScreen(MenuScreen::STATUS),
      displayOn(true),
      button1Latched(false),
      button2Latched(false),
      button3Latched(false),
      button4Latched(false),
      lastInteraction(0),
      lastRender(0),
      displayTimeout(10000),  // 10 секунд
      lastButton1Press(0),
      lastButton2Press(0),
      lastButton3Press(0),
      lastButton4Press(0) {}

void DisplayManager::begin() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(BG_COLOR);

    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);
    
    // Налаштування кнопок
    // GPIO 35 - input-only пін (не має внутрішнього pull-up), але на платі є зовнішній pull-up
    // GPIO 0 - Boot button, має внутрішній pull-up
    pinMode(BUTTON_1, INPUT);  // GPIO 35 - input-only, використовуємо INPUT
    pinMode(BUTTON_2, INPUT_PULLUP);  // GPIO 0 - має pull-up
    
    // Перевіряємо початковий стан кнопок
    Serial.printf("[BTN] GPIO %d (Button 1) initial: %d\n", BUTTON_1, digitalRead(BUTTON_1));
    Serial.printf("[BTN] GPIO %d (Button 2) initial: %d\n", BUTTON_2, digitalRead(BUTTON_2));
    
    // Додаткова діагностика: перевіримо початковий стан після налаштування
    delay(50); // Затримка для стабілізації
    Serial.printf("[BTN INIT] GPIO %d initial state: %d (0=LOW, 1=HIGH)\n", BUTTON_1, digitalRead(BUTTON_1));
    Serial.printf("[BTN INIT] GPIO %d initial state: %d (0=LOW, 1=HIGH)\n", BUTTON_2, digitalRead(BUTTON_2));
    
    // Додаткові кнопки біля екрану
    // На LILYGO T-Display можуть бути додаткові кнопки на різних пінах
    // Спробуємо найбільш ймовірні варіанти:
    // - GPIO 36, 39 (input-only, без pull-up)
    // - GPIO 32, 33, 25, 26, 27 (з pull-up)
    
    // Спочатку налаштуємо як INPUT (для GPIO 36, 39)
    pinMode(BUTTON_3, INPUT);
    pinMode(BUTTON_4, INPUT);
    
    // Невелика затримка для стабілізації пінів після завантаження
    delay(100);
    
    // Діагностика всіх кнопок при старті
    Serial.println("\n=== Button Diagnostics ===");
    Serial.printf("Button 1 (GPIO %d, INPUT): %s (LOW=Pressed, HIGH=Released)\n", 
                   BUTTON_1, digitalRead(BUTTON_1) == LOW ? "PRESSED" : "RELEASED");
    Serial.printf("Button 2 (GPIO %d, INPUT_PULLUP): %s (LOW=Pressed, HIGH=Released)\n", 
                   BUTTON_2, digitalRead(BUTTON_2) == LOW ? "PRESSED" : "RELEASED");
    Serial.printf("Button 3 (GPIO %d, INPUT): %s (LOW=Pressed, HIGH=Released)\n", 
                   BUTTON_3, digitalRead(BUTTON_3) == LOW ? "PRESSED" : "RELEASED");
    Serial.printf("Button 4 (GPIO %d, INPUT): %s (LOW=Pressed, HIGH=Released)\n", 
                   BUTTON_4, digitalRead(BUTTON_4) == LOW ? "PRESSED" : "RELEASED");
    Serial.println("Note: GPIO 35 is input-only (no internal pull-up, uses external)");
    Serial.println("Note: GPIO 0 is Boot button");
    Serial.println("Press buttons to test - check Serial Monitor for [BUTTON] messages");
    
    // Сканування можливих пінів для додаткових кнопок
    // Перевіряємо піни, які часто використовуються для кнопок
    uint8_t testPins[] = {32, 33, 25, 26, 27, 14, 12, 13, 2, 15, 16, 17, 5, 18, 19, 21, 22, 23};
    for (uint8_t i = 0; i < sizeof(testPins)/sizeof(testPins[0]); i++) {
        // Пропускаємо піни, які вже використовуються
        if (testPins[i] == BUTTON_1 || testPins[i] == BUTTON_2 || 
            testPins[i] == TFT_BL_PIN || testPins[i] == 19 || testPins[i] == 18 || 
            testPins[i] == 5 || testPins[i] == 16 || testPins[i] == 23) {
            continue;
        }
        pinMode(testPins[i], INPUT_PULLUP);
        delay(5);
    }
    Serial.println("Note: GPIO 36 and 39 are input-only (no internal pull-up)");
    Serial.println("Note: GPIO 0 is Boot button");
    Serial.println("==========================\n");

    showMessage("ESP32 BLUETTI", "Display Ready");
    lastInteraction = millis();
}

void DisplayManager::loop() {
    // ВАЖЛИВО: Обробляємо кнопки ЗАВЖДИ, навіть якщо дисплей вимкнений!
    handleButtons();

    // Якщо дисплей вимкнений, не рендеримо
    if (!displayOn) {
        return;
    }

    // Перевіряємо таймаут вимкнення дисплею
    unsigned long timeSinceInteraction = millis() - lastInteraction;
    if (timeSinceInteraction > displayTimeout) {
        turnDisplayOff();
        return; // Не рендеримо після вимкнення
    }

    // Рендеримо екран одразу після зміни або кожні 1000мс
    static MenuScreen lastRenderedScreen = MenuScreen::COUNT;
    if (currentScreen != lastRenderedScreen || (millis() - lastRender > 1000)) {
        lastRenderedScreen = currentScreen;
        lastRender = millis();
        render();
    }
}

void DisplayManager::setDisplayTimeout(uint32_t ms) {
    displayTimeout = ms;
}

bool DisplayManager::isDisplayOn() const {
    return displayOn;
}

void DisplayManager::showConnecting(const char* message) {
    showMessage("Connecting", message ? message : "...");
}

void DisplayManager::showError(const char* message) {
    showMessage("ERROR", message ? message : "Unknown", ERROR_COLOR);
}

void DisplayManager::showMessage(const char* line1, const char* line2, uint16_t color) {
    wakeDisplay();
    tft.fillScreen(BG_COLOR);
    tft.setTextColor(color);
    tft.setTextSize(2);
    tft.setCursor(10, 40);
    tft.println(line1);
    if (line2 && strlen(line2) > 0) {
        tft.setTextSize(1);
        tft.setCursor(10, 80);
        tft.setTextColor(TEXT_COLOR);
        tft.println(line2);
    }
}

// ============================================================================
// ВАЖЛИВО: Кнопки працюють правильно! Не змінюйте цю функцію!
// GPIO 35, 36, 39 - працюють з інвертованою логікою (HIGH = pressed)
// ============================================================================
void DisplayManager::handleButtons() {
    unsigned long now = millis();
    constexpr unsigned long DEBOUNCE_MS = 50;
    
    // Обробка кнопок - перевіряємо кожен цикл
    // GPIO 35 (BUTTON_1) - input-only, на платі є зовнішній pull-up, LOW = pressed
    // GPIO 0 (BUTTON_2) - має внутрішній pull-up, LOW = pressed
    
    // Читаємо стан кнопок
    int btn1State = digitalRead(BUTTON_1);
    int btn2State = digitalRead(BUTTON_2);
    
    // Діагностика - виводимо стан при зміні
    static int lastBtn1State = -1;
    static int lastBtn2State = -1;
    static unsigned long lastDebug = 0;
    
    // Виводимо стан при зміні для діагностики
    if (btn1State != lastBtn1State || btn2State != lastBtn2State) {
        Serial.printf("[BTN] GPIO %d: %s (%d), GPIO %d: %s (%d)\n", 
                     BUTTON_1, btn1State == LOW ? "PRESSED" : "RELEASED", btn1State,
                     BUTTON_2, btn2State == LOW ? "PRESSED" : "RELEASED", btn2State);
        lastBtn1State = btn1State;
        lastBtn2State = btn2State;
    }
    
    // Обробка кнопок - спрощена логіка
    // GPIO 35 (BUTTON_1) - input-only, зовнішній pull-up, LOW = pressed
    // GPIO 0 (BUTTON_2) - Boot button, внутрішній pull-up, LOW = pressed
    
    // Кнопка 1 (GPIO 35) - LOW = pressed
    bool btn1Pressed = (btn1State == LOW);
    if (btn1Pressed && !button1Latched) {
        if (now - lastButton1Press > DEBOUNCE_MS) {
            wakeDisplay();
            changeScreen(-1);
            lastInteraction = now;
            lastButton1Press = now;
            button1Latched = true;
            Serial.printf("[BUTTON] GPIO %d pressed - Previous screen\n", BUTTON_1);
        }
    } else if (!btn1Pressed && button1Latched) {
        button1Latched = false;
    }
    
    // Кнопка 2 (GPIO 0 - Boot) - LOW = pressed
    bool btn2Pressed = (btn2State == LOW);
    if (btn2Pressed && !button2Latched) {
        if (now - lastButton2Press > DEBOUNCE_MS) {
            wakeDisplay();
            changeScreen(+1);
            lastInteraction = now;
            lastButton2Press = now;
            button2Latched = true;
            Serial.printf("[BUTTON] GPIO %d pressed - Next screen\n", BUTTON_2);
        }
    } else if (!btn2Pressed && button2Latched) {
        button2Latched = false;
    }
    
    // lastInteraction оновлюється тільки при edge detection (в if блоках вище)
    // Не оновлюємо його при постійному натисканні, щоб екран міг вимкнутися
    
    // Тестуємо додаткові кнопки (якщо вони є)
    processButton(BUTTON_3, button3Latched, lastButton3Press, -1, "Button 3", now, DEBOUNCE_MS);
    processButton(BUTTON_4, button4Latched, lastButton4Press, +1, "Button 4", now, DEBOUNCE_MS);
    
    // Альтернативні піни обробляємо рідше для швидкості
    static unsigned long lastAltCheck = 0;
    if (now - lastAltCheck > 100) { // Перевіряємо альтернативні піни кожні 100мс
        static bool altButton1Latched = false;
        static bool altButton2Latched = false;
        static unsigned long altButton1Press = 0;
        static unsigned long altButton2Press = 0;
        
        uint8_t altPins[] = {32, 33, 25, 26, 27};
        for (uint8_t i = 0; i < 2 && i < sizeof(altPins)/sizeof(altPins[0]); i++) {
            bool& latched = (i == 0) ? altButton1Latched : altButton2Latched;
            unsigned long& lastPress = (i == 0) ? altButton1Press : altButton2Press;
            int8_t direction = (i == 0) ? -1 : +1;
            char name[20];
            snprintf(name, sizeof(name), "Alt Button %d (GPIO %d)", i+1, altPins[i]);
            processButton(altPins[i], latched, lastPress, direction, name, now, DEBOUNCE_MS);
        }
        lastAltCheck = now;
    }
}

void DisplayManager::processButton(uint8_t pin, bool& latched, unsigned long& lastPress, 
                                    int8_t direction, const char* name, 
                                    unsigned long now, unsigned long debounceMs) {
    bool pressed = (digitalRead(pin) == LOW);
    
    if (pressed && !latched) {
        // Edge detection - кнопка тільки що натиснута
        if (now - lastPress > debounceMs) {
            wakeDisplay();
            changeScreen(direction);
            lastInteraction = now; // Оновлюємо тільки при edge detection
            lastPress = now;
            latched = true;
            Serial.printf("[BUTTON] %s (GPIO %d) pressed - %s screen\n", 
                         name, pin, direction < 0 ? "Previous" : "Next");
        }
    } else if (!pressed && latched) {
        // Кнопка відпущена
        latched = false;
    }
    
    // НЕ оновлюємо lastInteraction при постійному натисканні
    // Це дозволяє екрану вимкнутися через таймаут навіть якщо кнопка залишається натиснутою
}

void DisplayManager::render() {
    switch (currentScreen) {
        case MenuScreen::STATUS:
            drawStatusScreen();
            break;
        case MenuScreen::WIFI:
            drawWiFiScreen();
            break;
        case MenuScreen::ESP_INFO:
            drawEsp32Screen();
            break;
        case MenuScreen::BLUETTI:
            drawBluettiScreen();
            break;
        default:
            currentScreen = MenuScreen::STATUS;
            break;
    }
}

void DisplayManager::drawStatusScreen() {
    static MenuScreen lastRenderedScreen = MenuScreen::COUNT;
    if (lastRenderedScreen != currentScreen) {
        tft.fillScreen(BG_COLOR);
        lastRenderedScreen = currentScreen;
    }
    tft.setTextColor(TITLE_COLOR);
    tft.setTextSize(2);
    tft.setCursor(20, 6);
    tft.println("BLUETTI EB3A");

    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.setTextColor(TEXT_COLOR);
    if (!status->bluettiEnabled) {
        tft.setTextColor(WARN_COLOR);
        tft.println("Bluetti disabled");
    } else if (!status->bluettiConnected) {
        tft.setTextColor(ERROR_COLOR);
        tft.println("No BLE connection");
    } else {
        tft.printf("Battery: %u%%\n", status->batteryLevel);
        tft.printf("AC: %s %dW\n", status->acOutputState ? "ON " : "OFF", status->acPower);
        tft.printf("DC: %s %dW\n", status->dcOutputState ? "ON " : "OFF", status->dcPower);
        tft.printf("Input: %dW\n", status->inputPower);
    }

    drawBatteryIcon(170, 25, status->batteryLevel);
    drawFooter("Status");
}

void DisplayManager::drawWiFiScreen() {
    static MenuScreen lastRenderedScreen = MenuScreen::COUNT;
    if (lastRenderedScreen != currentScreen) {
        tft.fillScreen(BG_COLOR);
        lastRenderedScreen = currentScreen;
    }
    tft.setTextColor(TITLE_COLOR);
    tft.setTextSize(2);
    tft.setCursor(80, 6);
    tft.println("WiFi");

    tft.setTextSize(1);
    tft.setTextColor(status->wifiConnected ? TEXT_COLOR : ERROR_COLOR);
    tft.setCursor(10, 40);
    tft.printf("Status: %s\n", status->wifiConnected ? "Connected" : "Lost");
    tft.setCursor(10, 55);
    tft.setTextColor(TEXT_COLOR);
    tft.printf("IP: %s\n", status->wifiConnected ? status->wifiIp.toString().c_str() : "0.0.0.0");
    tft.setCursor(10, 70);
    tft.printf("RSSI: %d dBm\n", status->wifiConnected ? WiFi.RSSI() : 0);
    tft.setCursor(10, 85);
    tft.setTextColor(status->mqttConnected ? TEXT_COLOR : WARN_COLOR);
    tft.printf("MQTT: %s\n", status->mqttConnected ? "Connected" : "Waiting");

    drawFooter("WiFi");
}

void DisplayManager::drawEsp32Screen() {
    static MenuScreen lastRenderedScreen = MenuScreen::COUNT;
    if (lastRenderedScreen != currentScreen) {
        tft.fillScreen(BG_COLOR);
        lastRenderedScreen = currentScreen;
    }
    tft.setTextColor(TITLE_COLOR);
    tft.setTextSize(2);
    tft.setCursor(70, 6);
    tft.println("ESP32");

    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.setTextColor(TEXT_COLOR);
    // Напруга акумулятора (якщо підключений) або USB живлення
    if (status->esp32UsbPowered) {
        // USB підключений - показуємо останню відому напругу батареї
        if (status->esp32BatteryVoltage > 0.1f) {
            tft.printf("Battery: %.2f V\n", status->esp32BatteryVoltage);
            tft.setCursor(10, 50);
            tft.printf("Percent: %u%%\n", status->esp32BatteryPercent);
            tft.setCursor(10, 60);
            tft.setTextColor(0x07E0); // Зелений колір для USB
            tft.println("USB Charging");
            tft.setTextColor(TEXT_COLOR);
        } else {
            tft.println("Battery: Unknown");
            tft.setCursor(10, 50);
            tft.setTextColor(0x07E0); // Зелений колір для USB
            tft.println("USB Powered");
            tft.setTextColor(TEXT_COLOR);
        }
    } else if (status->esp32Voltage < 0.05f) {
        tft.println("Battery: 0.00 V (USB?)");
    } else if (status->esp32Voltage >= 2.5f) {
        // Нормальна батарея - показуємо з відсотком
        tft.printf("Battery: %.2f V\n", status->esp32Voltage);
        tft.setCursor(10, 50);
        tft.printf("Percent: %u%%\n", status->esp32BatteryPercent);
    } else {
        // Низька напруга - можливо розряджена батарея, показуємо без відсотка
        tft.printf("Battery: %.2f V\n", status->esp32Voltage);
    }
    // Визначаємо початкову позицію Y для наступних рядків
    int startY = 60;
    if (status->esp32UsbPowered) {
        startY = 70; // Якщо USB підключений, є додатковий рядок
    }
    tft.setCursor(10, startY);
    tft.printf("Heap: %u KB\n", ESP.getFreeHeap() / 1024);
    tft.setCursor(10, startY + 10);
    tft.printf("Max Block: %u KB\n", ESP.getMaxAllocHeap() / 1024);
    tft.setCursor(10, startY + 20);
    tft.printf("CPU: %u MHz\n", ESP.getCpuFreqMHz());
    tft.setCursor(10, startY + 30);
    tft.printf("Uptime: %lu s\n", status->uptime);

    drawFooter("ESP32");
}

void DisplayManager::drawBluettiScreen() {
    static MenuScreen lastRenderedScreen = MenuScreen::COUNT;
    if (lastRenderedScreen != currentScreen) {
        tft.fillScreen(BG_COLOR);
        lastRenderedScreen = currentScreen;
    }
    tft.setTextColor(TITLE_COLOR);
    tft.setTextSize(2);
    tft.setCursor(70, 6);
    tft.println("BLE");

    tft.setTextSize(1);
    tft.setCursor(10, 40);
    if (!status->bluettiEnabled) {
        tft.setTextColor(WARN_COLOR);
        tft.println("Connection disabled");
    } else if (!status->bluettiConnected) {
        tft.setTextColor(ERROR_COLOR);
        tft.println("Not connected");
    } else {
        tft.setTextColor(TEXT_COLOR);
        tft.printf("Battery: %u%%\n", status->batteryLevel);
        tft.printf("Last update: %lus\n", (millis() - status->lastBluettiUpdate) / 1000UL);
        tft.printf("AC: %s %dW\n", status->acOutputState ? "ON " : "OFF", status->acPower);
        tft.printf("DC: %s %dW\n", status->dcOutputState ? "ON " : "OFF", status->dcPower);
    }

    drawFooter("Bluetti");
}

void DisplayManager::drawFooter(const char* label) {
    uint8_t index = static_cast<uint8_t>(currentScreen) + 1;
    uint8_t total = static_cast<uint8_t>(MenuScreen::COUNT);
    tft.setTextColor(TFT_DARKGREY, BG_COLOR);
    tft.setCursor(10, 120);
    tft.printf("%s", label);
    tft.setCursor(180, 120);
    tft.printf("%u/%u", index, total);
}

void DisplayManager::drawBatteryIcon(int x, int y, int level) {
    tft.drawRect(x, y, 50, 22, TEXT_COLOR);
    tft.fillRect(x + 50, y + 7, 4, 8, TEXT_COLOR);
    level = constrain(level, 0, 100);
    int width = map(level, 0, 100, 0, 48);
    uint16_t color = TEXT_COLOR;
    if (level < 20) {
        color = ERROR_COLOR;
    } else if (level < 50) {
        color = WARN_COLOR;
    }
    tft.fillRect(x + 1, y + 1, width, 20, color);
}

void DisplayManager::changeScreen(int8_t delta) {
    int8_t total = static_cast<int8_t>(MenuScreen::COUNT);
    int8_t current = static_cast<int8_t>(currentScreen);
    current = (current + delta + total) % total;
    currentScreen = static_cast<MenuScreen>(current);
    tft.fillScreen(BG_COLOR);
    // ВАЖЛИВО: одразу малюємо новий екран після зміни
    render();
}

void DisplayManager::wakeDisplay() {
    if (!displayOn) {
        digitalWrite(TFT_BL_PIN, HIGH);
        displayOn = true;
        Serial.println("[DISPLAY] Woke up");
    }
    lastInteraction = millis();
}

void DisplayManager::turnDisplayOff() {
    if (displayOn) {
        displayOn = false;
        digitalWrite(TFT_BL_PIN, LOW);
        tft.fillScreen(BG_COLOR); // Очищаємо екран
        Serial.println("[DISPLAY] Turned off - backlight LOW, screen cleared");
    }
}
