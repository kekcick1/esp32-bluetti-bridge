#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_wifi.h>

#include "bluetti_device.h"
#include "display_manager.h"
#include "mqtt_handler.h"
#include "secrets.h"
#include "system_status.h"
#include "web_server.h"

//------------------------------------------------------------------------------
// User configuration
//------------------------------------------------------------------------------
// Credentials are now in secrets.h

// Налаштування що можна змінювати через веб-інтерфейс
// За замовчуванням (якщо не збережено в Preferences)
static constexpr char DEFAULT_MQTT_SERVER[] = "192.168.1.100";
static constexpr char DEFAULT_BLUETTI_MAC[] = "D1:4C:11:6B:6A:3D";

// Глобальні змінні для конфігурації (можуть бути змінені через веб)
char mqttServer[64] = "";
char bluettiMac[18] = "";
char wifiSsid[64] = "";
char wifiPassword[64] = "";

//------------------------------------------------------------------------------
// Globals
//------------------------------------------------------------------------------
SystemStatus systemStatus;
BluettiDevice bluetti(&systemStatus);
DisplayManager display(&bluetti, &systemStatus);
MQTTHandler mqtt(&bluetti, &systemStatus);
WebServerManager webServer(&bluetti, &systemStatus);

unsigned long lastWiFiAttempt = 0;
unsigned long lastVoltageSample = 0;
unsigned long lastBluettiAttempt = 0;
bool otaReady = false;
bool webServerStarted = false;
bool apMode = false;
unsigned long apModeStartTime = 0;
unsigned long wifiConnectStartTime = 0;
const unsigned long WIFI_TIMEOUT_MS = 10 * 60 * 1000; // 10 хвилин
const unsigned long AP_TIMEOUT_MS = 10 * 60 * 1000;   // 10 хвилин

// Power Save - Smart polling інтервали
const unsigned long WIFI_RETRY_FAST_MS = 3000;         // Швидкі спроби - кожні 3 секунди
const unsigned long WIFI_RETRY_SLOW_MS = 5 * 60 * 1000; // Повільні спроби - кожні 5 хвилин
const unsigned long BLUETTI_RETRY_FAST_MS = 10000;      // Швидкі спроби - кожні 10 секунд  
const unsigned long BLUETTI_RETRY_SLOW_MS = 5 * 60 * 1000; // Повільні спроби - кожні 5 хвилин
const uint8_t WIFI_FAST_ATTEMPTS = 10;    // Кількість швидких спроб перед переходом на повільний режим
const uint8_t BLUETTI_FAST_ATTEMPTS = 6;  // Кількість швидких спроб перед переходом на повільний режим

uint8_t wifiFailedAttempts = 0;    // Лічильник невдалих спроб підключення WiFi
uint8_t bluettiFailedAttempts = 0; // Лічильник невдалих спроб підключення Bluetti
bool wifiSlowMode = false;         // Режим повільного опитування WiFi
bool bluettiSlowMode = false;      // Режим повільного опитування Bluetti

//------------------------------------------------------------------------------
// Читання напруги акумулятора/живлення ESP32
// Використовуємо GPIO 35 та дільник 2:1 (стандарт для LILYGO T-Display)
//------------------------------------------------------------------------------
float readEsp32Voltage() {
  static bool adcConfigured = false;
  if (!adcConfigured) {
    analogSetAttenuation(ADC_11db); // Дозволяє читати до 3.3V
    adcConfigured = true;
  }

  // Читаємо значення з GPIO 35 (батарея)
  uint32_t raw = analogRead(35);

  // Якщо значення 0, батарея не підключена або помилка
  if (raw == 0) {
    return 0.0f;
  }

  // Розрахунок напруги:
  // ADC (0-4095) -> 0-3.3V
  // Дільник 2:1 -> множимо на 2
  float voltage = (raw / 4095.0f) * 3.3f * 2.0f;

  // Корекція (можна підлаштувати, якщо потрібно)
  // voltage = voltage * 1.05;

  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 5000) {
    Serial.printf("[VOLTAGE] GPIO 35 Raw: %u, Voltage: %.2f V\n", raw, voltage);
    lastDebug = millis();
  }

  return voltage;
}

