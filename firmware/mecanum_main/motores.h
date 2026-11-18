#pragma once
#include <Arduino.h>

// ============================================================
//  MOTORES — 74HC595 (dirección) + TB6612 ×2 (PWM)
//  Cinemática mecanum y rampa de aceleración.
// ============================================================

enum { FL = 0, FR = 1, RL = 2, RR = 3 };
extern const char *const NOMBRE_RUEDA[4];

// Comando recibido desde la página
struct Comando {
  int vx, vy, w;   // -100..100
  int vmax;        // 10..100 (%)
  int rueda;       // -1 = movimiento normal; 0..3 = prueba de esa rueda (usa vy)
};

// Configura pines y deja todo detenido y en STANDBY.
void motoresIniciar();

// STBY de los TB6612: true = habilitados, false = standby (no giran aunque haya PWM).
void motoresHabilitar(bool habilitar);

// Detiene de inmediato (sin rampa): PWM 0 y dirección en coast.
void motoresDetener();

// Fija la velocidad deseada de cada rueda a partir del comando.
// vmaxLimite (%) recorta la velocidad máxima (protección de batería).
void motoresFijarObjetivo(const Comando &c, int vmaxLimite);

// Avanza la rampa hacia el objetivo y escribe las salidas.
// Llamar cada PERIODO_CONTROL_MS; dtSeg = tiempo desde la llamada anterior.
void motoresActualizar(float dtSeg);

// Imprime el comando y las salidas objetivo (byte de dirección + PWM).
void motoresImprimirObjetivo(const Comando &c);
