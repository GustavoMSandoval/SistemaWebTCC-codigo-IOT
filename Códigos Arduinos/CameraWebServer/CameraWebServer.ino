#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ESPmDNS.h>

#include "board_config.h"

Preferences preferences;

const char* fallback_ssid = "Klaus";
const char* fallback_password = "N21C30A19";

String current_ssid = "";
String current_password = "";

bool testandoNovaRede = false;
String novo_ssid_temp = "";
String nova_pass_temp = "";
unsigned long tempoInicioTesteWifi = 0;

// ============================================================
// CONFIGURAÇÃO INICIAL / FALLBACK NO CÓDIGO
// ============================================================
String current_apiUrl = "http://127.0.0.1:8000/api/captures/";
String current_authToken = "2db1c588dedfb17fddbb323813bfb7e0ce591613";
String current_dhtHost = "sensordht.local";

unsigned long intervaloCapturaMs = 60000; // 1 minuto para testes (mude para 600000 para 10 min)
unsigned long ultimoTempoCaptura = 0;

// ============================================================
// CACHE DA ÚLTIMA LEITURA VÁLIDA DO SENSOR
// ============================================================
float ultimaTemperatura = 0.0;
float ultimaUmidade = 0.0;
bool possuiLeituraAnterior = false;

void startCameraServer();
void setupLedFlash();

String urldecode(String str) {
  String encodedString = "";
  char c, code0, code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == '+') {
      encodedString += ' ';
    } else if (c == '%') {
      i++; code0 = str.charAt(i);
      i++; code1 = str.charAt(i);
      char hex[3] = {code0, code1, '\0'};
      encodedString += (char) strtol(hex, NULL, 16);
    } else {
      encodedString += c;
    }
  }
  return encodedString;
}

void aplicarNovoIntervalo(int segundos) {
  if (segundos <= 0) return;

  intervaloCapturaMs = (unsigned long)segundos * 1000;

  preferences.begin("tcc", false);
  preferences.putUInt("intervalo", intervaloCapturaMs);
  preferences.end();

  ultimoTempoCaptura = millis();

  Serial.printf("[SISTEMA] Novo intervalo aplicado: %d segundos\n", segundos);
}

void aplicarNovoBackend(String url, String token) {
  preferences.begin("tcc", false);
  if (url.length() > 0) {
    String urlDecodificada = urldecode(url);
    preferences.putString("apiUrl", urlDecodificada);
    current_apiUrl = urlDecodificada;
    Serial.println("[SISTEMA] URL do Backend atualizada: " + current_apiUrl);
  }
  if (token.length() > 0) {
    String tokenDecodificado = urldecode(token);
    preferences.putString("authToken", tokenDecodificado);
    current_authToken = tokenDecodificado;
    Serial.println("[SISTEMA] Token de autenticação atualizado!");
  }
  preferences.end();
  Serial.println("[SISTEMA] Configurações de Backend salvas!");
}

void agendarTesteWifi(String n_ssid, String n_pass) {
  novo_ssid_temp = urldecode(n_ssid);
  nova_pass_temp = urldecode(n_pass);
  testandoNovaRede = true;
  tempoInicioTesteWifi = millis();
}

void aplicarNovoSensor(String host) {
  if (host.length() > 0) {
    preferences.begin("tcc", false);
    preferences.putString("dhtHost", host);
    preferences.end();
    current_dhtHost = urldecode(host);
    Serial.println("[SISTEMA] Host do Sensor atualizado: " + current_dhtHost);
  }
}

bool obterDadosDHT(float& temperatura, float& umidade) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;
  String url = "http://" + current_dhtHost + "/sensor";

  Serial.println("\n================================");
  Serial.println("CONSULTANDO DHT22");
  Serial.print("[DHT22] URL: ");
  Serial.println(url);

  client.setTimeout(5000);
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  if (!http.begin(client, url)) return false;

  int httpCode = http.GET();
  if (httpCode <= 0 || httpCode != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  String resposta = http.getString();
  int tempInicio = resposta.indexOf("\"temperature\":");
  int humInicio = resposta.indexOf("\"humidity\":");

  if (tempInicio == -1 || humInicio == -1) {
    http.end();
    return false;
  }

  tempInicio += strlen("\"temperature\":");
  humInicio += strlen("\"humidity\":");
  int tempFim = resposta.indexOf(",", tempInicio);
  int humFim = resposta.indexOf("}", humInicio);

  temperatura = resposta.substring(tempInicio, tempFim).toFloat();
  umidade = resposta.substring(humInicio, humFim).toFloat();

  http.end();
  Serial.printf("[OK] Temperatura: %.1f C | Umidade: %.1f %%\n", temperatura, umidade);
  return true;
}