//------------------------------------------------------------------------------
// Розрахунок відсотка батареї на основі напруги
// Для літієвої батареї 3.7V:
// - Повна зарядка: ~4.2V (100%)
// - Нормальна робота: ~3.7V (50%)
// - Розряджена: ~3.0V (0%)
//------------------------------------------------------------------------------
uint8_t calculateBatteryPercent(float voltage) {
  // Для дуже низької напруги (< 0.3V) - не розраховуємо відсоток (можливо
  // помилка вимірювання)
  if (voltage < 0.3f)
    return 0;

  // Для нормальної батареї 3.7V LiPo:
  // - Розряджена: < 3.0V (0%)
  // - Нормальна: 3.0-4.2V (0-100%)
  // - Повна зарядка: >= 4.2V (100%)
  if (voltage < 3.0f)
    return 0; // Розряджена
  if (voltage >= 4.2f)
    return 100; // Повна зарядка

  // Лінійна інтерполяція між 3.0V (0%) та 4.2V (100%)
  float percent = ((voltage - 3.0f) / (4.2f - 3.0f)) * 100.0f;

  // Обмежуємо в межах 0-100%
  if (percent < 0)
    return 0;
  if (percent > 100)
    return 100;

  return (uint8_t)percent;
}

void startOTA() {
  if (!systemStatus.wifiConnected || otaReady) {
    return;
  }
  ArduinoOTA.begin(WiFi.localIP(), DEVICE_NAME, OTA_PASSWORD, InternalStorage);
  otaReady = true;
  Serial.println("OTA ready on port 3232");
}

void startAPMode() {
  if (apMode)
    return; // Вже в AP режимі

  Serial.println("Starting AP mode...");
  apMode = true;
  apModeStartTime = millis();

  // Зупиняємо спроби підключення до WiFi
  WiFi.disconnect();
  delay(100);

  WiFi.mode(WIFI_AP);
  bool apStarted = WiFi.softAP("esp32", "12345678");
  if (apStarted) {
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    Serial.printf(
        "AP started successfully! SSID=esp32, Password=12345678, IP=%s\n",
        WiFi.softAPIP().toString().c_str());
    display.showMessage("AP Mode", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("ERROR: Failed to start AP mode!");
    display.showError("AP Failed");
    apMode = false;
    return;
  }

  if (!webServerStarted) {
    webServer.begin();
    webServerStarted = true;
  }
}

void connectWiFi() {
  // Неблокуюче підключення WiFi
  static bool connecting = false;
  static unsigned long connectStart = 0;
  static uint8_t attempts = 0;

  if (apMode) {
    // Виходимо з AP режиму перед спробою підключення
    WiFi.mode(WIFI_STA);
    apMode = false;
    connecting = false;
    attempts = 0;
  }

  if (!connecting) {
    // Починаємо підключення
    lastWiFiAttempt = millis();
    wifiConnectStartTime = millis();
    connecting = true;
    connectStart = millis();
    const char *ssidToUse = (strlen(wifiSsid) > 0) ? wifiSsid : WIFI_SSID;
    const char *passwordToUse =
        (strlen(wifiPassword) > 0) ? wifiPassword : WIFI_PASSWORD;

    Serial.printf("Connecting to WiFi %s...\n", ssidToUse);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssidToUse, passwordToUse);
    return;
  }

  // Перевіряємо статус підключення (неблокуюче)
  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    connecting = false;
    systemStatus.wifiConnected = true;
    systemStatus.wifiIp = WiFi.localIP();
    
    // Скидаємо лічильник невдалих спроб та вимикаємо повільний режим
    wifiFailedAttempts = 0;
    wifiSlowMode = false;
    
    // Вмикаємо WiFi Power Save для економії енергії
    // WIFI_PS_MAX_MODEM - максимальна економія енергії (WiFi засинає між beacon'ами)
    // Bluetooth продовжує працювати навіть коли WiFi спить
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    Serial.println("[WiFi] Power Save enabled (MAX_MODEM)");
    
    display.showMessage("WiFi", WiFi.localIP().toString().c_str());
    Serial.printf("WiFi connected, IP: %s\n",
                  WiFi.localIP().toString().c_str());
    if (!webServerStarted) {
      webServer.begin();
      webServerStarted = true;
    }
    startOTA();
  } else if (millis() - connectStart > 20000) {
    // Таймаут 20 секунд (більше часу для підключення)
    connecting = false;
    systemStatus.wifiConnected = false;
    wifiFailedAttempts++; // Збільшуємо лічильник невдалих спроб
    
    Serial.printf("WiFi connection timeout (status: %d, attempt %d/%d)\n", 
                  status, wifiFailedAttempts, WIFI_FAST_ATTEMPTS);

    // Детальніше про помилки
    switch (status) {
    case WL_NO_SSID_AVAIL:
      Serial.println("  - SSID not found");
      break;
    case WL_CONNECT_FAILED:
      Serial.println("  - Connection failed (wrong password?)");
      break;
    case WL_CONNECTION_LOST:
      Serial.println("  - Connection lost");
      break;
    case WL_DISCONNECTED:
      Serial.println("  - Disconnected");
      break;
    default:
      Serial.printf("  - Unknown status: %d\n", status);
      break;
    }
    
    // Після декількох невдалих спроб переходимо на повільний режим
    if (wifiFailedAttempts >= WIFI_FAST_ATTEMPTS && !wifiSlowMode) {
      wifiSlowMode = true;
      Serial.printf("[WiFi] Switching to slow polling mode (every %lu minutes)\n", 
                    WIFI_RETRY_SLOW_MS / 60000);
    }
  }
}

