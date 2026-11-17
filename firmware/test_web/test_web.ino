#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ============================================================
//  PRUEBA CONTROL WEB — Paso 3a
//  Robot Mecanum 4WD — Fase 6
//  El robot crea su propia red Wi-Fi (Access Point) y sirve la
//  página guardada en LittleFS (carpeta data/).
//  Motores DESHABILITADOS en este sub-paso (STBY = LOW).
//
//  Carga en dos partes desde PlatformIO:
//    1. Upload                     → este programa
//    2. Upload Filesystem Image    → contenido de data/
// ============================================================

// --- Red Wi-Fi propia (modo Access Point) ---
#define WIFI_SSID   "MecanumRobot"
#define WIFI_PASS   "mecanum2026"   // mínimo 8 caracteres — cambiar si hay varios robots
#define WIFI_CANAL  6
// IP del robot: 192.168.4.1 (por defecto en modo AP)

// --- 74HC595 ---
#define DS_PIN   0   // GPIO0 / D3
#define SHCP_PIN 2   // GPIO2 / D4
#define STCP_PIN 10  // GPIO10 / SD3

// --- TB6612 ---
#define STBY_PIN 16  // GPIO16 / D0
#define PWM_FL  12   // D6
#define PWM_FR  13   // D7
#define PWM_RL  14   // D5
#define PWM_RR  15   // D8

AsyncWebServer server(80);

// ------------------------------------------------------------
//  Bajo nivel — motores en estado seguro
// ------------------------------------------------------------
void shiftWrite(byte data) {
  digitalWrite(STCP_PIN, LOW);
  shiftOut(DS_PIN, SHCP_PIN, MSBFIRST, data);
  digitalWrite(STCP_PIN, HIGH);
}

void motoresApagados() {
  pinMode(STBY_PIN, OUTPUT); digitalWrite(STBY_PIN, LOW);   // TB6612 en standby
  pinMode(PWM_FL, OUTPUT);   digitalWrite(PWM_FL, LOW);
  pinMode(PWM_FR, OUTPUT);   digitalWrite(PWM_FR, LOW);
  pinMode(PWM_RL, OUTPUT);   digitalWrite(PWM_RL, LOW);
  pinMode(PWM_RR, OUTPUT);   digitalWrite(PWM_RR, LOW);
  pinMode(DS_PIN,   OUTPUT);
  pinMode(SHCP_PIN, OUTPUT);
  pinMode(STCP_PIN, OUTPUT);
  shiftWrite(0x00);                                          // dirección: coast
}

// ------------------------------------------------------------
//  LittleFS
// ------------------------------------------------------------
void listarArchivos() {
  Serial.println("Archivos en LittleFS:");
  Dir dir = LittleFS.openDir("/");
  bool vacio = true;
  while (dir.next()) {
    Serial.printf("   /%s  (%u bytes)\n", dir.fileName().c_str(), (unsigned) dir.fileSize());
    vacio = false;
  }
  if (vacio) Serial.println("   (vacío)");
}

// ------------------------------------------------------------
void setup() {
  motoresApagados();               // primero: garantizar que nada se mueve
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== TEST WEB — Paso 3a ===");

  // --- Sistema de archivos ---
  if (!LittleFS.begin()) {
    Serial.println("ERROR: no se pudo montar LittleFS");
  } else {
    listarArchivos();
    if (!LittleFS.exists("/index.html")) {
      Serial.println("AVISO: falta /index.html → ejecutar 'Upload Filesystem Image'");
    }
  }

  // --- Wi-Fi en modo Access Point ---
  WiFi.persistent(false);          // no guardar config en flash en cada arranque
  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(WIFI_SSID, WIFI_PASS, WIFI_CANAL)) {
    Serial.printf("Red Wi-Fi: %s   clave: %s\n", WIFI_SSID, WIFI_PASS);
    Serial.print ("Abrir en el navegador: http://");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("ERROR: no se pudo crear la red Wi-Fi");
  }

  // --- Rutas del servidor ---
  // Estado del robot en JSON (la página lo consulta para saber si hay conexión)
  server.on("/estado", HTTP_GET, [](AsyncWebServerRequest *req) {
    char json[96];
    snprintf(json, sizeof(json),
             "{\"uptime\":%lu,\"heap\":%u,\"clientes\":%d}",
             millis() / 1000, (unsigned) ESP.getFreeHeap(), WiFi.softAPgetStationNum());
    req->send(200, "application/json", json);
  });

  // Archivos estáticos desde LittleFS (index.html, style.css, app.js)
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *req) {
    req->send(404, "text/plain", "No encontrado");
  });

  server.begin();
  Serial.println("Servidor web iniciado");
}

// ------------------------------------------------------------
void loop() {
  // Avisar por Serial cuando se conecta o desconecta un celular
  static unsigned long tPrevio = 0;
  static int clientesPrevio = -1;
  if (millis() - tPrevio >= 1000) {
    tPrevio = millis();
    int clientes = WiFi.softAPgetStationNum();
    if (clientes != clientesPrevio) {
      Serial.printf("Dispositivos conectados a la red: %d\n", clientes);
      clientesPrevio = clientes;
    }
  }
}
