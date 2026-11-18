#include "config.h"
#include "motores.h"

const char *const NOMBRE_RUEDA[4] = { "FL", "FR", "RL", "RR" };
static const uint8_t PIN_PWM[4] = { PWM_FL, PWM_FR, PWM_RL, PWM_RR };

static float objetivo[4] = { 0, 0, 0, 0 };  // velocidad deseada por rueda, -1..1
static float actual[4]   = { 0, 0, 0, 0 };  // velocidad aplicada (sigue al objetivo con la rampa)
static int   pwmTope     = PWM_MIN;         // PWM a velocidad máxima elegida

// Últimas salidas escritas (para no reescribir si no cambian)
static byte dirSalida = 0;
static int  pwmSalida[4] = { 0, 0, 0, 0 };

// ------------------------------------------------------------
//  Bajo nivel
// ------------------------------------------------------------
static void shiftWrite(byte data) {
  digitalWrite(STCP_PIN, LOW);
  shiftOut(DS_PIN, SHCP_PIN, MSBFIRST, data);
  digitalWrite(STCP_PIN, HIGH);
}

// Primero dirección, luego velocidad. forzar = escribir aunque no haya cambios.
static void escribirSalidas(byte dir, const int pwm[4], bool forzar) {
  if (forzar || dir != dirSalida) shiftWrite(dir);
  for (int i = 0; i < 4; i++) {
    if (forzar || pwm[i] != pwmSalida[i]) analogWrite(PIN_PWM[i], pwm[i]);
    pwmSalida[i] = pwm[i];
  }
  dirSalida = dir;
}

// Magnitud 0..1 → PWM: 0 bajo el umbral; si no, de PWM_MIN (arranque) a tope.
static int magnitudAPwm(float magnitud, int tope) {
  if (magnitud < UMBRAL_RUEDA) return 0;
  float t = (magnitud - UMBRAL_RUEDA) / (1.0f - UMBRAL_RUEDA);   // 0..1
  return PWM_MIN + (int) roundf(t * (tope - PWM_MIN));
}

// Velocidades -1..1 → byte del 74HC595 + PWM por rueda (sin tocar el hardware).
// Bits por rueda (par Qn+1,Qn): 01 adelante, 10 atrás, 00 libre (coast).
static void calcularSalidas(const float v[4], int tope, byte &dir, int pwm[4]) {
  dir = 0;
  for (int i = 0; i < 4; i++) {
    pwm[i] = magnitudAPwm(fabsf(v[i]), tope);
    if (pwm[i] > 0) dir |= (v[i] > 0 ? 0b01 : 0b10) << (2 * i);
  }
}

// ------------------------------------------------------------
//  Cinemática mecanum
//    FL = vy + vx + w     FR = vy − vx − w
//    RL = vy − vx + w     RR = vy + vx − w
//  Se normaliza para que ninguna rueda pase de ±1.
// ------------------------------------------------------------
static void calcularVelocidades(const Comando &c, float v[4]) {
  if (c.rueda >= 0 && c.rueda <= 3) {
    for (int i = 0; i < 4; i++) v[i] = 0;
    v[c.rueda] = c.vy / 100.0f;                 // prueba de una rueda
  } else {
    float vx = SIGNO_VX * c.vx / 100.0f;
    float vy = c.vy / 100.0f;
    float w  = SIGNO_W  * c.w  / 100.0f;
    v[FL] = vy + vx + w;
    v[FR] = vy - vx - w;
    v[RL] = vy - vx + w;
    v[RR] = vy + vx - w;
    float maximo = 1.0f;
    for (int i = 0; i < 4; i++) maximo = max(maximo, fabsf(v[i]));
    for (int i = 0; i < 4; i++) v[i] /= maximo;
  }
  for (int i = 0; i < 4; i++) v[i] *= INVERTIR[i];
}

// ------------------------------------------------------------
//  Interfaz
// ------------------------------------------------------------
void motoresIniciar() {
  pinMode(STBY_PIN, OUTPUT);
  digitalWrite(STBY_PIN, LOW);                  // standby mientras arranca
  for (int i = 0; i < 4; i++) pinMode(PIN_PWM[i], OUTPUT);
  pinMode(DS_PIN,   OUTPUT);
  pinMode(SHCP_PIN, OUTPUT);
  pinMode(STCP_PIN, OUTPUT);
  analogWriteRange(PWM_MAX);
  analogWriteFreq(PWM_FRECUENCIA);
  motoresDetener();
}

void motoresHabilitar(bool habilitar) {
  digitalWrite(STBY_PIN, habilitar ? HIGH : LOW);
}

void motoresDetener() {
  const int cero[4] = { 0, 0, 0, 0 };
  for (int i = 0; i < 4; i++) objetivo[i] = actual[i] = 0;
  escribirSalidas(0x00, cero, true);
}

void motoresFijarObjetivo(const Comando &c, int vmaxLimite) {
  int vmax = min(c.vmax, vmaxLimite);
  if (vmax <= 0) {                              // movimiento bloqueado
    for (int i = 0; i < 4; i++) objetivo[i] = 0;
    return;
  }
  pwmTope = max((int) roundf(vmax / 100.0f * PWM_MAX), PWM_MIN);
  calcularVelocidades(c, objetivo);
}

void motoresActualizar(float dtSeg) {
  // Rampa: cada rueda cambia como máximo "paso" por llamada.
  // Al invertir el sentido pasa por cero (bajo el umbral) antes de cambiar la dirección.
  float paso = (RAMPA_MS > 0) ? dtSeg * 1000.0f / RAMPA_MS : 2.0f;
  for (int i = 0; i < 4; i++) {
    float d = objetivo[i] - actual[i];
    if (fabsf(d) <= paso) actual[i] = objetivo[i];
    else                  actual[i] += (d > 0) ? paso : -paso;
  }
  byte dir;
  int pwm[4];
  calcularSalidas(actual, pwmTope, dir, pwm);
  escribirSalidas(dir, pwm, false);
}

void motoresImprimirObjetivo(const Comando &c) {
  byte dir;
  int pwm[4];
  calcularSalidas(objetivo, pwmTope, dir, pwm);
  if (c.rueda >= 0) {
    LOG("RUEDA %s  v=%4d  vmax=%3d%%", NOMBRE_RUEDA[c.rueda], c.vy, c.vmax);
  } else {
    LOG("CMD  vx=%4d  vy=%4d  w=%4d  vmax=%3d%%", c.vx, c.vy, c.w, c.vmax);
  }
  // Byte en el orden de la tabla del CLAUDE.md: RR RL FR FL (Q7..Q0)
  char bits[12];
  int k = 0;
  for (int i = 3; i >= 0; i--) {
    bits[k++] = '0' + ((dir >> (2 * i + 1)) & 1);
    bits[k++] = '0' + ((dir >> (2 * i)) & 1);
    if (i > 0) bits[k++] = ' ';
  }
  bits[k] = '\0';
  LOG("  →  dir %s (0x%02X)  pwm FL=%d FR=%d RL=%d RR=%d\n",
      bits, dir, pwm[FL], pwm[FR], pwm[RL], pwm[RR]);
}