void ensureWiFi() {
  if (apMode) {
    // Перевіряємо таймаут AP режиму (10 хвилин)
    if (millis() - apModeStartTime > AP_TIMEOUT_MS) {
      Serial.println("AP mode timeout, trying to connect to WiFi...");
      apMode = false;
      wifiConnectStartTime = millis();
      connectWiFi();
    }
    return;
  }

  if (systemStatus.wifiConnected && WiFi.status() != WL_CONNECTED) {
    systemStatus.wifiConnected = false;
    otaReady = false;
    wifiConnectStartTime = millis(); // Скидаємо таймер
    wifiFailedAttempts = 0; // Скидаємо лічильник при втраті з'єднання
    wifiSlowMode = false;   // Повертаємося до швидкого режиму
    
    // Вимикаємо WiFi Power Save при відключенні
    esp_wifi_set_ps(WIFI_PS_NONE);
    
    Serial.println("WiFi lost, retrying...");
  }

  if (!systemStatus.wifiConnected) {
    // ВАЖЛИВО: Перевіряємо, чи минуло 10 хвилин з початку спроби підключення
    // Але тільки якщо wifiConnectStartTime був встановлений (не 0)
    unsigned long timeSinceStart = (wifiConnectStartTime > 0) ? (millis() - wifiConnectStartTime) : 0;
    
    if (wifiConnectStartTime > 0 && timeSinceStart > WIFI_TIMEOUT_MS) {
      Serial.println(
          "WiFi connection timeout (10 minutes), starting AP mode...");
      startAPMode();
    } else {
      // Використовуємо smart polling - швидкий або повільний режим
      unsigned long retryInterval = wifiSlowMode ? WIFI_RETRY_SLOW_MS : WIFI_RETRY_FAST_MS;
      
      if (millis() - lastWiFiAttempt > retryInterval) {
        if (wifiSlowMode) {
          Serial.println("[WiFi] Slow mode: attempting reconnection (every 5 minutes)");
        }
        // Викликаємо connectWiFi() неблокуюче
        connectWiFi();
      }
    }
  }
}

