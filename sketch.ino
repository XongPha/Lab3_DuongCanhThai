#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define TRIG_PIN     13
#define ECHO_PIN     12
#define ONE_WIRE_BUS 19
#define VALVE_PIN    25 // LED Đỏ
#define PUMP_PIN     26 // LED Xanh dương (PWM)

// Cấu hình Wi-Fi & MQTT
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

const char* topic_telemetry = "aquaculture/sensors/data";
const char* topic_cmd_pump  = "aquaculture/control/pump";
const char* topic_cmd_valve = "aquaculture/control/valve";

// Đối tượng Cảm biến & MQTT
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensors(&oneWire);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Cấu trúc dữ liệu
struct SensorData {
  float waterLevel;
  float temperature;
};

// Queue FreeRTOS
QueueHandle_t dataQueue;
QueueHandle_t dataAlertQueue;

// Khai báo Task
void TaskSendData(void *pvParameters);
void TaskReadData(void *pvParameters);
void TaskAlert(void *pvParameters);

void setupWifi();
void mqttCallback(char* topic, byte* payload, unsigned int length);

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  pinMode(VALVE_PIN, OUTPUT);
  digitalWrite(VALVE_PIN, LOW);

  // Cấu hình PWM theo chuẩn ESP32 Core v3.x mới
  ledcAttach(PUMP_PIN, 5000, 8); 
  ledcWrite(PUMP_PIN, 0);        

  tempSensors.begin();

  dataQueue = xQueueCreate(10, sizeof(SensorData));
  dataAlertQueue = xQueueCreate(10, sizeof(float));

  // Phân chia Task theo yêu cầu
  xTaskCreatePinnedToCore(TaskSendData, "TaskSend", 8192, NULL, 1, NULL, 0); // Core 0
  xTaskCreatePinnedToCore(TaskReadData, "TaskRead", 4096, NULL, 2, NULL, 1); // Core 1
  xTaskCreatePinnedToCore(TaskAlert, "TaskAlert", 4096, NULL, 3, NULL, 1);   // Core 1
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// -------------------------------------------------------------
// CORE 1: Đọc Dữ Liệu
// -------------------------------------------------------------
void TaskReadData(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    float distance_m = (duration * 0.0343) / 200.0; // cm sang mét
    
    float water_level = 2.5 - distance_m; // Bể cao 2.5m
    if (water_level < 0) water_level = 0;

    tempSensors.requestTemperatures();
    float temp_c = tempSensors.getTempCByIndex(0);
    if (temp_c == DEVICE_DISCONNECTED_C) temp_c = 25.0;

    Serial.printf("[Đọc] Mực nước: %.2f m | Nhiệt độ: %.2f °C\n", water_level, temp_c);

    SensorData currentData = {water_level, temp_c};
    xQueueSend(dataQueue, &currentData, portMAX_DELAY);
    xQueueSend(dataAlertQueue, &water_level, portMAX_DELAY);

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// -------------------------------------------------------------
// CORE 1: Điều Khiển Tự Động Máy Bơm & Van
// -------------------------------------------------------------
void TaskAlert(void *pvParameters) {
  (void) pvParameters;
  float level = 0.0;

  for (;;) {
    if (xQueueReceive(dataAlertQueue, &level, portMAX_DELAY) == pdTRUE) {
      // Logic máy bơm 50%
      if (level >= 1.0f && level <= 1.5f) {
        Serial.println(">> Mực nước 1m-1.5m: Bật bơm 50% (10s)");
        ledcWrite(PUMP_PIN, 127); // 50% PWM
        vTaskDelay(pdMS_TO_TICKS(10000));
        ledcWrite(PUMP_PIN, 0);
      } 
      // Logic máy bơm 100%
      else if (level > 1.5f && level <= 2.0f) {
        Serial.println(">> Mực nước 1.5m-2m: Bật bơm 100% (20s)");
        ledcWrite(PUMP_PIN, 255); // 100% PWM
        vTaskDelay(pdMS_TO_TICKS(20000));
        ledcWrite(PUMP_PIN, 0);
      } 
      // Logic mở van thoát
      else if (level < 0.5f) {
        Serial.println(">> Mực nước < 0.5m: Mở Van thoát (10s)");
        digitalWrite(VALVE_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(10000));
        digitalWrite(VALVE_PIN, LOW);
      }
    }
  }
}

// -------------------------------------------------------------
// CORE 0: Gửi Dữ Liệu & Nhận Lệnh Manual
// -------------------------------------------------------------
void TaskSendData(void *pvParameters) {
  (void) pvParameters;
  setupWifi();
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  SensorData data;
  for (;;) {
    if (!mqttClient.connected()) {
      String clientId = "ESP32_Wokwi_" + String(random(0xffff), HEX);
      if (mqttClient.connect(clientId.c_str())) {
        mqttClient.subscribe(topic_cmd_pump);
        mqttClient.subscribe(topic_cmd_valve);
      } else {
        vTaskDelay(pdMS_TO_TICKS(3000));
        continue;
      }
    }
    mqttClient.loop();

    if (xQueueReceive(dataQueue, &data, pdMS_TO_TICKS(100)) == pdTRUE) {
      char payload[100];
      snprintf(payload, sizeof(payload), "{\"water_level\":%.2f,\"temperature\":%.2f}", data.waterLevel, data.temperature);
      mqttClient.publish(topic_telemetry, payload);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setupWifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  
  if (String(topic) == topic_cmd_pump) {
    if (message == "ON_50") ledcWrite(PUMP_PIN, 127);
    else if (message == "ON_100") ledcWrite(PUMP_PIN, 255);
    else ledcWrite(PUMP_PIN, 0);
  } 
  else if (String(topic) == topic_cmd_valve) {
    if (message == "ON") digitalWrite(VALVE_PIN, HIGH);
    else digitalWrite(VALVE_PIN, LOW);
  }
}
