#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ============================================================
//  PRUEBA CONTROL WEB — Paso 3b
//  Robot Mecanum 4WD — Fase 6
//  El robot crea su propia red Wi-Fi (Access Point), sirve la
//  página guardada en LittleFS y recibe comandos por WebSocket.
//  Los comandos se muestran en el Monitor Serial.
//  Motores DESHABILITADOS en este sub-paso (STBY = LOW).
//
//  Carga en dos partes desde PlatformIO:
//    1. Upload                     → este programa
//    2. Upload Filesystem Image    → contenido de data/
//
//  Protocolo WebSocket (/ws), mensajes de texto:
//    Página → robot   "C vx vy w vmax"  comando (vx,vy,w: -100..100, vmax: 10..100)
//                     "S"               STOP inmediato
//                     "P n"             ping (el robot lo devuelve igual)
//    Robot → página   {"up":..,"heap":..,"cli":..,"hz":..,"fs":0|1}  estado, 1 vez/s
//                     "P n"             respuesta al ping
//                     "TOMADO"          otro dispositivo tomó el control
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

// --- Control ---
#define FAILSAFE_MS   300   // sin comandos en este tiempo → motores detenidos
#define PERIODO_ESTADO_MS 1000

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ------------------------------------------------------------
//  Estado del control (escrito en el callback del WebSocket,
//  leído en loop())
// ------------------------------------------------------------
struct Comando {
  int vx, vy, w, vmax;
};

volatile uint32_t idControl = 0;          // cliente que tiene el control
volatile bool     expulsarOtros = false;  // cerrar clientes viejos en loop()
volatile bool     hayComandoNuevo = false;
volatile bool     stopPedido = false;
volatile bool     clienteDesconectado = false;
volatile uint32_t comandosRecibidos = 0;  // contador para calcular comandos/s
volatile unsigned long tUltimoComando = 0;
Comando comandoActual = {0, 0, 0, 50};

bool failsafeActivo = true;               // arranca detenido hasta recibir comandos

// ------------------------------------------------------------
//  Bajo nivel — motores
// ------------------------------------------------------------
void shiftWrite(byte data) {
  digitalWrite(STCP_PIN, LOW);
  shiftOut(DS_PIN, SHCP_PIN, MSBFIRST, data);
  digitalWrite(STCP_PIN, HIGH);
}

void detenerMotores() {
  analogWrite(PWM_FL, 0);
  analogWrite(PWM_FR, 0);
  analogWrite(PWM_RL, 0);
  analogWrite(PWM_RR, 0);
  shiftWrite(0x00);                                          // dirección: coast
}

void motoresApagados() {
  pinMode(STBY_PIN, OUTPUT); digitalWrite(STBY_PIN, LOW);   // TB6612 en standby
  pinMode(PWM_FL, OUTPUT);
  pinMode(PWM_FR, OUTPUT);
  pinMode(PWM_RL, OUTPUT);
  pinMode(PWM_RR, OUTPUT);
  pinMode(DS_PIN,   OUTPUT);
  pinMode(SHCP_PIN, OUTPUT);
  pinMode(STCP_PIN, OUTPUT);
  detenerMotores();
}

// ------------------------------------------------------------
//  WebSocket — se ejecuta fuera de loop(): solo guarda datos y
//  levanta banderas; las impresiones y acciones van en loop()
// ------------------------------------------------------------
void procesarMensaje(AsyncWebSocketClient *client, char *msg) {
  switch (msg[0]) {
    case 'C': {
      Comando c;
      if (sscanf(msg, "C %d %d %d %d", &c.vx, &c.vy, &c.w, &c.vmax) == 4) {
        c.vx   = constrain(c.vx,   -100, 100);
        c.vy   = constrain(c.vy,   -100, 100);
        c.w    = constrain(c.w,    -100, 100);
        c.vmax = constrain(c.vmax,   10, 100);
        comandoActual = c;
        tUltimoComando = millis();
        comandosRecibidos++;
        hayComandoNuevo = true;
      }
      break;
    }
    case 'S':
      comandoActual.vx = comandoActual.vy = comandoActual.w = 0;
      tUltimoComando = millis();
      stopPedido = true;
      break;
    case 'P':
      client->text(msg);            // eco del ping para medir latencia
      break;
  }
}

