#include <Wire.h>
#include "config.h"
#include "bateria.h"

Bateria bateria = { false, 0, 0, 0, 0, BAT_SIN_SENSOR };

// ------------------------------------------------------------
//  INA226 — bajo nivel (registros de 16 bits, MSB primero)
//    0x00 configuración · 0x01 voltaje del shunt (2.5 µV/bit, con signo)
//    0x02 voltaje del bus (1.25 mV/bit) · 0xFE fabricante · 0xFF chip
// ------------------------------------------------------------
static bool inaEscribir(uint8_t reg, uint16_t valor) {
  Wire.beginTransmission(INA226_DIR);
  Wire.write(reg);
  Wire.write(valor >> 8);
  Wire.write(valor & 0xFF);
  return Wire.endTransmission() == 0;
}

static bool inaLeer(uint8_t reg, uint16_t &valor) {
  Wire.beginTransmission(INA226_DIR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t) INA226_DIR, (uint8_t) 2) != 2) return false;
  uint8_t alto = Wire.read();
  uint8_t bajo = Wire.read();
  valor = ((uint16_t) alto << 8) | bajo;
  return true;
}

static bool inaIniciar() {
  uint16_t fabricante, chip;
  if (!inaLeer(0xFE, fabricante) || !inaLeer(0xFF, chip)) return false;
  if (fabricante != 0x5449 || chip != 0x2260) {
    LOG("AVISO: en 0x%02X hay un chip que no es INA226 (fab=0x%04X chip=0x%04X)\n",
        INA226_DIR, fabricante, chip);
    return false;
  }
  // 0x4527: promedio de 16 muestras, conversión de 1.1 ms, modo continuo
  // → dato nuevo cada ~35 ms
  return inaEscribir(0x00, 0x4527);
}

static bool inaMedir(float &voltios, float &amperios) {
  uint16_t rawBus, rawShunt;
  if (!inaLeer(0x02, rawBus) || !inaLeer(0x01, rawShunt)) return false;
  voltios  = rawBus * 0.00125f;
  amperios = (int16_t) rawShunt * 2.5e-6f / SHUNT_OHM;
  return true;
}

// ------------------------------------------------------------
//  Estado y protección
// ------------------------------------------------------------
static float vSimulado = 0;          // > 0: voltaje simulado (pruebas)
static bool  filtroListo = false;
static bool  bajoCorte = false;      // filtrado bajo BAT_AGOTADA_V…
static unsigned long tBajoCorte = 0; // …desde este instante

const char *bateriaNombreEstado(EstadoBateria e) {
  switch (e) {
    case BAT_SIN_SENSOR:  return "SIN SENSOR";
    case BAT_SIN_BATERIA: return "SIN BATERÍA (USB)";
    case BAT_NORMAL:      return "NORMAL";
    case BAT_BAJA:        return "BAJA";
    case BAT_CRITICA:     return "CRÍTICA";
    case BAT_AGOTADA:     return "AGOTADA";
  }
  return "?";
}

static EstadoBateria evaluarEstado(EstadoBateria previo, float v, unsigned long ahora) {
  if (previo == BAT_AGOTADA) return BAT_AGOTADA;       // se mantiene hasta reiniciar
  if (v < BAT_SIN_BATERIA_V) { bajoCorte = false; return BAT_SIN_BATERIA; }

  // Agotada: bajo el voltaje de corte de forma sostenida
  if (v < BAT_AGOTADA_V) {
    if (!bajoCorte) { bajoCorte = true; tBajoCorte = ahora; }
    if (ahora - tBajoCorte >= BAT_AGOTADA_MS) return BAT_AGOTADA;
  } else {
    bajoCorte = false;
  }

  // Histéresis: para salir de crítica/baja el voltaje debe subir un margen
  const float h = BAT_HISTERESIS_V;
  if (v < BAT_CRITICA_V || (previo == BAT_CRITICA && v < BAT_CRITICA_V + h)) return BAT_CRITICA;
  if (v < BAT_BAJA_V ||
      ((previo == BAT_BAJA || previo == BAT_CRITICA) && v < BAT_BAJA_V + h)) return BAT_BAJA;
  return BAT_NORMAL;
}