void manageBluetti() {
  if (!systemStatus.bluettiEnabled) {
    if (bluetti.isConnected()) {
      bluetti.disconnect();
    }
    return;
  }

  // BLE підключення до Bluetti - ESP32 підключається напряму
  // Smart polling: швидкі спроби (10 сек), потім повільні (5 хвилин)
  if (!bluetti.isConnected()) {
    // Використовуємо smart polling - швидкий або повільний режим
    unsigned long retryInterval = bluettiSlowMode ? BLUETTI_RETRY_SLOW_MS : BLUETTI_RETRY_FAST_MS;
    
    if (millis() - lastBluettiAttempt > retryInterval) {
      if (strlen(bluettiMac) > 0) {
        if (bluettiSlowMode) {
          Serial.println("[Bluetti] Slow mode: attempting reconnection (every 5 minutes)");
        }
        
        // ВАЖЛИВО: Викликаємо display.loop() ПЕРЕД блокуючою операцією!
        display.loop();
        Serial.println("[Main] Attempting to connect to Bluetti...");
        
        bool connectStarted = bluetti.scanAndConnect(bluettiMac);
        
        // Якщо підключення не вдалось, збільшуємо лічильник
        if (!connectStarted && !bluetti.isConnected()) {
          bluettiFailedAttempts++;
          Serial.printf("[Bluetti] Connection failed (attempt %d/%d)\n", 
                        bluettiFailedAttempts, BLUETTI_FAST_ATTEMPTS);
          
          // Після декількох невдалих спроб переходимо на повільний режим
          if (bluettiFailedAttempts >= BLUETTI_FAST_ATTEMPTS && !bluettiSlowMode) {
            bluettiSlowMode = true;
            Serial.printf("[Bluetti] Switching to slow polling mode (every %lu minutes)\n", 
                          BLUETTI_RETRY_SLOW_MS / 60000);
            Serial.println("[Bluetti] 💡 This saves power when Bluetti is OFF");
          }
        }
        
        display.loop();
      } else {
        Serial.println("[Main] ⚠️  Bluetti MAC address not configured!");
      }
      lastBluettiAttempt = millis();
    }
  } else {
    // Bluetti підключений - скидаємо лічильники
    if (bluettiFailedAttempts > 0 || bluettiSlowMode) {
      bluettiFailedAttempts = 0;
      bluettiSlowMode = false;
      Serial.println("[Bluetti] Connected, reset to fast polling mode");
    }
  }
  
  // Також обробляємо сканування, якщо воно активне
  if (!bluetti.isConnected() && millis() - lastBluettiAttempt < 15000) {
    if (strlen(bluettiMac) > 0) {
      bluetti.scanAndConnect(bluettiMac);
    }
  }

  bluetti.loop(); // Обробка BLE підключення та отримання даних
  display.loop(); // Після bluetti.loop()
}

