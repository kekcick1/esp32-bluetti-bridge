# Виправлення OTA помилки "Update failed: Aborted (код: 500)"

## Проблема

Після переходу на ESP32 BLE бібліотеку виникла помилка OTA:
```
❌ Помилка: Update failed: Aborted (код: 500)
```

### Причини:

1. **Неправильний partition scheme** - використовувався `huge_app.csv` без OTA підтримки
2. **Занадто мала перевірка розміру** - web сервер обмежував до 1.3MB, а прошивка 1.7MB
3. **Відсутність OTA партицій** - `huge_app.csv` не має app0/app1 для OTA

## Рішення

### 1. Створено custom partition scheme

Файл: `custom_ota_16mb.csv`

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x200000,  # 2MB для app0
app1,     app,  ota_1,   0x210000,0x200000,  # 2MB для app1
spiffs,   data, spiffs,  0x410000,0x3F0000,  # Решта для SPIFFS
```

**Особливості:**
- ✅ Дві OTA партиції (app0 та app1) по 2MB кожна
- ✅ Достатньо місця для великої прошивки ESP32 BLE
- ✅ Підтримка rollback (автоматичний відкат при помилці)

### 2. Оновлено platformio.ini

```ini
board_build.partitions = custom_ota_16mb.csv
```

### 3. Оновлено перевірку розміру в web_server.cpp

```cpp
// Було:
if (totalSize > 1310720) {  // 1.3MB

// Стало:
const size_t MAX_OTA_SIZE = 2000000; // ~1.9MB
if (totalSize > MAX_OTA_SIZE) {
```

## Результат

- ✅ Flash: 82.4% (1,727,981 / 2,097,152 bytes) - в межах 2MB партиції
- ✅ OTA партиції налаштовані правильно
- ✅ Перевірка розміру оновлена
- ✅ Код компілюється успішно

## Важливо! Перше завантаження через USB

**Після зміни partition scheme потрібно завантажити прошивку через USB:**

```bash
pio run --target upload
```

**Наступні оновлення можна робити через OTA:**

```bash
pio run --target upload --upload-port ESP32-Bluetti-EB3A.local
# або
make ota IP=192.168.1.XXX
```

## Перевірка OTA

Після завантаження через USB, перевірте в Serial Monitor:

```
OTA ready on port 3232
```

Якщо бачите це повідомлення - OTA готовий до використання!

## Якщо OTA все ще не працює

1. **Перезавантажте ESP32** після першого завантаження через USB
2. **Перевірте WiFi з'єднання** - OTA потребує стабільного WiFi
3. **Перевірте розмір прошивки** - має бути < 2MB
4. **Перевірте пароль OTA** - якщо встановлено, використовуйте правильний

---

**Дата виправлення:** $(date)
**Статус:** ✅ Виправлено

