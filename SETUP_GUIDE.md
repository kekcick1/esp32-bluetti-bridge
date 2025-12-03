# Покрокова інструкція налаштування

## Крок 1: Вимкнути Bluetti to MQTT аддон в Home Assistant

Оскільки ESP32 буде безпосередньо підключатись до Bluetti EB3A через Bluetooth, потрібно **вимкнути** аддон Home Assistant, щоб уникнути конфлікту підключень:

1. Відкрийте Home Assistant
2. Перейдіть в **Settings** → **Add-ons**
3. Знайдіть **Bluetti to MQTT**
4. Натисніть **Stop** (зупинити)
5. Вимкніть **Start on boot** (автозапуск)

⚠️ **ВАЖЛИВО:** Два пристрої не можуть одночасно підключатись до Bluetti через Bluetooth!

---

## Крок 2: Налаштувати прошивку ESP32

Відредагуйте файл `src/main.cpp`:

```cpp
// WiFi credentials
const char* WIFI_SSID = "ВАШ_WIFI_SSID";
const char* WIFI_PASSWORD = "ВАШ_WIFI_ПАРОЛЬ";

// MQTT settings (IP адреса вашого Home Assistant)
const char* MQTT_SERVER = "192.168.1.XXX";  // IP Home Assistant
const int MQTT_PORT = 1883;

// Якщо у вас налаштована авторизація MQTT:
const char* MQTT_USER = "ваш_mqtt_user";
const char* MQTT_PASSWORD = "ваш_mqtt_пароль";

// Bluetti MAC адреса (ваша адреса)
const char* BLUETTI_MAC = "D1:4C:11:6B:6A:3D";
const bool USE_MAC_ADDRESS = true;  // Використовувати MAC
```

---

## Крок 3: Завантажити прошивку

```bash
cd ~/Documents/esp32
pio run --target upload
```

---

## Крок 4: Переконатись що MQTT працює в Home Assistant

### Перевірити MQTT брокер:

1. **Settings** → **Add-ons** → **Mosquitto broker**
2. Переконайтесь що він запущений
3. Якщо потрібна авторизація, створіть користувача:
   - **Settings** → **People** → **Users** → **Add User**
   - Або використайте існуючого

### Налаштувати MQTT Integration:

1. **Settings** → **Devices & Services**
2. Якщо **MQTT** не додано, натисніть **Add Integration** → **MQTT**
3. Broker: `localhost` або `core-mosquitto`
4. Port: `1883`

---

## Крок 5: Перевірити роботу

### Через Serial Monitor:

```bash
pio device monitor
```

Ви маєте побачити:
```
=== Bluetti EB3A Bridge ===
Initializing Bluetti BLE...
Connecting to Bluetti by MAC: D1:4C:11:6B:6A:3D
Connected to Bluetti device by MAC!
Connecting to WiFi...
WiFi connected!
IP address: 192.168.1.XXX
Connecting to MQTT...
MQTT connected!
Published state to MQTT: SUCCESS
```

### Через дисплей ESP32:

Має відображатись:
- Рівень батареї
- Стан AC/DC виходів
- Потужність

### В Home Assistant:

Автоматично з'являться пристрої:
- `sensor.bluetti_eb3a_battery`
- `switch.bluetti_eb3a_ac_output`
- `switch.bluetti_eb3a_dc_output`
- `sensor.bluetti_eb3a_ac_power`

---

## Переваги ESP32 рішення

| Параметр | Home Assistant Addon | ESP32 Bridge |
|----------|---------------------|--------------|
| Навантаження на HA | Високе | Немає |
| Відстань до Bluetti | Обмежена розташуванням HA | ESP32 можна розмістити поруч |
| Візуальний статус | Немає | Дисплей 1.14" |
| Автономність | Потребує HA | Працює незалежно |
| Швидкість відгуку | Залежить від HA | Швидка |

---

## Усунення проблем

### ESP32 не підключається до Bluetti

1. Перевірте що аддон Home Assistant **ВИМКНЕНО**
2. Перезавантажте Bluetti EB3A
3. Перевірте MAC адресу в коді
4. Спробуйте встановити `USE_MAC_ADDRESS = false` та використовувати ім'я

### Не працює MQTT

1. Перевірте IP адресу MQTT сервера
2. Перевірте чи працює Mosquitto в Home Assistant
3. Перевірте логи: `pio device monitor`
4. Спробуйте без авторизації (залиште `MQTT_USER` порожнім)

### Home Assistant не бачить пристрої

1. Переконайтесь що MQTT Discovery увімкнено
2. Перезапустіть ESP32
3. Перевірте топіки MQTT в **Developer Tools** → **MQTT**
4. Шукайте топік: `homeassistant/sensor/bluetti_eb3a/+/config`

---

## Моніторинг MQTT топіків

В Home Assistant:
1. **Developer Tools** → **MQTT**
2. Subscribe to: `homeassistant/bluetti/eb3a/#`
3. Побачите всі повідомлення від ESP32

---

## Приклад автоматизації

### Автоматично вмикати AC вночі:

```yaml
automation:
  - alias: "Bluetti AC ON at night"
    trigger:
      platform: time
        at: "22:00:00"
    action:
      service: switch.turn_on
      target:
        entity_id: switch.bluetti_eb3a_ac_output
```

### Вимкнути AC коли батарея низька:

```yaml
automation:
  - alias: "Bluetti AC OFF when battery low"
    trigger:
      platform: numeric_state
      entity_id: sensor.bluetti_eb3a_battery
      below: 20
    action:
      service: switch.turn_off
      target:
        entity_id: switch.bluetti_eb3a_ac_output
```

---

## Налаштування протоколу Bluetti

Якщо команди не працюють (протокол відрізняється), потрібно:

1. **Аналіз протоколу** через BLE сканер (nRF Connect на смартфоні)
2. **Перевірити UUID** в `bluetti_device.h`
3. **Налаштувати команди** в `bluetti_device.cpp`

Корисні ресурси:
- https://github.com/warhammerkid/bluetti_mqtt
- https://github.com/Patrick762/hassio-bluetti-bt

---

Успіхів! 🔋⚡
