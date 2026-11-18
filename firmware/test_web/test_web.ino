#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ============================================================
//  PRUEBA CONTROL WEB — Paso 3c
//  Robot Mecanum 4WD — Fase 6
//  El robot crea su propia red Wi-Fi (Access Point), sirve la
//  página guardada en LittleFS y recibe comandos por WebSocket.
//  En este sub-paso los MOTORES SE MUEVEN: cinemática mecanum
//  aplicada a los botones de la página + prueba de ruedas.
//
//  ⚠ Primera prueba con el robot levantado (ruedas en el aire)
//    y velocidad máxima baja.
//
//  Carga en dos partes desde PlatformIO:
//    1. Upload                     → este programa
//    2. Upload Filesystem Image    → contenido de data/
//
//  Protocolo WebSocket (/ws), mensajes de texto:
//    Página → robot   "C vx vy w vmax"  movimiento (vx,vy,w: -100..100, vmax: 10..100)
//                     "M i v vmax"      prueba de una rueda (i: 0=FL 1=FR 2=RL 3=RR, v: -100..100)
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

// --- PWM ---
#define PWM_FRECUENCIA 1000   // Hz
#define PWM_MAX        255    // rango de analogWrite

// --- Control ---
#define FAILSAFE_MS       300   // sin comandos en este tiempo → motores detenidos
#define PERIODO_ESTADO_MS 1000

// ------------------------------------------------------------
//  CALIBRACIÓN — ajustar según la prueba en el robot real
// ------------------------------------------------------------
// Signo de los ejes: si "Derecha" mueve el robot a la izquierda,
// cambiar SIGNO_VX a -1. Si "Rotar derecha" gira al revés, SIGNO_W a -1.
#define SIGNO_VX  1
#define SIGNO_W   1
// Inversión por rueda (FL, FR, RL, RR): si en la prueba de ruedas una
// gira hacia atrás, poner -1 en su posición (o intercambiar sus cables
// en la bornera).
const int8_t INVERTIR[4] = { 1, 1, 1, 1 };

// Índices y nombres de las ruedas
enum { FL = 0, FR = 1, RL = 2, RR = 3 };
const char *NOMBRE_RUEDA[4] = { "FL", "FR", "RL", "RR" };
const uint8_t PIN_PWM[4] = { PWM_FL, PWM_FR, PWM_RL, PWM_RR };

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ------------------------------------------------------------
//  Estado del control (escrito en el callback del WebSocket,
//  leído en loop())
// ------------------------------------------------------------
struct Comando {
  int vx, vy, w, vmax;
  int rueda;          // -1 = movimiento normal; 0..3 = prueba de esa rueda (usa vy)
};

volatile uint32_t idControl = 0;          // cliente que tiene el control
volatile bool     expulsarOtros = false;  // cerrar clientes viejos en loop()
volatile bool     hayComandoNuevo = false;
volatile bool     stopPedido = false;
volatile bool     clienteDesconectado = false;
volatile uint32_t comandosRecibidos = 0;  // contador para calcular comandos/s
volatile unsigned long tUltimoComando = 0;
Comando comandoActual = {0, 0, 0, 30, -1};

bool failsafeActivo = true;               // arranca detenido hasta recibir comandos

// ------------------------------------------------------------
//  Bajo nivel — motores
// ------------------------------------------------------------
void shiftWrite(byte data) {
  digitalWrite(STCP_PIN, LOW);
  shiftOut(DS_PIN, SHCP_PIN, MSBFIRST, data);
  digitalWrite(STCP_PIN, HIGH);
}

// Aplica dirección y PWM a las 4 ruedas.
// velocidad[i]: -1.0 (atrás máx) … +1.0 (adelante máx), ya con calibración.
// Bits por rueda en el 74HC595 (par Qn+1,Qn): 01 adelante, 10 atrás, 00 libre.
void aplicarRuedas(const float velocidad[4], int vmax, byte *dirOut, int *pwmOut) {
  byte dir = 0;
  for (int i = 0; i < 4; i++) {
    float v = velocidad[i];
    int pwm = (int) roundf(fabsf(v) * vmax / 100.0f * PWM_MAX);
    if (pwm > 0) dir |= (v > 0 ? 0b01 : 0b10) << (2 * i);
    pwmOut[i] = pwm;
  }
  shiftWrite(dir);                         // primero dirección…
  for (int i = 0; i < 4; i++) analogWrite(PIN_PWM[i], pwmOut[i]);   // …luego velocidad
  *dirOut = dir;
}

void detenerMotores() {
  for (int i = 0; i < 4; i++) analogWrite(PIN_PWM[i], 0);
  shiftWrite(0x00);                        // dirección: coast
}

