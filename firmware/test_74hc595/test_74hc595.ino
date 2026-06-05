#include <Arduino.h>

// ============================================================
//  PRUEBA 74HC595 + ESP8266 NodeMCU
//  Robot Mecanum 4WD — Fase 6 / Step 1
//  Verifica que shiftOut() llega correcto a Q0-Q7
// ============================================================

#define DS_PIN   0   // GPIO0 / D3 — data serial
#define SHCP_PIN 2   // GPIO2 / D4 — clock
#define STCP_PIN 10  // GPIO10 / SD3 — latch (separado de STBY)

// ------------------------------------------------------------
//  Envía un byte al 74HC595
//  bit7 → Q7 ... bit0 → Q0
// ------------------------------------------------------------
void shiftWrite(byte data) {
  digitalWrite(STCP_PIN, LOW);
  shiftOut(DS_PIN, SHCP_PIN, MSBFIRST, data);
  digitalWrite(STCP_PIN, HIGH);
}

// ------------------------------------------------------------
//  Setup
// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(DS_PIN,   OUTPUT);
  pinMode(SHCP_PIN, OUTPUT);
  pinMode(STCP_PIN, OUTPUT);

  shiftWrite(0x00);  // apaga todo al inicio
  Serial.println("\n=== TEST 74HC595 ===");
  Serial.println("Comandos: 0-9 patrones, 'x' valor hex manual");
}

// ------------------------------------------------------------
//  Loop — espera comando por Serial Monitor
// ------------------------------------------------------------
void loop() {
  if (!Serial.available()) return;

  char cmd = Serial.read();

  switch (cmd) {

    case '0':
      shiftWrite(0x00);
      Serial.println("[0] Todo apagado        00000000");
      break;

    case '1':
      shiftWrite(0xFF);
      Serial.println("[1] Todo encendido      11111111");
      break;

    case '2':
      // Todos los motores ADELANTE
      // FL: Q0=1,Q1=0 | FR: Q2=1,Q3=0 | RL: Q4=1,Q5=0 | RR: Q6=1,Q7=0
      shiftWrite(0b01010101);
      Serial.println("[2] Todos ADELANTE      01010101  (0x55)");
      break;

    case '3':
      // Todos los motores ATRAS
      // FL: Q0=0,Q1=1 | FR: Q2=0,Q3=1 | RL: Q4=0,Q5=1 | RR: Q6=0,Q7=1
      shiftWrite(0b10101010);
      Serial.println("[3] Todos ATRAS         10101010  (0xAA)");
      break;

    case '4':
      // Solo Q0 — bit más bajo
      shiftWrite(0b00000001);
      Serial.println("[4] Solo Q0             00000001");
      break;

    case '5':
      // Solo Q7 — bit más alto
      shiftWrite(0b10000000);
      Serial.println("[5] Solo Q7             10000000");
      break;

    case '6':
      // Barrer Q0→Q7 uno por uno (ver el LED viajar)
      Serial.println("[6] Barrido Q0 → Q7 ...");
      for (int i = 0; i < 8; i++) {
        shiftWrite(1 << i);
        Serial.print("    Q"); Serial.print(i);
        Serial.print(" encendido  ");
        printBits(1 << i);
        delay(400);
      }
      shiftWrite(0x00);
      Serial.println("    fin barrido");
      break;

    case '7':
      // Parpadeo 5 veces — todos
      Serial.println("[7] Parpadeo x5 ...");
      for (int i = 0; i < 5; i++) {
        shiftWrite(0xFF); delay(250);
        shiftWrite(0x00); delay(250);
      }
      Serial.println("    fin parpadeo");
      break;

    case '8':
      // Pares encendidos (Q0,Q2,Q4,Q6) — AIN1 de los 4 motores
      shiftWrite(0b01010101);
      Serial.println("[8] AIN1 todos          01010101");
      break;

    case '9':
      // Impares encendidos (Q1,Q3,Q5,Q7) — AIN2 de los 4 motores
      shiftWrite(0b10101010);
      Serial.println("[9] AIN2 todos          10101010");
      break;

    case 'x':
    case 'X': {
      // Leer valor hex manual: escribe xAB y envía 0xAB
      delay(20);
      String hexStr = Serial.readStringUntil('\n');
      hexStr.trim();
      byte val = (byte) strtol(hexStr.c_str(), NULL, 16);
      shiftWrite(val);
      Serial.print("[x] Manual 0x");
      if (val < 16) Serial.print("0");
      Serial.print(val, HEX);
      Serial.print("  ");
      printBits(val);
      break;
    }

    case '\r':
    case '\n':
      break;  // ignorar saltos de línea

    default:
      Serial.println("Comandos disponibles:");
      Serial.println("  0 → todo apagado");
      Serial.println("  1 → todo encendido");
      Serial.println("  2 → todos motores ADELANTE");
      Serial.println("  3 → todos motores ATRAS");
      Serial.println("  4 → solo Q0");
      Serial.println("  5 → solo Q7");
      Serial.println("  6 → barrido Q0→Q7");
      Serial.println("  7 → parpadeo x5");
      Serial.println("  8 → solo pines AIN1 (Q0,Q2,Q4,Q6)");
      Serial.println("  9 → solo pines AIN2 (Q1,Q3,Q5,Q7)");
      Serial.println("  xAB → enviar valor hex manual (ej: xAB, xFF, x0F)");
      break;
  }
}

// ------------------------------------------------------------
//  Imprime byte como cadena de 8 bits en Serial
// ------------------------------------------------------------
void printBits(byte val) {
  for (int i = 7; i >= 0; i--) {
    Serial.print(bitRead(val, i));
  }
  Serial.println();
}
