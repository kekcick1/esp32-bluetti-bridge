#include "bluetti_device.h"
#include <cstring>

BluettiDevice* BluettiDevice::instance = nullptr;

// Функція для розрахунку CRC16 MODBUS RTU
static uint16_t calculateCRC16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

BluettiDevice::BluettiDevice(SystemStatus *sharedStatus)
    : client(nullptr), notifyCharacteristic(nullptr),
      writeCharacteristic(nullptr), connected(false), lastRequest(0),
      status(sharedStatus), cachedBattery(0), cachedAcPower(0),
      cachedDcPower(0), cachedInputPower(0), cachedAcState(false),
      cachedDcState(false), connecting(false), connectStartTime(0),
      connectAttempts(0), scanning(false), scanStartTime(0), scanner(nullptr),
      updateInterval(20000), lastRequestedPage(0x00) { // За замовчуванням 20 секунд
  instance = this;
}

bool BluettiDevice::begin() {
  Serial.println("[Bluetti] Initializing BLE...");
  NimBLEDevice::init("ESP32-BLUETTI");
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);
  
  // Не видаляємо bonding - EB3A потребує збереження bonding для стабільного з'єднання
  // NimBLEDevice::deleteAllBonds();
  // delay(100);
  Serial.println("[Bluetti] Keeping existing bonds for EB3A compatibility");
  
  Serial.println("[Bluetti] BLE initialized");
  Serial.println("[Bluetti] ⚠️  IMPORTANT: Bluetti allows only ONE BLE connection at a time!");
  Serial.println("[Bluetti] ⚠️  Make sure:");
  Serial.println("[Bluetti]     1. 'Bluetti to MQTT' addon is STOPPED in Home Assistant");
  Serial.println("[Bluetti]     2. No mobile apps are connected to Bluetti");
  Serial.println("[Bluetti]     3. Bluetti was RESTARTED after disabling addon");
  Serial.println("[Bluetti]     4. Wait 30 seconds after restart before connecting");
  return true;
}

bool BluettiDevice::scanAndConnect(const char *macAddress) {
  if (!macAddress || strlen(macAddress) == 0) {
    return false;
  }

  // Якщо вже скануємо, перевіряємо результати
  if (scanning) {
    if (millis() - scanStartTime > 5000) { // Таймаут сканування 5 секунд
      Serial.println("[Bluetti] Scan timeout, trying direct connection...");
      scanning = false;
      if (scanner) {
        scanner->stop();
        scanner = nullptr;
      }
      // Після сканування спробуємо пряме підключення
      return connectByMAC(macAddress);
    }
    
    // Перевіряємо результати сканування
    if (scanner && scanner->isScanning()) {
      return false; // Ще скануємо
    }
    
    // Сканування завершено, перевіряємо результати
    NimBLEScanResults results = scanner->getResults();
    bool found = false;
    
    for (int i = 0; i < results.getCount(); i++) {
      NimBLEAdvertisedDevice device = results.getDevice(i);
      String deviceAddress = device.getAddress().toString().c_str();
      deviceAddress.toUpperCase();
      
      String targetMac = String(macAddress);
      targetMac.toUpperCase();
      targetMac.replace(":", "");
      deviceAddress.replace(":", "");
      
      if (deviceAddress == targetMac || 
          device.getName().find("EB3A") != std::string::npos ||
          device.getName().find("BLUETTI") != std::string::npos) {
        Serial.printf("[Bluetti] ✅ Found device: %s (%s)\n", 
                      device.getName().c_str(), 
                      device.getAddress().toString().c_str());
        found = true;
        break;
      }
    }
    
    scanning = false;
    scanner = nullptr;
    
    if (found) {
      Serial.println("[Bluetti] Device found, attempting connection...");
      return connectByMAC(macAddress);
    } else {
      Serial.println("[Bluetti] ❌ Device not found in scan");
      Serial.println("[Bluetti] 💡 Make sure:");
      Serial.println("[Bluetti]    1. Bluetti is powered ON");
      Serial.println("[Bluetti]    2. Bluetti is within range (10m)");
      Serial.println("[Bluetti]    3. MAC address is correct");
      return false;
    }
  }
  
  // Починаємо сканування
  Serial.println("[Bluetti] 🔍 Scanning for Bluetti device...");
  scanner = NimBLEDevice::getScan();
  scanner->setActiveScan(true);
  scanner->setInterval(1349);
  scanner->setWindow(449);
  scanner->start(5, false); // Скануємо 5 секунд, не блокуємо
  
  scanning = true;
  scanStartTime = millis();
  return false;
}

bool BluettiDevice::connectByMAC(const char *macAddress) {
  if (!macAddress || strlen(macAddress) == 0) {
    return false;
  }

  // Неблокуюче підключення з агресивними спробами "витіснити" інше підключення
  if (!connecting) {
    // Починаємо підключення
    NimBLEAddress address(macAddress);

    if (!client) {
      client = NimBLEDevice::createClient();
      if (!client) {
        Serial.println("Failed to create NimBLE client");
        connecting = false;
        connectAttempts = 0;
        return false;
      }
    }

    // Відключаємося від попереднього з'єднання, якщо є
    if (client->isConnected()) {
      client->disconnect();
      delay(500); // Даємо час на відключення
    }
    
    // Не видаляємо bonding - EB3A потребує збереження bonding
    // NimBLEDevice::deleteBond(address); // ВИМКНЕНО
    delay(200);

    connectAttempts++;
    Serial.printf("[Bluetti] Connecting to %s... (attempt %d/5)\n", macAddress, connectAttempts);
    
    if (connectAttempts == 1) {
      Serial.println("[Bluetti] ⚠️  Attempting to DISCONNECT 'Bluetti to MQTT' addon...");
      Serial.println("[Bluetti] ⚠️  This may take several attempts if addon is still connected");
    }
    
    // ВАЖЛИВО: Додаємо затримку перед підключенням, щоб уникнути перезавантажень
    yield();
    delay(500);
    
    // Підключення з bonding (як bluetti-mqtt)
    // EB3A вимагає bonding для стабільного з'єднання
    bool connected = client->connect(address, true); // true = використовувати bonding
    
    yield();
    delay(200);
    
    if (!connected) {
      // Помилка підключення - можливо Bluetti зайнятий або вже підключений
      Serial.println("[Bluetti] ⚠️  Connection failed");
      Serial.println("[Bluetti] 💡 Possible causes:");
      Serial.println("[Bluetti]     - Bluetti may be busy or already connected");
      Serial.println("[Bluetti]     - 'Bluetti to MQTT' addon may still be connected");
      Serial.println("[Bluetti]     - Wait 5 seconds and try again...");
      
      if (connectAttempts < 5) {
        Serial.printf("[Bluetti] ⚠️  Connection failed, retrying in 2 seconds... (%d/5)\n", connectAttempts);
        Serial.println("[Bluetti] 💡 This will try to disconnect 'Bluetti to MQTT' addon");
        connecting = false; // Дозволяємо повторну спробу
        yield();
        delay(2000);
        return false;
      } else {
        Serial.printf("[Bluetti] ❌ Connection failed after %d attempts\n", connectAttempts);
        Serial.println("[Bluetti] 💡 Solutions:");
        Serial.println("[Bluetti]    1. STOP 'Bluetti to MQTT' addon in Home Assistant");
        Serial.println("[Bluetti]    2. Wait 30 seconds after stopping addon");
        Serial.println("[Bluetti]    3. Restart Bluetti if possible");
        Serial.println("[Bluetti]    4. Check MAC address");
        connecting = false;
        connectAttempts = 0;
        return false;
      }
    }
    
    // ВАЖЛИВО: Дозволяємо іншим задачам виконуватися (кнопки!)
    yield();
    connecting = true;
    connectStartTime = millis();
    connectAttempts = 0; // Скидаємо лічильник при успішному початку підключення
    return false; // Ще не підключено
  }

  // Перевіряємо статус підключення (неблокуюче)
  if (client->isConnected()) {
    yield(); // Дозволяємо кнопкам працювати
    
    // ВАЖЛИВО: Встановлюємо connection parameters для стабільного з'єднання
    // Це допомагає уникнути розривів з'єднання та покращує стабільність
    Serial.println("[Bluetti] Setting connection parameters...");
    client->updateConnParams(
      6,   // min_interval (7.5ms * 6 = 45ms)
      12,  // max_interval (7.5ms * 12 = 90ms)
      0,   // latency (0 = no latency)
      500  // timeout (10ms * 500 = 5000ms = 5s)
    );
    yield();
    delay(500); // Даємо час на встановлення параметрів
    
    if (!setupCharacteristics()) {
      status->bluettiConnected = false;
      connected = false;
      connecting = false;
      return false;
    }
    yield(); // Дозволяємо кнопкам працювати

    connected = true;
    status->bluettiConnected = true;
    status->lastBluettiUpdate = millis();
    connecting = false;
    Serial.println("Bluetti connected");
    return true;
  }

  // Таймаут підключення (10 секунд) та перевірка помилок
  if (millis() - connectStartTime > 10000) {
    Serial.println("[Bluetti] ❌ Connection timeout (10s)");
    Serial.println("[Bluetti] 💡 Possible causes:");
    Serial.println("[Bluetti]    1. 'Bluetti to MQTT' addon is still connected");
    Serial.println("[Bluetti]    2. Bluetti Bluetooth is OFF");
    Serial.println("[Bluetti]    3. Bluetti is out of range");
    Serial.println("[Bluetti]    4. Wrong MAC address");
    
    // Відключаємо клієнта перед наступною спробою
    if (client) {
      client->disconnect();
    }
    
    status->bluettiConnected = false;
    connected = false;
    connecting = false;
    connectAttempts = 0; // Скидаємо лічильник для нової серії спроб
    return false;
  }
  
  // Перевіряємо, чи є помилка підключення
  if (client && !client->isConnected() && millis() - connectStartTime > 2000) {
    // Якщо пройшло 2 секунди і все ще не підключено, можлива помилка
    // Але даємо ще час (до 10 секунд)
  }

  return false; // Ще підключаємося
}