void loadConfig() {
  Preferences prefs;
  prefs.begin("config", true); // read-only

  // Завантажуємо MQTT сервер
  String savedMqtt = prefs.getString("mqtt_server", "");
  if (savedMqtt.length() > 0) {
    savedMqtt.toCharArray(mqttServer, sizeof(mqttServer));
    Serial.printf("Loaded MQTT server from Preferences: %s\n", mqttServer);
  } else {
    strncpy(mqttServer, DEFAULT_MQTT_SERVER, sizeof(mqttServer) - 1);
    Serial.printf("Using default MQTT server: %s\n", mqttServer);
  }

  // Завантажуємо Bluetti MAC
  String savedMac = prefs.getString("bluetti_mac", "");
  if (savedMac.length() > 0) {
    savedMac.toCharArray(bluettiMac, sizeof(bluettiMac));
    Serial.printf("Loaded Bluetti MAC from Preferences: %s\n", bluettiMac);
  } else {
    strncpy(bluettiMac, DEFAULT_BLUETTI_MAC, sizeof(bluettiMac) - 1);
    Serial.printf("Using default Bluetti MAC: %s\n", bluettiMac);
  }

  // Завантажуємо WiFi SSID
  String savedSsid = prefs.getString("wifi_ssid", "");
  if (savedSsid.length() > 0) {
    savedSsid.toCharArray(wifiSsid, sizeof(wifiSsid));
    Serial.printf("Loaded WiFi SSID from Preferences: %s\n", wifiSsid);
  } else {
    strncpy(wifiSsid, WIFI_SSID, sizeof(wifiSsid) - 1);
    Serial.printf("Using default WiFi SSID: %s\n", wifiSsid);
  }

  // Завантажуємо WiFi пароль
  String savedPassword = prefs.getString("wifi_password", "");
  if (savedPassword.length() > 0) {
    savedPassword.toCharArray(wifiPassword, sizeof(wifiPassword));
    Serial.println("Loaded WiFi password from Preferences");
  } else {
    strncpy(wifiPassword, WIFI_PASSWORD, sizeof(wifiPassword) - 1);
    Serial.println("Using default WiFi password");
  }
  
  // Завантажуємо інтервал опитування Bluetti
  unsigned long savedInterval = prefs.getULong("update_interval", 20000); // За замовчуванням 20 секунд
  bluetti.setUpdateInterval(savedInterval);
  Serial.printf("Loaded Bluetti update interval: %lu ms (%lu seconds)\n", savedInterval, savedInterval/1000);

  prefs.end();
}

// Функція для скидання налаштувань WiFi
void resetWiFiConfig() {
  Preferences prefs;
  prefs.begin("config", false); // read-write
  
  Serial.println("\n=== Resetting WiFi Configuration ===");
  prefs.remove("wifi_ssid");
  prefs.remove("wifi_password");
  prefs.end();
  
  // Очищаємо глобальні змінні
  wifiSsid[0] = '\0';
  wifiPassword[0] = '\0';
  
  Serial.println("WiFi configuration cleared!");
  Serial.println("Will use default values from secrets.h on next restart");
  Serial.println("Restarting in 2 seconds...");
  delay(2000);
  ESP.restart();
}

