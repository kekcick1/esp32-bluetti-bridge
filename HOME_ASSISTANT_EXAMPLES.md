# Приклади конфігурації Home Assistant

## 🎨 Lovelace Dashboard Card

Додайте в ваш дашборд (`configuration.yaml` або через UI):

```yaml
type: vertical-stack
cards:
  # Основна інформація
  - type: entities
    title: Bluetti EB3A
    show_header_toggle: false
    entities:
      - entity: sensor.bluetti_eb3a_battery
        name: Батарея
        icon: mdi:battery
      - entity: sensor.bluetti_eb3a_ac_power
        name: AC Потужність
        icon: mdi:flash
      - type: divider
      - entity: switch.bluetti_eb3a_ac_output
        name: AC Вихід
        icon: mdi:power-socket-eu
      - entity: switch.bluetti_eb3a_dc_output
        name: DC Вихід
        icon: mdi:power-plug

  # Графік батареї
  - type: history-graph
    title: Історія заряду
    hours_to_show: 24
    entities:
      - entity: sensor.bluetti_eb3a_battery
```

---

## 🔔 Сповіщення про низький заряд

```yaml
# configuration.yaml або automations.yaml

automation:
  - alias: "Bluetti - Низький заряд батареї"
    description: "Сповіщення коли батарея нижче 20%"
    trigger:
      - platform: numeric_state
        entity_id: sensor.bluetti_eb3a_battery
        below: 20
    action:
      - service: notify.mobile_app_your_phone
        data:
          title: "⚠️ Bluetti EB3A"
          message: "Низький заряд батареї: {{ states('sensor.bluetti_eb3a_battery') }}%"
          data:
            priority: high
```

---

## ⚡ Автоматизації керування

### Вмикати AC вночі для охолодження

```yaml
automation:
  - alias: "Bluetti - Увімкнути AC на ніч"
    trigger:
      - platform: time
        at: "22:00:00"
    condition:
      - condition: numeric_state
        entity_id: sensor.bluetti_eb3a_battery
        above: 50  # Тільки якщо заряд більше 50%
    action:
      - service: switch.turn_on
        target:
          entity_id: switch.bluetti_eb3a_ac_output
      - service: notify.persistent_notification
        data:
          message: "Bluetti AC увімкнено на ніч"
```

### Вимикати AC вранці

```yaml
automation:
  - alias: "Bluetti - Вимкнути AC вранці"
    trigger:
      - platform: time
        at: "07:00:00"
    action:
      - service: switch.turn_off
        target:
          entity_id: switch.bluetti_eb3a_ac_output
```

### Захист від глибокого розряду

```yaml
automation:
  - alias: "Bluetti - Вимкнути все при критичному заряді"
    trigger:
      - platform: numeric_state
        entity_id: sensor.bluetti_eb3a_battery
        below: 10
    action:
      - service: switch.turn_off
        target:
          entity_id:
            - switch.bluetti_eb3a_ac_output
            - switch.bluetti_eb3a_dc_output
      - service: notify.mobile_app_your_phone
        data:
          title: "🚨 BLUETTI КРИТИЧНИЙ ЗАРЯД!"
          message: "Всі виходи вимкнено. Заряд: {{ states('sensor.bluetti_eb3a_battery') }}%"
```

---

## 🌤️ Інтеграція з погодою та сонячними панелями

### Увімкнути заряд при сонячній погоді

```yaml
automation:
  - alias: "Bluetti - Заряд при сонці"
    trigger:
      - platform: state
        entity_id: weather.home
        to: "sunny"
    condition:
      - condition: numeric_state
        entity_id: sensor.bluetti_eb3a_battery
        below: 80
      - condition: time
        after: "08:00:00"
        before: "18:00:00"
    action:
      - service: notify.persistent_notification
        data:
          message: "☀️ Сонячна погода - час заряджати Bluetti!"
```

---

## 📊 Energy Dashboard Integration

Додайте Bluetti до Energy Dashboard:

1. **Settings** → **Dashboards** → **Energy**
2. **Add Consumption** → Виберіть `sensor.bluetti_eb3a_ac_power`
3. **Add Battery** → Виберіть `sensor.bluetti_eb3a_battery`

---

## 🎛️ Розширена картка з кнопками