bool enviarParaDjango(camera_fb_t* fb, float temperatura, float umidade) {
  if (WiFi.status() != WL_CONNECTED || fb == nullptr) return false;

  WiFiClient client;
  HTTPClient http;

  Serial.print("[Django] A tentar ligar à URL: [");
  Serial.print(current_apiUrl);
  Serial.println("]");

  if (!http.begin(client, current_apiUrl)) {
    Serial.println("[ERRO] Falha ao iniciar http.begin com a URL do Django.");
    return false;
  }

  String boundary = "----ESP32CAMBoundary";
  String bodyStart = "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"temperature\"\r\n\r\n" + String(temperatura, 1) + "\r\n";
  bodyStart += "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"humidity\"\r\n\r\n" + String(umidade, 1) + "\r\n";
  bodyStart += "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"observation\"\r\n\r\nCaptura automatizada TCC\r\n";
  bodyStart += "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"image\"; filename=\"tomate.jpg\"\r\n";
  bodyStart += "Content-Type: image/jpeg\r\n\r\n";

  String bodyEnd = "\r\n--" + boundary + "--\r\n";
  size_t totalLength = bodyStart.length() + fb->len + bodyEnd.length();

  // Aloca em PSRAM e usa memcpy para não travar a CPU copiando byte a byte
  uint8_t* postBuf = (uint8_t*)ps_malloc(totalLength);
  if (!postBuf) {
    postBuf = (uint8_t*)malloc(totalLength);
  }
  if (!postBuf) {
    Serial.println("[ERRO] Memoria insuficiente para montar payload HTTP.");
    http.end();
    return false;
  }

  size_t offset = 0;
  memcpy(postBuf + offset, bodyStart.c_str(), bodyStart.length());
  offset += bodyStart.length();
  memcpy(postBuf + offset, fb->buf, fb->len);
  offset += fb->len;
  memcpy(postBuf + offset, bodyEnd.c_str(), bodyEnd.length());

  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("Authorization", "Token " + current_authToken);
  http.addHeader("Content-Length", String(totalLength));

  Serial.println("[Django] A enviar dados e imagem para o servidor...");
  int httpCode = http.POST(postBuf, totalLength);
  free(postBuf);

  Serial.print("[Django] HTTP Code recebido: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String resposta = http.getString();
    Serial.println("[Django] Resposta do Servidor:");
    Serial.println(resposta);
  } else {
    Serial.print("[ERRO] Falha na ligação HTTP com o Django: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
  return (httpCode >= 200 && httpCode < 300);
}

// Mantida para compatibilidade com app_http.cpp (rota /capture)
bool processarCapturaTCC(camera_fb_t* fb) {
  if (fb == nullptr) return false;
  float temperatura = 0.0, umidade = 0.0;

  if (obterDadosDHT(temperatura, umidade)) {
    ultimaTemperatura = temperatura;
    ultimaUmidade = umidade;
    possuiLeituraAnterior = true;
    Serial.println("[TCC] Nova leitura do sensor salva como referência.");
  } else {
    temperatura = ultimaTemperatura;
    umidade = ultimaUmidade;

    if (possuiLeituraAnterior) {
      Serial.printf("[AVISO] Falha no DHT22. Usando última leitura salva: %.1f C | %.1f %%\n", temperatura, umidade);
    } else {
      Serial.println("[AVISO] Falha no DHT22 e sem histórico prévio. Enviando foto com valores iniciais (0.0).");
    }
  }

  return enviarParaDjango(fb, temperatura, umidade);
}

bool conectarWiFi(String ssid, String pass, int timeoutSecs) {
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.printf("Tentando conectar a: %s ", ssid.c_str());

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < (timeoutSecs * 2)) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }
  Serial.println();
  return (WiFi.status() == WL_CONNECTED);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[SISTEMA] Inicializando TCC - OV5640 + DHT22 + Django");

  preferences.begin("tcc", false);
  current_ssid = preferences.getString("ssid", fallback_ssid);
  current_password = preferences.getString("password", fallback_password);
  intervaloCapturaMs = preferences.getUInt("intervalo", 60000);

  String rawApiUrl = preferences.getString("apiUrl", current_apiUrl);
  current_apiUrl = urldecode(rawApiUrl);
  current_authToken = preferences.getString("authToken", current_authToken);

  String rawHost = preferences.getString("dhtHost", "sensordht.local");
  current_dhtHost = urldecode(rawHost);
  preferences.end();

  Serial.println("[SISTEMA] API configurada: " + current_apiUrl);
  Serial.println(psramFound() ? "[OK] PSRAM detectada" : "[AVISO] PSRAM nao detectada");

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;

  // 10 MHz é o clock estável para o sensor OV5640 no ESP32-CAM
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 15; // Reduz o tamanho do JPEG do OV5640 sem perda visual relevante
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 16;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[ERRO] Câmera falhou: 0x%x\n", err);
    return;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s != nullptr) {
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_lenc(s, 1);
  }