// Функція для обробки Serial команд
void handleSerialCommands() {
  static String serialBuffer = "";
  
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        serialBuffer.trim();
        serialBuffer.toLowerCase();
        
        if (serialBuffer == "resetwifi" || serialBuffer == "clearwifi") {
          resetWiFiConfig();
        } else if (serialBuffer == "resetconfig" || serialBuffer == "clearconfig") {
          // Скидаємо всі налаштування
          Preferences prefs;
          prefs.begin("config", false);
          prefs.clear();
          prefs.end();
          Serial.println("\n=== All Configuration Cleared ===");
          Serial.println("Restarting in 2 seconds...");
          delay(2000);
          ESP.restart();
        } else if (serialBuffer == "ap" || serialBuffer == "startap") {
          // Примусово запускаємо AP режим
          Serial.println("\n=== Forcing AP Mode ===");
          startAPMode();
        } else if (serialBuffer == "status" || serialBuffer == "info") {
          // Показуємо поточний статус
          Serial.println("\n=== Current Status ===");
          Serial.printf("WiFi SSID: %s\n", strlen(wifiSsid) > 0 ? wifiSsid : WIFI_SSID);
          Serial.printf("WiFi Connected: %s\n", systemStatus.wifiConnected ? "Yes" : "No");
          Serial.printf("AP Mode: %s\n", apMode ? "Yes" : "No");
          if (systemStatus.wifiConnected) {
            Serial.printf("IP Address: %s\n", systemStatus.wifiIp.toString().c_str());
          } else if (apMode) {
            Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
          }
          Serial.printf("Uptime: %lu seconds\n", systemStatus.uptime);
        } else if (serialBuffer == "help") {
          Serial.println("\n=== Available Commands ===");
          Serial.println("resetwifi    - Clear WiFi SSID and password");
          Serial.println("resetconfig  - Clear all saved configuration");
          Serial.println("ap           - Force start AP mode");
          Serial.println("status       - Show current status");
          Serial.println("help         - Show this help");
        } else {
          Serial.printf("Unknown command: %s\n", serialBuffer.c_str());
          Serial.println("Type 'help' for available commands");
        }
        
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ESP32 Bluetti Bridge ===");
  Serial.println("=== Power Save Mode Enabled ===");

  analogReadResolution(12);
  systemStatus.esp32Voltage = readEsp32Voltage();
  systemStatus.esp32BatteryPercent =
      calculateBatteryPercent(systemStatus.esp32Voltage);

  display.begin();
  display.showConnecting("Booting...");

  // Завантажуємо конфігурацію з Preferences
  loadConfig();

  // Ініціалізуємо BLE для прямого підключення до Bluetti
  bluetti.begin();
  
  // Налаштовуємо MQTT з username та password
  mqtt.configure(mqttServer, MQTT_PORT, MQTT_USER, MQTT_PASSWORD);

  // Ініціалізуємо WiFi Power Save
  // ПРИМІТКА: WiFi Power Save буде увімкнено автоматично при підключенні
  // CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE вже встановлено в platformio.ini
  Serial.println("[WiFi] Power Save configuration:");
  Serial.println("  - Disconnected PM: ENABLED (from platformio.ini)");
  Serial.println("  - Connected PS: WIFI_PS_MAX_MODEM (will be set on connection)");
  Serial.println("  - BLE Coexistence: ENABLED");
  Serial.println("[WiFi] Smart polling:");
  Serial.printf("  - Fast retry: every %lu seconds\n", WIFI_RETRY_FAST_MS / 1000);
  Serial.printf("  - Slow retry: every %lu minutes (after %d failed attempts)\n", 
                WIFI_RETRY_SLOW_MS / 60000, WIFI_FAST_ATTEMPTS);
  Serial.println("[Bluetti] Smart polling:");
  Serial.printf("  - Fast retry: every %lu seconds\n", BLUETTI_RETRY_FAST_MS / 1000);
  Serial.printf("  - Slow retry: every %lu minutes (after %d failed attempts)\n", 
                BLUETTI_RETRY_SLOW_MS / 60000, BLUETTI_FAST_ATTEMPTS);

  wifiConnectStartTime = millis();
  connectWiFi();
}

