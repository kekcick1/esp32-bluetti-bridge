#!/usr/bin/env fish
# Скрипт для швидких команд проекту Bluetti EB3A Bridge
# Використання: ./commands.fish [команда]

set PROJECT_DIR (dirname (status --current-filename))

function help
    echo "🔧 Bluetti EB3A Bridge - Корисні команди"
    echo ""
    echo "Використання: ./commands.fish [команда]"
    echo ""
    echo "📦 Збірка та завантаження:"
    echo "  build         - Зібрати проект"
    echo "  upload        - Завантажити на ESP32 (USB)"
    echo "  ota           - Завантажити через WiFi (OTA)"
    echo "  monitor       - Відкрити Serial Monitor"
    echo "  clean         - Очистити збірку"
    echo "  full          - Очистити, зібрати та завантажити"
    echo ""
    echo "🔍 Діагностика:"
    echo "  devices       - Показати підключені пристрої"
    echo "  size          - Показати розмір прошивки"
    echo "  test          - Тест MQTT з'єднання"
    echo ""
    echo "📝 Налаштування:"
    echo "  config        - Відкрити main.cpp для редагування"
    echo "  check-config  - Перевірити налаштування"
    echo ""
    echo "📊 Інформація:"
    echo "  info          - Інформація про проект"
    echo "  libs          - Список бібліотек"
    echo "  help          - Показати це повідомлення"
end

function build
    echo "🔨 Збірка проекту..."
    cd $PROJECT_DIR
    pio run
end

function upload
    echo "⬆️  Завантаження на ESP32 (USB)..."
    cd $PROJECT_DIR
    pio run --target upload
end

function ota
    echo "📡 OTA оновлення через WiFi..."
    echo ""
    
    if set -q argv[1]
        set ip $argv[1]
    else
        echo "Введіть IP адресу або hostname (Enter для ESP32-Bluetti-EB3A.local):"
        read ip
        
        if test -z "$ip"
            set ip "ESP32-Bluetti-EB3A.local"
        end
    end
    
    echo "Оновлення пристрою: $ip"
    cd $PROJECT_DIR
    pio run --target upload --upload-port $ip
end

function monitor
    echo "📡 Відкриття Serial Monitor (Ctrl+C для виходу)..."
    cd $PROJECT_DIR
    pio device monitor
end

function clean
    echo "🧹 Очищення збірки..."
    cd $PROJECT_DIR
    pio run --target clean
end

function full
    echo "🚀 Повна збірка та завантаження..."
    clean
    build
    upload
    echo ""
    echo "✅ Готово! Запускаю монітор..."
    sleep 2
    monitor
end

function devices
    echo "🔌 Підключені пристрої:"
    cd $PROJECT_DIR
    pio device list
end

function size
    echo "📊 Розмір прошивки:"
    cd $PROJECT_DIR
    pio run --target size
end

function test_mqtt
    echo "🧪 Тест MQTT підключення..."
    echo ""
    echo "Введіть IP адресу MQTT сервера (наприклад, 192.168.1.100):"
    read mqtt_ip
    
    if test -z "$mqtt_ip"
        echo "❌ IP адреса не введена"
        return 1
    end
    
    echo ""
    echo "Тестую підключення до $mqtt_ip:1883..."
    
    if command -v mosquitto_sub > /dev/null
        timeout 5 mosquitto_sub -h $mqtt_ip -p 1883 -t "test" -C 1 &> /dev/null
        if test $status -eq 0
            echo "✅ MQTT сервер доступний"
        else
            echo "❌ Не вдалося підключитись до MQTT"
            echo "Перевірте:"
            echo "  - IP адресу"
            echo "  - Чи працює Mosquitto в Home Assistant"
            echo "  - Чи доступний порт 1883"
        end
    else
        echo "⚠️  mosquitto-clients не встановлено"
        echo "Встановіть: sudo apt install mosquitto-clients"
    end
end

function config
    echo "📝 Відкриття конфігурації..."
    if test -n "$EDITOR"
        eval $EDITOR $PROJECT_DIR/src/main.cpp
    else
        nano $PROJECT_DIR/src/main.cpp
    end
end

function check_config
    echo "🔍 Перевірка налаштувань в main.cpp..."
    echo ""
    
    set wifi_ssid (grep 'WIFI_SSID = "' $PROJECT_DIR/src/main.cpp | head -1)
    set mqtt_server (grep 'MQTT_SERVER = "' $PROJECT_DIR/src/main.cpp | head -1)
    set bluetti_mac (grep 'BLUETTI_MAC = "' $PROJECT_DIR/src/main.cpp | head -1)
    
    echo "WiFi:"
    echo "  $wifi_ssid"
    echo ""
    echo "MQTT:"
    echo "  $mqtt_server"
    echo ""
    echo "Bluetti:"
    echo "  $bluetti_mac"
    echo ""
    
    if string match -q '*YOUR_*' "$wifi_ssid"
        echo "⚠️  Потрібно налаштувати WiFi SSID"
    else
        echo "✅ WiFi налаштовано"
    end
    
    if string match -q '*YOUR_*' "$mqtt_server"
        echo "⚠️  Потрібно налаштувати MQTT сервер"
    else
        echo "✅ MQTT налаштовано"
    end
end

function info
    echo "ℹ️  Інформація про проект"
    echo ""
    echo "📁 Проект: Bluetti EB3A Bridge для Home Assistant"
    echo "📍 Розташування: $PROJECT_DIR"
    echo ""
    echo "📊 Статистика:"
    
    set cpp_files (find $PROJECT_DIR/src $PROJECT_DIR/include -name "*.cpp" -o -name "*.h" 2>/dev/null | wc -l)
    set cpp_lines (find $PROJECT_DIR/src $PROJECT_DIR/include -name "*.cpp" -o -name "*.h" -exec cat {} + 2>/dev/null | wc -l)
    set md_files (find $PROJECT_DIR -maxdepth 1 -name "*.md" 2>/dev/null | wc -l)
    
    echo "  - Файлів коду: $cpp_files"
    echo "  - Рядків коду: $cpp_lines"
    echo "  - Файлів документації: $md_files"
    echo ""
    echo "📦 PlatformIO:"
    cd $PROJECT_DIR
    pio --version
    echo ""
    echo "🔗 Корисні посилання:"
    echo "  - Документація: README.md"
    echo "  - Швидкий старт: QUICKSTART.md"
    echo "  - Вирішення проблем: TROUBLESHOOTING.md"
end

function libs
    echo "📚 Бібліотеки проекту:"
    cd $PROJECT_DIR
    pio lib list
end

# Головна логіка
if test (count $argv) -eq 0
    help
else
    switch $argv[1]
        case build
            build
        case upload
            upload
        case ota
            ota $argv[2]
        case monitor
            monitor
        case clean
            clean
        case full
            full
        case devices
            devices
        case size
            size
        case test
            test_mqtt
        case config
            config
        case check-config
            check_config
        case info
            info
        case libs
            libs
        case help
            help
        case '*'
            echo "❌ Невідома команда: $argv[1]"
            echo ""
            help
            exit 1
    end
end
