# Directriz de Proyecto — Robot Mecanum 4WD
**v1.0 — Mayo 2026**

## Estado de fases

| Fase | Descripción | Estado |
|------|-------------|--------|
| Fase 1 | Arquitectura del sistema | ✅ COMPLETADA |
| Fase 2 | Subsistema de potencia | ✅ COMPLETADA |
| Fase 3 | Control de motores | ✅ COMPLETADA |
| Fase 4 | Sensores I2C | ✅ COMPLETADA |
| Fase 5 | Diseño de PCB | ✅ COMPLETADA |
| Fase 6 | Firmware ESP-NOW | 🔄 EN CURSO |

---

## Objetivo general

Diseñar y construir un robot móvil omnidireccional basado en 4 ruedas mecanum impresas en 3D, controlado de forma inalámbrica mediante ESP-NOW, con una PCB personalizada que integra todos los subsistemas de potencia, control de motores y sensado.

---

## Plataforma física

### Ruedas mecanum
- Modelo: Thingiverse thing:1358552 — Jonah_Innoart
- Diámetro: 44mm / Ancho: 21.4mm
- Eje: 3mm tipo D (flat shaft)
- Rodillos: pines de costura 27mm
- Configuración: FL, FR, RL, RR

### Motores
- Pololu Metal Gear Motor 75:1
- Voltaje nominal: 6V–9V (operación a 7.4V)
- Corriente típica: ~400mA por motor
- Corriente de stall: ~1.2A por motor
- Eje: D-shaft 3mm

---

## Arquitectura — decisiones definitivas

### Microcontrolador
- ESP8266 NodeMCU v1
- Voltaje lógico: 3.3V
- Comunicación: ESP-NOW (peer-to-peer, sin router)
- Expansión DIR: 74HC595 shift register

### Alimentación
- Pack 18650 — 2S (7.4V nominal)
- BMS 2S 20A
- Conector: XT30 macho
- Regulación lógica: AMS1117-3.3V

### Control de motores
- 2× TB6612FNG
- VM: 7.4V desde batería
- VCC lógica: 3.3V (sin level shifter)
- STBY: GPIO16 compartido + pull-up 10k

### Sensores
- IMU: MPU-6050 — I2C 0x68
- ToF #1: VL53L0X — I2C 0x30 (reasignada), XSHUT←GPIO1
- ToF #2: VL53L0X — I2C 0x29 (default), XSHUT←GPIO3

---

## Protecciones de hardware (obligatorias)

| Protección | Componente | Valor |
|------------|------------|-------|
| Polaridad invertida | MOSFET-P AO3401 | SOT-23 |
| Sobrecorriente | Polyfuse MF-R600 | 6A |
| Transitorios EMI | TVS P6KE10A | clamp 10V |
| Desacople VM | C4, C5 | 100µF + 100nF |
| Desacople motores | C6–C13 | 100nF ×8 |
| Pull-up I2C | R6, R7 | 4.7kΩ |
| Pull-up STBY | R3 | 10kΩ a 3V3 |

---

## Especificaciones PCB

- Dimensiones: 100mm × 80mm (máximo)
- Capas: 2 (Top: componentes + señal / Bottom: plano GND)
- Pistas VBAT/VM: 2.5mm mínimo
- Pistas GND potencia: 3.0mm / plano completo
- Pistas 3V3: 0.8mm
- Pistas señal: 0.3mm
- Montaje: M3 ×4 esquinas, standoffs 10mm

### Zonas
- Zona superior: potencia (XT30, fusible, MOSFET-P, TVS, AMS1117)
- Zona media-izquierda: TB6612 ×2 + conectores JST motores
- Zona media-derecha: ESP8266 NodeMCU + 74HC595
- Zona inferior: MPU-6050 + VL53L0X ×2 + conectores I2C

---

## BOM consolidada

| Ref | Componente | Modelo | Package | Cant |
|-----|------------|--------|---------|------|
| U1 | Regulador 3.3V | AMS1117-3.3 | SOT-223 | 1 |
| U2, U3 | Driver motor | TB6612FNG | SOP-24 | 2 |
| U4 | Shift register | 74HC595 | SOP-16 | 1 |
| U5 | IMU 6DOF | MPU-6050 | QFN-24 | 1 |
| U6, U7 | ToF sensor | VL53L0X | LGA-12 | 2 |
| Q1 | MOSFET-P | AO3401 | SOT-23 | 1 |
| D1 | Diodo TVS | P6KE10A | DO-15 | 1 |
| F1 | Polyfuse 6A | MF-R600 | 1812 SMD | 1 |
| C1,C4,C5 | Electrolítico | 100µF/16V | SMD D6.3 | 3 |
| C2,C3,C6-C15 | Cerámico | 100nF/16V | 0805 | 12 |
| C16,C17 | Cerámico | 10µF/10V | 0805 | 2 |
| R1 | Resistencia | 10kΩ | 0805 | 1 |
| R2 | Resistencia | 100kΩ | 0805 | 1 |
| R3 | Pull-up STBY | 10kΩ | 0805 | 1 |
| R4 | Divisor bat. | 100kΩ | 0805 | 1 |
| R5 | Divisor bat. | 13kΩ | 0805 | 1 |
| R6, R7 | Pull-up I2C | 4.7kΩ | 0805 | 2 |
| R8, R9 | Resistencia LED | 330Ω | 0805 | 2 |
| LED1 | LED rojo | — | 0805 | 1 |
| LED2 | LED verde | — | 0805 | 1 |
| SW1 | Interruptor | KCD1 | Through hole | 1 |
| J1 | Conector bat. | XT30 macho | Through hole | 1 |
| J2-J5 | Conector motor | JST-PH 2pin | Through hole | 4 |
| J6 | Header I2C | JST-SH 4pin | SMD | 1 |
| J7, J8 | Conector ToF | JST-SH 6pin | SMD | 2 |

---

## Fuera de alcance v1.0

- Navegación autónoma / SLAM
- Encoders / odometría
- Pantalla en el robot
- Cargador de batería integrado en PCB