static void cambiarEstado(EstadoBateria nuevo) {
  if (nuevo == bateria.estado) return;
  LOG("BATERÍA: %s → %s (%.2f V)\n",
      bateriaNombreEstado(bateria.estado), bateriaNombreEstado(nuevo), bateria.v);
  bateria.estado = nuevo;
}

// ------------------------------------------------------------
//  Interfaz
// ------------------------------------------------------------
void bateriaIniciar() {
  bateria.sensor = inaIniciar();
  if (bateria.sensor) {
    LOG("INA226 listo (shunt %.3f Ω, máx %.1f A)\n", SHUNT_OHM, 0.08192f / SHUNT_OHM);
  } else {
    LOG("AVISO: INA226 no encontrado — sin protección de batería\n");
  }
}

void bateriaActualizar(unsigned long ahora) {
  static unsigned long tLectura = 0, tReintento = 0, tLog = 0;
  static int errores = 0;

  // Sin sensor: volver a buscarlo cada REINTENTO_INA_MS
  if (!bateria.sensor) {
    if (ahora - tReintento >= REINTENTO_INA_MS) {
      tReintento = ahora;
      if (inaIniciar()) {
        bateria.sensor = true;
        errores = 0;
        tLectura = 0;
        filtroListo = false;
        LOG("INA226 encontrado\n");
      }
    }
    return;
  }

  if (ahora - tLectura < PERIODO_INA_MS) return;
  unsigned long dt = (tLectura == 0) ? 0 : ahora - tLectura;   // primera: sin integrar
  tLectura = ahora;

  float vBus, amperios;
  if (!inaMedir(vBus, amperios)) {
    if (++errores >= 5) {
      bateria.sensor = false;
      tReintento = ahora;
      if (bateria.estado != BAT_AGOTADA) cambiarEstado(BAT_SIN_SENSOR);
      LOG("AVISO: INA226 dejó de responder — sin protección de batería\n");
    }
    return;
  }
  errores = 0;
  bateria.i = amperios;
  bateria.p = vBus * amperios;
  bateria.mah += amperios * 1000.0f * dt / 3600000.0f;

  // Voltaje filtrado: ignora caídas breves por picos de los motores.
  // Sin batería previa (USB o sensor recién encontrado) toma el valor directo,
  // así al conectar la batería no pasa por "crítica" mientras el filtro sube.
  float vMedido = (vSimulado > 0) ? vSimulado : vBus;
  if (!filtroListo || bateria.estado == BAT_SIN_BATERIA || bateria.estado == BAT_SIN_SENSOR) {
    bateria.v = vMedido;
    filtroListo = true;
  } else {
    float dts = dt / 1000.0f;
    bateria.v += (vMedido - bateria.v) * dts / (BAT_FILTRO_S + dts);
  }
  cambiarEstado(evaluarEstado(bateria.estado, bateria.v, ahora));

  if (ahora - tLog >= PERIODO_LOG_BAT_MS) {
    tLog = ahora;
    LOG("BAT  %.2f V  %.3f A  %.2f W  %.0f mAh  [%s]%s\n",
        bateria.v, bateria.i, bateria.p, bateria.mah,
        bateriaNombreEstado(bateria.estado), vSimulado > 0 ? "  (voltaje simulado)" : "");
  }
}

int bateriaLimiteVelocidad() {
  switch (bateria.estado) {
    case BAT_AGOTADA: return 0;
    case BAT_CRITICA: return BAT_LIMITE_CRITICA;
    default:          return 100;
  }
}

void bateriaSimular(float voltios) {
  vSimulado = (voltios > 0) ? voltios : 0;
  filtroListo = false;                   // el cambio se ve de inmediato
  bajoCorte = false;
  if (vSimulado == 0 && bateria.estado == BAT_AGOTADA) cambiarEstado(BAT_NORMAL);
  if (vSimulado > 0) LOG("SIM: voltaje simulado %.2f V\n", vSimulado);
  else               LOG("SIM: voltaje real del INA226\n");
}
