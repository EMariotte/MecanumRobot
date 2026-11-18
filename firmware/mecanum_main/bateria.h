#pragma once
#include <Arduino.h>

// ============================================================
//  BATERÍA — INA226 por I2C + protección de la batería 2S
// ============================================================

// Estado de la batería (se envía a la página como número en "bat")
enum EstadoBateria : uint8_t {
  BAT_SIN_SENSOR  = 0,   // INA226 no responde: sin protección
  BAT_SIN_BATERIA = 1,   // voltaje ~0: alimentado solo por USB
  BAT_NORMAL      = 2,
  BAT_BAJA        = 3,   // aviso
  BAT_CRITICA     = 4,   // velocidad máxima limitada
  BAT_AGOTADA     = 5,   // motores bloqueados hasta reiniciar
};

struct Bateria {
  bool  sensor;          // INA226 respondiendo
  float v;               // voltaje filtrado (V)
  float i;               // corriente (A); positiva = la batería entrega
  float p;               // potencia (W)
  float mah;             // consumidos desde el arranque
  EstadoBateria estado;
};
extern Bateria bateria;

// Busca el INA226 (llamar después de i2cIniciar).
void bateriaIniciar();

// Lectura periódica, filtro, estado y reintento si el sensor no responde.
void bateriaActualizar(unsigned long ahora);

// Velocidad máxima permitida (%): 100, BAT_LIMITE_CRITICA o 0.
int bateriaLimiteVelocidad();

const char *bateriaNombreEstado(EstadoBateria e);

// Solo pruebas: reemplaza el voltaje medido por uno fijo. 0 = volver al real
// (y desbloquear si quedó en AGOTADA).
void bateriaSimular(float voltios);
