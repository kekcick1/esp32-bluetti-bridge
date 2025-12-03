# Архітектура системи

## 📊 Схема підключення

```
┌─────────────────────────────────────────────────────────────┐
│                     BLUETTI EB3A                            │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Bluetooth Low Energy (BLE)                          │   │
│  │  MAC: D1:4C:11:6B:6A:3D                             │   │
│  │  Service UUID: 0000ff00-...-00805f9b34fb            │   │
│  └──────────────────────────────────────────────────────┘   │
└───────────────────────────┬─────────────────────────────────┘
                           │ BLE
                           │ (Bluetooth)
                           │
            ┌──────────────▼──────────────┐
            │   LILYGO T-Display ESP32    │
            │   ┌─────────────────────┐   │
            │   │  ESP32-S3 Chip      │   │
            │   │  - NimBLE Stack     │   │
            │   │  - WiFi Client      │   │
            │   │  - MQTT Publisher   │   │
            │   └─────────────────────┘   │
            │   ┌─────────────────────┐   │
            │   │  ST7789 Display     │   │
            │   │  135x240 pixels     │   │
            │   │  Shows:             │   │
            │   │  - Battery %        │   │
            │   │  - AC/DC Status     │   │
            │   │  - Power (W)        │   │
            │   └─────────────────────┘   │
            └──────────────┬──────────────┘
                           │ WiFi
                           │ 2.4GHz
                           │
            ┌──────────────▼──────────────┐
            │      WiFi Router            │
            │      (Home Network)         │
            └──────────────┬──────────────┘
                           │
                           │
            ┌──────────────▼──────────────┐
            │    HOME ASSISTANT           │
            │  ┌─────────────────────┐    │
            │  │ Mosquitto MQTT      │    │
            │  │ Broker              │    │
            │  │ Port: 1883          │    │
            │  └──────────┬──────────┘    │
            │             │                │
            │  ┌──────────▼──────────┐    │
            │  │ MQTT Integration    │    │
            │  │ (Auto Discovery)    │    │
            │  └──────────┬──────────┘    │
            │             │                │
            │  ┌──────────▼──────────┐    │
            │  │ Entities:           │    │
            │  │ - sensor.battery    │    │
            │  │ - switch.ac_output  │    │
            │  │ - switch.dc_output  │    │
            │  │ - sensor.ac_power   │    │
            │  └─────────────────────┘    │
            └─────────────────────────────┘
                           │
                           │
            ┌──────────────▼──────────────┐
            │    Automations              │
            │    Dashboards               │
            │    Voice Control            │
            │    Mobile Apps              │
            └─────────────────────────────┘
```

---

## 🔄 Потік даних

### 1. Читання даних з Bluetti

```
ESP32 → [BLE Request] → Bluetti EB3A
ESP32 ← [BLE Response: Battery=85%, AC=ON, Power=120W] ← Bluetti EB3A
```

**Частота:** Кожні 10 секунд

### 2. Публікація в MQTT

```
ESP32 → [MQTT Publish] → Home Assistant MQTT Broker

Topics:
  homeassistant/bluetti/eb3a/state         → {"battery_level":85, "ac_output_power":120, ...}
  homeassistant/bluetti/eb3a/battery       → "85"
  homeassistant/bluetti/eb3a/ac_power      → "120"
  homeassistant/bluetti/eb3a/ac_output/state → "ON"
```

**Частота:** Кожні 5 секунд

### 3. Команди від Home Assistant

```
User → Home Assistant → [MQTT Command] → ESP32 → [BLE Command] → Bluetti EB3A

Topic: homeassistant/bluetti/eb3a/ac_output/set
Payload: "ON" або "OFF"
```

---

## 📡 MQTT Topics структура

### State Topics (ESP32 публікує)

| Topic | Payload | Опис |
|-------|---------|------|
| `homeassistant/bluetti/eb3a/state` | JSON | Повний стан пристрою |
| `homeassistant/bluetti/eb3a/battery` | "85" | Рівень батареї (%) |
| `homeassistant/bluetti/eb3a/ac_power` | "120" | AC потужність (W) |
| `homeassistant/bluetti/eb3a/dc_power` | "45" | DC потужність (W) |
| `homeassistant/bluetti/eb3a/ac_output/state` | "ON"/"OFF" | Стан AC виходу |
| `homeassistant/bluetti/eb3a/dc_output/state` | "ON"/"OFF" | Стан DC виходу |

### Command Topics (ESP32 слухає)

| Topic | Payload | Дія |
|-------|---------|-----|
| `homeassistant/bluetti/eb3a/ac_output/set` | "ON" | Увімкнути AC вихід |
| `homeassistant/bluetti/eb3a/ac_output/set` | "OFF" | Вимкнути AC вихід |
| `homeassistant/bluetti/eb3a/dc_output/set` | "ON" | Увімкнути DC вихід |
| `homeassistant/bluetti/eb3a/dc_output/set` | "OFF" | Вимкнути DC вихід |

