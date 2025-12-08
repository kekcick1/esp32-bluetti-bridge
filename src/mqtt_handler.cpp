#include "mqtt_handler.h"

MQTTHandler *MQTTHandler::instance = nullptr;

MQTTHandler::MQTTHandler(BluettiDevice *device, SystemStatus *sharedStatus)
    : mqttClient(wifiClient), bluetti(device), status(sharedStatus),
      serverPort(1883), lastPublish(0), lastMqttAttempt(0),
      mqttConnecting(false) {
  instance = this;
  mqttClient.setCallback(callbackThunk);
  // Збільшуємо буфер для MQTT Discovery (потрібно мінімум 512 байт)
  mqttClient.setBufferSize(1024);
  // ВАЖЛИВО: Встановлюємо мінімальний таймаут для WiFiClient, щоб не блокувати
  // кнопки!
  wifiClient.setTimeout(1);       // 1 мс замість 3000мс за замовчуванням
  mqttClient.setSocketTimeout(1); // 1 секунда для MQTT
}

void MQTTHandler::configure(const char *server, uint16_t port, const char *user,
                            const char *pass) {
  serverHost = server ? server : "";
  serverPort = port;
  if (user) {
    username = user;
  }
  if (pass) {
    password = pass;
  }
  if (!serverHost.isEmpty()) {
    mqttClient.setServer(serverHost.c_str(), serverPort);
  }
}

void MQTTHandler::loop(bool wifiReady) {
  if (serverHost.isEmpty()) {
    return;
  }

  if (!wifiReady) {
    status->mqttConnected = false;
    mqttConnecting = false;
    return;
  }

  // Неблокуюче підключення
  // ВАЖЛИВО: ensureConnection() може блокувати, тому викликаємо display.loop()
  // всередині
  if (!ensureConnection()) {
    status->mqttConnected = false;
    return;
  }

  // Обробляємо MQTT (неблокуюче)
  mqttClient.loop();
  status->mqttConnected = true;

  // Публікуємо статус кожні 5 секунд (дані отримуються з Bluetti через BLE)
  if (millis() - lastPublish > 5000) {
    publishStatus();
    lastPublish = millis();
  }
}

bool MQTTHandler::isConnected() { return mqttClient.connected(); }

void MQTTHandler::republishDiscovery() {
  if (mqttClient.connected()) {
    publishDiscovery();
    Serial.println("MQTT Discovery republished");
  }
}

bool MQTTHandler::ensureConnection() {
  if (mqttClient.connected()) {
    mqttConnecting = false;
    return true;
  }

  // Неблокуюче підключення MQTT
  if (!mqttConnecting) {
    // Починаємо підключення
    Serial.print("Connecting to MQTT");
    Serial.printf(" %s:%d", serverHost.c_str(), serverPort);
    if (!username.isEmpty()) {
      Serial.printf(" (user: %s)", username.c_str());
    }
    Serial.print("...");

    String clientId =
        String("ESP32-BLUETTI-") + String((uint32_t)ESP.getEfuseMac(), HEX);

    // ВАЖЛИВО: Встановлюємо мінімальний таймаут ПЕРЕД connect()
    // Але на жаль, connect() все одно блокує на 1 секунду через
    // setSocketTimeout(1)
    wifiClient.setTimeout(
        1); // 1 мс - мінімальний таймаут для неблокуючого підключення
    mqttClient.setSocketTimeout(
        1); // 1 секунда для MQTT (мінімальний для connect())

    // ВАЖЛИВО: Викликаємо yield() ПЕРЕД connect(), щоб обробити кнопки останній
    // раз
    yield();

    // Викликаємо connect() - блокує на 1 секунду (мінімальний таймаут)
    // Під час цього кнопки не працюють, але це найкраще, що можна зробити
    bool connected = false;
    if (!username.isEmpty()) {
      connected = mqttClient.connect(clientId.c_str(), username.c_str(), password.c_str());
    } else {
      connected = mqttClient.connect(clientId.c_str());
    }

    // Дозволяємо іншим задачам виконуватися (кнопки!) після connect()
    yield();

    // Логуємо результат відразу
    if (connected) {
      Serial.println(" ✅ connected!");
      mqttConnecting = false;
      
      // Підписуємося на команди від Home Assistant
      mqttClient.subscribe("homeassistant/bluetti/eb3a/ac_output/set");
      mqttClient.subscribe("homeassistant/bluetti/eb3a/dc_output/set");
      mqttClient.subscribe("homeassistant/bluetti/eb3a/charging_speed/set");
      mqttClient.subscribe("homeassistant/bluetti/eb3a/eco_mode/set");
      mqttClient.subscribe("homeassistant/bluetti/eb3a/power_lifting/set");
      mqttClient.subscribe("homeassistant/bluetti/eb3a/led_mode/set");
      mqttClient.subscribe("homeassistant/bluetti/eb3a/eco_shutdown/set");
      mqttClient.subscribe("homeassistant/bluetti/eb3a/power_off");
      Serial.println("[MQTT] ✅ Subscribed to control commands");
      
      yield();
      publishDiscovery();
      yield();
      publishStatus();
      yield();
      return true;
    } else {
      int state = mqttClient.state();
      Serial.printf(" ❌ failed (code: %d", state);
      switch (state) {
      case -4: Serial.print(" - Timeout"); break;
      case -3: Serial.print(" - Lost"); break;
      case -2: Serial.print(" - Failed"); break;
      case -1: Serial.print(" - Disconnected"); break;
      case 1: Serial.print(" - Bad protocol"); break;
      case 2: Serial.print(" - Bad client ID"); break;
      case 3: Serial.print(" - Unavailable"); break;
      case 4: Serial.print(" - Bad credentials"); break;
      case 5: Serial.print(" - Unauthorized"); break;
      default: Serial.print(" - Unknown"); break;
      }
      Serial.println(")");
    }

    mqttConnecting = true;
    lastMqttAttempt = millis();
    return false;
  }

  // Таймаут для повторної спроби (5 секунд)
  if (millis() - lastMqttAttempt > 5000) {
    mqttConnecting = false;
  }

  return false;
}

