#include <Arduino.h>

// ============================================================
//  PRUEBA TB6612 + 74HC595 + ESP8266 NodeMCU
//  Robot Mecanum 4WD — Fase 6 / Step 2
//  Ciclo automático: adelante → stop → atrás → stop
// ============================================================

// --- 74HC595 ---
#define DS_PIN   0   // GPIO0 / D3
#define SHCP_PIN 2   // GPIO2 / D4
#define STCP_PIN 10  // GPIO10 / SD3 — latch (independiente de STBY)

// --- TB6612 enable ---
#define STBY_PIN 16  // GPIO16 / D0 — STBY ambos TB6612, siempre HIGH

// --- PWM de los 4 motores ---
#define PWM_FL  12   // D6 — Frontal Izquierdo
#define PWM_FR  13   // D7 — Frontal Derecho
#define PWM_RL  14   // D5 — Trasero Izquierdo
#define PWM_RR  15   // D8 — Trasero Derecho

// --- Bytes de dirección ---
#define DIR_ADELANTE  0x55   // 01010101 — todos adelante
#define DIR_ATRAS     0xAA   // 10101010 — todos atras
#define DIR_STOP      0x00   // coast

#define VELOCIDAD     180    // 0–255

// ------------------------------------------------------------
void shiftWrite(byte data) {
  digitalWrite(STCP_PIN, LOW);
  shiftOut(DS_PIN, SHCP_PIN, MSBFIRST, data);
  digitalWrite(STCP_PIN, HIGH);
}

void setPWM(int spd) {
  analogWrite(PWM_FL, spd);
  analogWrite(PWM_FR, spd);
  analogWrite(PWM_RL, spd);
  analogWrite(PWM_RR, spd);
}

void adelante(int spd, int ms) {
  shiftWrite(DIR_ADELANTE);
  setPWM(spd);
  delay(ms);
}

void atras(int spd, int ms) {
  shiftWrite(DIR_ATRAS);
  setPWM(spd);
  delay(ms);
}

void parar(int ms) {
  setPWM(0);
  shiftWrite(DIR_STOP);
  delay(ms);
}

// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(DS_PIN,   OUTPUT);
  pinMode(SHCP_PIN, OUTPUT);
  pinMode(STCP_PIN, OUTPUT);
  pinMode(STBY_PIN, OUTPUT);
  digitalWrite(STBY_PIN, HIGH);   // habilitar TB6612 desde el inicio
  pinMode(PWM_FL, OUTPUT);
  pinMode(PWM_FR, OUTPUT);
  pinMode(PWM_RL, OUTPUT);
  pinMode(PWM_RR, OUTPUT);

  parar(500);
  Serial.println("=== TEST 4 RUEDAS ===");
}

// ------------------------------------------------------------
void loop() {
  Serial.println(">> Adelante 2s");
  adelante(VELOCIDAD, 2000);

  Serial.println(">> Stop 1s");
  parar(1000);

  Serial.println(">> Atras 2s");
  atras(VELOCIDAD, 2000);

  Serial.println(">> Stop 1s");
  parar(1000);
}