void loop() {
  // ВАЖЛИВО: Обробляємо дисплей та кнопки ЗАВЖДИ ПЕРШИМ!
  display.loop();

  // Обробляємо Serial команди (неблокуюче)
  handleSerialCommands();

  // Обробляємо веб-сервер (неблокуючий)
  webServer.handleClient();

  // ВАЖЛИВО: Викликаємо display.loop() ПІСЛЯ веб-сервера для швидкої реакції
  // кнопок!
  display.loop();

  // Неблокуюче управління WiFi
  ensureWiFi();
  display.loop(); // Після WiFi

  if (systemStatus.wifiConnected) {
    if (!otaReady) {
      startOTA();
    }
    if (otaReady) {
      ArduinoOTA.handle();
    }
    display.loop(); // Після OTA
  }

  // MQTT та Bluetti - обробляємо неблокуюче
  // ВАЖЛИВО: Викликаємо display.loop() ПЕРЕД та ПІСЛЯ блокуючих операцій!
  // ПРИМІТКА: mqtt.loop() може блокувати на 1 секунду під час connect(),
  // але це мінімальний таймаут, який можна встановити для WiFiClient
  display.loop();
  mqtt.loop(systemStatus.wifiConnected);
  display.loop(); // Після MQTT - обробляємо кнопки одразу після блокування

  manageBluetti();
  display.loop(); // Після Bluetti

  // Дозволяємо іншим задачам виконуватися
  yield();
  display.loop(); // Після yield()

  if (millis() - lastVoltageSample > 5000) {
    float currentVoltage = readEsp32Voltage();
    systemStatus.esp32Voltage = currentVoltage;

    // Визначаємо, чи підключений USB
    // USB зазвичай дає стабільну високу напругу (4.2V+)
    // Батарея зазвичай має напругу 3.0-4.2V
    static float lastVoltage = 0.0f;
    static unsigned long lastVoltageChange = 0;
    static bool wasUsbPowered = false;
    static unsigned long usbDetectionTime = 0;

    // Якщо напруга >= 4.2V, це може бути USB
    // Батарея рідко досягає 4.2V+, особливо стабільно
    bool isUsbPowered = false;
    if (currentVoltage >= 4.2f) {
      // Висока напруга - ймовірно USB
      if (abs(currentVoltage - lastVoltage) < 0.1f) {
        // Напруга стабільна - це USB
        isUsbPowered = true;
        if (!wasUsbPowered) {
          usbDetectionTime = millis();
          Serial.printf("[VOLTAGE] USB detected: %.2f V (was: %.2f V)\n",
                        currentVoltage, lastVoltage);
        }
      } else if (wasUsbPowered) {
        // Була USB, напруга все ще висока - продовжуємо вважати USB
        isUsbPowered = true;
        if (currentVoltage < 4.1f) {
          // Напруга впала - USB відключено
          isUsbPowered = false;
          Serial.printf("[VOLTAGE] USB disconnected: %.2f V\n", currentVoltage);
        }
      } else if (currentVoltage >= 4.3f) {
        // Дуже висока напруга (>= 4.3V) - точно USB
        isUsbPowered = true;
        usbDetectionTime = millis();
        Serial.printf("[VOLTAGE] USB detected (high voltage): %.2f V\n",
                      currentVoltage);
      }
    } else {
      // Напруга низька - точно не USB
      isUsbPowered = false;
      if (wasUsbPowered) {
        Serial.printf("[VOLTAGE] USB disconnected: %.2f V\n", currentVoltage);
      }
    }

    // Зберігаємо напругу батареї ПЕРЕД підключенням USB
    if (!isUsbPowered && currentVoltage > 0.1f && currentVoltage < 4.2f) {
      // Це точно батарея (не USB) - зберігаємо
      systemStatus.esp32BatteryVoltage = currentVoltage;
    } else if (!wasUsbPowered && isUsbPowered && lastVoltage > 0.1f &&
               lastVoltage < 4.2f) {
      // USB щойно підключився - зберігаємо останню напругу батареї
      systemStatus.esp32BatteryVoltage = lastVoltage;
      Serial.printf("[VOLTAGE] Saved battery voltage before USB: %.2f V\n",
                    lastVoltage);
    }

    systemStatus.esp32UsbPowered = isUsbPowered;

    // Для відображення використовуємо напругу батареї, якщо USB підключений
    float displayVoltage =
        isUsbPowered ? systemStatus.esp32BatteryVoltage : currentVoltage;
    systemStatus.esp32BatteryPercent = calculateBatteryPercent(displayVoltage);

    // Діагностика
    if (isUsbPowered && systemStatus.esp32BatteryVoltage > 0.1f) {
      static unsigned long lastUsbLog = 0;
      if (millis() - lastUsbLog > 10000) {
        Serial.printf("[VOLTAGE] USB: %.2f V, Battery: %.2f V (%u%%)\n",
                      currentVoltage, systemStatus.esp32BatteryVoltage,
                      systemStatus.esp32BatteryPercent);
        lastUsbLog = millis();
      }
    }

    lastVoltage = currentVoltage;
    lastVoltageChange = millis();
    wasUsbPowered = isUsbPowered;

    lastVoltageSample = millis();
  }

  // Оновлення uptime та RSSI
  systemStatus.uptime = millis() / 1000;
  if (systemStatus.wifiConnected) {
    systemStatus.wifiRssi = WiFi.RSSI();
  }

  // Невелика затримка для стабільності
  delay(10);
}
