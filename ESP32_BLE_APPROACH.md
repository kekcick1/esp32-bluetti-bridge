# Альтернативний підхід: ESP32 BLE замість NimBLE

## Що зроблено

Переписано код для використання **стандартної ESP32 BLE бібліотеки** (`BLEDevice.h`) замість **NimBLE**. Це повністю інша реалізація BLE, яка може по-іншому обробляти нотифікації.

## Зміни

### 1. Бібліотека
- **Видалено:** `h2zero/NimBLE-Arduino@^1.4.1`
- **Використовується:** Вбудована ESP32 BLE бібліотека (не потрібно додавати в `platformio.ini`)

### 2. API зміни

#### NimBLE → ESP32 BLE:
- `NimBLEDevice::init()` → `BLEDevice::init()`
- `NimBLEDevice::createClient()` → `BLEDevice::createClient()`
- `NimBLEAddress` → `BLEAddress`
- `NimBLERemoteService` → `BLERemoteService`
- `NimBLERemoteCharacteristic` → `BLERemoteCharacteristic`
- `characteristic->subscribe(callback)` → `characteristic->registerForNotify(callback)`

### 3. Callback механізм

**NimBLE:**
```cpp
static void notificationThunk(NimBLERemoteCharacteristic* characteristic,
                              uint8_t* data, size_t length, bool isNotify);
characteristic->subscribe(true, notificationThunk);
```

**ESP32 BLE:**
```cpp
void notificationCallback(BLERemoteCharacteristic* characteristic,
                          uint8_t* data, size_t length, bool isNotify);
characteristic->registerForNotify(notificationCallback);
```

## Переваги ESP32 BLE

1. **Стандартна бібліотека** - вбудована в ESP32 Arduino framework
2. **Інша реалізація** - може по-іншому обробляти нотифікації
3. **Можливо краща сумісність** з деякими BLE пристроями

## Недоліки

1. **Більший розмір** - ESP32 BLE бібліотека займає більше flash пам'яті
2. **Можливі проблеми з розміром** - потрібно перевірити partition scheme

## Розмір програми

Поточна помилка:
```
Error: The program size (1727981 bytes) is greater than maximum allowed (1310720 bytes)
```

### Рішення:

1. **Змінити partition scheme** в `platformio.ini`:
```ini
board_build.partitions = huge_app.csv
```

2. **Або використати оптимізацію:**
```ini
build_flags = 
    -Os  ; Optimize for size
    # ... інші flags
```

3. **Або використати плату з більшим flash** (якщо доступна)

## Тестування

Після компіляції та завантаження, перевірте в Serial Monitor:

1. **Підключення:**
```
[Bluetti] Connecting to Bluetti D1:4C:11:6B:6A:3D...
[Bluetti] Connected and configured
```

2. **Підписка на нотифікації:**
```
[Bluetti] Subscribing to notifications (ESP32 BLE)...
[Bluetti] Callback registered
[Bluetti] Successfully subscribed to notifications
```

3. **Отримання даних:**
```
[Bluetti] *** CALLBACK CALLED (ESP32 BLE) ***
[Bluetti] Received XX bytes
[Bluetti] *** NOTIFICATION RECEIVED ***
```

## Якщо не працює

Якщо ESP32 BLE також не отримує нотифікації, це означає що:

1. **Bluetti EB3A прошивка не підтримує нотифікації** - обмеження прошивки пристрою
2. **Потрібно використати MQTT** - через аддон "Bluetti to MQTT" (див. FINAL_SOLUTION.md)

## Відкат до NimBLE

Якщо потрібно повернутися до NimBLE:

1. Відновити `platformio.ini`:
```ini
lib_deps = 
    # ...
    h2zero/NimBLE-Arduino@^1.4.1
```

2. Відновити файли з git (якщо збережено):
```bash
git checkout include/bluetti_device.h src/bluetti_device.cpp
```

---

**Дата створення:** $(date)
**Статус:** Код компілюється, але потрібна оптимізація розміру