bool BluettiDevice::setupCharacteristics() {
  Serial.println("[Bluetti] Setting up characteristics...");
  
  // ВАЖЛИВО: Додаємо затримку перед BLE операціями, щоб уникнути перезавантажень
  yield();
  delay(200);
  
  NimBLERemoteService *service = client->getService(BLUETTI_SERVICE_UUID);
  yield(); // Дозволяємо кнопкам працювати
  delay(100);
  
  if (!service) {
    Serial.println("[Bluetti] ERROR: Service missing");
    client->disconnect();
    return false;
  }
  Serial.println("[Bluetti] Service found");

  yield();
  delay(100);
  notifyCharacteristic = service->getCharacteristic(BLUETTI_NOTIFY_UUID);
  yield();
  delay(100);
  writeCharacteristic = service->getCharacteristic(BLUETTI_WRITE_UUID);
  yield(); // Дозволяємо кнопкам працювати
  delay(100);

  if (!notifyCharacteristic || !writeCharacteristic) {
    Serial.println("[Bluetti] ERROR: Characteristics missing");
    Serial.printf("[Bluetti] notifyCharacteristic: %p, writeCharacteristic: %p\n", 
                  notifyCharacteristic, writeCharacteristic);
    client->disconnect();
    return false;
  }
  Serial.println("[Bluetti] Characteristics found");
  Serial.printf("[Bluetti] notifyCharacteristic: %p, writeCharacteristic: %p\n", 
                notifyCharacteristic, writeCharacteristic);

  // ВАЖЛИВО: Спробуємо різні команди активації ПЕРЕД підпискою
  // Деякі пристрої Bluetti потребують активації перед підпискою
  // ВАЖЛИВО: Додаємо більше затримок, щоб уникнути перезавантаження ESP32
  if (writeCharacteristic) {
    Serial.println("[Bluetti] Sending activation commands BEFORE subscription...");
    
    // ВАЖЛИВО: Додаємо велику затримку перед командою активації, щоб уникнути перезавантажень
    // ESP32 може перезавантажуватися, якщо BLE операції виконуються занадто швидко
    yield();
    delay(500); // Збільшена затримка перед командою
    
    // ВАЖЛИВО: Команда активації для Bluetti (використовується bluetti_mqtt)
    // Ця команда "пробуджує" Bluetti і дозволяє йому приймати команди
    static const uint8_t activateCmd1[] = {0xAA, 0x55, 0x90, 0xEB};
    Serial.println("[Bluetti] Sending activation command (0xAA 0x55 0x90 0xEB)...");
    Serial.println("[Bluetti] This command is used by bluetti_mqtt to wake up the device");
    
    // ВАЖЛИВО: Додаємо yield() перед BLE операціями, щоб уникнути перезавантажень
    yield();
    delay(200); // Збільшена затримка
    writeCharacteristic->writeValue(activateCmd1, sizeof(activateCmd1), false);
    Serial.println("[Bluetti] Activation command sent (write-without-response)");
    yield();
    delay(500); // Збільшена затримка після команди
    delay(2000); // Даємо час Bluetti на обробку
    
    // Відправляємо ще раз для надійності (як у bluetti_mqtt)
    yield();
    delay(500); // Збільшена затримка перед другою командою
    writeCharacteristic->writeValue(activateCmd1, sizeof(activateCmd1), false);
    Serial.println("[Bluetti] Activation command sent again");
    yield();
    delay(500); // Збільшена затримка після команди
    delay(2000); // Збільшена затримка для активації
  }

  if (notifyCharacteristic->canNotify()) {
    Serial.println("[Bluetti] Subscribing to notifications...");
    Serial.printf("[Bluetti] Instance pointer: %p\n", instance);
    Serial.printf("[Bluetti] notifyCharacteristic pointer: %p\n", notifyCharacteristic);
    Serial.printf("[Bluetti] notificationThunk function pointer: %p\n", (void*)notificationThunk);
    
    // ВАЖЛИВО: Додаємо затримку перед BLE операціями, щоб уникнути перезавантажень
    yield();
    delay(200);
    
    // ВАЖЛИВО: Перевіряємо, чи instance встановлено
    if (!instance) {
      Serial.println("[Bluetti] ERROR: Instance is NULL! Cannot subscribe!");
      return false;
    }
    
    // ВАЖЛИВО: Спробуємо встановити descriptor ПЕРЕД підпискою
    Serial.println("[Bluetti] Setting descriptor BEFORE subscription...");
    yield();
    delay(100);
    NimBLERemoteDescriptor* descriptor = notifyCharacteristic->getDescriptor(NimBLEUUID((uint16_t)0x2902));
    if (descriptor) {
      Serial.println("[Bluetti] Found descriptor 0x2902, setting value to enable notifications...");
      uint8_t notifyValue[] = {0x01, 0x00}; // Enable notifications
      bool descSet = descriptor->writeValue(notifyValue, sizeof(notifyValue), true);
      Serial.printf("[Bluetti] Descriptor set (before subscribe): %s\n", descSet ? "OK" : "FAILED");
      yield();
      delay(500);
      
      // ВАЖЛИВО: Перевіряємо значення descriptor після встановлення
      Serial.println("[Bluetti] Verifying descriptor value...");
      try {
        std::string descValue = descriptor->readValue();
        Serial.printf("[Bluetti] Descriptor value after set: ");
        for (size_t i = 0; i < descValue.length(); i++) {
          Serial.printf("%02X ", (uint8_t)descValue[i]);
        }
        Serial.println();
        if (descValue.length() >= 2 && descValue[0] == 0x01 && descValue[1] == 0x00) {
          Serial.println("[Bluetti] Descriptor verified: notifications ENABLED");
        } else {
          Serial.println("[Bluetti] WARNING: Descriptor value mismatch!");
        }
      } catch (...) {
        Serial.println("[Bluetti] Could not read descriptor value (may be normal)");
      }
      yield();
      delay(500);
    } else {
      Serial.println("[Bluetti] Descriptor 0x2902 not found (will be set by subscribe)");
    }
    
    // ВАЖЛИВО: Спробуємо підписатися на notifications
    // Деякі пристрої Bluetti використовують indications, але спочатку спробуємо notifications
    bool subscribed = false;
    
    // ВАЖЛИВО: Спочатку спробуємо notifications (більшість пристроїв використовують notifications)
    Serial.println("[Bluetti] Subscribing to notifications...");
    Serial.printf("[Bluetti] canNotify: %d, canIndicate: %d\n", 
                  notifyCharacteristic->canNotify(), 
                  notifyCharacteristic->canIndicate());
    subscribed = notifyCharacteristic->subscribe(true, notificationThunk); // true = notifications
    Serial.printf("[Bluetti] subscribe() (notifications) returned: %s\n", subscribed ? "true" : "false");
    
    // Якщо notifications не спрацювали, спробуємо indications
    if (!subscribed && notifyCharacteristic->canIndicate()) {
      Serial.println("[Bluetti] Notifications failed, trying indications...");
      subscribed = notifyCharacteristic->subscribe(false, notificationThunk); // false = indications
      Serial.printf("[Bluetti] subscribe() (indications) returned: %s\n", subscribed ? "true" : "false");
    }
    
    // Додаткова перевірка після підписки
    if (subscribed) {
      delay(1000);
      // Перевіряємо descriptor після підписки
      NimBLERemoteDescriptor* desc = notifyCharacteristic->getDescriptor(NimBLEUUID((uint16_t)0x2902));
      if (desc) {
        try {
          std::string descVal = desc->readValue();
          Serial.printf("[Bluetti] Descriptor after subscribe: ");
          for (size_t i = 0; i < descVal.length(); i++) {
            Serial.printf("%02X ", (uint8_t)descVal[i]);
          }
          Serial.println();
        } catch (...) {
          Serial.println("[Bluetti] Could not read descriptor after subscribe");
        }
      }
    }
    
    // ВАЖЛИВО: Перевіряємо, чи callback функція правильно зареєстрована
    Serial.printf("[Bluetti] Callback function pointer: %p\n", (void*)notificationThunk);
    Serial.printf("[Bluetti] Instance pointer: %p\n", instance);
    
    yield();
    delay(1000); // Затримка для встановлення підписки
    
    if (subscribed) {
      Serial.println("[Bluetti] Successfully subscribed to notifications/indications");
      Serial.println("[Bluetti] Callback should be called when Bluetti sends data");
      
      yield();
      delay(1000); // Затримка для встановлення підписки
      
      Serial.println("[Bluetti] Successfully subscribed to notifications");
      Serial.println("[Bluetti] Status requests will be sent from loop()");
    } else {
      Serial.println("[Bluetti] WARNING: Failed to subscribe to notifications!");
      Serial.println("[Bluetti] This may indicate that Bluetti Bluetooth is turned off");
    }
  } else {
    Serial.println("[Bluetti] WARNING: Characteristic cannot notify!");
    Serial.println("[Bluetti] This may indicate that Bluetti Bluetooth is turned off");
  }
  yield();

  // Відправляємо перший запит ВІДРАЗУ після підключення
  Serial.println("[Bluetti] Sending initial status request...");
  requestStatus();
  
  Serial.println("[Bluetti] Setup complete - status will be requested in loop()");
  return true;
}