### Discovery Topics (ESP32 публікує один раз)

| Topic | Опис |
|-------|------|
| `homeassistant/sensor/bluetti_eb3a/battery/config` | Конфігурація сенсора батареї |
| `homeassistant/switch/bluetti_eb3a/ac_output/config` | Конфігурація перемикача AC |
| `homeassistant/switch/bluetti_eb3a/dc_output/config` | Конфігурація перемикача DC |
| `homeassistant/sensor/bluetti_eb3a/ac_power/config` | Конфігурація сенсора потужності |

---

## 🧩 Компоненти коду

### Модуль: `bluetti_device.cpp`

**Відповідальність:**
- Підключення до Bluetti через BLE
- Відправка команд (AC/DC ON/OFF)
- Отримання даних (батарея, потужність)
- Парсинг BLE відповідей

**Ключові функції:**
```cpp
bool connect(const char* deviceName);
bool connectByMAC(const char* macAddress);
bool setACOutput(bool state);
bool setDCOutput(bool state);
int getBatteryLevel();
```

### Модуль: `mqtt_handler.cpp`

**Відповідальність:**
- Підключення до WiFi
- Підключення до MQTT брокера
- Публікація статусу
- Підписка на команди
- MQTT Discovery для Home Assistant

**Ключові функції:**
```cpp
bool begin(const char* ssid, const char* password, const char* mqttServer);
void publishStatus();
void publishDiscovery();
void callback(char* topic, byte* payload, unsigned int length);
```

### Модуль: `display_manager.cpp`

**Відповідальність:**
- Ініціалізація TFT дисплею
- Відображення статусу Bluetti
- Повідомлення про підключення/помилки
- Графічні індикатори (батарея, потужність)

**Ключові функції:**
```cpp
void begin();
void showStatus();
void showConnecting(const char* message);
void showError(const char* message);
```

### Головний файл: `main.cpp`

**Відповідальність:**
- Ініціалізація всіх модулів
- Головний цикл програми
- Обробка переподключень
- Періодичне оновлення даних

---

## ⚡ Робочий цикл (Loop)

```
┌─────────────────────────────────────────┐
│         Початок циклу (100ms)           │
└────────────────┬────────────────────────┘
                 │
                 ▼
┌────────────────────────────────────────┐
│  mqttHandler->update()                 │
│  - Перевірка з'єднання MQTT            │
│  - Обробка вхідних команд              │
│  - Публікація статусу (кожні 5 сек)    │
└────────────────┬───────────────────────┘
                 │
                 ▼
┌────────────────────────────────────────┐
│  Bluetti оновлення (кожні 10 сек)      │
│  - bluetti.update()                    │
│  - Запит даних з EB3A                  │
│  - Парсинг відповіді                   │
└────────────────┬───────────────────────┘
                 │
                 ▼
┌────────────────────────────────────────┐
│  display->update()                     │
│  - Оновлення TFT дисплею               │
│  - Відображення даних                  │
└────────────────┬───────────────────────┘
                 │
                 ▼
┌────────────────────────────────────────┐
│  delay(100ms)                          │
└────────────────┬───────────────────────┘
                 │
                 └───────► (повторити)
```

---

## 🔐 Безпека

### WiFi
- WPA2 шифрування
- Паролі зберігаються в коді (можна винести в EEPROM)

### MQTT
- Підтримка авторизації (username/password)
- Можна додати TLS/SSL (потребує модифікації)

### Bluetooth
- BLE з'єднання зашифровано
- Унікальна MAC адреса для підключення

---

## 📈 Продуктивність

| Параметр | Значення |
|----------|----------|
| Споживання ESP32 | ~80mA (WiFi) / ~120mA (WiFi + BLE) |
| Оновлення даних | Кожні 10 секунд |
| Публікація MQTT | Кожні 5 секунд |
| Оновлення дисплею | Постійно (100ms) |
| Затримка команди | <1 секунда |

---

## 🔧 Налаштування під інші моделі Bluetti

Для адаптації під інші моделі (AC200P, AC300, EB70):

1. **Знайти UUID сервісів** (через BLE сканер)
2. **Змінити в** `bluetti_device.h`:
   ```cpp
   #define BLUETTI_SERVICE_UUID "ваш_uuid"
   ```
3. **Налаштувати протокол** в `parseNotification()`
4. **Тестувати команди** через Serial Monitor

---

Ця архітектура забезпечує надійний, швидкий та гнучкий міст між Bluetti та Home Assistant! 🚀
