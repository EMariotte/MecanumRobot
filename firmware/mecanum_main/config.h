#pragma once
#include <Arduino.h>

// ============================================================
//  CONFIGURACIÓN — Robot Mecanum 4WD, firmware principal
//  Pines (fijos, ver CLAUDE.md), red Wi-Fi, tiempos,
//  protección de batería y calibración de las ruedas.
// ============================================================

#define FW_VERSION "0.1"

// --- Depuración por Monitor Serial ---
// 1 = mensajes por Serial y comandos de prueba (sim).
// Poner en 0 cuando se monten los VL53L0X: sus XSHUT usan GPIO1/GPIO3
// (TX/RX) y el Serial deja de estar disponible.
#define DEPURACION 1

#if DEPURACION
  #define LOG(...) Serial.printf(__VA_ARGS__)
#else
  #define LOG(...) do {} while (0)
#endif

// --- Red Wi-Fi propia (modo Access Point) — IP 192.168.4.1 ---
#define WIFI_SSID   "MecanumRobot"
#define WIFI_PASS   "mecanum2026"   // mínimo 8 caracteres — cambiar si hay varios robots
#define WIFI_CANAL  6

// --- 74HC595 (dirección de los motores) ---
#define DS_PIN    0   // GPIO0  / D3
#define SHCP_PIN  2   // GPIO2  / D4
#define STCP_PIN  10  // GPIO10 / SD3 (requiere flash en modo DIO)

// --- TB6612 ×2 ---
#define STBY_PIN  16  // GPIO16 / D0
#define PWM_FL    12  // D6
#define PWM_FR    13  // D7
#define PWM_RL    14  // D5
#define PWM_RR    15  // D8

// --- Bus I2C ---
#define SDA_PIN         4   // GPIO4 / D2
#define SCL_PIN         5   // GPIO5 / D1
#define I2C_FRECUENCIA  400000

// --- PWM ---
#define PWM_FRECUENCIA  1000   // Hz
#define PWM_MAX         255    // rango de analogWrite
#define PWM_MIN_PCT     30     // arranque mínimo medido en 3c: por debajo no giran
#define PWM_MIN         ((PWM_MIN_PCT * PWM_MAX + 50) / 100)
#define UMBRAL_RUEDA    0.08f  // bajo esto (0..1) la rueda se apaga en vez de saltar al mínimo

// --- Control ---
#define FAILSAFE_MS         300   // sin comandos en este tiempo → motores detenidos
#define PERIODO_CONTROL_MS  20    // actualización de motores: 50 veces/s
#define RAMPA_MS            200   // tiempo para ir de 0 a 100% de velocidad (0 = sin rampa)
                                  // STOP y failsafe frenan de inmediato, sin rampa
#define PERIODO_ESTADO_MS   1000  // estado a la página

// --- INA226 (monitor de batería) ---
#define INA226_DIR          0x40    // A0 = A1 = GND
#define SHUNT_OHM           0.010f  // resistencia R010 del módulo
#define PERIODO_INA_MS      100
#define REINTENTO_INA_MS    5000
#define PERIODO_LOG_BAT_MS  5000

// --- Protección de batería 2S Li-ion ---
// Se evalúa con el voltaje filtrado para no reaccionar a caídas breves
// cuando los motores arrancan o invierten el sentido.
#define CELDAS              2
#define BAT_FILTRO_S        2.0f              // constante de tiempo del filtro (s)
#define BAT_SIN_BATERIA_V   3.0f              // por debajo: solo USB, sin batería
#define BAT_BAJA_V          (3.30f * CELDAS)  // 6.6 V → aviso
#define BAT_CRITICA_V       (3.10f * CELDAS)  // 6.2 V → velocidad limitada
#define BAT_AGOTADA_V       (3.00f * CELDAS)  // 6.0 V sostenido → motores bloqueados
#define BAT_AGOTADA_MS      5000              // tiempo bajo 6.0 V antes de bloquear
#define BAT_HISTERESIS_V    0.15f             // margen para salir de baja / crítica
#define BAT_LIMITE_CRITICA  50                // % de velocidad máxima con batería crítica

// ------------------------------------------------------------
//  CALIBRACIÓN — validada en 3c con estos valores por defecto
// ------------------------------------------------------------
#define SIGNO_VX  1   // -1 si "derecha" mueve el robot a la izquierda
#define SIGNO_W   1   // -1 si "rotar derecha" gira al revés
// Inversión por rueda (FL, FR, RL, RR): -1 si esa rueda gira al revés
const int8_t INVERTIR[4] = { 1, 1, 1, 1 };
