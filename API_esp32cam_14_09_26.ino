#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

#include "board_config.h"

const char* ssid = "Lagus2G";
const char* password = "@Lagus991512777";

const char* dht_esp32_ip = "192.168.0.186";

const char* apiUrl = "http://192.168.0.165:8000/api/captures/";

const char* authToken = "aff54da4f4b76dddda4c810cfa9bbeebe2f64133";

WebServer server(80);

bool inicializarCamera() {
  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 14;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("[ERRO] Falha ao inicializar câmera: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();

  if (s != nullptr) {
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_sharpness(s, 1);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_lenc(s, 1);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
  }

  Serial.println("[OK] Câmera inicializada!");

  return true;
}

bool obterDadosDHT(float& temperatura, float& umidade) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ERRO] Wi-Fi desconectado.");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  String url = "http://" + String(dht_esp32_ip) + "/sensor";

  Serial.println();
  Serial.println("================================");
  Serial.println("CONSULTANDO DHT22");
  Serial.println("================================");
  Serial.print("[DHT22] URL: ");
  Serial.println(url);

  client.setTimeout(5000);
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  if (!http.begin(client, url)) {
    Serial.println("[ERRO] Falha ao iniciar conexão HTTP.");
    return false;
  }

  Serial.println("[DHT22] Enviando GET /sensor...");

  int httpCode = http.GET();

  Serial.print("[DHT22] HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode <= 0) {
    Serial.print("[DHT22] Erro de conexão: ");
    Serial.println(http.errorToString(httpCode));

    http.end();
    return false;
  }

  String resposta = http.getString();

  Serial.println("[DHT22] Resposta recebida:");
  Serial.println(resposta);

  if (httpCode != HTTP_CODE_OK) {
    Serial.print("[ERRO] DHT22 respondeu HTTP ");
    Serial.println(httpCode);

    http.end();
    return false;
  }

  // Procura os campos do JSON
  int tempInicio = resposta.indexOf("\"temperature\":");
  int humInicio = resposta.indexOf("\"humidity\":");

  if (tempInicio == -1 || humInicio == -1) {
    Serial.println("[ERRO] Campos temperature/humidity não encontrados.");
    Serial.println("[ERRO] JSON recebido:");
    Serial.println(resposta);

    http.end();
    return false;
  }

  // Avança para depois de "temperature":
  tempInicio += strlen("\"temperature\":");

  // Avança para depois de "humidity":
  humInicio += strlen("\"humidity\":");

  // Encontra o final dos valores
  int tempFim = resposta.indexOf(",", tempInicio);
  int humFim = resposta.indexOf("}", humInicio);

  if (tempFim == -1 || humFim == -1) {
    Serial.println("[ERRO] Formato JSON inválido.");

    http.end();
    return false;
  }

  // Converte os valores para float
  String tempString = resposta.substring(tempInicio, tempFim);
  String humString = resposta.substring(humInicio, humFim);

  temperatura = tempString.toFloat();
  umidade = humString.toFloat();

  http.end();

  Serial.println();
  Serial.println("[OK] DADOS DO DHT22 RECEBIDOS!");
  Serial.print("Temperatura: ");
  Serial.print(temperatura, 1);
  Serial.println(" °C");

  Serial.print("Umidade: ");
  Serial.print(umidade, 1);
  Serial.println(" %");

  Serial.println("================================");

  return true;
}

