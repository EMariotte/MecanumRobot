# CLAUDE.md — Robot Mecanum 4WD
> Directriz maestra para Claude Code. Leer completo antes de cualquier tarea.

## Estado del proyecto
_Última actualización: 2026-11-18_

- **Fase actual:** Fase 6 — Firmware (EN CURSO)
- **Fases 1–4:** COMPLETADAS (arquitectura, potencia, motores, sensores)
- **Fase 5 — PCB:**
  - **v1.0 (Fritzing):** layout terminado, exportes de fabricación (etch/mask/silk/paste) generados en `hardware/fritzing/`
  - **v1.1 (KiCad):** esquemático jerárquico en curso — hojas PWR, MCU, MOTOR, SENSOR dibujadas; hoja **AUX pendiente** (excluida del repo vía `.gitignore`). Layout PCB **no iniciado** (`.kicad_pcb` vacío, `gerbers/` y `pcb/` vacías)
- **Paso 2 — `test_tb6612`:** ✅ validado en hardware el 2026-11-17 (4 motores adelante → stop → atrás → stop)
- **Paso 3 — `test_web`:** ✅ completo (3a–3e) — control remoto por página web desde LittleFS + telemetría de batería INA226, validado en hardware el 2026-11-18
- **Paso actual de firmware:** por definir — `test_i2c` sigue pospuesto (faltan MPU-6050 y VL53L0X); candidato: `mecanum_main`
- **Sensores I2C:** aún no se tienen físicamente → `test_i2c` pospuesto hasta adquirirlos
- **Sketch activo en PlatformIO:** `firmware/test_web` (ver `src_dir` en `platformio.ini`)

---

## Plataforma de desarrollo

| Parámetro | Valor |
|-----------|-------|
| MCU | ESP8266 NodeMCU v1 |
| IDE | VS Code + extensión PlatformIO (recomendado) o Arduino IDE |
| Framework | Arduino |
| Board string PlatformIO | `nodemcuv2` |
| Modo de flash | **DIO** (obligatorio — libera GPIO10) |
| Velocidad Serial | 115200 baud |
| Comunicación inalámbrica | Wi-Fi modo AP + página web en LittleFS + WebSocket (ESP-NOW: opcional, a futuro) |
| Sistema de archivos | LittleFS (2 MB, layout `eagle.flash.4m2m.ld`) |
| Librerías | `esp32async/ESPAsyncWebServer` + `esp32async/ESPAsyncTCP` |

### platformio.ini
```ini
[platformio]
src_dir = firmware/test_web             ; cambiar al avanzar de paso
data_dir = firmware/test_web/data       ; archivos de la página → LittleFS

[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
board_build.flash_mode = dio            ; necesario para usar GPIO10 (STCP)
board_build.filesystem = littlefs
board_build.ldscript = eagle.flash.4m2m.ld
monitor_speed = 115200
monitor_filters = send_on_enter, colorize, esp8266_exception_decoder
lib_deps =
    esp32async/ESPAsyncTCP
    esp32async/ESPAsyncWebServer
```

> Para cambiar de sketch de prueba, solo se modifica `src_dir` (y `data_dir` si el sketch tiene página web). Cada carpeta de `firmware/` es un sketch independiente.
> Los sketches con página web se cargan en **dos partes**: *Upload* (programa) y *Upload Filesystem Image* (carpeta `data/`). Si solo cambia la página, basta con la segunda.
> En Arduino IDE no hay `platformio.ini`: seleccionar manualmente *Flash Mode: DIO* en el menú Herramientas.

---

## Mapa de pines — FIJO, no modificar

| GPIO | Pin NodeMCU | Función | Periférico |
|------|-------------|---------|------------|
| GPIO4 | D2 | SDA I2C | MPU-6050 + VL53L0X ×2 + INA226 |
| GPIO5 | D1 | SCL I2C | MPU-6050 + VL53L0X ×2 + INA226 |
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
| ADC0 | A0 | Monitor batería (respaldo) | Divisor 100k/13k — no montado; reemplazado por INA226 |