void MQTTHandler::publishStatus() {
  if (!mqttClient.connected() || !bluetti) {
    return;
  }

  char value[16];
  
  // Публікуємо кожне значення окремо для Home Assistant
  snprintf(value, sizeof(value), "%u", bluetti->getBatteryLevel());
  mqttClient.publish("homeassistant/bluetti/eb3a/battery", value, true);
  
  snprintf(value, sizeof(value), "%u", bluetti->getACOutputPower());
  mqttClient.publish("homeassistant/bluetti/eb3a/ac_power", value, true);
  
  snprintf(value, sizeof(value), "%u", bluetti->getDCOutputPower());
  mqttClient.publish("homeassistant/bluetti/eb3a/dc_power", value, true);
  
  snprintf(value, sizeof(value), "%u", bluetti->getInputPower());
  mqttClient.publish("homeassistant/bluetti/eb3a/input_power", value, true);
  
  // Температура (якщо доступна)
  float temp = bluetti->getTemperature();
  if (temp > 0 && temp < 100) {
    snprintf(value, sizeof(value), "%.1f", temp);
    mqttClient.publish("homeassistant/bluetti/eb3a/temperature", value, true);
  }
  
  // Напруга батареї
  float voltage = bluetti->getBatteryVoltage();
  if (voltage > 0) {
    snprintf(value, sizeof(value), "%.1f", voltage);
    mqttClient.publish("homeassistant/bluetti/eb3a/voltage", value, true);
  }
  
  mqttClient.publish("homeassistant/bluetti/eb3a/ac_output/state",
                     bluetti->getACOutputState() ? "ON" : "OFF", true);
  mqttClient.publish("homeassistant/bluetti/eb3a/dc_output/state",
                     bluetti->getDCOutputState() ? "ON" : "OFF", true);
  
  // Charging speed
  const char* speedNames[] = {"Standard", "Silent", "Turbo"};
  uint8_t speedIdx = status->chargingSpeed;
  if (speedIdx > 2) speedIdx = 0;
  mqttClient.publish("homeassistant/bluetti/eb3a/charging_speed", speedNames[speedIdx], true);
  
  // ECO mode
  mqttClient.publish("homeassistant/bluetti/eb3a/eco_mode/state",
                     status->ecoMode ? "ON" : "OFF", true);
  
  // Power Lifting
  mqttClient.publish("homeassistant/bluetti/eb3a/power_lifting/state",
                     status->powerLifting ? "ON" : "OFF", true);
  
  // LED mode
  const char* ledNames[] = {"", "Low", "High", "SOS", "Off"};
  uint8_t ledIdx = status->ledMode;
  if (ledIdx < 1 || ledIdx > 4) ledIdx = 4; // Default Off
  mqttClient.publish("homeassistant/bluetti/eb3a/led_mode", ledNames[ledIdx], true);
  
  // ECO Shutdown
  const char* ecoShutNames[] = {"", "1h", "2h", "3h", "4h"};
  uint8_t ecoIdx = status->ecoShutdown;
  if (ecoIdx < 1 || ecoIdx > 4) ecoIdx = 1; // Default 1h
  mqttClient.publish("homeassistant/bluetti/eb3a/eco_shutdown", ecoShutNames[ecoIdx], true);
}