```yaml
type: custom:vertical-stack-in-card
cards:
  # Заголовок
  - type: markdown
    content: |
      # 🔋 Bluetti EB3A
      **ESP32 Bridge Controller**

  # Батарея з кольоровою індикацією
  - type: gauge
    entity: sensor.bluetti_eb3a_battery
    min: 0
    max: 100
    severity:
      green: 50
      yellow: 30
      red: 0
    needle: true

  # Потужність
  - type: horizontal-stack
    cards:
      - type: sensor
        entity: sensor.bluetti_eb3a_ac_power
        name: AC Power
        icon: mdi:lightning-bolt
      - type: sensor
        entity: sensor.bluetti_eb3a_dc_power
        name: DC Power
        icon: mdi:current-dc

  # Кнопки керування
  - type: horizontal-stack
    cards:
      - type: button
        entity: switch.bluetti_eb3a_ac_output
        name: AC Output
        icon: mdi:power-socket-eu
        tap_action:
          action: toggle
        hold_action:
          action: more-info
      - type: button
        entity: switch.bluetti_eb3a_dc_output
        name: DC Output
        icon: mdi:power-plug
        tap_action:
          action: toggle

  # Останнє оновлення
  - type: markdown
    content: |
      Останнє оновлення: {{ relative_time(states.sensor.bluetti_eb3a_battery.last_updated) }}
```

---

## 🔧 Сенсори для розширеної інформації

```yaml
# configuration.yaml

sensor:
  # Час до повного розряду (приблизно)
  - platform: template
    sensors:
      bluetti_time_to_empty:
        friendly_name: "Час до розряду"
        unit_of_measurement: "год"
        value_template: >
          {% set battery = states('sensor.bluetti_eb3a_battery') | float %}
          {% set power = states('sensor.bluetti_eb3a_ac_power') | float %}
          {% if power > 0 %}
            {{ ((battery * 268) / power) | round(1) }}
          {% else %}
            999
          {% endif %}

  # Статус батареї (текстовий)
  - platform: template
    sensors:
      bluetti_battery_status:
        friendly_name: "Статус батареї"
        value_template: >
          {% set battery = states('sensor.bluetti_eb3a_battery') | float %}
          {% if battery > 80 %}
            Повний заряд
          {% elif battery > 50 %}
            Достатньо
          {% elif battery > 20 %}
            Низький
          {% else %}
            Критичний
          {% endif %}
        icon_template: >
          {% set battery = states('sensor.bluetti_eb3a_battery') | float %}
          {% if battery > 80 %}
            mdi:battery
          {% elif battery > 50 %}
            mdi:battery-60
          {% elif battery > 20 %}
            mdi:battery-30
          {% else %}
            mdi:battery-alert
          {% endif %}
```

---

## 📱 Actionable Notifications (iOS/Android)

```yaml
automation:
  - alias: "Bluetti - Запит на вимкнення при простої"
    trigger:
      - platform: numeric_state
        entity_id: sensor.bluetti_eb3a_ac_power
        below: 5
        for:
          minutes: 30  # 30 хвилин без навантаження
    action:
      - service: notify.mobile_app_your_phone
        data:
          title: "Bluetti EB3A"
          message: "AC вихід працює без навантаження 30 хв. Вимкнути?"
          data:
            actions:
              - action: "BLUETTI_AC_OFF"
                title: "Вимкнути AC"
              - action: "BLUETTI_IGNORE"
                title: "Ігнорувати"

  - alias: "Bluetti - Обробка відповіді"
    trigger:
      - platform: event
        event_type: mobile_app_notification_action
        event_data:
          action: "BLUETTI_AC_OFF"
    action:
      - service: switch.turn_off
        target:
          entity_id: switch.bluetti_eb3a_ac_output
      - service: notify.mobile_app_your_phone
        data:
          message: "✓ AC вихід вимкнено"
```

---

## 🎮 Сцени (Scenes)

```yaml
# scenes.yaml

# Економний режим
- name: "Bluetti - Економія"
  entities:
    switch.bluetti_eb3a_ac_output: off
    switch.bluetti_eb3a_dc_output: off

# Робочий режим
- name: "Bluetti - Робота"
  entities:
    switch.bluetti_eb3a_ac_output: on
    switch.bluetti_eb3a_dc_output: on

# Нічний режим
- name: "Bluetti - Ніч"
  entities:
    switch.bluetti_eb3a_ac_output: on
    switch.bluetti_eb3a_dc_output: off
```

Активувати через автоматизацію:

```yaml
automation:
  - alias: "Bluetti - Робочий день"
    trigger:
      - platform: time
        at: "09:00:00"
    action:
      - service: scene.turn_on
        target:
          entity_id: scene.bluetti_robota
```

---

## 🔊 Голосове керування (Google Home / Alexa)

Після налаштування через Home Assistant Cloud або Nabu Casa:

**"Окей Google, увімкни Bluetti AC вихід"**
**"Alexa, turn on Bluetti AC output"**

---

Використовуйте ці приклади як основу для власних автоматизацій! 🚀
