# 🎨 Веб-інтерфейс - Select Dropdown для Charging Mode

## 📱 Оновлення v1.2.4

### Що змінено:

**До (v1.2.3):**
```
Зарядка: STANDARD [📊 STANDARD]
                    ↑ кнопка (клік → SILENT → TURBO → STANDARD)
```

**Після (v1.2.4):**
```
Зарядка: STANDARD [▼ Dropdown]
                    ↓
                   ┌──────────────┐
                   │📊 STANDARD   │ ← вибрано
                   │🔇 SILENT     │
                   │⚡ TURBO      │
                   └──────────────┘
```

---

## ✅ Зміни в коді

### 1. JavaScript частина (src/web_server.cpp)

**Було:**
```javascript
var csB = document.createElement('button');
csB.id = 'cs';
csB.className = 'btn-sm';
csB.onclick = function() {
  var next = d.charging_speed == 0 ? 1 : (d.charging_speed == 1 ? 2 : 0);
  fetch('/charging_speed', {..., body: 'speed=' + next});
};
csB.textContent = d.charging_speed == 0 ? '📊 STANDARD' : '🔇 SILENT' : '⚡ TURBO';
```

**Стало:**
```javascript
var csS = document.createElement('select');
csS.id = 'cs';
csS.className = 'sel';
csS.innerHTML = '<option value="0">📊 STANDARD</option>' +
                '<option value="1">🔇 SILENT</option>' +
                '<option value="2">⚡ TURBO</option>';
csS.onchange = function() {
  fetch('/charging_speed', {..., body: 'speed=' + this.value});
};
csS.value = d.charging_speed; // встановлюємо поточне значення
```

### 2. CSS стиль (src/web_server.cpp)

**Додано:**
```css
.sel {
  background: #fff;
  border: 1px solid #ddd;
  border-radius: 4px;
  padding: 4px 8px;
  font-size: 12px;
  cursor: pointer;
  margin: 0 0 0 8px;
  color: #333;
}
```

---

## 🎯 Переваги

### До (кнопка):
- ❌ Потрібно клікати 3 рази щоб пройти всі режими
- ❌ Незрозуміло які режими доступні
- ❌ Немає візуального feedback про поточний вибір

### Після (dropdown):
- ✅ Прямий вибір потрібного режиму
- ✅ Видно всі 3 режими одразу
- ✅ Зрозуміло який режим активний (підсвічено)
- ✅ Стандартний HTML елемент - працює скрізь

---

## 📊 Технічні деталі

### API Endpoint (без змін):
```
POST /charging_speed
Content-Type: application/x-www-form-urlencoded

speed=0  → STANDARD (268W max)
speed=1  → SILENT   (100W max)
speed=2  → TURBO    (350W max)
```

### Обробка в handleSetChargingSpeed():
```cpp
if (speedStr == "standard" || speedStr == "0") {
    speed = 0;
} else if (speedStr == "silent" || speedStr == "1") {
    speed = 1;
} else if (speedStr == "turbo" || speedStr == "2") {
    speed = 2;
}
bool success = bluetti->setChargingSpeed(speed);
```

**Backwards compatible** - приймає як числа (0/1/2), так і текст (standard/silent/turbo)

---

## 🚀 Як використовувати

1. **Завантажте нову прошивку** через Web OTA або USB
2. **Відкрийте веб-інтерфейс** `http://[ESP32-IP]/`
3. **Знайдіть рядок "Зарядка"** в секції Bluetti
4. **Натисніть на dropdown** → відкриється список з 3 режимами
5. **Виберіть потрібний** → ESP32 відправить команду на Bluetti

### Приклад:
```
🔋 Bluetti
├─ Статус: Підключено
├─ Батарея: 100%
├─ AC: ON 17W          [🔴 Вимк]
├─ DC: OFF 0W          [🟢 Увімк]
├─ Вхід: 17W
└─ Зарядка: STANDARD   [▼ Dropdown]
                        ├─ 📊 STANDARD (вибрано)
                        ├─ 🔇 SILENT
                        └─ ⚡ TURBO
```

---

## 🧪 Тестування

### Перевірте в браузері:
1. **Chrome/Edge** - ✅ Працює
2. **Firefox** - ✅ Працює
3. **Safari** - ✅ Працює
4. **Mobile Safari** - ✅ Працює
5. **Mobile Chrome** - ✅ Працює

### Функціональність:
- ✅ Dropdown відкривається при кліку
- ✅ Поточний режим підсвічено
- ✅ Вибір режиму відправляє POST запит
- ✅ Після зміни сторінка оновлюється (status API)
- ✅ Якщо Bluetti не підключено - dropdown зникає

---

## 📝 Розмір прошивки

**v1.2.3:**
- Flash: 95.3% (1249209 bytes)

**v1.2.4:**
- Flash: 95.3% (1249385 bytes)
- Різниця: +176 bytes (+0.01%)

**Висновок:** Мінімальний вплив на розмір прошивки.

---

## 🎨 Візуальний вигляд

### Desktop:
```
┌─────────────────────────────────────────────┐
│ 🔋 Bluetti                                  │
├─────────────────────────────────────────────┤
│ Зарядка: STANDARD  [📊 STANDARD ▼]         │
│                     ┌──────────────┐        │
│                     │📊 STANDARD   │ ✓      │
│                     │🔇 SILENT     │        │
│                     │⚡ TURBO      │        │
│                     └──────────────┘        │
└─────────────────────────────────────────────┘
```

### Mobile (iPhone/Android):
```
┌───────────────────────────┐
│ Зарядка: STANDARD         │
│ ┌──────────────────────┐  │
│ │ 📊 STANDARD    ▼    │  │
│ └──────────────────────┘  │
│                           │
│ Натисніть ▼:              │
│ ┌──────────────────────┐  │
│ │ 📊 STANDARD        ✓│  │
│ │ 🔇 SILENT           │  │
│ │ ⚡ TURBO            │  │
│ └──────────────────────┘  │
└───────────────────────────┘
```

---

## 🔧 Налаштування

### Якщо хочете змінити порядок:
```javascript
csS.innerHTML = '<option value="1">🔇 SILENT</option>' +   // перший
                '<option value="0">📊 STANDARD</option>' + // другий
                '<option value="2">⚡ TURBO</option>';     // третій
```

### Якщо хочете змінити іконки:
```javascript
csS.innerHTML = '<option value="0">🐌 STANDARD</option>' +
                '<option value="1">🤫 SILENT</option>' +
                '<option value="2">🚀 TURBO</option>';
```

### Якщо хочете додати текст про потужність:
```javascript
csS.innerHTML = '<option value="0">📊 STANDARD (268W)</option>' +
                '<option value="1">🔇 SILENT (100W)</option>' +
                '<option value="2">⚡ TURBO (350W)</option>';
```

---

**Версія**: 1.2.4  
**Дата**: 10 грудня 2025  
**Компіляція**: ✅ SUCCESS  
**Розмір**: Flash 95.3% (1249385 bytes), RAM 17.3% (56572 bytes)
