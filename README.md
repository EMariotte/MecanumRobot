# Robot Mecanum 4WD — Semillero

Proyecto de construcción de un robot con ruedas Mecanum desarrollado en el **Semillero de Robótica**, como trabajo conjunto entre **Breyner** (aprendiz) y su facilitador, con apoyo de **Claude (Anthropic)** como asistente de ingeniería.

## Plataforma

| Parámetro | Valor |
|-----------|-------|
| MCU | ESP8266 NodeMCU v1 |
| Framework | Arduino (vía PlatformIO) |
| Comunicación | ESP-NOW (peer-to-peer, sin router) |
| Motores | 4× DC con ruedas Mecanum |
| Driver | 2× TB6612FNG |
| Batería | 2S 18650 — 7.4V nominal |

## Estado del proyecto

| Paso | Descripción | Estado |
|------|-------------|--------|
| 1 | `test_74hc595` — ESP8266 + 74HC595 + 8 LEDs | ✅ Completado |
| 2 | `test_tb6612` — TB6612 ×2 + motores reales | 🔄 En curso |
| 3 | `test_i2c` — MPU-6050 + VL53L0X ×2 | ⬜ Pendiente |
| 4 | `test_espnow` — Comunicación inalámbrica | ⬜ Pendiente |
| 5 | `mecanum_main` — Firmware final integrado | ⬜ Pendiente |

## Mapa de pines

| GPIO | Pin NodeMCU | Función |
|------|-------------|---------|
| GPIO4 | D2 | SDA I2C |
| GPIO5 | D1 | SCL I2C |
| GPIO12 | D6 | PWM Motor FL |
| GPIO13 | D7 | PWM Motor FR |
| GPIO14 | D5 | PWM Motor RL |
| GPIO15 | D8 | PWM Motor RR |
| GPIO0 | D3 | DS — data 74HC595 |
| GPIO2 | D4 | SHCP — clock 74HC595 |
| GPIO10 | SD3 | STCP — latch 74HC595 |
| GPIO16 | D0 | STBY TB6612 ×2 |
| GPIO1 | TX | XSHUT VL53L0X #1 |
| GPIO3 | RX | XSHUT VL53L0X #2 |
| ADC0 | A0 | Monitor batería |

## Estructura del repositorio

```
MecanumRobot/
├── firmware/
│   ├── test_74hc595/      ← Paso 1 (completado)
│   ├── test_tb6612/       ← Paso 2 (en curso)
│   ├── test_i2c/
│   └── mecanum_main/
├── hardware/
│   └── bom/
├── docs/
└── platformio.ini
```

## Cómo usar

1. Abrir el proyecto en VS Code con la extensión PlatformIO
2. El `platformio.ini` apunta al firmware activo via `src_dir`
3. Para cambiar de subsistema editar `src_dir` en `platformio.ini`
4. Compilar y subir con el botón **Build / Upload** de PlatformIO

---

*Proyecto semillero — Construcción incremental por subsistemas*
