# CLAUDE.md — Robot Mecanum 4WD
> Directriz maestra para Claude Code. Leer completo antes de cualquier tarea.

## Estado del proyecto
- **Fase actual:** Fase 6 — Firmware ESP-NOW (EN CURSO)
- **Fases 1–5:** COMPLETADAS (arquitectura, potencia, motores, sensores, PCB)
- **Paso actual:** Prueba subsistema 74HC595 con 8 LEDs antes de conectar TB6612

---

## Plataforma de desarrollo

| Parámetro | Valor |
|-----------|-------|
| MCU | ESP8266 NodeMCU v1 |
| IDE | VS Code + extensión PlatformIO (recomendado) o Arduino IDE |
| Framework | Arduino |
| Board string PlatformIO | `nodemcuv2` |
| Velocidad Serial | 115200 baud |
| Comunicación inalámbrica | ESP-NOW (peer-to-peer, sin router) |

### platformio.ini base
```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
monitor_speed = 115200
```

---

## Mapa de pines — FIJO, no modificar

| GPIO | Pin NodeMCU | Función | Periférico |
|------|-------------|---------|------------|
| GPIO4 | D2 | SDA I2C | MPU-6050 + VL53L0X ×2 |
| GPIO5 | D1 | SCL I2C | MPU-6050 + VL53L0X ×2 |
| GPIO12 | D6 | PWM Motor FL | TB6612 #1 PWMA |
| GPIO13 | D7 | PWM Motor FR | TB6612 #1 PWMB |
| GPIO14 | D5 | PWM Motor RL | TB6612 #2 PWMA |
| GPIO15 | D8 | PWM Motor RR | TB6612 #2 PWMB |
| GPIO0 | D3 | DS — data 74HC595 | Shift register |
| GPIO2 | D4 | SHCP — clock 74HC595 | Shift register |
| GPIO10 | SD3 | STCP — latch 74HC595 | Shift register |
| GPIO16 | D0 | STBY TB6612 | TB6612 ×2 |
| GPIO1 | TX | XSHUT VL53L0X #1 | ToF izquierda |
| GPIO3 | RX | XSHUT VL53L0X #2 | ToF derecha |
| ADC0 | A0 | Monitor batería | Divisor 100k/13k |

> **Nota GPIO10/SD3:** disponible porque el flash corre en modo DIO (solo usa GPIO6–8 y GPIO11).
> **Nota GPIO16/D0:** solo STBY — se pone HIGH en setup() y no se toca durante operación.

---

## Arquitectura de motores y 74HC595

### Mapeo de salidas Q0–Q7 → pines DIR de TB6612

```
Q0 → AIN1  TB6612 #1   Motor FL bit1
Q1 → AIN2  TB6612 #1   Motor FL bit2
Q2 → BIN1  TB6612 #1   Motor FR bit1
Q3 → BIN2  TB6612 #1   Motor FR bit2
Q4 → AIN1  TB6612 #2   Motor RL bit1
Q5 → AIN2  TB6612 #2   Motor RL bit2
Q6 → BIN1  TB6612 #2   Motor RR bit1
Q7 → BIN2  TB6612 #2   Motor RR bit2
```

### Tabla de dirección por motor (AINx / BINx)

| bit_alto | bit_bajo | Resultado |
|----------|----------|-----------|
| 0 | 1 | Adelante |
| 1 | 0 | Atrás |
| 0 | 0 | Coast (libre) |
| 1 | 1 | Brake (freno) |

### Byte de dirección para movimientos mecanum

| Movimiento | Byte binario | Hex |
|------------|-------------|-----|
| Todos adelante | 01 01 01 01 | 0x55 |
| Todos atrás | 10 10 10 10 | 0xAA |
| Todos stop | 00 00 00 00 | 0x00 |
| Derecha (strafe) | 10 01 01 10 | — |
| Izquierda (strafe) | 01 10 10 01 | — |
| Rotar CW | 01 10 01 10 | — |
| Rotar CCW | 10 01 10 01 | — |

> Los vectores completos se calcularán en Fase 6 firmware.

---

## Subsistemas de hardware

### Potencia
- Batería 2S 18650 → 7.4V nominal (6.0–8.4V rango)
- BMS 2S 20A
- Protección polaridad: MOSFET-P AO3401
- Fusible: Polyfuse MF-R600 6A
- TVS: P6KE10A (clamp 10V)
- Regulador lógica: AMS1117-3.3V → 3V3 para todo el circuito lógico

### Control de motores
- 2× TB6612FNG (2 motores cada uno)
- VM = 7.4V directo desde batería
- VCC lógica = 3.3V
- STBY compartido GPIO16 + pull-up 10k a 3V3
- Corriente continua: 1.2A/canal, pico 3.2A

### Sensores I2C (bus GPIO4/GPIO5)
- MPU-6050 → dirección 0x68 (AD0=GND)
- VL53L0X #1 → dirección 0x30 (reasignada via XSHUT GPIO1)
- VL53L0X #2 → dirección 0x29 (default, XSHUT GPIO3)

### Monitor de batería
```cpp
// Leer voltaje real desde ADC0
float leerVoltajeBateria() {
  int raw = analogRead(A0);           // 0–1023
  float vADC = raw * (3.3 / 1023.0); // voltaje en el pin
  float vBat = vADC * (113.0 / 13.0); // divisor 100k/13k
  return vBat;
}
```

---

## PCB

| Parámetro | Valor |
|-----------|-------|
| Dimensiones | 100mm × 80mm |
| Capas | 2 (Top señal / Bottom GND plane) |
| Conector batería | XT30 macho |
| Conectores motor | JST-PH 2pin ×4 |
| Software | KiCad o EasyEDA |

---

## Estructura del repositorio

```
MecanumRobot/
├── CLAUDE.md                  ← este archivo
├── docs/
│   └── Directriz_v1_0.md     ← directriz completa del proyecto
├── hardware/
│   └── bom/
│       └── BOM_consolidada.md
└── firmware/
    ├── test_74hc595/          ← PASO ACTUAL
    │   └── test_74hc595.ino
    ├── test_tb6612/           ← siguiente paso
    ├── test_i2c/              ← después
    └── mecanum_main/          ← firmware final
```

---

## Convenciones de código

- Un archivo `.ino` por subsistema de prueba
- Comentarios en español
- Constantes de pines en `#define` al inicio
- Serial.begin(115200) en todo sketch de prueba
- Funciones de bajo nivel separadas de la lógica de movimiento

---

## Flujo de trabajo — construcción incremental

1. ✅ **test_74hc595** — ESP8266 + 74HC595 + 8 LEDs
2. ⬜ **test_tb6612** — agregar TB6612 ×2 + motores reales
3. ⬜ **test_i2c** — agregar MPU-6050 y VL53L0X
4. ⬜ **test_espnow** — comunicación inalámbrica
5. ⬜ **mecanum_main** — firmware completo integrado
