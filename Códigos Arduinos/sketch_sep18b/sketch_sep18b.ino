#include <WiFi.h>
#include <WebServer.h>
#include "DHT.h"
#include <ESPmDNS.h>
#include <Preferences.h>

#define DHTPIN 15
#define DHTTYPE DHT22

// ============================================================
// CREDENCIAIS PADRÃO (USADAS SEMPRE QUE UM CÓDIGO NOVO FOR GRAVADO)
// ============================================================
const char* default_ssid = "Klaus";
const char* default_password = "N21C30A19";

// Assinatura única gerada automaticamente na compilação
const String BUILD_ID = String(__DATE__) + " " + String(__TIME__);

Preferences preferences;
String current_ssid = "";
String current_password = "";

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
  String html = "<!DOCTYPE html><html lang='pt-BR'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Painel do Sensor DHT22</title>";
  html += "<style>body{font-family: Arial; max-width: 400px; margin: 20px auto; padding: 20px; background: #f4f4f4;} .card{background: white; padding: 15px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px;} a{display:inline-block; padding:10px; background:#007bff; color:white; text-decoration:none; border-radius:4px;}</style>";
  html += "</head><body>";
  html += "<h2>ESP32 + DHT22 (Sensor)</h2>";
  html += "<div class='card'>";
  html += "<p>Rede atual: <b>" + current_ssid + "</b></p>";
  html += "<a href='/config'>Configurar Wi-Fi do Sensor</a>";
  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleConfig() {
  String html = "<!DOCTYPE html><html lang='pt-BR'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Configurar Wi-Fi - Sensor</title>";
  html += "<style>body{font-family: Arial; max-width: 400px; margin: 20px auto; padding: 20px; background: #f4f4f4;} .card{background: white; padding: 15px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1);} input, button{width: 100%; padding: 10px; margin: 5px 0 15px 0; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box;} button{background: #28a745; color: white; border: none; cursor: pointer; font-weight: bold;}</style>";
  html += "</head><body>";
  html += "<h2>Wi-Fi do Sensor DHT22</h2>";
  html += "<div class='card'>";
  html += "<form action='/save' method='POST'>";
  html += "<label>SSID (Nome da Rede):</label>";
  html += "<input type='text' name='ssid' required>";
  html += "<label>Senha da Rede:</label>";
  html += "<input type='password' name='password' required>";
  html += "<button type='submit'>Salvar e Reiniciar Sensor</button>";
  html += "</form></div>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String new_ssid = server.arg("ssid");
    String new_pass = server.arg("password");

    // Salva a nova rede sem alterar o BUILD_ID
    preferences.begin("tcc_sensor", false);
    preferences.putString("ssid", new_ssid);
    preferences.putString("password", new_pass);
    preferences.end();

    String html = "<h2>Credenciais salvas!</h2><p>O sensor sera reiniciado agora para conectar na nova rede.</p>";
    server.send(200, "text/html", html);
    
    Serial.println("[REDE] Novas credenciais recebidas via /config. Reiniciando...");
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Faltam dados.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n================================");
  Serial.println("ESP32 + DHT22");
  Serial.println("================================");

  preferences.begin("tcc_sensor", false);
  String saved_build = preferences.getString("build_id", "");

  if (saved_build != BUILD_ID) {
    // O código foi recompilado e gravado agora: força as credenciais do topo
    Serial.println("[SISTEMA] Novo upload detectado! Aplicando Wi-Fi definido no codigo...");
    preferences.putString("ssid", default_ssid);
    preferences.putString("password", default_password);
    preferences.putString("build_id", BUILD_ID);

    current_ssid = default_ssid;
    current_password = default_password;
  } else {
    // Mesma compilação (reinício comum ou via /config): lê da memória
    current_ssid = preferences.getString("ssid", default_ssid);
    current_password = preferences.getString("password", default_password);
  }
  preferences.end();

  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(current_ssid.c_str(), current_password.c_str());

  Serial.print("Conectando ao Wi-Fi: ");
  Serial.println(current_ssid);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 40) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[ERRO] Nao foi possivel conectar ao Wi-Fi.");
  } else {
    Serial.println("\n[OK] Wi-Fi conectado!");
    Serial.print("IP da ESP32: ");
    Serial.println(WiFi.localIP());

    if (MDNS.begin("sensordht")) {
      Serial.println("MDNS iniciado: sensordht.local");
    }
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/sensor", HTTP_GET, handleSensor);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/save", HTTP_POST, handleSave);

  server.begin();

  Serial.println("[OK] Servidor do DHT22 iniciado!");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Acesse: http://");
    Serial.println(WiFi.localIP());
    Serial.println("Ou acesse: http://sensordht.local");
  }
}

void loop() {
  server.handleClient();
}