> **Nota GPIO10/SD3:** disponible solo porque el flash corre en modo DIO (usa GPIO6–9 y GPIO11). Si se compila en QIO el ESP8266 se resetea o cuelga al manipular el latch.
> **Nota GPIO16/D0:** solo STBY — se pone HIGH en setup() y no se toca durante operación.
> **Nota GPIO0/GPIO2/GPIO15 (pines de arranque):** GPIO0 y GPIO2 deben estar en HIGH y GPIO15 en LOW al encender. Las entradas del 74HC595 son alta impedancia y el TB6612 tiene pull-down interno de 200k en PWM, así que el arranque es compatible. No agregar pull-downs a D3/D4 ni pull-ups a D8.
> **⚠ Nota GPIO1/GPIO3 (TX/RX):** comparten el puerto Serial con XSHUT de los VL53L0X. A partir de `test_i2c`, el monitor serial **no estará disponible** mientras los XSHUT estén activos. Hay que decidir en ese paso la estrategia de depuración (p. ej. Serial solo antes de iniciar los ToF, o depurar vía ESP-NOW).

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

> En el 74HC595, Q0 (QA) es el **pin 15**, no el pin 1. MR (pin 10) a +3V3, OE (pin 13) a GND, Q7S (pin 9) sin conectar.

### Diagnóstico del 74HC595
Si **todas las salidas marcan 0 V**, revisar antes de dar el chip por dañado:
1. Pin 16 (VCC) ≈ 3.3 V · pin 13 (OE) = 0 V · **pin 10 (MR) ≈ 3.3 V**
2. Las líneas DS / SHCP / STCP llegan a los pines 14 / 11 / 12

> **Caso real (2026-11-17):** salidas en 0 con el chip sano — MR no hacía contacto con 3V3 y el registro se borraba continuamente.
> Para medir con multímetro usar `firmware/diag_74hc595` (deja cada línea en nivel fijo y los TB6612 en STANDBY).

### Tabla de dirección por motor (AINx / BINx)

| bit_alto | bit_bajo | Resultado |
|----------|----------|-----------|
| 0 | 1 | Adelante |
| 1 | 0 | Atrás |
| 0 | 0 | Coast (libre) |
| 1 | 1 | Brake (freno) |

> Con dirección fija, PWM = LOW también produce *short brake* en el TB6612 (ver tabla H-SW del datasheet).

### Byte de dirección para movimientos mecanum — ✅ validado en el robot (2026-11-18)

Orden de los pares: **RR RL FR FL** (Q7..Q0). A = adelante, T = atrás.

| Movimiento | FL | FR | RL | RR | Byte binario | Hex |
|------------|----|----|----|----|-------------|-----|
| Adelante | A | A | A | A | 01 01 01 01 | 0x55 |
| Atrás | T | T | T | T | 10 10 10 10 | 0xAA |
| Derecha (strafe) | A | T | T | A | 01 10 10 01 | 0x69 |
| Izquierda (strafe) | T | A | A | T | 10 01 01 10 | 0x96 |
| Rotar CW (derecha) | A | T | A | T | 10 01 10 01 | 0x99 |
| Rotar CCW (izquierda) | T | A | T | A | 01 10 01 10 | 0x66 |
| Stop (coast) | — | — | — | — | 00 00 00 00 | 0x00 |

> Estos bytes salen de la cinemática mecanum (sección *Control remoto por página web*) con calibración por defecto (`SIGNO_VX = 1`, `SIGNO_W = 1`, `INVERTIR = {1,1,1,1}`). El firmware no usa la tabla directamente: calcula el byte en cada comando.

---

## Control remoto por página web

### Arquitectura
- **Red propia (modo AP):** SSID `MecanumRobot`, clave `mecanum2026`, IP `192.168.4.1`. No depende de router (definidos en `#define` al inicio del sketch; cambiar el SSID si hay varios robots juntos).
- **Página en LittleFS:** `index.html` + `style.css` + `app.js` en `data/`. **Sin librerías ni recursos externos (CDN, fuentes web):** el celular no tiene internet mientras está conectado a la red del robot.
- **Control por WebSocket** (desde 3b): la página envía el estado del joystick ~20 veces/s. No usar una petición HTTP por comando (latencia excesiva).
- **Failsafe obligatorio:** si no llega ningún comando en **300 ms**, los motores se detienen. Cubre desconexión, pantalla bloqueada o salir de alcance.
- **Batería:** INA226 por I2C (ver *Monitor de batería*), leído cada 100 ms y enviado a la página 1 vez/s. Ya no se usa `analogRead(A0)` (en el ESP8266, leer A0 seguido puede cortar el Wi-Fi).
- **Un solo dispositivo con el control:** el último en conectarse lo toma; a los anteriores se les envía `TOMADO` y se desconectan. La página no reconecta sola tras `TOMADO` (evita que dos celulares se peleen el control); muestra un botón *Recuperar control*.
- **Callback del WebSocket:** se ejecuta fuera de `loop()`. Solo guarda datos y levanta banderas; las impresiones por Serial, el manejo de motores y el cierre de clientes se hacen en `loop()`.

