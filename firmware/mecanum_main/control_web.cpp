#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "config.h"
#include "control_web.h"

// Protocolo WebSocket (/ws), mensajes de texto:
//   Página → robot   "C vx vy w vmax"  movimiento (vx,vy,w: -100..100, vmax: 10..100)
//                    "M i v vmax"      prueba de una rueda (i: 0=FL 1=FR 2=RL 3=RR)
//                    "S"               STOP inmediato
//                    "P n"             ping (se devuelve igual)
//   Robot → página   JSON de estado (ver mecanum_main.ino), "P n", "TOMADO"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// Escrito en el callback del WebSocket, leído en loop()
static volatile uint32_t idControl = 0;          // cliente que tiene el control
static volatile bool     expulsarOtros = false;
static volatile bool     hayComandoNuevo = false;
static volatile bool     stopPedido = false;
static volatile bool     controlPerdido = false;
static volatile uint32_t comandosRecibidos = 0;
static volatile unsigned long tUltimoComando = 0;
static Comando comandoRecibido = { 0, 0, 0, 30, -1 };

// ------------------------------------------------------------
//  WebSocket (fuera de loop)
// ------------------------------------------------------------
static void procesarMensaje(AsyncWebSocketClient *client, char *msg) {
  switch (msg[0]) {
    case 'C': {
      Comando c;
      if (sscanf(msg, "C %d %d %d %d", &c.vx, &c.vy, &c.w, &c.vmax) == 4) {
        c.vx   = constrain(c.vx,   -100, 100);
        c.vy   = constrain(c.vy,   -100, 100);
        c.w    = constrain(c.w,    -100, 100);
        c.vmax = constrain(c.vmax,   10, 100);
        c.rueda = -1;
        comandoRecibido = c;
        tUltimoComando = millis();
        comandosRecibidos++;
        hayComandoNuevo = true;
      }
      break;
    }
    case 'M': {
      Comando c = { 0, 0, 0, 0, -1 };
      if (sscanf(msg, "M %d %d %d", &c.rueda, &c.vy, &c.vmax) == 3 && c.rueda >= 0 && c.rueda <= 3) {
        c.vy   = constrain(c.vy,   -100, 100);
        c.vmax = constrain(c.vmax,   10, 100);
        comandoRecibido = c;
        tUltimoComando = millis();
        comandosRecibidos++;
        hayComandoNuevo = true;
      }
      break;
    }
    case 'S':
      comandoRecibido.vx = comandoRecibido.vy = comandoRecibido.w = 0;
      comandoRecibido.rueda = -1;
      hayComandoNuevo = false;          // el STOP gana sobre un comando aún no aplicado
      tUltimoComando = millis();
      stopPedido = true;
      break;
    case 'P':
      client->text(msg);                // eco del ping para medir latencia
      break;
  }
}

static void onWsEvento(AsyncWebSocket *server, AsyncWebSocketClient *client,
                       AwsEventType tipo, void *arg, uint8_t *data, size_t len) {
  switch (tipo) {
    case WS_EVT_CONNECT:
      // El último dispositivo en conectarse toma el control
      idControl = client->id();
      expulsarOtros = true;
      break;

    case WS_EVT_DISCONNECT:
      if (client->id() == idControl) controlPerdido = true;
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
//  Arranque
// ------------------------------------------------------------
static void revisarArchivos() {
  if (!LittleFS.begin()) {
    LOG("ERROR: no se pudo montar LittleFS\n");
    return;
  }
  LOG("Archivos en LittleFS:\n");
  Dir dir = LittleFS.openDir("/");
  bool vacio = true;
  while (dir.next()) {
    LOG("   /%s  (%u bytes)\n", dir.fileName().c_str(), (unsigned) dir.fileSize());
    vacio = false;
  }
  if (vacio) LOG("   (vacío)\n");
  if (!LittleFS.exists("/index.html")) {
    LOG("AVISO: falta /index.html → ejecutar 'Upload Filesystem Image'\n");
  }
}

void webIniciar() {
  revisarArchivos();

  WiFi.persistent(false);               // no guardar config en flash en cada arranque
  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(WIFI_SSID, WIFI_PASS, WIFI_CANAL)) {
    LOG("Red Wi-Fi: %s   clave: %s\n", WIFI_SSID, WIFI_PASS);
    LOG("Abrir en el navegador: http://%s\n", WiFi.softAPIP().toString().c_str());
  } else {
    LOG("ERROR: no se pudo crear la red Wi-Fi\n");
  }

  ws.onEvent(onWsEvento);
  server.addHandler(&ws);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest *req) {
    req->send(404, "text/plain", "No encontrado");
  });
  server.begin();
  LOG("Servidor web iniciado — WebSocket en /ws\n");
}

// ------------------------------------------------------------
//  Interfaz para loop()
// ------------------------------------------------------------
void webAtender() {
  // Un nuevo dispositivo tomó el control: cerrar los anteriores.
  // Se hace aquí y no en el callback porque durante WS_EVT_CONNECT
  // la librería tiene bloqueada la lista de clientes.
  if (expulsarOtros) {
    expulsarOtros = false;
    LOG("Dispositivo #%u tomó el control\n", (unsigned) idControl);
    for (auto &c : ws.getClients()) {
      if (c.id() != idControl && c.status() == WS_CONNECTED) {
        c.text("TOMADO");
        c.close();
        LOG("   dispositivo #%u desconectado\n", (unsigned) c.id());
      }
    }
  }
}

bool webTomarComando(Comando &c) {
  if (!hayComandoNuevo) return false;
  hayComandoNuevo = false;
  c = comandoRecibido;
  return true;
}

bool webTomarStop() {
  if (!stopPedido) return false;
  stopPedido = false;
  return true;
}

bool webTomarControlPerdido() {
  if (!controlPerdido) return false;
  controlPerdido = false;
  return true;
}

unsigned long webTiempoUltimoComando() {
  return tUltimoComando;
}

uint32_t webTomarContadorComandos() {
  uint32_t n = comandosRecibidos;
  comandosRecibidos = 0;
  return n;
}

int webDispositivos() {
  return WiFi.softAPgetStationNum();
}

void webEnviarEstado(const char *json) {
  ws.textAll(json);
  ws.cleanupClients();
}