void BluettiDevice::disconnect() {
  if (client && client->isConnected()) {
    client->disconnect();
  }
  connected = false;
  status->bluettiConnected = false;
}

bool BluettiDevice::isConnected() const {
  return connected && client && client->isConnected();
}

void BluettiDevice::loop() {
  // Перевіряємо підключення через client
  if (!client || !client->isConnected()) {
    connected = false;
    status->bluettiConnected = false;
    // Очищаємо характеристики при відключенні
    notifyCharacteristic = nullptr;
    writeCharacteristic = nullptr;
    return;
  }
  
  // Встановлюємо connected = true, якщо підключення є
  if (!connected && client && client->isConnected()) {
    Serial.println("[Bluetti] Connection confirmed in loop(), setting up characteristics...");
    
    // ВАЖЛИВО: Встановлюємо connection parameters для стабільного з'єднання
    // Це допомагає уникнути розривів з'єднання та покращує стабільність
    Serial.println("[Bluetti] Setting connection parameters...");
    client->updateConnParams(
      6,   // min_interval (7.5ms * 6 = 45ms)
      12,  // max_interval (7.5ms * 12 = 90ms)
      0,   // latency (0 = no latency)
      500  // timeout (10ms * 500 = 5000ms = 5s)
    );
    yield();
    delay(500); // Даємо час на встановлення параметрів
    
    if (setupCharacteristics()) {
      connected = true;
      status->bluettiConnected = true;
      status->lastBluettiUpdate = millis();
      Serial.println("[Bluetti] Setup complete, connected = true");
    } else {
      Serial.println("[Bluetti] ERROR: Failed to setup characteristics in loop()");
      connected = false;
      status->bluettiConnected = false;
    }
  }
  
  // Перевіряємо характеристики - якщо втрачені, отримуємо їх знову
  if (connected && (!writeCharacteristic || !notifyCharacteristic)) {
    Serial.println("[Bluetti] Characteristics lost, reacquiring...");
    NimBLERemoteService *service = client->getService(BLUETTI_SERVICE_UUID);
    if (service) {
      if (!notifyCharacteristic) {
        notifyCharacteristic = service->getCharacteristic(BLUETTI_NOTIFY_UUID);
        if (notifyCharacteristic && notifyCharacteristic->canNotify()) {
          Serial.println("[Bluetti] Re-subscribing to notifications...");
          bool subscribed = notifyCharacteristic->subscribe(true, notificationThunk);
          Serial.printf("[Bluetti] Re-subscribe result: %s\n", subscribed ? "OK" : "FAILED");
          if (subscribed) {
            Serial.println("[Bluetti] Notify characteristic reacquired and subscribed");
          }
        }
      }
      if (!writeCharacteristic) {
        writeCharacteristic = service->getCharacteristic(BLUETTI_WRITE_UUID);
        if (writeCharacteristic) {
          Serial.println("[Bluetti] Write characteristic reacquired");
        }
      }
      yield();
    } else {
      Serial.println("[Bluetti] ERROR: Service not available for reacquiring");
    }
  }

  // Запитуємо статус з налаштованим інтервалом
  if (millis() - lastRequest > updateInterval) {
    requestStatus();
    delay(500); // Даємо час на отримання відповіді
    requestChargingMode(); // Читаємо поточний режим зарядки
    delay(300);
    pollFeatureState(); // Опитуємо додаткові функції (ротація)
  }
  
  // ВАЖЛИВО: Якщо не отримуємо дані більше 10 секунд, спробуємо перепідключитися
  // Це може допомогти, якщо Bluetti Bluetooth вимкнено
  static unsigned long lastDataReceived = 0;
  if (connected && status->lastBluettiUpdate > 0) {
    lastDataReceived = status->lastBluettiUpdate;
  }
  
  // Використовуємо updateInterval + 10 секунд як таймаут
  unsigned long timeout = updateInterval + 10000;
  if (connected && lastDataReceived > 0 && millis() - lastDataReceived > timeout) {
    Serial.printf("[Bluetti] WARNING: No data received for %lu seconds!\n", timeout / 1000);
    Serial.println("[Bluetti] Bluetti Bluetooth may be turned off - trying to reactivate...");
    
    // Спробуємо відправити команду активації знову
    if (writeCharacteristic) {
      static const uint8_t activateCmd[] = {0xAA, 0x55, 0x90, 0xEB};
      writeCharacteristic->writeValue(activateCmd, sizeof(activateCmd), false);
      yield();
      delay(500);
      
      // ВАЖЛИВО: Використовуємо write-without-response (як bluetti_mqtt)
      static const uint8_t statusCmd[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x10};
      writeCharacteristic->writeValue(statusCmd, sizeof(statusCmd), false);
      yield();
    }
    
    lastDataReceived = millis(); // Оновлюємо, щоб не повторювати занадто часто
  }
}