### Protocolo WebSocket (`/ws`, mensajes de texto)
| Dirección | Mensaje | Significado |
|---|---|---|
| Página → robot | `C vx vy w vmax` | Comando, 20 veces/s (también es el latido del failsafe). `vx`,`vy`,`w`: −100…100; `vmax`: 10…100 (%) |
| Página → robot | `M i v vmax` | Prueba de una rueda, reemplaza a `C` mientras se mantiene (`i`: 0=FL 1=FR 2=RL 3=RR; `v`: −100…100) |
| Página → robot | `S` | STOP inmediato |
| Página → robot | `P n` | Ping; el robot lo devuelve igual para medir latencia |
| Robot → página | `{"up","heap","cli","hz","fs","ina","v","i","p","mah"}` | Estado 1 vez/s: tiempo activo, memoria libre, dispositivos, comandos/s recibidos, failsafe activo, INA226 presente, voltaje (V), corriente (A), potencia (W), mAh consumidos |
| Robot → página | `TOMADO` | Otro dispositivo tomó el control |

> La página deja de enviar cuando está oculta (pantalla bloqueada, cambio de app) → el robot entra en failsafe.

### Cinemática mecanum
El joystick entrega `vy` (avance), `vx` (lateral) y `w` (giro), cada uno en −1…+1:
```
FL = vy + vx + w     FR = vy − vx − w
RL = vy − vx + w     RR = vy + vx − w
```
- Normalizar: si algún |valor| > 1, dividir los cuatro por el máximo.
- **Signo** → bits de dirección del motor en el byte del 74HC595 (01 adelante, 10 atrás).
- **Magnitud** → PWM del motor, con arranque mínimo:
  - **PWM mínimo de arranque: 30 %** (`PWM_MIN_PCT`, medido en 3c: por debajo los motores no giran). Toda velocidad distinta de cero va de 30 % hasta la velocidad máxima elegida en la página.
  - **Umbral por rueda: 0.08** (`UMBRAL_RUEDA`): bajo ese valor la rueda se apaga en vez de saltar al 30 %, para no deformar las diagonales.
  - La barra de velocidad máxima de la página va de 30 % a 100 %.
- **Joysticks de la página:** izquierdo = mover (`vx`, `vy`), derecho horizontal = girar (`w`, derecha = horario). Zona muerta central del **12 %** del recorrido (`ZONA_MUERTA` en `app.js`); el resto se reescala a 0…100.
- Los signos de `vx` y `w` dependen del montaje de los rodillos: ✅ **validados en 3c** con calibración por defecto (ver tabla *Byte de dirección para movimientos mecanum*).
- **Calibración en `test_web.ino`:** `SIGNO_VX`, `SIGNO_W` (±1) para invertir un eje y `INVERTIR[4]` (±1 por rueda FL, FR, RL, RR) para una rueda con polaridad invertida.
- Bytes que produce la fórmula (orden RR RL FR FL = Q7..Q0): adelante `01 01 01 01` (0x55) · derecha `01 10 10 01` (0x69) · rotar CW `10 01 10 01` (0x99). El Monitor Serial imprime el byte y el PWM de cada comando.

### Sub-pasos de `test_web`
| Sub-paso | Valida |
|---|---|
| 3a ✅ | AP + página servida desde LittleFS (motores en STANDBY) |
| 3b ✅ | WebSocket: comandos de la página visibles en el Monitor Serial + failsafe (sin motores) |
| 3c ✅ | Botones (adelante, atrás, strafe, rotar) moviendo motores vía cinemática + prueba de ruedas individuales + failsafe; confirmar sentidos |
| 3d ✅ | Dos joysticks táctiles (mover + girar) + velocidad proporcional con PWM mínimo; prueba de ruedas pasa a sección de diagnóstico |
| 3e ✅ | Telemetría de batería con INA226: voltaje, % de carga, corriente, potencia, mAh y avisos de batería baja/crítica |

---

## Subsistemas de hardware

