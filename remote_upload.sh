#!/bin/bash

# Конфігурація
SERVER="11.18.10.40"
# Явно встановлюємо значення (не залежимо від змінних середовища)
REMOTE_USER="user"  # Логін на віддаленому сервері
REMOTE_PORT="/dev/ttyACM0"  # Порт ESP32 на сервері
# Дозволяємо перевизначення через змінні середовища
[ -n "$REMOTE_USER_ENV" ] && REMOTE_USER="$REMOTE_USER_ENV"
[ -n "$REMOTE_PORT_ENV" ] && REMOTE_PORT="$REMOTE_PORT_ENV"

# Кольори для виводу
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}🔌 Віддалене завантаження прошивки ESP32${NC}"
echo -e "Сервер: ${YELLOW}$REMOTE_USER@$SERVER${NC}"
echo -e "Віддалений порт: ${YELLOW}$REMOTE_PORT${NC}"
echo ""
# Діагностика: показуємо які змінні використовуються
echo -e "${YELLOW}Діагностика:${NC}"
echo "  REMOTE_USER='$REMOTE_USER'"
echo "  REMOTE_PORT='$REMOTE_PORT'"
echo "  SERVER='$SERVER'"
echo ""

# Перевірка чи встановлено rsync
if ! command -v rsync &> /dev/null; then
    echo -e "${RED}❌ Помилка: rsync не встановлено${NC}"
    echo "Встановіть: sudo apt-get install rsync"
    exit 1
fi

# Крок 1: Перевірка підключення до сервера
echo -e "${YELLOW}📡 Перевірка підключення до сервера...${NC}"
echo -e "${YELLOW}Використовується: $REMOTE_USER@$SERVER${NC}"
echo ""

# Спочатку спробуємо без пароля (якщо є SSH ключі)
if ssh -o ConnectTimeout=5 -o BatchMode=yes -o StrictHostKeyChecking=no -o User="$REMOTE_USER" "$SERVER" exit 2>/dev/null; then
    echo -e "${GREEN}✅ Автоматичне підключення успішне (SSH ключі)${NC}"
else
    echo -e "${YELLOW}⚠️  SSH ключі не налаштовані, буде потрібен пароль${NC}"
    echo -e "${YELLOW}Перевіряємо доступність сервера...${NC}"
    # Просто перевіряємо доступність, не підключаючись
    if ! ping -c 1 -W 2 $SERVER >/dev/null 2>&1; then
        echo -e "${RED}❌ Сервер $SERVER недоступний${NC}"
        exit 1
    fi
    echo -e "${GREEN}✅ Сервер доступний, буде потрібен пароль для SSH${NC}"
fi

# Крок 2: Перевірка порту на сервері
echo -e "${YELLOW}🔍 Перевірка порту $REMOTE_PORT на сервері...${NC}"
echo -e "${YELLOW}Виконується: ssh -o User=$REMOTE_USER $SERVER${NC}"
if ! ssh -o User="$REMOTE_USER" "$SERVER" "test -e $REMOTE_PORT" 2>/dev/null; then
    echo -e "${RED}❌ Порт $REMOTE_PORT не знайдено на сервері${NC}"
    echo "Доступні порти:"
    ssh -o User="$REMOTE_USER" "$SERVER" "ls -la /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo 'Не знайдено'"
    exit 1
fi
echo -e "${GREEN}✅ Порт знайдено${NC}"

# Крок 3: Синхронізація коду на сервер
echo -e "${YELLOW}📡 Синхронізація коду на сервер...${NC}"
rsync -avz --exclude '.pio' --exclude '.git' --exclude '*.bin' \
  /home/kekcick/Documents/esp32/ \
  $REMOTE_USER@$SERVER:~/esp32/ 2>&1 | grep -E "(sending|sent|speedup)" || echo "Синхронізація завершена"

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ Помилка синхронізації коду${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Синхронізація завершена${NC}"
echo ""

# Крок 4: Виконання завантаження на сервері
echo -e "${GREEN}📤 Завантаження прошивки на сервері...${NC}"
echo -e "${YELLOW}Якщо потрібен пароль, введіть його зараз:${NC}"
echo ""

ssh -o User="$REMOTE_USER" -o StrictHostKeyChecking=no "$SERVER" bash << EOF
cd ~/esp32
echo "Поточна директорія: \$(pwd)"
echo "Перевірка PlatformIO..."
if ! command -v pio &> /dev/null; then
    echo "❌ PlatformIO не встановлено на сервері"
    exit 1
fi
echo "✅ PlatformIO знайдено"
echo ""
echo "🔨 Компіляція та завантаження..."
pio run --target upload --upload-port $REMOTE_PORT
EOF

UPLOAD_RESULT=$?

if [ $UPLOAD_RESULT -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✅ Прошивка успішно завантажена!${NC}"
    exit 0
else
    echo ""
    echo -e "${RED}❌ Помилка завантаження прошивки${NC}"
    exit 1
fi