void BluettiDevice::requestStatus() {
  // Перевіряємо підключення через client
  if (!client || !client->isConnected()) {
    Serial.println("[Bluetti] requestStatus: Not connected (client check)");
    return;
  }
  
  // Якщо writeCharacteristic втрачено, спробуємо отримати його знову
  if (!writeCharacteristic) {
    Serial.println("[Bluetti] Write characteristic lost, reacquiring...");
    NimBLERemoteService *service = client->getService(BLUETTI_SERVICE_UUID);
    if (service) {
      writeCharacteristic = service->getCharacteristic(BLUETTI_WRITE_UUID);
      if (writeCharacteristic) {
        Serial.println("[Bluetti] Write characteristic reacquired");
      } else {
        Serial.println("[Bluetti] ERROR: Failed to reacquire write characteristic");
        return;
      }
    } else {
      Serial.println("[Bluetti] ERROR: Service not available");
      return;
    }
  }
  
  // EB3A підтримує тільки page 0x00 (Core registers)
  // Page 0x0B не підтримується (повертає MODBUS Exception 0x02)
  
  uint8_t cmd[8];
  cmd[0] = 0x01; // Device ID
  cmd[1] = 0x03; // Function code (Read Holding Registers)
  cmd[2] = 0x00; // Start address high
  cmd[3] = 0x0A; // Start address low (0x000A)
  cmd[4] = 0x00; // Quantity high
  cmd[5] = 0x28; // Quantity low (40 registers)
  lastRequestedPage = 0x00;
  
  // Розраховуємо CRC16
  uint16_t crc = calculateCRC16(cmd, 6);
  cmd[6] = crc & 0xFF;        // CRC low byte
  cmd[7] = (crc >> 8) & 0xFF; // CRC high byte
  
  uint16_t startAddr = (cmd[2] << 8) | cmd[3];
  Serial.printf("[Bluetti] Sending status request (40 regs from 0x%04X) WITH CRC: ", startAddr);
  for (size_t i = 0; i < sizeof(cmd); i++) {
    Serial.printf("%02X ", cmd[i]);
  }
  Serial.println();
  
  // ВАЖЛИВО: EB3A вимагає write-without-response (як bluetti_mqtt)
  Serial.println("[Bluetti] Sending command (write-without-response)...");
  bool sent = writeCharacteristic->writeValue(cmd, sizeof(cmd), false); // false = without response
  Serial.printf("[Bluetti] Command sent: %s\n", sent ? "OK" : "FAILED");
  
  if (sent) {
    lastRequest = millis();
    Serial.println("[Bluetti] Waiting for response...");
    
    // ВАЖЛИВО: Чекаємо на відповідь через callback (notifications)
    // Але також спробуємо прочитати response з write characteristic
    unsigned long waitStart = millis();
    bool callbackReceived = false;
    
    // Чекаємо до 2 секунд на callback
    while (millis() - waitStart < 2000) {
      yield(); // Дозволяємо callback виконатися
      delay(50); // Невелика затримка
      
      // Перевіряємо, чи callback викликався
      if (status->lastBluettiUpdate > lastRequest) {
        callbackReceived = true;
        Serial.println("[Bluetti] ✅ Callback received (data updated)!");
        break;
      }
    }
    
    if (!callbackReceived) {
      Serial.println("[Bluetti] ⚠️  No callback received, trying alternative methods...");
      Serial.println("[Bluetti] 💡 EB3A may not send data automatically through notifications");
      Serial.println("[Bluetti] 💡 Trying to read data directly after command...");
      
      // ВАЖЛИВО: EB3A може не відправляти дані через notifications автоматично
      // Спробуємо прочитати дані безпосередньо після команди
      // Деякі Bluetti пристрої вимагають polling замість notifications
      
      // ВАЖЛИВО: Notify characteristic зазвичай не підтримує read (rc=258 - це нормально)
      // Дані мають надходити через notifications callback, а не через read
      Serial.println("[Bluetti] Method 1: Notify characteristic typically doesn't support read");
      Serial.println("[Bluetti] 💡 Data should come through notifications callback, not read");
      Serial.println("[Bluetti] 💡 If callback is not called, EB3A may not send data automatically");
      
      // Спробуємо ще раз прочитати, але очікуємо помилку
      if (notifyCharacteristic) {
        Serial.println("[Bluetti] Attempting read anyway (will likely fail with rc=258)...");
        yield();
        delay(500);
        try {
          std::string value = notifyCharacteristic->readValue();
          if (value.length() > 0) {
            Serial.printf("[Bluetti] ✅ Unexpectedly read %d bytes from notify characteristic!\n", value.length());
            Serial.print("[Bluetti] Data: ");
            for (size_t i = 0; i < value.length() && i < 67; i++) {
              Serial.printf("%02X ", (uint8_t)value[i]);
            }
            Serial.println();
            handleNotification((uint8_t*)value.data(), value.length());
            return;
          }
        } catch (...) {
          Serial.println("[Bluetti] Read failed (expected - notify characteristic doesn't support read)");
        }
      }
      
      // МЕТОД 2: Спробуємо прочитати response з write characteristic
      Serial.println("[Bluetti] Method 2: Checking write characteristic response...");
      if (writeCharacteristic && writeCharacteristic->canRead()) {
        Serial.println("[Bluetti] Write characteristic supports read, trying...");
        delay(500);
        try {
          std::string value = writeCharacteristic->readValue();
          if (value.length() > 0) {
            Serial.printf("[Bluetti] ✅ Read %d bytes from write characteristic!\n", value.length());
            Serial.print("[Bluetti] Data: ");
            for (size_t i = 0; i < value.length() && i < 67; i++) {
              Serial.printf("%02X ", (uint8_t)value[i]);
            }
            Serial.println();
            handleNotification((uint8_t*)value.data(), value.length());
            return;
          }
        } catch (...) {
          Serial.println("[Bluetti] Write characteristic read failed");
        }
      }
      
      Serial.println("[Bluetti] ❌ All methods failed - EB3A may not support data reading");
      Serial.println("[Bluetti] 💡 Possible reasons:");
      Serial.println("[Bluetti]     1. EB3A Bluetooth is turned off (auto-off after ~1h)");
      Serial.println("[Bluetti]     2. EB3A requires different command format");
      Serial.println("[Bluetti]     3. EB3A doesn't send data through BLE notifications");
    }
    
    // Перевіряємо, чи callback викликався
    static unsigned long lastCallbackCheck = 0;
    if (millis() - lastCallbackCheck > 5000) {
      lastCallbackCheck = millis();
      Serial.println("[Bluetti] DIAGNOSTIC: No callback received in last 5 seconds");
      Serial.println("[Bluetti] Trying alternative methods...");
    }
  }
}

bool BluettiDevice::sendCommand(const uint8_t *data, size_t length) {
  if (!writeCharacteristic || !isConnected()) {
    return false;
  }
  // Використовуємо write-without-response (як bluetti_mqtt)
  return writeCharacteristic->writeValue(data, length, false);
}

bool BluettiDevice::setACOutput(bool state) {
  // Команда: 0x01 0x06 0x0BBF VALUE CRC16 (Write Single Register)
  uint8_t cmd[8];
  cmd[0] = 0x01; // Device ID
  cmd[1] = 0x06; // Function code (Write Single Register)
  cmd[2] = 0x0B; // Register address high
  cmd[3] = 0xBF; // Register address low (0x0BBF = AC output)
  cmd[4] = 0x00; // Value high
  cmd[5] = state ? 0x01 : 0x00; // Value low (0=OFF, 1=ON)
  
  // Розраховуємо CRC16
  uint16_t crc = calculateCRC16(cmd, 6);
  cmd[6] = crc & 0xFF;        // CRC low byte
  cmd[7] = (crc >> 8) & 0xFF; // CRC high byte
  
  lastWriteRegister = 0x0BBF;
  bool ok = sendCommand(cmd, sizeof(cmd));
  if (ok) {
    cachedAcState = state;
    status->acOutputState = state;
  }
  return ok;
}

