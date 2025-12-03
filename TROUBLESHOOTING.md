# 🐛 Налагодження та діагностика

## 📋 Загальні рекомендації

### Увімкнути детальні логи

В `platformio.ini` вже налаштовано:
```ini
build_flags = 
    -D CORE_DEBUG_LEVEL=3  # Детальні логи
```

Рівні логування:
- `0` - None
- `1` - Error
- `2` - Warning  
- `3` - Info (поточний)
- `4` - Debug
- `5` - Verbose

---

## 🔍 Діагностичні команди

### Перевірити з'єднання

```bash
# Подивитись порт ESP32
pio device list

# Відкрити монітор
pio device monitor

# Очистити та пересібрати
pio run --target clean
pio run

# Завантажити з логами
pio run --target upload && pio device monitor
```

### Перевірити MQTT в Home Assistant

**Developer Tools → MQTT → Listen to topic:**
```
homeassistant/bluetti/eb3a/#
```

Має з'являтись кожні 5 секунд нові повідомлення.

---

## ❌ Проблема: ESP32 не підключається до Bluetti

### Симптоми:
```
Scanning for Bluetti device...
Bluetti device not found!
```

### Рішення:

#### 1. Перевірити що аддон HA вимкнено
```bash
# В Home Assistant перевірте що аддон зупинено
Settings → Add-ons → Bluetti to MQTT → STOP
```

#### 2. Перезавантажити Bluetti
- Вимкніть EB3A
- Почекайте 10 секунд
- Увімкніть знову

#### 3. Перевірити MAC адресу

Використайте BLE сканер на смартфоні (nRF Connect):
1. Встановіть nRF Connect (iOS/Android)
2. Скануйте пристрої
3. Знайдіть "EB3A" або "BLUETTI"
4. Запишіть MAC адресу
5. Оновіть в `main.cpp`

#### 4. Спробувати підключення по імені

В `main.cpp` змініть:
```cpp
const bool USE_MAC_ADDRESS = false;  // Спробувати по імені
const char* BLUETTI_NAME = "EB3A";   // Або "BLUETTI EB3A"
```

#### 5. Збільшити час сканування

В `bluetti_device.cpp` змініть:
```cpp
// Було:
NimBLEScanResults results = pScan->start(10);

// Зробіть:
NimBLEScanResults results = pScan->start(20); // 20 секунд
```

---

## ❌ Проблема: Не підключається до WiFi

### Симптоми:
```
Connecting to WiFi...........
WiFi connection failed!
```

### Рішення:

#### 1. Перевірити SSID та пароль
```cpp
// Переконайтесь що немає пробілів
const char* WIFI_SSID = "MyWiFi";      // ✓
const char* WIFI_SSID = " MyWiFi ";    // ✗
```

#### 2. Перевірити частоту WiFi
ESP32 підтримує лише **2.4GHz**, не 5GHz!

#### 3. Додати діагностику

В `mqtt_handler.cpp` додайте логи:
```cpp
Serial.printf("Connecting to: %s\n", ssid);
Serial.printf("Signal strength: %d dBm\n", WiFi.RSSI());
```

#### 4. Спробувати статичний IP

```cpp
// В mqtt_handler.cpp, функція connectWiFi():
IPAddress local_IP(192, 168, 1, 200);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

WiFi.config(local_IP, gateway, subnet);
WiFi.begin(ssid, password);
```

---

## ❌ Проблема: Не підключається до MQTT

### Симптоми:
```
MQTT connection failed, rc=-2
```

### Коди помилок MQTT:

| Код | Значення | Рішення |
|-----|----------|---------|
| -4 | Connection timeout | Перевірити IP та порт |
| -3 | Connection lost | Нестабільна мережа |
| -2 | Connect failed | MQTT брокер не відповідає |
| 1 | Bad protocol | Версія протоколу |
| 2 | Bad client ID | Змінити clientId |
| 4 | Bad credentials | Неправильний логін/пароль |
| 5 | Unauthorized | Немає доступу |

### Рішення:

#### 1. Перевірити IP адресу Home Assistant
```bash
# В Home Assistant:
Settings → System → Network
# Запишіть IP адресу
```

#### 2. Перевірити Mosquitto
```bash
# В Home Assistant:
Settings → Add-ons → Mosquitto broker
# Переконайтесь що Status: Started
```

#### 3. Спробувати без авторизації
```cpp
const char* MQTT_USER = "";      // Порожнє
const char* MQTT_PASSWORD = "";  // Порожнє
```

#### 4. Тест з'єднання з комп'ютера
```bash
# Встановіть mosquitto-clients
sudo apt install mosquitto-clients

# Тест підключення
mosquitto_sub -h 192.168.1.XXX -p 1883 -t "test" -v
```

#### 5. Створити MQTT користувача

В Home Assistant:
```bash
# Settings → Add-ons → Mosquitto broker → Configuration

# Додайте:
logins:
  - username: esp32
    password: esp32password
```

---

## ❌ Проблема: Home Assistant не бачить пристрої

### Симптоми:
- MQTT працює
- ESP32 публікує дані
- Але в HA немає пристроїв

### Рішення:

#### 1. Перевірити MQTT Discovery