void inicializarMotores() {
  pinMode(STBY_PIN, OUTPUT); digitalWrite(STBY_PIN, LOW);   // standby mientras arranca
  for (int i = 0; i < 4; i++) pinMode(PIN_PWM[i], OUTPUT);
  pinMode(DS_PIN,   OUTPUT);
  pinMode(SHCP_PIN, OUTPUT);
  pinMode(STCP_PIN, OUTPUT);
  analogWriteRange(PWM_MAX);
  analogWriteFreq(PWM_FRECUENCIA);
  detenerMotores();
}

// ------------------------------------------------------------
//  Lógica de movimiento — cinemática mecanum
// ------------------------------------------------------------
//   FL = vy + vx + w     FR = vy − vx − w
//   RL = vy − vx + w     RR = vy + vx − w
// Se normaliza para que ninguna rueda pase de ±1.
void calcularVelocidades(const Comando &c, float velocidad[4]) {
  if (c.rueda >= 0) {
    // Prueba de una rueda: solo esa gira, con valor vy
    for (int i = 0; i < 4; i++) velocidad[i] = 0;
    velocidad[c.rueda] = c.vy / 100.0f;
  } else {
    float vx = SIGNO_VX * c.vx / 100.0f;
    float vy = c.vy / 100.0f;
    float w  = SIGNO_W  * c.w  / 100.0f;
    velocidad[FL] = vy + vx + w;
    velocidad[FR] = vy - vx - w;
    velocidad[RL] = vy - vx + w;
    velocidad[RR] = vy + vx - w;

    float maximo = 1.0f;
    for (int i = 0; i < 4; i++) maximo = max(maximo, fabsf(velocidad[i]));
    for (int i = 0; i < 4; i++) velocidad[i] /= maximo;
  }
  for (int i = 0; i < 4; i++) velocidad[i] *= INVERTIR[i];
}

void imprimirAplicado(const Comando &c, byte dir, const int pwm[4]) {
  if (c.rueda >= 0) {
    Serial.printf("RUEDA %s  v=%4d  vmax=%3d%%", NOMBRE_RUEDA[c.rueda], c.vy, c.vmax);
  } else {
    Serial.printf("CMD  vx=%4d  vy=%4d  w=%4d  vmax=%3d%%", c.vx, c.vy, c.w, c.vmax);
  }
  // Byte en el orden de la tabla del CLAUDE.md: RR RL FR FL (Q7..Q0)
  Serial.print("  →  dir ");
  for (int i = 3; i >= 0; i--) {
    Serial.print((dir >> (2 * i + 1)) & 1);
    Serial.print((dir >> (2 * i)) & 1);
    Serial.print(i > 0 ? " " : "");
  }
  Serial.printf(" (0x%02X)  pwm FL=%d FR=%d RL=%d RR=%d\n", dir, pwm[FL], pwm[FR], pwm[RL], pwm[RR]);
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
        c.rueda = -1;
        comandoActual = c;
        tUltimoComando = millis();
        comandosRecibidos++;
        hayComandoNuevo = true;
      }
      break;
    }
    case 'M': {
      Comando c = {0, 0, 0, 0, -1};
      if (sscanf(msg, "M %d %d %d", &c.rueda, &c.vy, &c.vmax) == 3 && c.rueda >= 0 && c.rueda <= 3) {
        c.vy   = constrain(c.vy,   -100, 100);
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
      comandoActual.rueda = -1;
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
  inicializarMotores();            // primero: garantizar que nada se mueve
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== TEST WEB — Paso 3c (motores activos) ===");
  Serial.printf("Calibración: SIGNO_VX=%d  SIGNO_W=%d  INVERTIR={%d,%d,%d,%d}\n",
                SIGNO_VX, SIGNO_W, INVERTIR[FL], INVERTIR[FR], INVERTIR[RL], INVERTIR[RR]);

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

  // Motores listos: salir de standby (dirección en coast y PWM en 0)
  digitalWrite(STBY_PIN, HIGH);
  Serial.println("TB6612 habilitados (STBY = HIGH)");
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
    detenerMotores();
    Serial.println("El dispositivo con el control se desconectó → motores detenidos");
  }

  // --- STOP pedido desde la página ---
  if (stopPedido) {
    stopPedido = false;
    detenerMotores();
    Serial.println(">> STOP desde la página");
  }

  // --- Comando nuevo: aplicar a los motores si cambió ---
  if (hayComandoNuevo) {
    hayComandoNuevo = false;
    static Comando aplicado = {999, 999, 999, 999, 999};
    Comando c = comandoActual;
    bool cambio = c.vx != aplicado.vx || c.vy != aplicado.vy || c.w != aplicado.w ||
                  c.vmax != aplicado.vmax || c.rueda != aplicado.rueda;
    if (cambio || failsafeActivo) {
      float velocidad[4];
      byte dir;
      int pwm[4];
      calcularVelocidades(c, velocidad);
      aplicarRuedas(velocidad, c.vmax, &dir, pwm);
      if (cambio) imprimirAplicado(c, dir, pwm);
      aplicado = c;
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