bool enviarParaDjango(camera_fb_t* fb, float temperatura, float umidade) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ERRO] Wi-Fi desconectado.");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  Serial.println();
  Serial.println("================================");
  Serial.println("ENVIANDO PARA DJANGO");
  Serial.println("================================");

  if (!http.begin(client, apiUrl)) {
    Serial.println("[ERRO] Não foi possível conectar ao Django.");
    return false;
  }

  String boundary = "----ESP32CAMBoundary";

  String bodyStart;

  bodyStart += "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"temperature\"\r\n\r\n";
  bodyStart += String(temperatura, 1);
  bodyStart += "\r\n";

  bodyStart += "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"humidity\"\r\n\r\n";
  bodyStart += String(umidade, 1);
  bodyStart += "\r\n";

  bodyStart += "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"observation\"\r\n\r\n";
  bodyStart += "Captura realizada pela ESP32-CAM";
  bodyStart += "\r\n";

  bodyStart += "--" + boundary + "\r\n";
  bodyStart += "Content-Disposition: form-data; name=\"image\"; filename=\"tomate.jpg\"\r\n";
  bodyStart += "Content-Type: image/jpeg\r\n\r\n";

  String bodyEnd;

  bodyEnd += "\r\n--";
  bodyEnd += boundary;
  bodyEnd += "--\r\n";

  size_t totalLength = bodyStart.length() + fb->len + bodyEnd.length();

  http.addHeader(
    "Content-Type",
    "multipart/form-data; boundary=" + boundary
  );

  http.addHeader(
    "Authorization",
    "Token " + String(authToken)
  );

  http.addHeader(
    "Content-Length",
    String(totalLength)
  );

  Serial.print("Imagem: ");
  Serial.print(fb->len);
  Serial.println(" bytes");

  Serial.print("Temperatura: ");
  Serial.print(temperatura);
  Serial.println(" °C");

  Serial.print("Umidade: ");
  Serial.print(umidade);
  Serial.println(" %");

  Serial.println("Enviando...");

  String payload;
  payload.reserve(totalLength);

  payload = bodyStart;

  for (size_t i = 0; i < fb->len; i++) {
    payload += (char)fb->buf[i];
  }

  payload += bodyEnd;

  int httpCode = http.POST(
    (uint8_t*)payload.c_str(),
    payload.length()
  );

  Serial.print("HTTP Django: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String resposta = http.getString();

    Serial.println("--- Resposta Django ---");
    Serial.println(resposta);
    Serial.println("-----------------------");
  } else {
    Serial.print("[ERRO] Falha no envio: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();

  return httpCode >= 200 && httpCode < 300;
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-CAM</title>
<style>
body {
  font-family: Arial, sans-serif;
  text-align: center;
  margin: 30px;
}

button {
  padding: 14px 28px;
  font-size: 18px;
  cursor: pointer;
}

img {
  max-width: 100%;
  margin-top: 20px;
}

#status {
  margin-top: 15px;
}
</style>
</head>

<body>

<h1>ESP32-CAM</h1>

<button onclick="getStill()">Get Still</button>

<div id="status">Aguardando captura...</div>

<img id="foto">

<script>
async function getStill() {
  const status = document.getElementById("status");
  const foto = document.getElementById("foto");

  status.innerText = "Capturando imagem e lendo DHT22...";

  try {
    const response = await fetch("/capture");

    if (!response.ok) {
      const erro = await response.text();
      status.innerText = "Erro: " + erro;
      return;
    }

    const blob = await response.blob();

    foto.src = URL.createObjectURL(blob);

    status.innerText = "Captura enviada para o Django com sucesso!";
  } catch (error) {
    status.innerText = "Erro na captura: " + error;
  }
}
</script>

</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleCapture() {
  Serial.println();
  Serial.println("================================");
  Serial.println("NOVA CAPTURA");
  Serial.println("================================");

  float temperatura;
  float umidade;

  if (!obterDadosDHT(temperatura, umidade)) {
    server.send(
      500,
      "text/plain",
      "Nao foi possivel obter os dados do DHT22."
    );

    return;
  }

  delay(200);

  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("[ERRO] Falha ao capturar imagem.");

    server.send(
      500,
      "text/plain",
      "Falha ao capturar imagem."
    );

    return;
  }

  Serial.print("[OK] Imagem capturada: ");
  Serial.print(fb->len);
  Serial.println(" bytes");

  bool enviado = enviarParaDjango(
    fb,
    temperatura,
    umidade
  );

  if (!enviado) {
    esp_camera_fb_return(fb);

    server.send(
      500,
      "text/plain",
      "Imagem capturada, mas nao foi possivel enviar para Django."
    );

    return;
  }

  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");

  WiFiClient client = server.client();

  size_t enviadoBytes = 0;

  while (enviadoBytes < fb->len) {
    size_t restante = fb->len - enviadoBytes;

    size_t bloco = restante > 1024 ? 1024 : restante;

    size_t escrito = client.write(
      fb->buf + enviadoBytes,
      bloco
    );

    if (escrito == 0) {
      break;
    }

    enviadoBytes += escrito;
  }

  Serial.print("[OK] Imagem enviada ao navegador: ");
  Serial.print(enviadoBytes);
  Serial.println(" bytes");

  esp_camera_fb_return(fb);

  Serial.println("[OK] Frame buffer liberado.");
  Serial.println("================================");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP32-CAM + DHT22 + DJANGO");
  Serial.println("================================");

  if (!inicializarCamera()) {
    Serial.println("[ERRO] Câmera não inicializada.");
    return;
  }

  Serial.println();
  Serial.println("================================");
  Serial.println("CONECTANDO AO WI-FI");
  Serial.println("================================");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("Conectando");

  int tentativas = 0;

  while (WiFi.status() != WL_CONNECTED && tentativas < 40) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println();
    Serial.println("[ERRO] Falha ao conectar no Wi-Fi.");
    return;
  }

  Serial.println();
  Serial.println("[OK] Wi-Fi conectado!");

  Serial.print("IP da ESP32-CAM: ");
  Serial.println(WiFi.localIP());

  Serial.print("ESP32 DHT22: ");
  Serial.println(dht_esp32_ip);

  Serial.print("API Django: ");
  Serial.println(apiUrl);

  Serial.println();
  Serial.println("================================");
  Serial.println("TESTE DE COMUNICACAO COM DHT22");
  Serial.println("================================");

  WiFiClient teste;

  Serial.println("[TESTE] Conectando ao DHT22...");

  if (teste.connect("192.168.0.186", 80)) {
    Serial.println("[OK] ESP32-CAM consegue acessar o DHT22!");

    teste.println("GET /sensor HTTP/1.1");
    teste.println("Host: 192.168.0.186");
    teste.println("Connection: close");
    teste.println();

    unsigned long inicio = millis();

    while (teste.connected() && millis() - inicio < 5000) {
      while (teste.available()) {
        Serial.write(teste.read());
      }
    }

    teste.stop();

    Serial.println();
    Serial.println("[OK] Teste finalizado.");
  } else {
    Serial.println("[ERRO] ESP32-CAM NÃO consegue acessar o DHT22.");
  }

  teste.stop();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/capture", HTTP_GET, handleCapture);

  server.begin();

  Serial.println();
  Serial.println("================================");
  Serial.println("SERVIDOR WEB INICIADO");
  Serial.println("================================");

  Serial.print("Acesse: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();
}