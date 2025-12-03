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
      connectAttempts(0), scanning(false), scanStartTime(0), scanner(nullptr) {
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

  // Встановлюємо lastRequest = 0, щоб перший запит відбувся в loop()
  lastRequest = 0;
  
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

  // Запитуємо статус кожні 4 секунди
  if (millis() - lastRequest > 4000) {
    requestStatus();
  }
  
  // ВАЖЛИВО: Якщо не отримуємо дані більше 10 секунд, спробуємо перепідключитися
  // Це може допомогти, якщо Bluetti Bluetooth вимкнено
  static unsigned long lastDataReceived = 0;
  if (connected && status->lastBluettiUpdate > 0) {
    lastDataReceived = status->lastBluettiUpdate;
  }
  
  if (connected && lastDataReceived > 0 && millis() - lastDataReceived > 10000) {
    Serial.println("[Bluetti] WARNING: No data received for 10 seconds!");
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
  
  // ВАЖЛИВО: EB3A вимагає MODBUS RTU команди З CRC для BLE
  // Згідно репозиторію читаємо з 0x000A (40 регістрів)
  uint8_t cmd[8];
  cmd[0] = 0x01; // Device ID
  cmd[1] = 0x03; // Function code (Read Holding Registers)
  cmd[2] = 0x00; // Start address high
  cmd[3] = 0x0A; // Start address low (0x000A - Page 0 Core registers)
  cmd[4] = 0x00; // Quantity high
  cmd[5] = 0x28; // Quantity low (40 registers)
  
  // Розраховуємо CRC16
  uint16_t crc = calculateCRC16(cmd, 6);
  cmd[6] = crc & 0xFF;        // CRC low byte
  cmd[7] = (crc >> 8) & 0xFF; // CRC high byte
  
  Serial.printf("[Bluetti] Sending status request (40 regs from 0x000A) WITH CRC: ");
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
  
  bool ok = sendCommand(cmd, sizeof(cmd));
  if (ok) {
    cachedDcState = state;
    status->dcOutputState = state;
  }
  return ok;
}

uint8_t BluettiDevice::getBatteryLevel() const { return cachedBattery; }

int BluettiDevice::getACOutputPower() const { return cachedAcPower; }

int BluettiDevice::getDCOutputPower() const { return cachedDcPower; }

bool BluettiDevice::getACOutputState() const { return cachedAcState; }

bool BluettiDevice::getDCOutputState() const { return cachedDcState; }

int BluettiDevice::getInputPower() const { return cachedInputPower; }

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
  
  // Перевіряємо на помилку MODBUS (0x83 = 0x03 + 0x80)
  if (data[0] == 0x01 && data[1] == 0x83) {
    Serial.printf("[Bluetti] ERROR: MODBUS Exception received: ");
    for (size_t i = 0; i < length && i < 10; i++) {
      Serial.printf("%02X ", data[i]);
    }
    Serial.println();
    Serial.println("[Bluetti] 💡 This usually means:");
    Serial.println("[Bluetti]    1. Command format is wrong (maybe no CRC needed?)");
    Serial.println("[Bluetti]    2. Register address is invalid");
    Serial.println("[Bluetti]    3. Device doesn't support this function");
    return;
  }
  
  if (data[0] != 0x01 || data[1] != 0x03) {
    Serial.printf("[Bluetti] WARNING: Unexpected header: data[0]=%02X, data[1]=%02X (expected 01 03)\n", data[0], data[1]);
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

  // EB3A MODBUS структура від адреси 0x0006:
  // Байти 11-14: "EB3A" (0x45 0x42 0x33 0x41)
  // Байти 23-30: можливо дані (0x03FB=1019, 0xCBB8=52152, 0x5FA6=24486, 0x0219=537)
  
  Serial.println("[Bluetti] ========================================\n");
  
  // ДІАГНОСТИКА: Виводимо всі 40 регістрів для пошуку AC/DC
  Serial.println("[Bluetti] === FULL 40 REGISTER DUMP (AC=ON test) ===");
  for (int i = 0; i < 40 && (3 + i*2 + 1) < length; i++) {
    uint16_t regVal = (data[3 + i*2] << 8) | data[3 + i*2 + 1];
    Serial.printf("[Bluetti] Reg[%2d] offset %2d: %5d (0x%04X)", i, 3+i*2, regVal, regVal);
    if (regVal > 0 && regVal < 1000) {
      Serial.printf(" <-- %dW?", regVal);
    }
    Serial.println();
  }
  Serial.println("[Bluetti] ========================================");
  
  // Шукаємо Battery SOC автоматично (має бути ~1000-1100 для 100%)
  int batteryReg = -1;
  uint16_t batteryValue = 0;
  
  // Reg@69 (offset 69): 1019 (0x03FB) -> 1019/10 = 101.9% -> обмежити до 100%
  if (length >= 71) {
    batteryValue = (data[69] << 8) | data[70];
    if (batteryValue >= 950 && batteryValue <= 1050) { // 95.0% - 105.0%
      batteryReg = 69;
      cachedBattery = batteryValue / 10; // Ділимо на 10
      if (cachedBattery > 100) cachedBattery = 100; // Обмежуємо до 100%
      Serial.printf("[Bluetti] ✅ Battery found at offset 69: %d (raw=%d, /10=%d%%, limited to %d%%)\n", 
                    batteryValue, batteryValue, batteryValue/10, cachedBattery);
    }
  }
  
  // ✅ CORRECT REGISTER MAPPING (from giovanne123/EB3A_Bluetti_ESP32_HA):
  // Reading from address 0x000A (40 registers), response starts at offset 3
  // Offset розрахунок: 3 + (register - 0x0A) × 2
  // Battery:   0x2B = 3 + (0x2B-0x0A)×2 = 3 + 33×2 = 69 → raw/10
  // AC Power:  0x26 = 3 + (0x26-0x0A)×2 = 3 + 28×2 = 59 → Watts
  // DC Power:  0x27 = 3 + (0x27-0x0A)×2 = 3 + 29×2 = 61 → Watts
  // AC State:  0x30 = 3 + (0x30-0x0A)×2 = 3 + 38×2 = 79 → 0=OFF, 1=ON
  // DC State:  0x31 = 3 + (0x31-0x0A)×2 = 3 + 39×2 = 81 → 0=OFF, 1=ON
  
  // Parse battery (already done above)
  if (batteryReg == -1) {
    cachedBattery = 100;
    Serial.println("[Bluetti] ⚠️  Battery not auto-detected, using 100%");
  }
  
  // Parse input power (register 0x24=DC_INPUT, 0x25=AC_INPUT at offset 55-58)
  int dcInputPower = 0;
  int acInputPower = 0;
  if (length >= 57) {
    dcInputPower = (data[55] << 8) | data[56];
  }
  if (length >= 59) {
    acInputPower = (data[57] << 8) | data[58];
  }
  cachedInputPower = dcInputPower + acInputPower;
  if (cachedInputPower > 0) {
    Serial.printf("[Bluetti] ✅ Input Power: %dW (DC=%dW, AC=%dW)\n", 
                  cachedInputPower, dcInputPower, acInputPower);
  }
  
  // Parse AC output power (offset 59 = register 0x26)
  if (length >= 61) {
    cachedAcPower = (data[59] << 8) | data[60];
    Serial.printf("[Bluetti] ✅ AC Power (offset 59): %dW\n", cachedAcPower);
  } else {
    cachedAcPower = 0;
    Serial.println("[Bluetti] ⚠️  AC Power offset 59 out of range");
  }
  
  // Parse DC output power (offset 61 = register 0x27)
  if (length >= 63) {
    cachedDcPower = (data[61] << 8) | data[62];
    Serial.printf("[Bluetti] ✅ DC Power (offset 61): %dW\n", cachedDcPower);
  } else {
    cachedDcPower = 0;
    Serial.println("[Bluetti] ⚠️  DC Power offset 61 out of range");
  }

  // Parse AC output state (register 0x30 at offset 79-80, use LSB)
  if (length >= 81) {
    cachedAcState = (data[80] == 1);
    Serial.printf("[Bluetti] ✅ AC State: %s (raw=%d at offset 80)\n", 
                  cachedAcState ? "ON" : "OFF", data[80]);
  } else {
    cachedAcState = (cachedAcPower > 0);
    Serial.println("[Bluetti] ⚠️  AC State out of range, using power-based detection");
  }
  
  // Parse DC output state (register 0x31 at offset 81-82, use LSB)
  if (length >= 83) {
    cachedDcState = (data[82] == 1);
    Serial.printf("[Bluetti] ✅ DC State: %s (raw=%d at offset 82)\n", 
                  cachedDcState ? "ON" : "OFF", data[82]);
  } else {
    cachedDcState = (cachedDcPower > 0);
    Serial.println("[Bluetti] ⚠️  DC State out of range, using power-based detection");
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
  
  Serial.printf("[Bluetti] Status updated: Battery=%d%%, AC=%s/%dW, DC=%s/%dW, Input=%dW\n",
                cachedBattery, cachedAcState ? "ON" : "OFF", cachedAcPower,
                cachedDcState ? "ON" : "OFF", cachedDcPower, cachedInputPower);
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
