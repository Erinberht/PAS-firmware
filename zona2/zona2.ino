#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <string.h>
#include "secrets.h"

#define REED_PIN    27
#define BUZZER_PIN  26
#define PIR_PIN     14

const int FRECUENCIA_BUZZER = 2000;
const unsigned long TIEMPO_SONIDO_MS = 1000;
const int BUZZER_RESOLUTION = 8;

// Topics zona 2 (deben coincidir con dispositivos.topic_* en la BD)
const char* TOPIC_COMANDO = "jardin/zona2/comando";
const char* TOPIC_ESTADO  = "jardin/zona2/estado";
const char* TOPIC_EVENTOS = "jardin/zona2/eventos";

// NTP
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.nist.gov";

// Códigos de sensores/eventos
const char* CODIGO_SENSOR_REED     = "Z2-MAG-01";
const char* CODIGO_SENSOR_PIR      = "Z2-MOV-01";
const char* CODIGO_CONFIG_ARMAR    = "Z2-CFG-ARM";
const char* CODIGO_CONFIG_DESARMAR = "Z2-CFG-DSR";

WiFiClient espClient;
PubSubClient client(espClient);

int estadoAnteriorReed = HIGH;
int estadoAnteriorPir  = LOW;

bool buzzerActivo = false;
unsigned long buzzerInicio = 0;

bool pirListo = false;
unsigned long inicioEstabilizacionPir = 0;
const unsigned long TIEMPO_ESTABILIZACION_PIR = 30000;

bool zonaArmada = true;

// 0 = ninguno, 1 = armar, 2 = desarmar
uint8_t eventoConfigPendiente = 0;

void iniciarBuzzer() {
  ledcWriteTone(BUZZER_PIN, FRECUENCIA_BUZZER);
  buzzerActivo = true;
  buzzerInicio = millis();
}

void detenerBuzzer() {
  ledcWriteTone(BUZZER_PIN, 0);
  buzzerActivo = false;
}

void actualizarBuzzer() {
  if (buzzerActivo && (millis() - buzzerInicio >= TIEMPO_SONIDO_MS)) {
    detenerBuzzer();
  }
}

void reiniciarSensores() {
  estadoAnteriorReed = digitalRead(REED_PIN);
  estadoAnteriorPir  = digitalRead(PIR_PIN);
  pirListo = false;
  inicioEstabilizacionPir = millis();
}

void publicarEstadoZona() {
  if (client.connected()) {
    client.publish(TOPIC_ESTADO, zonaArmada ? "ARMADA" : "DESARMADA", true);
  }
}

void configurarHoraLocal() {
  const long GMT_OFFSET_SEC = -5 * 3600;
  const int DAYLIGHT_OFFSET_SEC = 0;

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);

  struct tm timeinfo;
  Serial.print("Sincronizando hora local NTP");
  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&timeinfo, 500)) {
      Serial.println("\nHora local sincronizada");
      Serial.print("Fecha y hora local: ");
      Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S");
      return;
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nNo se pudo sincronizar la hora por NTP");
}

bool obtenerTimestampISO8601Local(char* buffer, size_t bufferSize) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) {
    return false;
  }

  strftime(buffer, bufferSize, "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return true;
}

void publicarEventoJSON(const char* codigo_sensor, const char* tipo_sensor, const char* tipo_evento) {
  if (!client.connected()) return;

  char ts[25];
  if (!obtenerTimestampISO8601Local(ts, sizeof(ts))) {
    strncpy(ts, "1970-01-01T00:00:00", sizeof(ts));
    ts[sizeof(ts) - 1] = '\0';
  }

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  StaticJsonDocument<256> doc;
#endif

  doc["codigo_sensor"] = codigo_sensor;
  doc["tipo_sensor"]   = tipo_sensor;
  doc["tipo_evento"]   = tipo_evento;
  doc["ts"]            = ts;

  char payload[256];
  size_t n = serializeJson(doc, payload, sizeof(payload));
  client.publish(TOPIC_EVENTOS, payload, n);

  Serial.print("Evento publicado: ");
  Serial.println(payload);
}

void conectarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi conectado");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensaje recibido en ");
  Serial.print(topic);
  Serial.print(": ");

  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, TOPIC_COMANDO) == 0) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    StaticJsonDocument<128> doc;
#endif

    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
      Serial.print("Error parseando JSON: ");
      Serial.println(error.c_str());
      return;
    }

    const char* accion = doc["accion"];
    if (accion == nullptr) {
      Serial.println("JSON sin campo 'accion'");
      return;
    }

    if (strcmp(accion, "ARMAR") == 0) {
      zonaArmada = true;
      detenerBuzzer();
      reiniciarSensores();
      eventoConfigPendiente = 1;
      Serial.println("Zona ARMADA");
    }
    else if (strcmp(accion, "DESARMAR") == 0) {
      zonaArmada = false;
      detenerBuzzer();
      reiniciarSensores();
      eventoConfigPendiente = 2;
      Serial.println("Zona DESARMADA");
    }
    else {
      Serial.print("Accion no reconocida: ");
      Serial.println(accion);
    }
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    String clientId = "ESP32-Zona2-";
    clientId += String((uint32_t)ESP.getEfuseMac(), HEX);

    Serial.print("Conectando a MQTT... ");
    if (client.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println("conectado");

      client.subscribe(TOPIC_COMANDO);

      publicarEstadoZona();
    } else {
      Serial.print("fallo, rc=");
      Serial.print(client.state());
      Serial.println(" reintentando en 3 segundos");
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(REED_PIN, INPUT_PULLUP);
  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  ledcAttach(BUZZER_PIN, FRECUENCIA_BUZZER, BUZZER_RESOLUTION);
  detenerBuzzer();

  delay(300);
  reiniciarSensores();

  Serial.println("Sistema iniciando...");
  Serial.println("Zona 2");
  Serial.println("Reed listo");
  Serial.println("PIR en espera de estabilizacion al armar");

  conectarWiFi();
  configurarHoraLocal();

  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }

  client.loop();
  actualizarBuzzer();

  if (eventoConfigPendiente != 0) {
    publicarEstadoZona();

    if (eventoConfigPendiente == 1) {
      publicarEventoJSON(CODIGO_CONFIG_ARMAR, "CONFIGURACION", "ACTIVACION");
    } else if (eventoConfigPendiente == 2) {
      publicarEventoJSON(CODIGO_CONFIG_DESARMAR, "CONFIGURACION", "ACTIVACION");
    }

    eventoConfigPendiente = 0;
  }

  if (!zonaArmada) {
    detenerBuzzer();
    delay(20);
    return;
  }

  int estadoActualReed = digitalRead(REED_PIN);

  if (estadoAnteriorReed == LOW && estadoActualReed == HIGH) {
    Serial.println("Iman alejado / puerta abierta");
    iniciarBuzzer();
    publicarEventoJSON(CODIGO_SENSOR_REED, "MAGNETICO", "INTRUSION");
  }

  estadoAnteriorReed = estadoActualReed;

  if (!pirListo) {
    if (millis() - inicioEstabilizacionPir >= TIEMPO_ESTABILIZACION_PIR) {
      pirListo = true;
      estadoAnteriorPir = digitalRead(PIR_PIN);
      Serial.println("PIR listo");
    }
  } else {
    int estadoActualPir = digitalRead(PIR_PIN);

    if (estadoAnteriorPir == LOW && estadoActualPir == HIGH) {
      Serial.println("Movimiento detectado");
      iniciarBuzzer();
      publicarEventoJSON(CODIGO_SENSOR_PIR, "MOVIMIENTO", "INTRUSION");
    }

    estadoAnteriorPir = estadoActualPir;
  }

  delay(20);
}