```yaml
# configuration.yaml
mqtt:
  discovery: true  # Має бути true
  discovery_prefix: homeassistant  # За замовчуванням
```

#### 2. Подивитись топіки вручну

Developer Tools → MQTT:
```
homeassistant/sensor/bluetti_eb3a/+/config
homeassistant/switch/bluetti_eb3a/+/config
```

Якщо топіків немає - ESP32 не публікує Discovery.

#### 3. Перезапустити ESP32

```bash
# Через Serial Monitor відправте:
Ctrl + T → r  (для перезавантаження)

# Або просто:
pio run --target upload
```

#### 4. Видалити старі entity

Якщо були старі з аддону:
```
Settings → Devices & Services → MQTT → 
Знайти "Bluetti EB3A" → Delete
```

Потім перезапустити ESP32.

#### 5. Форсувати Discovery

Додайте в `mqtt_handler.cpp`:
```cpp
void MQTTHandler::update() {
    if (!mqttClient.connected()) {
        if (connectMQTT()) {
            publishDiscovery();  // Публікувати при кожному підключенні
        }
    }
    // ...
}
```

---

## ❌ Проблема: Команди не працюють

### Симптоми:
- Можна бачити дані
- Але AC/DC не вмикаються/вимикаються

### Рішення:

#### 1. Перевірити що ESP32 отримує команди

Додайте логи в `mqtt_handler.cpp`:
```cpp
void MQTTHandler::callback(char* topic, byte* payload, unsigned int length) {
    Serial.printf("📥 MQTT Command: %s = %.*s\n", topic, length, payload);
    
    // ... решта коду
}
```

#### 2. Перевірити формат команд Bluetti

Протокол Bluetti може відрізнятись. Спробуйте:

```cpp
// В bluetti_device.cpp
bool BluettiDevice::setACOutput(bool state) {
    // Варіант 1 (поточний):
    uint8_t cmd[] = {0x01, 0x03, 0x00, CMD_AC_OUTPUT, 0x00, 0x01, state ? 0x01 : 0x00};
    
    // Варіант 2:
    uint8_t cmd[] = {0x01, 0x06, 0x00, 0x0D, state ? 0x01 : 0x00, 0x00};
    
    // Варіант 3:
    uint8_t cmd[] = {0x55, 0xAA, 0x02, state ? 0x01 : 0x00, 0xFF};
    
    return sendCommand(cmd, sizeof(cmd));
}
```

#### 3. Аналіз протоколу з аддону

Подивіться логи аддону "Bluetti to MQTT" щоб зрозуміти формат команд:

```bash
# В Home Assistant:
Settings → Add-ons → Bluetti to MQTT → Logs
# Виконайте команду ON/OFF та подивіться що відправляється
```

---

## 🔬 Додаткова діагностика

### Перевірити пам'ять ESP32

Додайте в `main.cpp`:
```cpp
void loop() {
    // В кінці циклу:
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    
    // ...
}
```

### Перевірити BLE характеристики

```cpp
// В bluetti_device.cpp після підключення:
Serial.printf("Service UUID: %s\n", pService->getUUID().toString().c_str());
Serial.printf("Notify UUID: %s\n", pNotifyCharacteristic->getUUID().toString().c_str());
Serial.printf("Write UUID: %s\n", pWriteCharacteristic->getUUID().toString().c_str());
```

### Дамп BLE пакетів

```cpp
void BluettiDevice::parseNotification(uint8_t* data, size_t length) {
    // Додайте на початок:
    Serial.print("BLE RX [");
    Serial.print(length);
    Serial.print("]: ");
    for (size_t i = 0; i < length; i++) {
        Serial.printf("%02X ", data[i]);
    }
    Serial.println();
    
    // ... решта парсингу
}
```

---

## 📊 Корисні тестові команди

### Тест MQTT публікації вручну

```bash
# Відправити команду з комп'ютера:
mosquitto_pub -h 192.168.1.XXX -t "homeassistant/bluetti/eb3a/ac_output/set" -m "ON"
```

### Тест BLE з'єднання

Використайте `bluetoothctl` на Linux:
```bash
bluetoothctl
scan on
# Знайдіть D1:4C:11:6B:6A:3D
connect D1:4C:11:6B:6A:3D
```

---

## 📝 Чеклист перед зверненням за допомогою

- [ ] Перевірили що аддон HA вимкнено
- [ ] Перезавантажили Bluetti EB3A
- [ ] Перезавантажили ESP32
- [ ] Перевірили MAC адресу
- [ ] Перевірили WiFi SSID та пароль
- [ ] Перевірили IP MQTT сервера
- [ ] Подивились логи через Serial Monitor
- [ ] Перевірили MQTT Discovery в HA
- [ ] Спробували підключення по імені замість MAC

---

## 🆘 Де отримати допомогу

1. **GitHub Issues** - створіть issue з логами
2. **Home Assistant Community** - форум
3. **Bluetti Discord** - спільнота користувачів
4. **Проекти-аналоги:**
   - https://github.com/warhammerkid/bluetti_mqtt
   - https://github.com/Patrick762/hassio-bluetti-bt

---

Успішного налагодження! 🔧
