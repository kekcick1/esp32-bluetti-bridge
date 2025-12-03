# 🔧 Встановлення та налаштування середовища

## 📦 Встановлення PlatformIO

### Автоматичне встановлення (рекомендовано)

PlatformIO вже встановлено! ✅

Якщо потрібно перевстановити:

```bash
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o get-platformio.py
python3 get-platformio.py
```

### Додати в PATH (Fish shell)

```bash
echo 'set -gx PATH $HOME/.platformio/penv/bin $PATH' >> ~/.config/fish/config.fish
source ~/.config/fish/config.fish
```

### Додати в PATH (Bash/Zsh)

```bash
echo 'export PATH="$HOME/.platformio/penv/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### Перевірка встановлення

```bash
pio --version
# Має показати: PlatformIO Core, version 6.1.18
```

---

## 🔌 Підключення ESP32

### 1. Підключіть ESP32 через USB

Використайте USB-C кабель для LILYGO T-Display

### 2. Перевірте чи система бачить пристрій

```bash
pio device list
```

**Має з'явитись щось на зразок:**
```
/dev/ttyUSB0
------------
Hardware ID: USB VID:PID=1A86:55D4 SER=0001 LOCATION=1-2:1.0
Description: USB Single Serial
```

Або:
```
/dev/ttyACM0
------------
Hardware ID: USB VID:PID=303A:1001
Description: USB JTAG/serial debug unit
```

### 3. Якщо ESP32 не виявлено

#### Проблема: Драйвер CH9102F

LILYGO T-Display використовує чіп **CH9102F** для USB-UART.

**Рішення для Linux:**

```bash
# Arch Linux
sudo pacman -S ch341-dkms-git

# Ubuntu/Debian
sudo apt install linux-headers-$(uname -r)
git clone https://github.com/WCHSoftGroup/ch343ser_linux.git
cd ch343ser_linux/driver
make
sudo make install

# Перезавантажте модуль
sudo modprobe ch341
```

#### Права доступу

Додайте себе в групу `dialout` або `uucp`:

```bash
# Для більшості дистрибутивів
sudo usermod -a -G dialout $USER

# Для Arch Linux
sudo usermod -a -G uucp $USER

# Вийдіть і зайдіть знову, або:
newgrp dialout
```

#### Перевірка USB пристрою

```bash
lsusb | grep -i "WCH\|CH340\|CH341\|1a86"
```

Має показати щось на зразок:
```
Bus 001 Device 005: ID 1a86:55d4 QinHeng Electronics USB Single Serial
```

---

## 🚀 Перша збірка проекту

### 1. Встановити бібліотеки

```bash
cd ~/Documents/esp32
pio lib install
```

Або автоматично при першій збірці:
```bash
pio run
```

### 2. Зібрати проект

```bash
pio run
```

**Тривалість:** 2-5 хвилин (перша збірка), далі ~30 секунд

### 3. Завантажити на ESP32

```bash
pio run --target upload
```

Або:
```bash
make upload
```

### 4. Моніторинг Serial

```bash
pio device monitor
```

Або:
```bash
make monitor
```

**Вийти:** `Ctrl + C`

---

## 🛠️ Корисні команди PlatformIO

```bash
# Інформація про проект
pio project config

# Показати встановлені бібліотеки
pio lib list

# Оновити бібліотеки
pio lib update

# Очистити збірку
pio run --target clean

# Показати розмір прошивки
pio run --target size

# Список підтримуваних плат
pio boards esp32

# Показати інформацію про плату
pio boards lilygo-t-display-s3
```

---

## 🐧 Специфіка для різних дистрибутивів Linux

### Arch Linux / Manjaro

```bash
# Встановити необхідні пакети
sudo pacman -S python python-pip git base-devel

# Драйвер CH341/CH9102
yay -S ch341-dkms-git

# Додати в групу
sudo usermod -a -G uucp $USER
```

### Ubuntu / Debian / Linux Mint

```bash
# Встановити необхідні пакети
sudo apt update
sudo apt install python3 python3-pip git build-essential

# Драйвер CH341/CH9102
sudo apt install linux-headers-$(uname -r)
# Далі компіляція драйвера (див. вище)

# Додати в групу
sudo usermod -a -G dialout $USER
```

### Fedora / RHEL / CentOS

```bash
# Встановити необхідні пакети
sudo dnf install python3 python3-pip git gcc gcc-c++ make

# Драйвер
sudo dnf install kernel-devel

# Додати в групу
sudo usermod -a -G dialout $USER
```

---

## 🪟 Windows

### 1. Встановити Python

Завантажте з [python.org](https://www.python.org/downloads/)

**Важливо:** Відмітьте "Add Python to PATH"

### 2. Встановити PlatformIO

```cmd
python -m pip install platformio
```

### 3. Драйвер CH9102F

Завантажте з офіційного сайту WCH:
- [CH343SER](http://www.wch.cn/downloads/CH343SER_EXE.html)

### 4. Перевірка

```cmd
pio device list
```

---

## 🍎 macOS

### 1. Встановити Homebrew (якщо немає)

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### 2. Встановити Python

```bash
brew install python
```

### 3. Встановити PlatformIO

```bash
python3 -m pip install platformio
```

### 4. Драйвер CH9102F

Завантажте з офіційного сайту WCH або використайте:
```bash
brew install --cask wch-ch34x-usb-serial-driver
```

---

## 🔍 Діагностика проблем

### ESP32 не виявлено

**1. Перевірте USB кабель**
- Використайте якісний USB кабель з підтримкою даних (не тільки зарядки)
- Спробуйте інший USB порт
- Спробуйте інший кабель

**2. Перевірте що ESP32 живий**
- Світлодіод має світитись
- Дисплей може показувати щось

**3. Режим завантаження**
Деякі плати потребують ручного входу в режим завантаження:
- Утримуйте кнопку **BOOT** (GPIO 0)
- Натисніть **RESET**
- Відпустіть **RESET**
- Відпустіть **BOOT**

**4. Перезавантажте udev (Linux)**
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

---

## 📊 Системні вимоги

| Компонент | Мінімум | Рекомендовано |
|-----------|---------|---------------|
| Python | 3.6+ | 3.9+ |
| RAM | 2 GB | 4 GB+ |
| Вільне місце | 2 GB | 5 GB+ |
| USB порт | USB 2.0 | USB 3.0 |

---

## 🎓 Корисні посилання

### Офіційна документація
- [PlatformIO Docs](https://docs.platformio.org/)
- [ESP32 Arduino Core](https://docs.espressif.com/projects/arduino-esp32/)
- [LILYGO T-Display](https://github.com/Xinyuan-LilyGO/TTGO-T-Display)

### Драйвери
- [CH341/CH343 Linux Driver](https://github.com/WCHSoftGroup/ch343ser_linux)
- [CH341 Windows Driver](http://www.wch.cn/downloads/CH341SER_EXE.html)

### Спільноти
- [PlatformIO Forum](https://community.platformio.org/)
- [ESP32 Forum](https://www.esp32.com/)
- [LILYGO GitHub](https://github.com/Xinyuan-LilyGO)

---

## ✅ Чеклист після встановлення

- [ ] `pio --version` показує версію
- [ ] `pio device list` показує пристрої
- [ ] ESP32 виявлено в списку пристроїв
- [ ] `pio run` успішно збирає проект
- [ ] `pio run --target upload` завантажує прошивку
- [ ] `pio device monitor` показує логи

**Якщо все ✅ - можна приступати до роботи!**

---

Успішного встановлення! 🚀