void MQTTHandler::publishDiscovery() {
  Serial.println("[MQTT] 📡 Publishing discovery configuration...");
  
  char buffer[768];  // Збільшено буфер для складних JSON
  JsonDocument doc;

  // Device info - спільний для всіх сенсорів
  JsonObject device;

  // 1. Battery sensor
  doc.clear();
  doc["name"] = "Bluetti Battery";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/battery";
  doc["unit_of_measurement"] = "%";
  doc["device_class"] = "battery";
  doc["unique_id"] = "bluetti_eb3a_battery";
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  Serial.printf("[MQTT] JSON: %s\n", buffer);
  bool result = mqttClient.publish("homeassistant/sensor/bluetti_eb3a/battery/config", buffer, true);
  Serial.printf("[MQTT] Battery config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // 2. AC Power sensor
  doc.clear();
  doc["name"] = "Bluetti AC Power";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/ac_power";
  doc["unit_of_measurement"] = "W";
  doc["device_class"] = "power";
  doc["state_class"] = "measurement";
  doc["unique_id"] = "bluetti_eb3a_ac_power";
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/sensor/bluetti_eb3a/ac_power/config", buffer, true);
  Serial.printf("[MQTT] AC Power config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // 3. DC Power sensor
  doc.clear();
  doc["name"] = "Bluetti DC Power";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/dc_power";
  doc["unit_of_measurement"] = "W";
  doc["device_class"] = "power";
  doc["state_class"] = "measurement";
  doc["unique_id"] = "bluetti_eb3a_dc_power";
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/sensor/bluetti_eb3a/dc_power/config", buffer, true);
  Serial.printf("[MQTT] DC Power config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // 4. Input Power sensor
  doc.clear();
  doc["name"] = "Bluetti Input Power";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/input_power";
  doc["unit_of_measurement"] = "W";
  doc["device_class"] = "power";
  doc["state_class"] = "measurement";
  doc["unique_id"] = "bluetti_eb3a_input_power";
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/sensor/bluetti_eb3a/input_power/config", buffer, true);
  Serial.printf("[MQTT] Input Power config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // Temperature - прибрано (EB3A не передає температуру через BLE)
  mqttClient.publish("homeassistant/sensor/bluetti_eb3a/temperature/config", "", true);
  yield();

  // 4b. Battery Voltage sensor
  doc.clear();
  doc["name"] = "Bluetti Battery Voltage";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/voltage";
  doc["unit_of_measurement"] = "V";
  doc["device_class"] = "voltage";
  doc["state_class"] = "measurement";
  doc["unique_id"] = "bluetti_eb3a_voltage";
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/sensor/bluetti_eb3a/voltage/config", buffer, true);
  Serial.printf("[MQTT] Voltage config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // 5. AC Output switch
  doc.clear();
  doc["name"] = "Bluetti AC Output";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/ac_output/state";
  doc["command_topic"] = "homeassistant/bluetti/eb3a/ac_output/set";
  doc["unique_id"] = "bluetti_eb3a_ac_switch";
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/switch/bluetti_eb3a/ac_output/config", buffer, true);
  Serial.printf("[MQTT] AC Switch config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // 6. DC Output switch
  doc.clear();
  doc["name"] = "Bluetti DC Output";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/dc_output/state";
  doc["command_topic"] = "homeassistant/bluetti/eb3a/dc_output/set";
  doc["unique_id"] = "bluetti_eb3a_dc_switch";
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/switch/bluetti_eb3a/dc_output/config", buffer, true);
  Serial.printf("[MQTT] DC Switch config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // 6. Charging Speed select
  doc.clear();
  doc["name"] = "Bluetti Charging Speed";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/charging_speed";
  doc["command_topic"] = "homeassistant/bluetti/eb3a/charging_speed/set";
  doc["unique_id"] = "bluetti_eb3a_charging_speed";
  JsonArray options = doc["options"].to<JsonArray>();
  options.add("Standard");
  options.add("Silent");
  options.add("Turbo");
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/select/bluetti_eb3a/charging_speed/config", buffer, true);
  Serial.printf("[MQTT] Charging Speed config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // 7. ECO Mode switch
  doc.clear();
  doc["name"] = "Bluetti ECO Mode";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/eco_mode/state";
  doc["command_topic"] = "homeassistant/bluetti/eb3a/eco_mode/set";
  doc["unique_id"] = "bluetti_eb3a_eco_mode";
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/switch/bluetti_eb3a/eco_mode/config", buffer, true);
  Serial.printf("[MQTT] ECO Mode config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // 8. Power Lifting switch
  doc.clear();
  doc["name"] = "Bluetti Power Lifting";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/power_lifting/state";
  doc["command_topic"] = "homeassistant/bluetti/eb3a/power_lifting/set";
  doc["unique_id"] = "bluetti_eb3a_power_lifting";
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/switch/bluetti_eb3a/power_lifting/config", buffer, true);
  Serial.printf("[MQTT] Power Lifting config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // 9. LED Mode select
  doc.clear();
  doc["name"] = "Bluetti LED Mode";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/led_mode";
  doc["command_topic"] = "homeassistant/bluetti/eb3a/led_mode/set";
  doc["unique_id"] = "bluetti_eb3a_led_mode";
  options = doc["options"].to<JsonArray>();
  options.add("Low");
  options.add("High");
  options.add("SOS");
  options.add("Off");
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/select/bluetti_eb3a/led_mode/config", buffer, true);
  Serial.printf("[MQTT] LED Mode config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // 10. ECO Shutdown select
  doc.clear();
  doc["name"] = "Bluetti ECO Shutdown";
  doc["state_topic"] = "homeassistant/bluetti/eb3a/eco_shutdown";
  doc["command_topic"] = "homeassistant/bluetti/eb3a/eco_shutdown/set";
  doc["unique_id"] = "bluetti_eb3a_eco_shutdown";
  options = doc["options"].to<JsonArray>();
  options.add("1h");
  options.add("2h");
  options.add("3h");
  options.add("4h");
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/select/bluetti_eb3a/eco_shutdown/config", buffer, true);
  Serial.printf("[MQTT] ECO Shutdown config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  // 11. Power Off button
  doc.clear();
  doc["name"] = "Bluetti Power Off";
  doc["command_topic"] = "homeassistant/bluetti/eb3a/power_off";
  doc["unique_id"] = "bluetti_eb3a_power_off";
  doc["device_class"] = "restart";
  device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "bluetti_eb3a";
  device["manufacturer"] = "Bluetti";
  device["model"] = "EB3A";
  device["name"] = "Bluetti EB3A";
  serializeJson(doc, buffer);
  result = mqttClient.publish("homeassistant/button/bluetti_eb3a/power_off/config", buffer, true);
  Serial.printf("[MQTT] Power Off config: %s (size: %d)\n", result ? "✅" : "❌", strlen(buffer));
  yield();

  Serial.println("[MQTT] ✅ Published 12 entities to Home Assistant");
}

void MQTTHandler::onMessage(char *topic, byte *payload, unsigned int length) {
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    message += static_cast<char>(payload[i]);
  }

  // Команди від Home Assistant
  if (strcmp(topic, "homeassistant/bluetti/eb3a/ac_output/set") == 0) {
    bluetti->setACOutput(message == "ON");
    Serial.printf("[MQTT] AC Output command: %s\n", message.c_str());
  } else if (strcmp(topic, "homeassistant/bluetti/eb3a/charging_speed/set") == 0) {
    uint8_t speed = 0; // Standard
    if (message == "Silent" || message == "silent" || message == "1") {
      speed = 1;
    } else if (message == "Turbo" || message == "turbo" || message == "2") {
      speed = 2;
    }
    Serial.printf("[MQTT] Charging speed command: %s -> %d\n", message.c_str(), speed);
    bluetti->setChargingSpeed(speed);
    return;
  } else if (strcmp(topic, "homeassistant/bluetti/eb3a/eco_mode/set") == 0) {
    bluetti->setEcoMode(message == "ON");
    Serial.printf("[MQTT] ECO mode command: %s\n", message.c_str());
    return;
  } else if (strcmp(topic, "homeassistant/bluetti/eb3a/power_lifting/set") == 0) {
    bluetti->setPowerLifting(message == "ON");
    Serial.printf("[MQTT] Power Lifting command: %s\n", message.c_str());
    return;
  } else if (strcmp(topic, "homeassistant/bluetti/eb3a/led_mode/set") == 0) {
    uint8_t mode = 4; // Default Off
    if (message == "Low") mode = 1;
    else if (message == "High") mode = 2;
    else if (message == "SOS") mode = 3;
    bluetti->setLedMode(mode);
    Serial.printf("[MQTT] LED mode command: %s -> %d\n", message.c_str(), mode);
    return;
  } else if (strcmp(topic, "homeassistant/bluetti/eb3a/eco_shutdown/set") == 0) {
    uint8_t hours = 1; // Default 1h
    if (message == "2h") hours = 2;
    else if (message == "3h") hours = 3;
    else if (message == "4h") hours = 4;
    bluetti->setEcoShutdown(hours);
    Serial.printf("[MQTT] ECO shutdown command: %s -> %dh\n", message.c_str(), hours);
    return;
  } else if (strcmp(topic, "homeassistant/bluetti/eb3a/power_off") == 0) {
    bluetti->powerOff();
    Serial.println("[MQTT] Power Off command");
    return;
  }
  
  // Дані тепер отримуються напряму з Bluetti через BLE, не з MQTT
  // Це дозволяє керувати Bluetti навіть коли Home Assistant вимкнено
}

void MQTTHandler::callbackThunk(char *topic, byte *payload,
                                unsigned int length) {
  if (instance) {
    instance->onMessage(topic, payload, length);
  }
}