### Potencia
- Batería 2S 18650 → 7.4V nominal (6.0–8.4V rango)
- BMS 2S 20A
- Protección polaridad: MOSFET-P AO3401
- Fusible: Polyfuse MF-R600 6A
- TVS: P6KE10A (clamp 10V)
- Regulador lógica: AMS1117-3.3V → 3V3 para todo el circuito lógico
- Net de potencia protegida en KiCad: **`+VBAT_P`** (unificado en todas las hojas)

### Control de motores
- 2× TB6612FNG (SSOP-24) — U2 motores frontales (FL/FR), U3 traseros (RL/RR)
- 74HC595 (SOIC-16) como expansor de dirección
- VM = 7.4V directo desde batería (`+VBAT_P`)
- VCC lógica = 3.3V
- STBY compartido GPIO16 + pull-up 10k a 3V3 (R8, uno solo para ambos drivers)
- Corriente continua: 1.2A/canal, pico 3.2A
- Desacople por TB6612: 100µF bulk VM + 100nF VM + 100nF VCC; 100nF en el 74HC595
- Filtro por motor: 100nF/50V entre terminales (C25–C28)
- Detalle completo: `hardware/MecanumRobot_v1.1/HOJA_MOTOR_notas.md`

### Sensores I2C (bus GPIO4/GPIO5)
- MPU-6050 → dirección 0x68 (AD0=GND)
- VL53L0X #1 → dirección 0x30 (reasignada via XSHUT GPIO1)
- VL53L0X #2 → dirección 0x29 (default, XSHUT GPIO3)
- **INA226 → dirección 0x40** (A0 = A1 = GND) — monitor de batería
- Al arrancar, `test_web` escanea el bus e imprime los dispositivos encontrados.

### Monitor de batería — INA226 (módulo con shunt R010)
- **Shunt 10 mΩ** → rango ±81.92 mV / 0.010 Ω = **±8.19 A** (el polyfuse de 6 A queda dentro del rango).
- Bus voltage hasta 36 V → mide la batería 2S directo, sin divisor. Resolución 1.25 mV.
- Sin librerías: registros leídos con `Wire`.
  - `0x00` configuración = `0x4527` (promedio 16, conversión 1.1 ms, continuo → dato nuevo cada ~35 ms)
  - `0x01` voltaje del shunt: 2.5 µV/bit con signo → `I = Vshunt / 0.010`
  - `0x02` voltaje del bus: 1.25 mV/bit
  - `0xFE` = `0x5449` y `0xFF` = `0x2260` → identifica que el chip es INA226
- **Conexión fase 1 (solo voltaje):** IN+, IN- y VBUS unidos a `+VBAT_P`; la corriente marca ≈ 0.
- **Conexión fase 2 (voltaje + corriente):** shunt en serie en el positivo: batería protegida → IN+ → shunt → IN- → resto del robot; VBUS a IN-. Si la corriente sale negativa, IN+ e IN- están invertidos.
- **Página:** % de carga por curva Li-ion por celda (tabla en `app.js`); aviso **Batería baja < 6.6 V** (3.3 V/celda) y **Batería crítica < 6.2 V** (3.1 V/celda). Con motores andando el voltaje cae y el % se ve menor: es normal.
- **Respaldo:** divisor 100k/13k en A0 (no montado). Si se usa, considerar que la NodeMCU tiene su propio divisor interno en A0 (220k/100k) que desvía la lectura ~3–4 %.

---

## PCB

| Parámetro | Valor |
|-----------|-------|
| Dimensiones | 100mm × 80mm |
| Capas | 2 (Top señal / Bottom GND plane) |
| Conector batería | XT30 macho |
| Conector motores | **Bornera de resorte 8 posiciones** (J2), pitch 3.5mm, entrada horizontal |
| Software | Fritzing (v1.0) · KiCad (v1.1, en curso) |

### Bornera de motores J2 (serigrafía)

| Pin | Net | Serigrafía |
|-----|-----|------------|
| 1 | MOT_FL_A | FL+ |
| 2 | MOT_FL_B | FL- |
| 3 | MOT_FR_A | FR+ |
| 4 | MOT_FR_B | FR- |
| 5 | MOT_RL_A | RL+ |
| 6 | MOT_RL_B | RL- |
| 7 | MOT_RR_A | RR+ |
| 8 | MOT_RR_B | RR- |

> Se eligió bornera de resorte (no de tornillo) porque las de tornillo se aflojan con la vibración. Reemplaza a los 4× JST-PH del diseño original.

### Convenciones KiCad v1.1
- Global Labels: tipo **Output** en la hoja MCU (origen), tipo **Input** en las hojas destino
- Nets locales de motor: sufijo `_M1` = TB6612 #1 (U2), `_M2` = TB6612 #2 (U3)