void onWsEvento(AsyncWebSocket *server, AsyncWebSocketClient *client,
                AwsEventType tipo, void *arg, uint8_t *data, size_t len) {
  switch (tipo) {
    case WS_EVT_CONNECT:
      // El último dispositivo en conectarse toma el control
      idControl = client->id();
      expulsarOtros = true;
      break;

    case WS_EVT_DISCONNECT:
      if (client->id() == idControl) clienteDesconectado = true;
      break;

    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo *) arg;
      // Solo mensajes de texto completos en un solo fragmento
      if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) return;
      if (client->id() != idControl) return;    // ignorar dispositivos sin el control
      char buf[48];
      size_t n = min(len, sizeof(buf) - 1);
      memcpy(buf, data, n);
      buf[n] = '\0';
      procesarMensaje(client, buf);
      break;
    }

    default:
      break;
  }
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
  Serial.println("\n\n=== TEST WEB — Paso 3b (WebSocket) ===");

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

  // --- WebSocket ---
  ws.onEvent(onWsEvento);
  server.addHandler(&ws);

  // --- Rutas HTTP ---
  server.on("/estado", HTTP_GET, [](AsyncWebServerRequest *req) {
    char json[96];
    snprintf(json, sizeof(json),
             "{\"uptime\":%lu,\"heap\":%u,\"clientes\":%d}",
             millis() / 1000, (unsigned) ESP.getFreeHeap(), WiFi.softAPgetStationNum());
    req->send(200, "application/json", json);
  });

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *req) {
    req->send(404, "text/plain", "No encontrado");
  });

  server.begin();
  Serial.println("Servidor web iniciado — WebSocket en /ws");
}

// ------------------------------------------------------------
void loop() {
  unsigned long ahora = millis();

  // --- Un nuevo dispositivo tomó el control: cerrar los anteriores ---
  if (expulsarOtros) {
    expulsarOtros = false;
    Serial.printf("Dispositivo #%u tomó el control\n", (unsigned) idControl);
    for (auto &c : ws.getClients()) {
      if (c.id() != idControl && c.status() == WS_CONNECTED) {
        c.text("TOMADO");
        c.close();
        Serial.printf("   dispositivo #%u desconectado\n", (unsigned) c.id());
      }
    }
  }

  // --- Desconexión del dispositivo con el control ---
  if (clienteDesconectado) {
    clienteDesconectado = false;
    Serial.println("El dispositivo con el control se desconectó");
  }

  // --- STOP pedido desde la página ---
  if (stopPedido) {
    stopPedido = false;
    detenerMotores();
    Serial.println(">> STOP desde la página");
  }

  // --- Comando nuevo: imprimir solo si cambió ---
  if (hayComandoNuevo) {
    hayComandoNuevo = false;
    static Comando impreso = {999, 999, 999, 999};
    Comando c = comandoActual;
    if (c.vx != impreso.vx || c.vy != impreso.vy || c.w != impreso.w || c.vmax != impreso.vmax) {
      Serial.printf("CMD  vx=%4d  vy=%4d  w=%4d  vmax=%3d%%\n", c.vx, c.vy, c.w, c.vmax);
      impreso = c;
    }
    if (failsafeActivo) {
      failsafeActivo = false;
      Serial.println("Comandos recibiéndose — control activo");
    }
  }

  // --- Failsafe: sin comandos recientes → detener ---
  if (!failsafeActivo && ahora - tUltimoComando > FAILSAFE_MS) {
    failsafeActivo = true;
    detenerMotores();
    Serial.printf("FAILSAFE: sin comandos por %d ms → motores detenidos\n", FAILSAFE_MS);
  }

  // --- Estado a la página y frecuencia de comandos, 1 vez/s ---
  static unsigned long tEstado = 0;
  if (ahora - tEstado >= PERIODO_ESTADO_MS) {
    tEstado = ahora;
    uint32_t hz = comandosRecibidos;
    comandosRecibidos = 0;

    char json[112];
    snprintf(json, sizeof(json),
             "{\"up\":%lu,\"heap\":%u,\"cli\":%d,\"hz\":%u,\"fs\":%d}",
             ahora / 1000, (unsigned) ESP.getFreeHeap(), WiFi.softAPgetStationNum(),
             (unsigned) hz, failsafeActivo ? 1 : 0);
    ws.textAll(json);
    ws.cleanupClients();
  }
}
