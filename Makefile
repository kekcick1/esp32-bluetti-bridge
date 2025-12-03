.PHONY: help build upload monitor clean full devices size test config check info ota

# Кольори для виводу
CYAN := \033[0;36m
GREEN := \033[0;32m
YELLOW := \033[0;33m
RED := \033[0;31m
NC := \033[0m # No Color

help: ## Показати це повідомлення
	@echo "$(CYAN)🔧 Bluetti EB3A Bridge - Корисні команди$(NC)"
	@echo ""
	@echo "$(GREEN)📦 Збірка та завантаження:$(NC)"
	@echo "  make build         - Зібрати проект"
	@echo "  make upload        - Завантажити на ESP32 (USB)"
	@echo "  make ota           - Завантажити через WiFi (OTA)"
	@echo "  make monitor       - Відкрити Serial Monitor"
	@echo "  make clean         - Очистити збірку"
	@echo "  make full          - Очистити, зібрати та завантажити"
	@echo ""
	@echo "$(GREEN)🔍 Діагностика:$(NC)"
	@echo "  make devices       - Показати підключені пристрої"
	@echo "  make size          - Показати розмір прошивки"
	@echo "  make test          - Тест MQTT з'єднання"
	@echo ""
	@echo "$(GREEN)📝 Налаштування:$(NC)"
	@echo "  make config        - Відкрити main.cpp для редагування"
	@echo "  make check         - Перевірити налаштування"
	@echo ""
	@echo "$(GREEN)📊 Інформація:$(NC)"
	@echo "  make info          - Інформація про проект"
	@echo "  make help          - Показати це повідомлення"

build: ## Зібрати проект
	@echo "$(CYAN)🔨 Збірка проекту...$(NC)"
	@pio run

upload: ## Завантажити на ESP32
	@echo "$(CYAN)⬆️  Завантаження на ESP32 (USB)...$(NC)"
	@pio run --target upload

ota: ## Завантажити через WiFi (OTA)
	@echo "$(CYAN)📡 OTA оновлення через WiFi...$(NC)"
	@if [ -z "$(IP)" ]; then \
		echo "$(YELLOW)Використання: make ota IP=192.168.1.XXX$(NC)"; \
		echo "$(YELLOW)Або: make ota IP=ESP32-Bluetti-EB3A.local$(NC)"; \
		echo ""; \
		echo "Спроба автовизначення..."; \
		pio run --target upload --upload-port ESP32-Bluetti-EB3A.local; \
	else \
		pio run --target upload --upload-port $(IP); \
	fi

monitor: ## Відкрити Serial Monitor
	@echo "$(CYAN)📡 Відкриття Serial Monitor (Ctrl+C для виходу)...$(NC)"
	@pio device monitor

clean: ## Очистити збірку
	@echo "$(CYAN)🧹 Очищення збірки...$(NC)"
	@pio run --target clean

full: clean build upload ## Повна збірка та завантаження
	@echo ""
	@echo "$(GREEN)✅ Готово! Запускаю монітор через 2 секунди...$(NC)"
	@sleep 2
	@$(MAKE) monitor

devices: ## Показати підключені пристрої
	@echo "$(CYAN)🔌 Підключені пристрої:$(NC)"
	@pio device list

size: ## Показати розмір прошивки
	@echo "$(CYAN)📊 Розмір прошивки:$(NC)"
	@pio run --target size

test: ## Тест MQTT підключення
	@echo "$(CYAN)🧪 Тест MQTT підключення...$(NC)"
	@echo ""
	@read -p "Введіть IP адресу MQTT сервера (наприклад, 192.168.1.100): " mqtt_ip; \
	if [ -z "$$mqtt_ip" ]; then \
		echo "$(RED)❌ IP адреса не введена$(NC)"; \
		exit 1; \
	fi; \
	echo ""; \
	echo "Тестую підключення до $$mqtt_ip:1883..."; \
	if command -v mosquitto_sub > /dev/null 2>&1; then \
		if timeout 5 mosquitto_sub -h $$mqtt_ip -p 1883 -t "test" -C 1 > /dev/null 2>&1; then \
			echo "$(GREEN)✅ MQTT сервер доступний$(NC)"; \
		else \
			echo "$(RED)❌ Не вдалося підключитись до MQTT$(NC)"; \
			echo "Перевірте:"; \
			echo "  - IP адресу"; \
			echo "  - Чи працює Mosquitto в Home Assistant"; \
			echo "  - Чи доступний порт 1883"; \
		fi; \
	else \
		echo "$(YELLOW)⚠️  mosquitto-clients не встановлено$(NC)"; \
		echo "Встановіть: sudo apt install mosquitto-clients"; \
	fi

config: ## Відкрити конфігурацію
	@echo "$(CYAN)📝 Відкриття конфігурації...$(NC)"
	@$${EDITOR:-nano} src/main.cpp

check: ## Перевірити налаштування
	@echo "$(CYAN)🔍 Перевірка налаштувань в main.cpp...$(NC)"
	@echo ""
	@echo "WiFi:"
	@grep 'WIFI_SSID = "' src/main.cpp | head -1 || true
	@echo ""
	@echo "MQTT:"
	@grep 'MQTT_SERVER = "' src/main.cpp | head -1 || true
	@echo ""
	@echo "Bluetti:"
	@grep 'BLUETTI_MAC = "' src/main.cpp | head -1 || true
	@echo ""
	@if grep -q 'YOUR_' src/main.cpp; then \
		echo "$(YELLOW)⚠️  Потрібно налаштувати конфігурацію$(NC)"; \
		echo "Виконайте: make config"; \
	else \
		echo "$(GREEN)✅ Конфігурація виглядає налаштованою$(NC)"; \
	fi

info: ## Інформація про проект
	@echo "$(CYAN)ℹ️  Інформація про проект$(NC)"
	@echo ""
	@echo "📁 Проект: Bluetti EB3A Bridge для Home Assistant"
	@echo "📍 Розташування: $(shell pwd)"
	@echo ""
	@echo "📊 Статистика:"
	@echo "  - Файлів коду: $(shell find src include -name '*.cpp' -o -name '*.h' 2>/dev/null | wc -l)"
	@echo "  - Рядків коду: $(shell find src include -name '*.cpp' -o -name '*.h' -exec cat {} + 2>/dev/null | wc -l)"
	@echo "  - Файлів документації: $(shell find . -maxdepth 1 -name '*.md' 2>/dev/null | wc -l)"
	@echo ""
	@echo "📦 PlatformIO:"
	@pio --version
	@echo ""
	@echo "🔗 Корисні посилання:"
	@echo "  - Документація: README.md"
	@echo "  - Швидкий старт: QUICKSTART.md"
	@echo "  - Вирішення проблем: TROUBLESHOOTING.md"

.DEFAULT_GOAL := help