bool BluettiDevice::setDCOutput(bool state) {
  // Команда: 0x01 0x06 0x0BC0 VALUE CRC16 (Write Single Register)
  uint8_t cmd[8];
  cmd[0] = 0x01; // Device ID
  cmd[1] = 0x06; // Function code (Write Single Register)
  cmd[2] = 0x0B; // Register address high
  cmd[3] = 0xC0; // Register address low (0x0BC0 = DC output)
  cmd[4] = 0x00; // Value high
  cmd[5] = state ? 0x01 : 0x00; // Value low (0=OFF, 1=ON)
  
  // Розраховуємо CRC16
  uint16_t crc = calculateCRC16(cmd, 6);
  cmd[6] = crc & 0xFF;        // CRC low byte
  cmd[7] = (crc >> 8) & 0xFF; // CRC high byte
  
  lastWriteRegister = 0x0BC0;
  bool ok = sendCommand(cmd, sizeof(cmd));
  if (ok) {
    cachedDcState = state;
    status->dcOutputState = state;
  }
  return ok;
}

bool BluettiDevice::setChargingSpeed(uint8_t speed) {
  // EB3A Charging Mode:
  // 0 = Standard (268W), 1 = Silent (100W), 2 = Turbo (350W)
  
  if (speed > 2) {
    Serial.println("[Bluetti] ERROR: Invalid charging speed");
    return false;
  }
  
  const char* modeNames[] = {"Standard", "Silent", "Turbo"};
  const uint16_t powerWatts[] = {268, 100, 350};
  
  Serial.printf("[Bluetti] 🔋 Setting charging mode: %s (%dW)\n", modeNames[speed], powerWatts[speed]);
  
  // ПРАВИЛЬНИЙ регістр для EB3A: 0x0BF9 (3065 decimal) = charging_mode
  // НЕ 0x0BBF (3007 = AC output) і НЕ 0x0BC0 (3008 = DC output)!
  const uint16_t CHARGING_MODE_REGISTER = 0x0BF9;
  
  uint8_t cmd[8];
  cmd[0] = 0x01; // Device ID
  cmd[1] = 0x06; // Function: Write Single Register
  cmd[2] = (CHARGING_MODE_REGISTER >> 8) & 0xFF;
  cmd[3] = CHARGING_MODE_REGISTER & 0xFF;
  cmd[4] = 0x00; // Value High
  cmd[5] = speed; // Value Low: 0, 1, or 2
  
  uint16_t crc = calculateCRC16(cmd, 6);
  cmd[6] = crc & 0xFF;
  cmd[7] = (crc >> 8) & 0xFF;
  
  lastWriteRegister = CHARGING_MODE_REGISTER;
  Serial.printf("[Bluetti] Writing 0x%04X (charging_mode) = %d... ", CHARGING_MODE_REGISTER, speed);
  bool success = sendCommand(cmd, sizeof(cmd));
  Serial.println(success ? "✅" : "❌");
  
  if (success) {
    status->chargingSpeed = speed;
    Serial.printf("[Bluetti] ✅ Charging mode: %s (%dW)\n", modeNames[speed], powerWatts[speed]);
    
    // Запитуємо поточний режим зарядки для підтвердження
    delay(500);
    requestChargingMode();
  }
  
  return success;
}

bool BluettiDevice::setEcoMode(bool state) {
  if (ecoWriteBlocked) {
    Serial.println("[Bluetti] ECO write skipped: device rejected ECO register earlier");
    return false;
  }
  constexpr uint16_t ECO_MODE_REGISTER = 0x0BF7; // 3063 decimal (eco_on)
  return writeSingleRegister(ECO_MODE_REGISTER, state ? 1 : 0);
}

bool BluettiDevice::setPowerLifting(bool state) {
  constexpr uint16_t POWER_LIFTING_REGISTER = 0x0BFA; // 3066 decimal (power_lifting_on)
  return writeSingleRegister(POWER_LIFTING_REGISTER, state ? 1 : 0);
}

bool BluettiDevice::setLedMode(uint8_t mode) {
  // 1=Low, 2=High, 3=SOS, 4=Off
  if (mode < 1 || mode > 4) {
    Serial.println("[Bluetti] ERROR: Invalid LED mode (1-4)");
    return false;
  }
  constexpr uint16_t LED_MODE_REGISTER = 0x0BDA; // 3034 decimal (led_mode)
  return writeSingleRegister(LED_MODE_REGISTER, mode);
}

bool BluettiDevice::setEcoShutdown(uint8_t hours) {
  // 1-4 години
  if (hours < 1 || hours > 4) {
    Serial.println("[Bluetti] ERROR: Invalid ECO shutdown hours (1-4)");
    return false;
  }
  constexpr uint16_t ECO_SHUTDOWN_REGISTER = 0x0BF8; // 3064 decimal (eco_shutdown)
  return writeSingleRegister(ECO_SHUTDOWN_REGISTER, hours);
}

bool BluettiDevice::powerOff() {
  constexpr uint16_t POWER_OFF_REGISTER = 0x0BF4; // 3060 decimal (power_off)
  return writeSingleRegister(POWER_OFF_REGISTER, 1);
}

uint8_t BluettiDevice::getBatteryLevel() const { return cachedBattery; }

int BluettiDevice::getACOutputPower() const { return cachedAcPower; }

int BluettiDevice::getDCOutputPower() const { return cachedDcPower; }

bool BluettiDevice::getACOutputState() const { return cachedAcState; }

bool BluettiDevice::getDCOutputState() const { return cachedDcState; }

int BluettiDevice::getInputPower() const { return cachedInputPower; }

float BluettiDevice::getTemperature() const { 
  // Температура зберігається в форматі ×10 (наприклад 250 = 25.0°C)
  return status->temperature / 10.0f; 
}

float BluettiDevice::getBatteryVoltage() const { 
  // Напруга зберігається в форматі ×10 (наприклад 537 = 53.7V)
  return status->batteryVoltage / 10.0f; 
}

uint8_t BluettiDevice::getChargingSpeed() const {
  return status->chargingSpeed;
}

bool BluettiDevice::getEcoMode() const {
  return status->ecoMode;
}

bool BluettiDevice::getPowerLifting() const {
  return status->powerLifting;
}

uint8_t BluettiDevice::getLedMode() const {
  return status->ledMode;
}

uint8_t BluettiDevice::getEcoShutdown() const {
  return status->ecoShutdown;
}

bool BluettiDevice::writeSingleRegister(uint16_t reg, uint16_t value) {
  if (!connected || !client || !client->isConnected() || !writeCharacteristic) {
    return false;
  }
  
  uint8_t cmd[8];
  cmd[0] = 0x01; // Device ID
  cmd[1] = 0x06; // Function: Write Single Register
  cmd[2] = (reg >> 8) & 0xFF;
  cmd[3] = reg & 0xFF;
  cmd[4] = (value >> 8) & 0xFF;
  cmd[5] = value & 0xFF;
  
  uint16_t crc = calculateCRC16(cmd, 6);
  cmd[6] = crc & 0xFF;
  cmd[7] = (crc >> 8) & 0xFF;
  
  lastWriteRegister = reg;
  Serial.printf("[Bluetti] Write reg 0x%04X = %d... ", reg, value);
  bool success = sendCommand(cmd, sizeof(cmd));
  Serial.println(success ? "✅" : "❌");
  return success;
}

void BluettiDevice::requestRegister(uint16_t reg) {
  if (!connected || !client || !client->isConnected() || !writeCharacteristic) {
    return;
  }
  
  uint8_t cmd[8];
  cmd[0] = 0x01; // Device ID
  cmd[1] = 0x03; // Function: Read Holding Registers
  cmd[2] = (reg >> 8) & 0xFF;
  cmd[3] = reg & 0xFF;
  cmd[4] = 0x00; // Quantity High
  cmd[5] = 0x01; // Quantity Low (1 register)
  
  uint16_t crc = calculateCRC16(cmd, 6);
  cmd[6] = crc & 0xFF;
  cmd[7] = (crc >> 8) & 0xFF;
  
  sendCommand(cmd, sizeof(cmd));
  lastSingleRegisterRequested = reg;
}

void BluettiDevice::pollFeatureState() {
  // Ротація опитування додаткових функцій (не критичні, опитуємо рідко)
  switch (featurePollIndex % 4) {
    case 0: requestRegister(0x0BF7); break; // ECO Mode
    case 1: requestRegister(0x0BFA); break; // Power Lifting
    case 2: requestRegister(0x0BDA); break; // LED Mode
    case 3: requestRegister(0x0BF8); break; // ECO Shutdown
  }
  featurePollIndex++;
}

