#include <Arduino.h>

// ============================================================
//  DIAGNÓSTICO 74HC595 — medición con multímetro
//  Robot Mecanum 4WD — Fase 6
//  Mantiene cada línea de control en un nivel FIJO para poder
//  medirla con calma y aislar la falla: ESP8266, cableado o chip.
//  Los TB6612 quedan en STANDBY: los motores no se mueven.
// ============================================================

// --- 74HC595 ---
#define DS_PIN   0   // GPIO0 / D3  → pin 14 del 74HC595
#define SHCP_PIN 2   // GPIO2 / D4  → pin 11 (también LED azul de la placa, activo en LOW)
#define STCP_PIN 10  // GPIO10 / SD3 → pin 12

// --- TB6612 (se mantienen apagados) ---
#define STBY_PIN 16  // GPIO16 / D0
#define PWM_FL  12
#define PWM_FR  13
#define PWM_RL  14
#define PWM_RR  15

bool nivelDS = LOW, nivelSHCP = LOW, nivelSTCP = LOW;

// ------------------------------------------------------------
//  Bajo nivel
// ------------------------------------------------------------
void pulso(int pin) {           // pulso LOW→HIGH→LOW lento
  digitalWrite(pin, LOW);  delayMicroseconds(10);
  digitalWrite(pin, HIGH); delayMicroseconds(10);
  digitalWrite(pin, LOW);
}

void shiftWrite(byte data) {
  digitalWrite(STCP_PIN, LOW);
  shiftOut(DS_PIN, SHCP_PIN, MSBFIRST, data);
  digitalWrite(STCP_PIN, HIGH);
  nivelSTCP = HIGH;
  nivelSHCP = LOW;              // shiftOut deja el clock en LOW
  nivelDS = data & 0x01;        // último bit enviado
}

void imprimirEstado() {
  Serial.printf("   DS(D3)=%s  SHCP(D4)=%s  STCP(SD3)=%s\n",
                nivelDS ? "HIGH" : "LOW ",
                nivelSHCP ? "HIGH" : "LOW ",
                nivelSTCP ? "HIGH" : "LOW ");
}

void imprimirAyuda() {
  Serial.println("\n=== DIAGNÓSTICO 74HC595 ===");
  Serial.println("Medir con multímetro en DC entre el pin indicado y GND.");
  Serial.println("-- Líneas de control (nivel fijo, medir en el pin del 74HC595) --");
  Serial.println("  d → alternar DS    (pin 14)");
  Serial.println("  c → alternar SHCP  (pin 11)  LED azul encendido = LOW");
  Serial.println("  l → alternar STCP  (pin 12)");
  Serial.println("-- Escritura completa (medir Q0..Q7) --");
  Serial.println("  1 → 0xFF  todas las salidas en HIGH");
  Serial.println("  0 → 0x00  todas en LOW");
  Serial.println("  5 → 0x55  Q0,Q2,Q4,Q6 en HIGH (pines 15,2,4,6)");
  Serial.println("-- Paso a paso manual --");
  Serial.println("  p → un pulso de reloj SHCP (desplaza el nivel actual de DS)");
  Serial.println("  L → un pulso de latch STCP (copia el registro a las salidas)");
  Serial.println("  ? → esta ayuda");
}

// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Motores apagados antes de tocar el 74HC595
  pinMode(STBY_PIN, OUTPUT); digitalWrite(STBY_PIN, LOW);
  pinMode(PWM_FL, OUTPUT);   digitalWrite(PWM_FL, LOW);
  pinMode(PWM_FR, OUTPUT);   digitalWrite(PWM_FR, LOW);
  pinMode(PWM_RL, OUTPUT);   digitalWrite(PWM_RL, LOW);
  pinMode(PWM_RR, OUTPUT);   digitalWrite(PWM_RR, LOW);

  pinMode(DS_PIN,   OUTPUT); digitalWrite(DS_PIN,   LOW);
  pinMode(SHCP_PIN, OUTPUT); digitalWrite(SHCP_PIN, LOW);
  pinMode(STCP_PIN, OUTPUT); digitalWrite(STCP_PIN, LOW);

  delay(200);
  imprimirAyuda();
  imprimirEstado();
}

// ------------------------------------------------------------
void loop() {
  if (!Serial.available()) return;
  char cmd = Serial.read();

  switch (cmd) {
    case 'd':
      nivelDS = !nivelDS;     digitalWrite(DS_PIN, nivelDS);
      Serial.println("[d] DS alternado");   imprimirEstado(); break;
    case 'c':
      nivelSHCP = !nivelSHCP; digitalWrite(SHCP_PIN, nivelSHCP);
      Serial.println("[c] SHCP alternado"); imprimirEstado(); break;
    case 'l':
      nivelSTCP = !nivelSTCP; digitalWrite(STCP_PIN, nivelSTCP);
      Serial.println("[l] STCP alternado"); imprimirEstado(); break;

    case '1': shiftWrite(0xFF); Serial.println("[1] 0xFF → Q0..Q7 deben marcar ~3.3V"); break;
    case '0': shiftWrite(0x00); Serial.println("[0] 0x00 → Q0..Q7 deben marcar ~0V");   break;
    case '5': shiftWrite(0x55); Serial.println("[5] 0x55 → pines 15,2,4,6 en ~3.3V; 1,3,5,7 en ~0V"); break;

    case 'p':
      pulso(SHCP_PIN); nivelSHCP = LOW;
      Serial.printf("[p] Pulso de reloj — entró un %d al registro\n", nivelDS); break;
    case 'L':
      pulso(STCP_PIN); nivelSTCP = LOW;
      Serial.println("[L] Pulso de latch — salidas actualizadas"); break;

    case '\r': case '\n': break;
    default: imprimirAyuda(); imprimirEstado(); break;
  }
}
