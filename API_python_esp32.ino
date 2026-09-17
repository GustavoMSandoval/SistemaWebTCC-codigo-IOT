#include <WiFi.h>
#include <WebServer.h>
#include "DHT.h"

#define DHTPIN 15
#define DHTTYPE DHT22

const char* ssid = "Lagus2G";
const char* password = "@Lagus991512777";

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

void handleSensor() {
  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();

  if (isnan(temperatura) || isnan(umidade)) {
    server.send(500, "application/json", "{\"error\":\"Erro ao ler DHT22\"}");
    Serial.println("[ERRO] Falha ao ler DHT22");
    return;
  }

  String json = "{";
  json += "\"temperature\":" + String(temperatura, 1) + ",";
  json += "\"humidity\":" + String(umidade, 1);
  json += "}";

  Serial.println("================================");
  Serial.println("LEITURA DO DHT22");
  Serial.println("================================");
  Serial.println("Temperatura: " + String(temperatura, 1) + " °C");
  Serial.println("Umidade: " + String(umidade, 1) + " %");
  Serial.println("JSON: " + json);

  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>DHT22</title>";
  html += "</head><body>";
  html += "<h1>ESP32 + DHT22</h1>";
  html += "<p>Use <a href='/sensor'>/sensor</a> para obter os dados.</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP32 + DHT22");
  Serial.println("================================");

  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Conectando ao Wi-Fi");

  int tentativas = 0;

  while (WiFi.status() != WL_CONNECTED && tentativas < 40) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println();
    Serial.println("[ERRO] Não foi possível conectar ao Wi-Fi.");
    return;
  }

  Serial.println();
  Serial.println("[OK] Wi-Fi conectado!");
  Serial.print("IP da ESP32: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/sensor", HTTP_GET, handleSensor);

  server.begin();

  Serial.println("[OK] Servidor do DHT22 iniciado!");
  Serial.print("Acesse: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();
}