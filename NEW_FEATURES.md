# Нові функції v1.2.0

## Додані всі функції з tjhowse/esphome_bluetti_eb3a

### ✨ Нові керовані функції:

#### 1. 🌿 **ECO Mode**
- **Тип**: Switch (вимикач)
- **Регістр**: `0x0BC3` (3011)
- **Опис**: Режим економії енергії — автоматично вимикає AC вихід при низькому навантаженні
- **Керування**:
  - MQTT: `homeassistant/bluetti/eb3a/eco_mode/set` → `ON`/`OFF`
  - HTTP: `POST /eco_mode?state=on`
  - Home Assistant: Switch `Bluetti ECO Mode`

#### 2. ⚡ **Power Lifting**
- **Тип**: Switch (вимикач)
- **Регістр**: `0x0BC6` (3014)
- **Опис**: Збільшує максимальну AC потужність з 600W до ~800W для короткочасних навантажень
- **Керування**:
  - MQTT: `homeassistant/bluetti/eb3a/power_lifting/set` → `ON`/`OFF`
  - HTTP: `POST /power_lifting?state=on`
  - Home Assistant: Switch `Bluetti Power Lifting`

#### 3. 💡 **LED Mode (Фонарик)**
- **Тип**: Select (вибір режиму)
- **Регістр**: `0x0BBA` (3002)
- **Режими**:
  - `Low` (1) — низька яскравість
  - `High` (2) — висока яскравість
  - `SOS` (3) — сигнал SOS
  - `Off` (4) — вимкнено
- **Керування**:
  - MQTT: `homeassistant/bluetti/eb3a/led_mode/set` → `Low`/`High`/`SOS`/`Off`
  - HTTP: `POST /led_mode?mode=high`
  - Home Assistant: Select `Bluetti LED Mode`

#### 4. ⏰ **ECO Shutdown**
- **Тип**: Select (вибір часу)
- **Регістр**: `0x0BC4` (3012)
- **Опції**: `1h`, `2h`, `3h`, `4h`
- **Опис**: Час до автоматичного вимкнення в ECO режимі при низькому навантаженні
- **Керування**:
  - MQTT: `homeassistant/bluetti/eb3a/eco_shutdown/set` → `1h`/`2h`/`3h`/`4h`
  - HTTP: `POST /eco_shutdown?hours=2`
  - Home Assistant: Select `Bluetti ECO Shutdown`

#### 5. 🔴 **Power Off**
- **Тип**: Button (кнопка)
- **Регістр**: `0x0BBC` (3004)
- **Опис**: Повністю вимикає Bluetti EB3A
- **Керування**:
  - MQTT: `homeassistant/bluetti/eb3a/power_off` → будь-яке значення
  - HTTP: `POST /power_off`
  - Home Assistant: Button `Bluetti Power Off`

---

## 📡 MQTT Discovery

Всі функції автоматично публікуються в Home Assistant через MQTT Discovery:

```yaml
# ECO Mode
homeassistant/switch/bluetti_eb3a/eco_mode/config

# Power Lifting
homeassistant/switch/bluetti_eb3a/power_lifting/config

# LED Mode
homeassistant/select/bluetti_eb3a/led_mode/config

# ECO Shutdown
homeassistant/select/bluetti_eb3a/eco_shutdown/config

# Power Off
homeassistant/button/bluetti_eb3a/power_off/config
```

---

## 🔧 Технічні деталі

### Регістри MODBUS (tjhowse repo):

| Функція         | Адреса   | Значення          |
|-----------------|----------|-------------------|
| AC Output       | `0x0BBF` | 0=OFF, 1=ON       |
| DC Output       | `0x0BC0` | 0=OFF, 1=ON       |
| LED Mode        | `0x0BBA` | 1..4              |
| Power Off       | `0x0BBC` | 1=OFF             |
| ECO Mode        | `0x0BC3` | 0=OFF, 1=ON       |
| ECO Shutdown    | `0x0BC4` | 1..4 (години)     |
| Charging Mode   | `0x0BF9` | 0=Std, 1=Sil, 2=Turbo |
| Power Lifting   | `0x0BC6` | 0=OFF, 1=ON       |

### Опитування стану

Функції опитуються **поротаційно** (кожна 4-та ітерація) для економії BLE трафіку:

```cpp
pollFeatureState():
  0: ECO Mode (0x0BC3)
  1: Power Lifting (0x0BC6)
  2: LED Mode (0x0BBA)
  3: ECO Shutdown (0x0BC4)
```

---

## 📖 Приклади використання

### Home Assistant Automation

```yaml
# Вмикання ECO режиму вночі
automation:
  - alias: "Bluetti ECO Mode Night"
    trigger:
      - platform: time
        at: "22:00:00"
    action:
      - service: switch.turn_on
        target:
          entity_id: switch.bluetti_eb3a_eco_mode

# Фонарик SOS при аварії
automation:
  - alias: "Emergency Flashlight"
    trigger:
      - platform: state
        entity_id: binary_sensor.power_outage
        to: "on"
    action:
      - service: select.select_option
        target:
          entity_id: select.bluetti_eb3a_led_mode
        data:
          option: "SOS"
```

### cURL приклади

```bash
# Увімкнути ECO Mode
curl -X POST http://esp32-ip/eco_mode -d "state=on"

# Встановити фонарик на High
curl -X POST http://esp32-ip/led_mode -d "mode=high"

# Встановити ECO shutdown на 3 години
curl -X POST http://esp32-ip/eco_shutdown -d "hours=3"

# Увімкнути Power Lifting
curl -X POST http://esp32-ip/power_lifting -d "state=on"

# Вимкнути Bluetti
curl -X POST http://esp32-ip/power_off
```

---

## 🎯 Сумісність

- ✅ **Bluetti EB3A** — повністю підтримується
- ❓ **Інші моделі Bluetti** — можуть підтримуватись (потребує тестування)

---

## 🔗 Джерела

Функції портовані з репозиторію: https://github.com/tjhowse/esphome_bluetti_eb3a

Регістри MODBUS узяті з ESPHome конфігурації (`esphome.yaml`).