---

## Estructura del repositorio

```
MecanumRobot/
├── CLAUDE.md                          ← este archivo
├── platformio.ini                     ← src_dir apunta al sketch activo
├── docs/
│   └── Directriz_v1_0.md             ← directriz completa del proyecto
├── hardware/
│   ├── fritzing/                      ← PCB v1.0
│   │   ├── RobotMecanum.fzz          ← proyecto editable
│   │   ├── RobotMecanum_bb.png       ← vista protoboard
│   │   ├── RobotMecanum_esquemático.png
│   │   └── RobotMecanum_etch_*.pdf/svg ← exportes de fabricación
│   └── MecanumRobot_v1.1/             ← KiCad v1.1 (en curso)
│       ├── MecanumRobot_v1.1.kicad_sch  ← hoja raíz
│       ├── Schematic/                 ← PWR, MCU, MOTOR, SENSOR (+ AUX pendiente)
│       ├── libs/                      ← símbolo propio MiNodeMCU
│       ├── bom/BOM_MecanumRobot_v1.1.csv
│       ├── HOJA_MOTOR_notas.md        ← notas de diseño hoja MOTOR
│       ├── pcb/                       ← (vacío — layout pendiente)
│       └── gerbers/                   ← (vacío)
└── firmware/
    ├── test_74hc595/                  ← PASO 1 ✅
    │   └── test_74hc595.ino
    ├── diag_74hc595/                  ← herramienta de diagnóstico (multímetro)
    │   └── diag_74hc595.ino
    ├── test_tb6612/                   ← PASO 2 ✅
    │   └── test_tb6612.ino
    ├── test_web/                      ← PASO 3 — ACTUAL (control web)
    │   ├── test_web.ino
    │   └── data/                      ← se carga a LittleFS
    │       ├── index.html
    │       ├── style.css
    │       └── app.js
    ├── test_i2c/                      ← paso 4 (pospuesto: faltan sensores)
    ├── test_espnow/                   ← opcional, a futuro
    └── mecanum_main/                  ← firmware final (por crear)
```

---

## Convenciones de código

- Un archivo `.ino` por subsistema de prueba, cada uno en su carpeta bajo `firmware/`
- Comentarios en español
- Constantes de pines en `#define` al inicio
- Serial.begin(115200) en todo sketch de prueba (ver nota GPIO1/GPIO3 a partir de `test_i2c`)
- Funciones de bajo nivel separadas de la lógica de movimiento

---

## Flujo de trabajo — construcción incremental

1. ✅ **test_74hc595** — ESP8266 + 74HC595 + 8 LEDs
2. ✅ **test_tb6612** — agregar TB6612 ×2 + motores reales
3. ✅ **test_web** — control remoto por página web (sub-pasos 3a–3e, ver sección *Control remoto por página web*)
4. ⏸ **test_i2c** — agregar MPU-6050 y VL53L0X (pospuesto hasta tener los sensores)
5. ⬜ **mecanum_main** — firmware completo integrado
6. ⬜ **test_espnow** — opcional: control con un segundo ESP como mando físico

### Pendientes de hardware
- [ ] Completar hoja AUX en KiCad v1.1 y quitarla del `.gitignore`
- [ ] ERC global del esquemático v1.1
- [ ] **Agregar INA226 al esquemático v1.1** (reemplaza al divisor de A0 como monitor de batería), en la hoja PWR porque el shunt va en la línea de potencia:
  - Shunt 10 mΩ 1 % en 2512, ≥ 1 W (a 6 A disipa 0.36 W), **conexión Kelvin** (pistas de medición separadas desde los pads del shunt hasta IN+/IN-)
  - Ubicación: entre la salida de la protección (polyfuse + MOSFET) y `+VBAT_P` → crear un net intermedio (p. ej. `+VBAT_PROT`) para que el resto de hojas no cambie. Así mide el consumo total (motores + lógica)
  - INA226AIDGSR (VSSOP-10, pitch 0.5 mm) o footprint de módulo con pines si se prefiere no soldar paso fino
  - VS a +3V3 con 100 nF, A0/A1 a GND (0x40), SDA/SCL a los Global Labels del bus I2C, ALERT sin conectar
  - Divisor 100k/13k de A0: dejar como DNP (no montar) o eliminar
- [ ] Layout PCB v1.1 en KiCad + gerbers