#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  if (!conectarWiFi(current_ssid, current_password, 15)) {
    Serial.println("[AVISO] Falha na rede gravada. Tentando Fallback...");
    if (!conectarWiFi(fallback_ssid, fallback_password, 15)) {
      Serial.println("[ERRO CRÍTICO] Falha total de Wi-Fi. A reiniciar em 10 seg...");
      delay(10000);
      ESP.restart();
    }
  }

  Serial.print("[OK] Conectado! IP da ESP32-CAM: ");
  Serial.println(WiFi.localIP());

  if (!MDNS.begin("cam-tomate")) {
    Serial.println("[AVISO] Erro ao iniciar mDNS.");
  }

  startCameraServer();
  ultimoTempoCaptura = millis();
}

void loop() {
  if (testandoNovaRede && (millis() - tempoInicioTesteWifi > 3000)) {
    testandoNovaRede = false;
    Serial.println("\n[REDE] Aplicando novas credenciais...");

    if (conectarWiFi(novo_ssid_temp, nova_pass_temp, 15)) {
      Serial.println("[REDE] Sucesso! Salvando configurações...");
      preferences.begin("tcc", false);
      preferences.putString("ssid", novo_ssid_temp);
      preferences.putString("password", nova_pass_temp);
      preferences.end();
      current_ssid = novo_ssid_temp;
      current_password = nova_pass_temp;
    } else {
      Serial.println("[ERRO] Nova rede falhou. Revertendo para anterior...");
      conectarWiFi(current_ssid, current_password, 15);
    }
  }

  if (WiFi.status() == WL_CONNECTED && !testandoNovaRede) {
    if (millis() - ultimoTempoCaptura >= intervaloCapturaMs) {
      Serial.println("\n[TCC] Iniciando captura automatizada agendada...");
      ultimoTempoCaptura = millis();

      // 1. Consulta o DHT22 antes de ocupar o buffer da câmera
      float temperatura = 0.0, umidade = 0.0;
      if (obterDadosDHT(temperatura, umidade)) {
        ultimaTemperatura = temperatura;
        ultimaUmidade = umidade;
        possuiLeituraAnterior = true;
        Serial.println("[TCC] Nova leitura do sensor salva como referência.");
      } else {
        temperatura = ultimaTemperatura;
        umidade = ultimaUmidade;
        if (possuiLeituraAnterior) {
          Serial.printf("[AVISO] Falha no DHT22. Usando última leitura salva: %.1f C | %.1f %%\n", temperatura, umidade);
        } else {
          Serial.println("[AVISO] Falha no DHT22 e sem histórico prévio. Enviando foto com valores iniciais (0.0).");
        }
      }

      // 2. Descarta quadro antigo, captura o atual e envia via memcpy
      camera_fb_t* dummy_fb = esp_camera_fb_get();
      if (dummy_fb) esp_camera_fb_return(dummy_fb);
      delay(50);

      camera_fb_t* fb = esp_camera_fb_get();
      if (fb) {
        enviarParaDjango(fb, temperatura, umidade);
        esp_camera_fb_return(fb);
      } else {
        Serial.println("[ERRO] Falha ao obter frame.");
      }
    }
  }

  delay(10);
}