void BluettiDevice::requestChargingMode() {
  // Читання поточного режиму зарядки з регістра 0x0BF9 (3065 decimal)
  if (!connected || !client || !client->isConnected() || !writeCharacteristic) {
    return;
  }
  
  const uint16_t CHARGING_MODE_REGISTER = 0x0BF9;
  
  uint8_t cmd[8];
  cmd[0] = 0x01; // Device ID
  cmd[1] = 0x03; // Function code: Read Holding Registers
  cmd[2] = (CHARGING_MODE_REGISTER >> 8) & 0xFF;
  cmd[3] = CHARGING_MODE_REGISTER & 0xFF;
  cmd[4] = 0x00; // Quantity High
  cmd[5] = 0x01; // Quantity Low (1 register)
  
  uint16_t crc = calculateCRC16(cmd, 6);
  cmd[6] = crc & 0xFF;
  cmd[7] = (crc >> 8) & 0xFF;
  
  Serial.print("[Bluetti] Requesting charging mode from 0x0BF9... ");
  if (sendCommand(cmd, sizeof(cmd))) {
    Serial.println("✅");
  } else {
    Serial.println("❌");
  }
}

void BluettiDevice::handleNotification(uint8_t *data, size_t length) {
  // ВАЖЛИВО: Ця функція викликається коли Bluetti відправляє дані через notifications
  // Якщо ця функція ніколи не викликається, це означає що Bluetti не відправляє дані
  Serial.println("\n\n\n[Bluetti] ========================================");
  Serial.println("[Bluetti] *** NOTIFICATION RECEIVED ***");
  Serial.printf("[Bluetti] Length: %d bytes\n", length);
  Serial.print("[Bluetti] Data: ");
  for (size_t i = 0; i < length && i < 67; i++) {
    Serial.printf("%02X ", data[i]);
    if ((i + 1) % 16 == 0) Serial.println("    ");
  }
  Serial.println();
  Serial.println("[Bluetti] ========================================\n\n\n");

  // Перевіряємо заголовок
  if (length < 5) {
    Serial.printf("[Bluetti] WARNING: Notification too short: length=%d\n", length);
    return;
  }
  
  // Перевіряємо на помилку MODBUS (0x83 = 0x03 + 0x80, 0x86 = 0x06 + 0x80)
  if (data[0] == 0x01 && (data[1] == 0x83 || data[1] == 0x86)) {
    uint8_t exceptionCode = (length >= 3) ? data[2] : 0;
    Serial.printf("[Bluetti] ERROR: MODBUS Exception received (code 0x%02X): ", data[1]);
    for (size_t i = 0; i < length && i < 10; i++) {
      Serial.printf("%02X ", data[i]);
    }
    Serial.println();
    Serial.printf("[Bluetti] Exception code: 0x%02X\n", exceptionCode);
    Serial.println("[Bluetti] 💡 This usually means:");
    Serial.println("[Bluetti]    1. Register address is invalid or not supported");
    Serial.println("[Bluetti]    2. Device doesn't support this function");
    Serial.println("[Bluetti]    3. Register is read-only");
    if (data[1] == 0x86) {
      Serial.printf("[Bluetti] Last write register: 0x%04X\n", lastWriteRegister);
      switch (lastWriteRegister) {
        case 0x0BF9: Serial.println("[Bluetti] ⚠️  Charging speed may not be supported on this device"); break;
        case 0x0BF7:
          Serial.println("[Bluetti] ⚠️  ECO mode register not supported (will skip further ECO writes)");
          ecoWriteBlocked = true;
          break;
        case 0x0BFA: Serial.println("[Bluetti] ⚠️  Power Lifting register not supported (ignored)"); break;
        case 0x0BDA: Serial.println("[Bluetti] ⚠️  LED mode register not supported (ignored)"); break;
        case 0x0BF8: Serial.println("[Bluetti] ⚠️  ECO shutdown register not supported (ignored)"); break;
        case 0x0BBF: Serial.println("[Bluetti] ⚠️  AC output write rejected"); break;
        case 0x0BC0: Serial.println("[Bluetti] ⚠️  DC output write rejected"); break;
        default:     Serial.println("[Bluetti] ⚠️  Write rejected by device"); break;
      }
    }
    return;
  }
  
  // 0x06 = write single register response (OK), 0x03 = read response
  if (data[0] == 0x01 && data[1] == 0x06) {
    Serial.println("[Bluetti] ✅ Write command acknowledged");
    return;
  }
  
  if (data[0] != 0x01 || data[1] != 0x03) {
    Serial.printf("[Bluetti] WARNING: Unexpected header: data[0]=%02X, data[1]=%02X (expected 01 03 or 01 06)\n", data[0], data[1]);
    Serial.printf("[Bluetti] Full response: ");
    for (size_t i = 0; i < length && i < 20; i++) {
      Serial.printf("%02X ", data[i]);
    }
    Serial.println();
    return;
  }

  uint8_t dataLength = data[2];
  if (length < 3 + dataLength) {
    Serial.printf("[Bluetti] Invalid length: expected %d, got %d\n", 3 + dataLength, length);
    return;
  }

  // Перевірка: чи це відповідь на запит окремого регістра? (1 регістр = 2 байти)
  if (dataLength == 2 && length == 7) {
    uint16_t valueRaw = (data[3] << 8) | data[4];
    
    // Визначаємо за останнім запитаним регістром
    if (lastSingleRegisterRequested == 0x0BF9) {
      // Charging mode
      if (valueRaw <= 2) {
        status->chargingSpeed = (uint8_t)valueRaw;
        const char* modeNames[] = {"Standard", "Silent", "Turbo"};
        Serial.printf("[Bluetti] 🔋 Charging mode: %s (%d)\n", modeNames[status->chargingSpeed], status->chargingSpeed);
      }
    } else if (lastSingleRegisterRequested == 0x0BF7) {
      // ECO Mode
      status->ecoMode = (valueRaw == 1);
      Serial.printf("[Bluetti] 🌿 ECO mode: %s\n", status->ecoMode ? "ON" : "OFF");
    } else if (lastSingleRegisterRequested == 0x0BFA) {
      // Power Lifting
      status->powerLifting = (valueRaw == 1);
      Serial.printf("[Bluetti] ⚡ Power Lifting: %s\n", status->powerLifting ? "ON" : "OFF");
    } else if (lastSingleRegisterRequested == 0x0BDA) {
      // LED Mode
      if (valueRaw >= 1 && valueRaw <= 4) {
        status->ledMode = (uint8_t)valueRaw;
        const char* ledNames[] = {"", "Low", "High", "SOS", "Off"};
        Serial.printf("[Bluetti] 💡 LED mode: %s (%d)\n", ledNames[valueRaw], status->ledMode);
      }
    } else if (lastSingleRegisterRequested == 0x0BF8) {
      // ECO Shutdown
      if (valueRaw >= 1 && valueRaw <= 4) {
        status->ecoShutdown = (uint8_t)valueRaw;
        Serial.printf("[Bluetti] ⏰ ECO shutdown: %dh\n", status->ecoShutdown);
      }
    }
    return;
  }

  // EB3A MODBUS структура від адреси 0x000A (40 registers), response starts at offset 3
  // Зберігаємо всі регістри для аналізу
  for (int i = 0; i < 40 && (3 + i*2 + 1) < (int)length; i++) {
    status->registers[i] = (data[3 + i*2] << 8) | data[3 + i*2 + 1];
  }
  
  Serial.println("\n[Bluetti] ========================================");
  Serial.println("[Bluetti] === ВСІ ДАНІ BLUETTI ===");
  Serial.println("[Bluetti] ========================================");
  
  // ✅ CORRECT REGISTER MAPPING (from giovanne123/EB3A_Bluetti_ESP32_HA):
  // Reading from address 0x000A (40 registers), response starts at offset 3
  // Offset розрахунок: 3 + (register - 0x0A) × 2
  // Battery:   0x2B = 3 + (0x2B-0x0A)×2 = 3 + 33×2 = 69 → raw/10
  // AC Power:  0x26 = 3 + (0x26-0x0A)×2 = 3 + 28×2 = 59 → Watts
  // DC Power:  0x27 = 3 + (0x27-0x0A)×2 = 3 + 29×2 = 61 → Watts
  // AC State:  0x30 = 3 + (0x30-0x0A)×2 = 3 + 38×2 = 79 → 0=OFF, 1=ON
  // DC State:  0x31 = 3 + (0x31-0x0A)×2 = 3 + 39×2 = 81 → 0=OFF, 1=ON
  
  // 1. Модель пристрою (регістри 0x000A-0x000B)
  // Регістр 0x000A = offset 3 + (0x0A - 0x0A) * 2 = 3 (байти 3-4)
  // Регістр 0x000B = offset 3 + (0x0B - 0x0A) * 2 = 5 (байти 5-6)
  // Але згідно документації "EB3A" на offsets 11-14, що відповідає регістрам 0x000A-0x000B
  // Тобто: data[11]=0x45, data[12]=0x42, data[13]=0x33, data[14]=0x41
  // Але це означає, що регістр 0x000A знаходиться на offset 11-12
  // Перевіримо обидва варіанти
  if (length >= 15) {
    // Спробуємо offsets 11-14 (згідно документації)
    if (data[11] == 0x45 && data[12] == 0x42) {
      status->modelName[0] = (char)data[11];
      status->modelName[1] = (char)data[12];
      status->modelName[2] = (char)data[13];
      status->modelName[3] = (char)data[14];
      status->modelName[4] = '\0';
    } else {
      // Спробуємо регістри 0x000A-0x000B (offsets 3-6)
      uint16_t reg0A = (data[3] << 8) | data[4];
      uint16_t reg0B = (data[5] << 8) | data[6];
      status->modelName[0] = (char)((reg0A >> 8) & 0xFF);
      status->modelName[1] = (char)(reg0A & 0xFF);
      status->modelName[2] = (char)((reg0B >> 8) & 0xFF);
      status->modelName[3] = (char)(reg0B & 0xFF);
      status->modelName[4] = '\0';
    }
    Serial.printf("[Bluetti] Модель: %s\n", status->modelName);
  }
  
  // 2. Battery SOC (регістр 0x0010)
  // Регістр 0x0010 = offset 3 + (0x10 - 0x0A) × 2 = 3 + 6 × 2 = 15 (байти 15-16)
  // Значення 1019 = 101.9% → обмежуємо до 100%
  int batteryRegOffset = -1;
  uint16_t batteryReg0010 = 0;
  uint16_t batteryReg002B = 0;

  // Читаємо обидва потенційні регістри SoC
  if (length >= 17) {
    batteryReg0010 = (data[15] << 8) | data[16]; // 0x0010
  }
  if (length >= 71) {
    batteryReg002B = (data[69] << 8) | data[70]; // 0x002B
  }

  // Основна логіка: віддаємо перевагу 0x002B, якщо він у межах 0-100%.
  // У польових логах саме 0x002B змінюється (84→85), тоді як 0x0010 часто «залипає» на 1019.
  bool reg002BValid = (batteryReg002B > 0 && batteryReg002B <= 100);
  bool reg0010ValidPercent = (batteryReg0010 > 0 && batteryReg0010 <= 100);
  bool reg0010ValidX10 = (batteryReg0010 > 100 && batteryReg0010 <= 1100);

  if (reg002BValid) {
    cachedBattery = batteryReg002B;
    batteryRegOffset = 69; // offset у відповіді (0x002B)
    status->batteryRaw = batteryReg002B;
  } else if (reg0010ValidX10) {
    cachedBattery = batteryReg0010 / 10;
    batteryRegOffset = 15; // offset у відповіді (0x0010)
    status->batteryRaw = batteryReg0010;
  } else if (reg0010ValidPercent) {
    cachedBattery = batteryReg0010;
    batteryRegOffset = 15;
    status->batteryRaw = batteryReg0010;
  }

  // Обмежуємо до 0-100
  if (cachedBattery > 100) cachedBattery = 100;

  if (batteryRegOffset == -1) {
    cachedBattery = 100;
    status->batteryRaw = 1000;
    Serial.println("[Bluetti] ⚠️  Battery not detected, using default 100%");
  } else {
    Serial.printf("[Bluetti] Батарея: %d%% (reg 0x%04X raw: %d%s)\n",
                  cachedBattery,
                  (batteryRegOffset == 69) ? 0x002B : 0x0010,
                  status->batteryRaw,
                  reg0010ValidX10 ? " ÷10" : "");
    if (reg002BValid && reg0010ValidX10) {
      Serial.printf("[Bluetti]     Debug: reg0x0010=%d, reg0x002B=%d -> using 0x002B as SoC\n",
                    batteryReg0010, batteryReg002B);
    }
  }
  
  // 3. Напруга батареї (регістр 0x0013)
  // Регістр 0x0013 = offset 3 + (0x13 - 0x0A) × 2 = 3 + 9 × 2 = 21 (байти 21-22)
  if (length >= 23) {
    status->batteryVoltage = (data[21] << 8) | data[22];
    // Напруга в форматі ×10 (537 = 53.7V) або ×100 (537 = 5.37V)
    // Зазвичай для EB3A це ×10, тобто 537 = 53.7V
    Serial.printf("[Bluetti] Напруга батареї: %d (%.1fV)\n", 
                  status->batteryVoltage, status->batteryVoltage / 10.0f);
  }
  
  // 4. Температура
  // EB3A зберігає температуру в регістрах page 0x00 в форматі Кельвіни або прямі °C × 10
  // Регістр 0x0028 або 0x0029 може містити температуру для деяких моделей
  // Формат: значення / 10 = температура в °C
  // Або формат Кельвіни: (значення - 2731) / 10 = °C
  status->temperature = 0; // За замовчуванням невідомо
  
  if (length >= 35) {
    // Спробуємо кілька відомих регістрів для температури
    // Регістр 0x0028 (offset 63-64) - часто використовується для внутрішньої температури
    int tempOffset = 3 + (0x28 - 0x0A) * 2; // = 3 + 30*2 = 63
    if (tempOffset + 1 < (int)length) {
      uint16_t regVal = (data[tempOffset] << 8) | data[tempOffset + 1];
      // Перевіряємо чи це розумне значення температури
      // Формат ×10: 200-500 = 20-50°C
      // Формат Кельвіни: 2931-3231 = 20-50°C (2731 + 200 to 2731 + 500)
      if (regVal >= 100 && regVal <= 700) {
        // Прямий формат ×10 (наприклад 250 = 25.0°C)
        status->temperature = regVal;
        Serial.printf("[Bluetti] Температура (reg 0x0028): %d (%.1f°C)\n", 
                      status->temperature, status->temperature / 10.0f);
      } else if (regVal >= 2731 && regVal <= 3531) {
        // Формат Кельвіни (2731 = 0°C, 2981 = 25°C)
        status->temperature = (regVal - 2731); // Конвертуємо в ×10 °C
        Serial.printf("[Bluetti] Температура (reg 0x0028, Kelvin): %d K = %.1f°C\n", 
                      regVal, status->temperature / 10.0f);
      }
    }
    
    // Якщо не знайдено в 0x0028, спробуємо 0x0029
    if (status->temperature == 0) {
      tempOffset = 3 + (0x29 - 0x0A) * 2; // = 3 + 31*2 = 65
      if (tempOffset + 1 < (int)length) {
        uint16_t regVal = (data[tempOffset] << 8) | data[tempOffset + 1];
        if (regVal >= 100 && regVal <= 700) {
          status->temperature = regVal;
          Serial.printf("[Bluetti] Температура (reg 0x0029): %d (%.1f°C)\n", 
                        status->temperature, status->temperature / 10.0f);
        } else if (regVal >= 2731 && regVal <= 3531) {
          status->temperature = (regVal - 2731);
          Serial.printf("[Bluetti] Температура (reg 0x0029, Kelvin): %d K = %.1f°C\n", 
                        regVal, status->temperature / 10.0f);
        }
      }
    }
    
    // Якщо все ще не знайдено, шукаємо будь-яке значення в розумному діапазоні
    if (status->temperature == 0) {
      for (int i = 0; i < 40; i++) {
        int offset = 3 + i * 2;
        if (offset + 1 < (int)length) {
          uint16_t regVal = (data[offset] << 8) | data[offset + 1];
          // Шукаємо значення 150-500 (15-50°C в форматі ×10)
          if (regVal >= 150 && regVal <= 500) {
            // Пропускаємо регістри з відомими іншими значеннями
            uint16_t regAddr = 0x000A + i;
            if (regAddr != 0x0010 && regAddr != 0x0013 && 
                regAddr != 0x0017 && regAddr != 0x0019 &&
                regAddr != 0x002B) {
              status->temperature = regVal;
              Serial.printf("[Bluetti] Температура (reg 0x%04X): %d (%.1f°C) - можливо\n", 
                            regAddr, status->temperature, status->temperature / 10.0f);
              break;
            }
          }
        }
      }
    }
    
    // EB3A doesn't have temperature sensors in this register page
  }
  
  // 5. Input Power (регістри 0x0024=DC_INPUT, 0x0025=AC_INPUT, offsets 55-58)
  if (length >= 57) {
    status->dcInputPower = (data[55] << 8) | data[56];
  }
  if (length >= 59) {
    status->acInputPower = (data[57] << 8) | data[58];
  }
  cachedInputPower = status->dcInputPower + status->acInputPower;
  Serial.printf("[Bluetti] Вхідна потужність: %dW (DC: %dW, AC: %dW)\n", 
                cachedInputPower, status->dcInputPower, status->acInputPower);
  
  // 6. AC Output Power (регістр 0x0026, offset 59-60)
  if (length >= 61) {
    cachedAcPower = (data[59] << 8) | data[60];
    Serial.printf("[Bluetti] AC вихідна потужність: %dW\n", cachedAcPower);
  } else {
    cachedAcPower = 0;
  }
  
  // 7. DC Output Power (регістр 0x0027, offset 61-62)
  if (length >= 63) {
    cachedDcPower = (data[61] << 8) | data[62];
    Serial.printf("[Bluetti] DC вихідна потужність: %dW\n", cachedDcPower);
  } else {
    cachedDcPower = 0;
  }

  // 8. Max DC Limit (регістр 0x002B, offset 69-70)
  // Регістр 0x002B = offset 3 + (0x2B - 0x0A) × 2 = 3 + 33 × 2 = 69
  if (length >= 71) {
    status->maxDcLimit = (data[69] << 8) | data[70];
    Serial.printf("[Bluetti] Макс. DC ліміт: %dW\n", status->maxDcLimit);
  }

  // 9. AC Output State (регістр 0x0030, offset 79-80)
  // ВАЖЛИВО: 0x0001 = ON, 0x0000 = OFF
  if (length >= 81) {
    uint16_t acStateReg = (data[79] << 8) | data[80];
    cachedAcState = (acStateReg == 1);
    Serial.printf("[Bluetti] AC вихід: %s (reg=0x%04X)\n", cachedAcState ? "УВІМКНЕНО" : "ВИМКНЕНО", acStateReg);
  } else {
    cachedAcState = (cachedAcPower > 0);
  }
  
  // 10. DC Output State (регістр 0x0031, offset 81-82)
  // ВАЖЛИВО: 0x0001 = ON, 0x0000 = OFF
  if (length >= 83) {
    uint16_t dcStateReg = (data[81] << 8) | data[82];
    cachedDcState = (dcStateReg == 1);
    Serial.printf("[Bluetti] DC вихід: %s (reg=0x%04X)\n", cachedDcState ? "УВІМКНЕНО" : "ВИМКНЕНО", dcStateReg);
  } else {
    cachedDcState = (cachedDcPower > 0);
  }
  
  // Виводимо всі регістри для аналізу
  Serial.println("\n[Bluetti] --- Всі 40 регістрів ---");
  for (int i = 0; i < 40; i++) {
    uint16_t regAddr = 0x000A + i;
    uint16_t regVal = status->registers[i];
    Serial.printf("[Bluetti] Reg 0x%04X [%2d]: %5d (0x%04X)", regAddr, i, regVal, regVal);
    
    // Додаємо інтерпретацію для відомих регістрів
    if (regAddr == 0x000A || regAddr == 0x000B) {
      Serial.print(" [Модель]");
    } else if (regAddr == 0x0010) {
      Serial.print(" [Battery SOC ×10]");
    } else if (regAddr == 0x0013) {
      Serial.print(" [Напруга]");
    } else if (regAddr == 0x0017) {
      Serial.print(" [Температура?]");
    } else if (regAddr == 0x0024) {
      Serial.print(" [DC Input]");
    } else if (regAddr == 0x0025) {
      Serial.print(" [AC Input]");
    } else if (regAddr == 0x0026) {
      Serial.print(" [AC Output Power]");
    } else if (regAddr == 0x0027) {
      Serial.print(" [DC Output Power]");
    } else if (regAddr == 0x002B) {
      Serial.print(" [Max DC Limit]");
    } else if (regAddr == 0x002D) {
      Serial.print(" [DC State?]");
    } else if (regAddr == 0x0030) {
      Serial.print(" [AC State]");
    } else if (regAddr == 0x0031) {
      Serial.print(" [DC State]");
    }
    Serial.println();
  }

  // Оновлюємо статус
  status->bluettiConnected = true;
  status->batteryLevel = cachedBattery;
  status->acPower = cachedAcPower;
  status->dcPower = cachedDcPower;
  status->inputPower = cachedInputPower;
  status->acOutputState = cachedAcState;
  status->dcOutputState = cachedDcState;
  status->lastBluettiUpdate = millis();
  
  Serial.println("\n[Bluetti] === ПІДСУМОК ===");
  Serial.printf("[Bluetti] Батарея: %d%%\n", cachedBattery);
  Serial.printf("[Bluetti] Напруга: %d (%.1fV)\n", status->batteryVoltage, status->batteryVoltage / 10.0f);
  Serial.printf("[Bluetti] Вхід: %dW (DC: %dW, AC: %dW)\n", cachedInputPower, status->dcInputPower, status->acInputPower);
  Serial.printf("[Bluetti] AC вихід: %s, %dW\n", cachedAcState ? "УВІМКНЕНО" : "ВИМКНЕНО", cachedAcPower);
  Serial.printf("[Bluetti] DC вихід: %s, %dW\n", cachedDcState ? "УВІМКНЕНО" : "ВИМКНЕНО", cachedDcPower);
  Serial.printf("[Bluetti] Макс. DC ліміт: %dW\n", status->maxDcLimit);
  Serial.println("[Bluetti] ========================================\n");
}

