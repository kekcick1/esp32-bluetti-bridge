#!/bin/bash

# Пряме завантаження через SSH без складних тунелів
# Використовуємо rsync для синхронізації та виконання на сервері

SERVER="11.18.10.40"
REMOTE_USER="user"
REMOTE_PORT="/dev/ttyACM0"

echo "🔌 Віддалене завантаження прошивки ESP32"
echo "Сервер: $REMOTE_USER@$SERVER"
echo "Порт: $REMOTE_PORT"
echo ""

# Крок 1: Синхронізуємо код
echo "📡 Синхронізація коду на сервер..."
rsync -avz --exclude '.pio' --exclude '.git' --exclude '*.bin' \
  /home/kekcick/Documents/esp32/ \
  $REMOTE_USER@$SERVER:~/esp32/ 2>&1 | grep -E "(sending|sent|speedup)" || echo "Синхронізація завершена"

# Крок 2: Виконуємо завантаження на сервері
echo ""
echo "📤 Завантаження прошивки на сервері..."
ssh -o User=$REMOTE_USER -o StrictHostKeyChecking=no $SERVER << 'ENDSSH'
cd ~/esp32
echo "Поточна директорія: $(pwd)"
echo "Перевірка PlatformIO..."
if ! command -v pio &> /dev/null; then
    echo "❌ PlatformIO не встановлено на сервері"
    exit 1
fi
echo "✅ PlatformIO знайдено"
echo ""
echo "🔨 Компіляція та завантаження..."
pio run --target upload --upload-port /dev/ttyACM0
ENDSSH

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Прошивка успішно завантажена!"
else
    echo ""
    echo "❌ Помилка завантаження прошивки"
    exit 1
fi

