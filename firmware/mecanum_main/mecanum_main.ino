#include <Arduino.h>
#include "config.h"
#include "motores.h"
#include "bus_i2c.h"
#include "bateria.h"
#include "control_web.h"

// ============================================================
//  FIRMWARE PRINCIPAL — Robot Mecanum 4WD — Paso 5a
//
//  Integra lo validado en test_web:
//    · Wi-Fi AP + página en LittleFS + WebSocket (dos joysticks)
//    · cinemática mecanum con PWM mínimo de arranque
//    · failsafe de 300 ms y "un solo dispositivo con el control"
//    · telemetría de batería con INA226
//  Y agrega:
//    · rampa de aceleración (RAMPA_MS) para suavizar arranques e
//      inversiones (menos picos de corriente)
//    · protección de batería: velocidad limitada con batería crítica
//      y motores bloqueados con batería agotada
//    · depuración por Serial desactivable (DEPURACION en config.h)
//
//  Módulos:
//    config.h        pines, tiempos, umbrales y calibración
//    motores.*       74HC595 + TB6612, cinemática, rampa
//    bus_i2c.*       bus I2C y escaneo (sensores: paso 5b)
//    bateria.*       INA226 y protección de la batería
//    control_web.*   Wi-Fi, servidor, WebSocket y protocolo
//
//  Carga en dos partes desde PlatformIO:
//    1. Upload                     → este programa
//    2. Upload Filesystem Image    → contenido de data/
//
//  Estado a la página (1 vez/s):
//    {"up","heap","cli","hz","fs","bat","lim","v","i","p","mah","fw"}
//    bat: 0 sin sensor · 1 sin batería (USB) · 2 normal · 3 baja
//         4 crítica · 5 agotada        lim: velocidad máxima permitida (%)
// ============================================================

bool failsafeActivo = true;      // arranca detenido hasta recibir comandos
bool motoresBloqueados = false;  // por batería agotada

// ------------------------------------------------------------
//  Comandos por Monitor Serial (solo con DEPURACION)
//    sim 6.1   → simula ese voltaje de batería (prueba de la protección)
//    sim 0     → vuelve al voltaje real (y desbloquea si quedó AGOTADA)
// ------------------------------------------------------------
#if DEPURACION
void procesarLineaSerial(char *linea) {
  if (strncmp(linea, "sim", 3) == 0) {
    bateriaSimular(atof(linea + 3));
  } else if (linea[0] != '\0') {
    LOG("Comandos: 'sim <voltios>' simula la batería · 'sim 0' vuelve al valor real\n");
  }
}

void atenderSerial() {
  static char linea[24];
  static uint8_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      linea[n] = '\0';
      n = 0;
      procesarLineaSerial(linea);
    } else if (n < sizeof(linea) - 1) {
      linea[n++] = ch;
    }
  }
}
#endif

// ------------------------------------------------------------
void setup() {
  motoresIniciar();                // primero: garantizar que nada se mueve
#if DEPURACION
  Serial.begin(115200);
  delay(100);
#endif
  LOG("\n\n=== MECANUM MAIN v%s — Paso 5a ===\n", FW_VERSION);
  LOG("Calibración: SIGNO_VX=%d  SIGNO_W=%d  INVERTIR={%d,%d,%d,%d}\n",
      SIGNO_VX, SIGNO_W, INVERTIR[FL], INVERTIR[FR], INVERTIR[RL], INVERTIR[RR]);
  LOG("PWM mínimo %d%%  ·  rampa %d ms (0 → 100%%)\n", PWM_MIN_PCT, RAMPA_MS);

  i2cIniciar();
  i2cEscanear();
  bateriaIniciar();
  webIniciar();

  motoresHabilitar(true);          // salir de standby (dirección coast, PWM 0)
  LOG("TB6612 habilitados (STBY = HIGH)\n");
  LOG("Comandos serie: 'sim <voltios>' · 'sim 0'\n");
}

// ------------------------------------------------------------
void loop() {
  unsigned long ahora = millis();

  webAtender();
#if DEPURACION
  atenderSerial();
#endif

  // --- Eventos que detienen de inmediato ---
  if (webTomarControlPerdido()) {
    motoresDetener();
    LOG("El dispositivo con el control se desconectó → motores detenidos\n");
  }
  if (webTomarStop()) {
    motoresDetener();
    LOG(">> STOP desde la página\n");
  }

  // --- Comando nuevo → velocidad objetivo de cada rueda ---
  // El Monitor Serial imprime como máximo 5 veces por segundo.
  static Comando ultimo = { 999, 999, 999, 999, 999 };
  static bool pendienteImprimir = false;
  static unsigned long tImpresion = 0;

  Comando c;
  if (webTomarComando(c)) {
    motoresFijarObjetivo(c, bateriaLimiteVelocidad());
    if (c.vx != ultimo.vx || c.vy != ultimo.vy || c.w != ultimo.w ||
        c.vmax != ultimo.vmax || c.rueda != ultimo.rueda) {
      pendienteImprimir = true;
      ultimo = c;
    }
    if (failsafeActivo) {
      failsafeActivo = false;
      LOG("Comandos recibiéndose — control activo\n");
    }
  }
  if (pendienteImprimir && ahora - tImpresion >= 200) {
    pendienteImprimir = false;
    tImpresion = ahora;
    motoresImprimirObjetivo(ultimo);
  }

  // --- Failsafe: sin comandos recientes → detener ---
  if (!failsafeActivo && ahora - webTiempoUltimoComando() > FAILSAFE_MS) {
    failsafeActivo = true;
    motoresDetener();
    LOG("FAILSAFE: sin comandos por %d ms → motores detenidos\n", FAILSAFE_MS);
  }

  // --- Motores: rampa y salidas, 50 veces/s ---
  static unsigned long tControl = 0;
  if (ahora - tControl >= PERIODO_CONTROL_MS) {
    float dt = min((ahora - tControl) / 1000.0f, 0.1f);   // acotado si el loop se demoró
    tControl = ahora;
    motoresActualizar(dt);
  }

  // --- Batería y protección ---
  bateriaActualizar(ahora);
  bool bloquear = (bateria.estado == BAT_AGOTADA);
  if (bloquear != motoresBloqueados) {
    motoresBloqueados = bloquear;
    if (bloquear) {
      motoresDetener();
      motoresHabilitar(false);
      LOG("BATERÍA AGOTADA → motores bloqueados (STBY = LOW). Cargar la batería y reiniciar.\n");
    } else {
      motoresHabilitar(true);
      LOG("Motores desbloqueados (STBY = HIGH)\n");
    }
  }

  // --- Estado a la página, 1 vez/s ---
  static unsigned long tEstado = 0;
  if (ahora - tEstado >= PERIODO_ESTADO_MS) {
    tEstado = ahora;
    char json[220];
    snprintf(json, sizeof(json),
             "{\"up\":%lu,\"heap\":%u,\"cli\":%d,\"hz\":%u,\"fs\":%d,"
             "\"bat\":%d,\"lim\":%d,\"v\":%.2f,\"i\":%.3f,\"p\":%.2f,\"mah\":%.0f,"
             "\"fw\":\"%s\"}",
             ahora / 1000, (unsigned) ESP.getFreeHeap(), webDispositivos(),
             (unsigned) webTomarContadorComandos(), failsafeActivo ? 1 : 0,
             (int) bateria.estado, bateriaLimiteVelocidad(),
             bateria.v, bateria.i, bateria.p, bateria.mah, FW_VERSION);
    webEnviarEstado(json);
  }
}