void BluettiDevice::notificationThunk(
    NimBLERemoteCharacteristic *characteristic, uint8_t *data,
    size_t length, bool isNotify) {
  // ВАЖЛИВО: Використовуємо Serial.print замість Serial.println для швидшої обробки
  // Це може допомогти, якщо callback викликається, але дані втрачаються
  // ДОДАТКОВО: Використовуємо Serial.flush() для гарантії виводу
  Serial.flush();
  Serial.print("\n\n\n[Bluetti] ========================================\n");
  Serial.print("[Bluetti] *** CALLBACK CALLED ***\n");
  Serial.printf("[Bluetti] characteristic=%p\n", characteristic);
  Serial.printf("[Bluetti] length=%d, isNotify=%d\n", length, isNotify);
  Serial.printf("[Bluetti] instance=%p\n", instance);
  Serial.flush();
  
  if (!instance) {
    Serial.print("[Bluetti] ERROR: Instance is NULL!\n");
    Serial.print("[Bluetti] This should never happen!\n");
    Serial.flush();
    return;
  }
  
  Serial.print("[Bluetti] Instance found, calling handleNotification...\n");
  Serial.flush();
  instance->handleNotification(data, length);
  Serial.print("[Bluetti] handleNotification completed\n");
  Serial.print("[Bluetti] ========================================\n");
  Serial.flush();
}

void BluettiDevice::setUpdateInterval(unsigned long intervalMs) {
  updateInterval = intervalMs;
  Serial.printf("[Bluetti] Update interval set to %lu ms (%lu seconds)\n", intervalMs, intervalMs/1000);
}

unsigned long BluettiDevice::getUpdateInterval() const {
  return updateInterval;
}
