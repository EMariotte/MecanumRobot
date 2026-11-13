# Notas de diseño — Hoja MOTOR

**Proyecto:** Robot Mecanum 4WD v1.1
**Hoja:** MOTOR — Drivers de motor y expansor de dirección
**Autor:** Efrain
**Fecha inicio:** _(completar)_
**Estado:** F1 y F2 completadas — listo para F3 (dibujo en KiCad)

---

## 1. Propósito de la hoja

La hoja MOTOR implementa el subsistema de **control de los 4 motores DC** del robot mecanum. Recibe señales de control del MCU (PWM y dirección) y las traduce en corriente real hacia los motores. Es el eslabón entre el mundo lógico (3.3V) y el mundo de potencia (7.4V/1.2A).

### Funcionalidad esperada
- Control de velocidad por PWM (4 canales independientes — uno por motor)
- Control de dirección de giro (CW, CCW, freno corto, libre) mediante shift register
- Habilitación/deshabilitación global (STBY)
- Protección contra arranque indefinido (pull-up STBY)

---

## 2. Componentes principales — confirmados

| Ref | Componente | Cantidad | Package | Disponibilidad KiCad |
|-----|------------|----------|---------|----------------------|
| U2 | TB6612FNG | 1 | SSOP-24 (pitch 0.65mm) | ✓ Existe en librería |
| U3 | TB6612FNG | 1 | SSOP-24 (pitch 0.65mm) | ✓ Existe en librería |
| U4 | 74HC595 | 1 | SOIC-16 (pitch 1.27mm) | ✓ Existe en librería |
| J2-J5 | JST-PH 2pin | 4 | Through-hole | ✓ Existe en librería |

### Equipo de soldadura disponible
- **Estación Baku 909 3-en-1** (cautín + hot air + reflow)
- Permite manejar SSOP-24 (0.65mm pitch) y SOIC-16 (1.27mm pitch) sin problemas

---

## 3. Interfaces de la hoja

### 3.1 Señales que ENTRAN (Global Labels)

| Label | Tipo | Origen | Destino en MOTOR |
|-------|------|--------|------------------|
| \`PWM_FL\` | Output del MCU | Hoja MCU (GPIO12) | U2 pin PWMA |
| \`PWM_FR\` | Output del MCU | Hoja MCU (GPIO13) | U2 pin PWMB |
| \`PWM_RL\` | Output del MCU | Hoja MCU (GPIO14) | U3 pin PWMA |
| \`PWM_RR\` | Output del MCU | Hoja MCU (GPIO15) | U3 pin PWMB |
| \`SPI_DS\` | Output del MCU | Hoja MCU (GPIO0) | U4 pin DS (data) |
| \`SPI_SHCP\` | Output del MCU | Hoja MCU (GPIO2) | U4 pin SHCP (clock) |
| \`SPI_STCP\` | Output del MCU | Hoja MCU (GPIO10) | U4 pin STCP (latch) |
| \`STBY\` | Output del MCU | Hoja MCU (GPIO16) | U2 + U3 pines STBY |

### 3.2 Power Ports que ENTRAN

| Power | Función | Conecta a |
|-------|---------|-----------|
| \`+VBAT_P\` | Potencia para motores (7.4V) | VM1/VM2/VM3 de cada TB6612 |
| \`+3V3\` | Lógica | VCC de cada TB6612, VCC del 74HC595 |
| \`GND\` | Tierra común | GND/PGND de todos los ICs |

### 3.3 Salidas físicas

Conectores JST-PH 2pin hacia los 4 motores. No salen Global Labels — los motores son cargas físicas externas.

---

## 4. Análisis del datasheet TB6612FNG

**Fuente:** Toshiba TB6612FNG datasheet rev. 2014-10-01
**URL:** https://toshiba.semicon-storage.com/info/TB6612FNG_datasheet_en_20141001.pdf

### 4.1 Características clave para nuestro diseño

| Parámetro | Valor | Implicación |
|-----------|-------|-------------|
| VM (motor) | 2.5V – 13.5V | Compatible con nuestro 7.4V ✓ |
| VCC (lógica) | 2.7V – 5.5V | Compatible con nuestro 3.3V ✓ |
| I_OUT continuo | 1.2A por canal | Margen ajustado con stall 1.2A motor |
| I_OUT pico | 3.2A (10ms) | Suficiente para arranque |
| Rds(on) | 0.5Ω (típico) | Calor calculado: 0.72W por motor en stall |
| Frecuencia PWM | hasta 100kHz | Usaremos 1kHz desde firmware |
| Pull-down interno | 200kΩ en pines IN/PWM/STBY | Estado seguro si flotan |
| Package | SSOP-24 (pitch 0.65mm) | Soldable con Baku 909 + flux |

### 4.2 Tabla de control H-SW (la "verdad" funcional)

Esta tabla define cómo se comportan los motores según las entradas:

| IN1 | IN2 | PWM | STBY | Modo |
|-----|-----|-----|------|------|
| H | H | H/L | H | **Short brake** (freno corto) |
| L | H | H | H | **CCW** (sentido anti-horario) |
| L | H | L | H | Short brake |
| H | L | H | H | **CW** (sentido horario) |
| H | L | L | H | Short brake |
| L | L | H | H | **Stop** (alta impedancia) |
| X | X | X | L | **Standby** (modo bajo consumo) |

**Decisión de firmware:** controlar con IN1/IN2 fijos (definen dirección) y modular velocidad con PWM. Cuando PWM=L automáticamente entra en short brake — útil para parar el motor rápidamente.

### 4.3 Pinout completo (24 pines SSOP-24)

\`\`\`
Pin 1  -> AO1     (output A1)         | Pin 24 -> VM1     (motor supply)
Pin 2  -> AO1     (output A1, doble)  | Pin 23 -> PWMA    (PWM canal A)
Pin 3  -> PGND1   (Power GND 1)       | Pin 22 -> AIN2    (input A2)
Pin 4  -> PGND1   (Power GND 1, dob.) | Pin 21 -> AIN1    (input A1)
Pin 5  -> AO2     (output A2)         | Pin 20 -> Vcc     (lógica)
Pin 6  -> AO2     (output A2, doble)  | Pin 19 -> STBY    (standby)
Pin 7  -> BO2     (output B2)         | Pin 18 -> GND     (signal GND)
Pin 8  -> BO2     (output B2, doble)  | Pin 17 -> BIN1    (input B1)
Pin 9  -> PGND2   (Power GND 2)       | Pin 16 -> BIN2    (input B2)
Pin 10 -> PGND2   (Power GND 2, dob.) | Pin 15 -> PWMB    (PWM canal B)
Pin 11 -> BO1     (output B1)         | Pin 14 -> VM3     (motor supply)
Pin 12 -> BO1     (output B1, doble)  | Pin 13 -> VM2     (motor supply)
\`\`\`

### 4.4 Observaciones importantes sobre el pinout

- **Pines duplicados de salida** (AO1, AO2, BO1, BO2, PGND1, PGND2): el datasheet los duplica para permitir mayor corriente. En el footprint son pads físicos separados que **deben conectarse externamente** en la PCB. No es opcional — si solo conectas uno, la capacidad de corriente se reduce a la mitad.

- **VM duplicado** (VM1, VM2, VM3): tres pines para la alimentación del motor. Conectar los tres a \`+VBAT_P\` con pistas anchas.

- **PGND vs GND**: el datasheet distingue Power GND (PGND1, PGND2) de Small Signal GND (GND pin 18). En nuestro PCB de 2 capas con plano de GND, conectamos todos a GND. En diseños TRL 7+ con planos separados, se manejarían como islas diferentes.

- **Pull-down interno de 200kΩ** en todos los pines de entrada: si el ESP8266 los deja flotando momentáneamente (boot), los TB6612 quedan en estado seguro (Stop o Standby).

### 4.5 Circuito de aplicación típico — capacitores recomendados

Del datasheet (Figura "Typical Application Diagram"):

| Capacitor | Valor | Función | Ubicación |
|-----------|-------|---------|-----------|
| C1 | 10µF electrolítico | Bulk VM | Cerca de VM1/VM2/VM3 |
| C2 | 0.1µF cerámico | Desacople VM alta frecuencia | Cerca de VM (en paralelo con C1) |
| C3 | 10µF electrolítico | Bulk VCC | Cerca de Vcc |
| C4 | 0.1µF cerámico | Desacople VCC alta frecuencia | Cerca de Vcc |

**Nota crítica del datasheet:** "Condensers for noise absorption (C1, C2, C3, and C4) should be connected as close as possible to the IC." — esto es regla de layout, no de esquemático.

### 4.6 Verificación del símbolo TB6612FNG en KiCad

Verificado el día del análisis: pines 15=PWMB, 16=BIN2, 17=BIN1, 19=STBY, 21=AIN1, 22=AIN2, 23=PWMA. **Todos coinciden con el datasheet.** ✓

---

## 5. Análisis del 74HC595

**Fuente:** Texas Instruments SN74HC595 datasheet rev. SCLS041J (Octubre 2021)

### 5.1 Función general

Shift register **8 bits SIPO** (Serial In, Parallel Out). Convierte **3 señales SPI** del MCU en **8 salidas paralelas independientes**. Sin él, necesitaríamos 8 GPIOs adicionales del ESP8266 para controlar las direcciones de los 4 motores.

### 5.2 Características clave

| Parámetro | Valor | Implicación |
|-----------|-------|-------------|
| VCC operación | 2V – 6V | 3.3V dentro del rango ✓ |
| Velocidad típica (tpd) | 13ns a 5V | Más rápido de lo que necesitamos |
| Corriente por salida | ±6mA a 5V | Suficiente para alimentar entradas TB6612 (1µA) |
| Consumo (ICC) | 80µA máximo | Despreciable en el sistema |
| Package elegido | SOIC-16 (pitch 1.27mm) | Soldable manualmente con flux |

### 5.3 Cómo funciona — el protocolo de control

El 74HC595 tiene **dos registros internos**:

1. **Shift register** (registro de desplazamiento): donde se cargan los bits uno a uno mediante DS+SHCP. Las salidas físicas NO cambian aún.

2. **Storage register** (registro de almacenamiento): donde están las 8 salidas visibles QA-QH. Solo se actualiza cuando se da un pulso en STCP.

**Secuencia para cambiar las 8 salidas:**
1. MCU pone bit en \`DS\` y pulsa \`SHCP\` (clock) — se desplaza al shift register
2. Repetir 8 veces hasta cargar los 8 bits en el shift register interno
3. MCU pulsa \`STCP\` (latch) — los 8 bits se copian al storage register simultáneamente
4. Las 8 salidas QA-QH cambian a la vez — sin combinaciones intermedias

**Por qué SHCP y STCP separados:** mientras se cargan los bits, las salidas no cambian. Solo cuando se hace el latch (paso 3), las 8 salidas cambian todas juntas. Esto evita que los motores reciban combinaciones de dirección inválidas durante la actualización.

### 5.4 Pinout completo (SOIC-16)

| Pin # | Nombre | Tipo | Función |
|-------|--------|------|---------|
| 1 | QB (Q1) | Output | Salida paralela bit 1 |
| 2 | QC (Q2) | Output | Salida paralela bit 2 |
| 3 | QD (Q3) | Output | Salida paralela bit 3 |
| 4 | QE (Q4) | Output | Salida paralela bit 4 |
| 5 | QF (Q5) | Output | Salida paralela bit 5 |
| 6 | QG (Q6) | Output | Salida paralela bit 6 |
| 7 | QH (Q7) | Output | Salida paralela bit 7 (último) |
| 8 | GND | Power | Tierra de referencia |
| 9 | QH' / Q7S | Output | Salida serial (cascada — no usado) |
| 10 | SRCLR / MR | Control | **Reset** activo en LOW. A +3V3 = no reset |
| 11 | SRCLK / SHCP | Input | **Clock** del shift register (SPI_SHCP del MCU) |
| 12 | RCLK / STCP | Input | **Latch** hacia las salidas (SPI_STCP del MCU) |
| 13 | OE | Control | **Output Enable** activo en LOW. A GND = salidas habilitadas |
| 14 | SER / DS | Input | **Data Serial** entrada de datos (SPI_DS del MCU) |
| 15 | QA (Q0) | Output | Salida paralela bit 0 (primer bit) |
| 16 | VCC | Power | +3V3 |

**Atención:** el pin 15 es la *primera* salida (QA o Q0), no el pin 1. Algunos datasheets antiguos las llaman Q0-Q7, los modernos QA-QH.

### 5.5 Conexiones del 74HC595 en nuestra Hoja MOTOR

| Pin | Nombre | Conectar a | Notas |
|-----|--------|-----------|-------|
| 14 | DS | \`SPI_DS\` (Global Label) | Datos seriales desde MCU |
| 11 | SHCP | \`SPI_SHCP\` (Global Label) | Clock SPI |
| 12 | STCP | \`SPI_STCP\` (Global Label) | Latch — GPIO10 dedicado en v1.1 ✓ |
| 10 | SRCLR / MR | \`+3V3\` | Pull-up para evitar reset accidental |
| 13 | OE | \`GND\` | Habilita salidas permanentemente |
| 16 | VCC | \`+3V3\` | Alimentación lógica |
| 8 | GND | \`GND\` | Tierra |
| 9 | QH' / Q7S | No Connect (X) | Solo se usa en cascada — no aplica |
| 15 | QA | \`AIN1_M1\` (net local) | Dirección Motor FL bit 1 |
| 1 | QB | \`AIN2_M1\` | Dirección Motor FL bit 2 |
| 2 | QC | \`BIN1_M1\` | Dirección Motor FR bit 1 |
| 3 | QD | \`BIN2_M1\` | Dirección Motor FR bit 2 |
| 4 | QE | \`AIN1_M2\` | Dirección Motor RL bit 1 |
| 5 | QF | \`AIN2_M2\` | Dirección Motor RL bit 2 |
| 6 | QG | \`BIN1_M2\` | Dirección Motor RR bit 1 |
| 7 | QH | \`BIN2_M2\` | Dirección Motor RR bit 2 |

### 5.6 Decisión de package — SOIC-16

**Comparativa evaluada:**

| Package | Pitch | Veredicto |
|---------|-------|-----------|
| DIP-16 | 2.54mm | Descartado — through-hole inconsistente con SMD del resto |
| **SOIC-16** | **1.27mm** | **★ ELEGIDO** — fácil de soldar con flux, compacto, coherente |
| TSSOP-16 | 0.65mm | Descartado — pitch innecesariamente difícil |
| SSOP-16 | 0.635mm | Descartado — mismo argumento que TSSOP |

**Footprint KiCad:** \`Package_SO:SOIC-16_3.9x9.9mm_P1.27mm\`

**Tamaño físico:** 9.9 × 3.9 mm — encaja perfectamente en PCB de 100×80mm.

**Part number para compra:** \`SN74HC595D\` (el sufijo "D" indica SOIC). Disponible en LCSC, Mouser, AliExpress.

### 5.7 Implementación de firmware esperada

\`\`\`cpp
// Pseudocódigo para cambiar direcciones de los 4 motores
void setMotorsDirection(uint8_t dirByte) {
    // dirByte estructura:
    // bit 0: AIN1_M1, bit 1: AIN2_M1, bit 2: BIN1_M1, bit 3: BIN2_M1
    // bit 4: AIN1_M2, bit 5: AIN2_M2, bit 6: BIN1_M2, bit 7: BIN2_M2

    digitalWrite(SPI_STCP_PIN, LOW);                        // baja latch
    shiftOut(SPI_DS_PIN, SPI_SHCP_PIN, MSBFIRST, dirByte);  // envía 8 bits
    digitalWrite(SPI_STCP_PIN, HIGH);                       // pulso latch
    // Las 8 salidas QA-QH cambian simultáneamente
}
\`\`\`

### 5.8 Componentes pasivos del 74HC595

| Ref | Valor | Conexión | Footprint KiCad |
|-----|-------|----------|-----------------|
| C18 | 100nF / 10V cerámico | VCC (pin 16) a GND (pin 8) | \`Capacitor_SMD:C_0805_2012Metric\` |

**Solo un capacitor.** El 74HC595 es de baja corriente (80µA). Un capacitor de desacople 100nF lo más cerca posible de pines 16 y 8 es todo lo que necesita. No requiere bulk electrolítico.

---

## 6. Mapeo completo de motores

### 6.1 TB6612 #1 (U2) — controla motores frontales

| Canal | Motor | Entrada PWM | Entradas DIR | Salida física |
|-------|-------|-------------|--------------|---------------|
| A | FL — Frontal Izquierdo | \`PWM_FL\` (pin 23) | AIN1=QA, AIN2=QB | AO1/AO2 -> J2 |
| B | FR — Frontal Derecho | \`PWM_FR\` (pin 15) | BIN1=QC, BIN2=QD | BO1/BO2 -> J3 |

### 6.2 TB6612 #2 (U3) — controla motores traseros

| Canal | Motor | Entrada PWM | Entradas DIR | Salida física |
|-------|-------|-------------|--------------|---------------|
| A | RL — Trasero Izquierdo | \`PWM_RL\` (pin 23) | AIN1=QE, AIN2=QF | AO1/AO2 -> J4 |
| B | RR — Trasero Derecho | \`PWM_RR\` (pin 15) | BIN1=QG, BIN2=QH | BO1/BO2 -> J5 |

---

## 7. Decisiones de diseño tomadas

1. **STBY compartido entre U2 y U3**: un solo GPIO controla ambos drivers. Decisión basada en que nunca queremos un par de motores activo y el otro apagado.

2. **74HC595 como expansor**: alternativa a usar 8 GPIOs adicionales del ESP8266. Libera GPIOs para futuras expansiones.

3. **Capacitores de desacople duplicados por TB6612**: cada IC tiene su propio par bulk + filter, no compartidos. Independencia de fallas y mejor rendimiento en alta frecuencia.

4. **Package 74HC595 en SOIC-16**: balance óptimo entre facilidad de soldadura (pitch 1.27mm) y tamaño compacto (9.9×3.9mm). Coherente con el resto del diseño SMD.

5. **Package TB6612FNG en SSOP-24**: el más estrecho (pitch 0.65mm), pero solo se justifica porque hay únicamente 2 unidades en el diseño y el Baku 909 lo maneja bien.

---

## 8. Lista de verificación pre-ERC

- [ ] Todos los Global Labels listados en sección 3.1 colocados
- [ ] Power Ports +VBAT_P, +3V3, GND en cada conexión correspondiente
- [ ] STBY conectado a ambos TB6612 + pull-up 10kΩ a +3V3
- [ ] Capacitores de desacople en cada IC (4 caps por TB6612 + 1 cap por HC595)
- [ ] 4 conectores JST-PH numerados según mapa (J2=FL, J3=FR, J4=RL, J5=RR)
- [ ] Verificación visual: PWM_FL llega al canal correcto del U2, no del U3
- [ ] OE del 74HC595 a GND (siempre habilitado)
- [ ] MR del 74HC595 a +3V3 (sin reset)
- [ ] QH' del 74HC595 marcado como No Connect (X)

---

## 9. Próximos pasos — Fase 3 (Dibujo)

Orden de trabajo en KiCad:

1. **3a. Power Ports y Global Labels** (~5 min): colocar entradas en borde izquierdo
2. **3b. Bloque 74HC595** (~10 min): U4 + C18 + conexiones a 8 nets locales
3. **3c. Bloque TB6612 #1** (~15 min): U2 + caps + conexiones FL/FR
4. **3d. Bloque TB6612 #2** (~10 min): U3 + caps + conexiones RL/RR
5. **3e. Conectores motores** (~5 min): J2-J5 + capacitores de filtro motor
6. **Fase 4 — Verificación**: ERC global, captura, documentación de cierre

---

## 10. Equipo de soldadura

**Estación Baku 909 3-en-1** disponible:
- Cautín de punta fina para SMD 0805, SOIC-16
- Hot air gun para SSOP-24 (TB6612), refinado de SOIC
- Reflow para casos especiales

Componentes adicionales sugeridos:
- Flux en pasta o gel (esencial para SSOP-24 del TB6612)
- Trenza desoldadora (para limpiar puentes accidentales)
- Pinzas anti-estáticas

---

_Documento vivo — actualizar conforme avanza el diseño_

---

## 11. CIERRE DE SESIÓN — Hoja MOTOR completada

**Estado:** ✅ Hoja MOTOR dibujada y verificada con ERC limpio (solo quedan warnings pendientes de Hoja SENSOR).

### Decisiones tomadas durante el dibujo

1. **Conector de motores: bornera única de 8 posiciones** (en vez de 4 borneras 2P o 4 JST).
   - Razón: optimización de espacio (~30% menos área), un solo componente que rutear.
   - Tipo: bornera de resorte (spring terminal) — NO de tornillo, porque las de tornillo se aflojan con vibración.
   - Riesgo de cableado mitigado con: serigrafía agrupada por pares + polaridad marcada (+/-) + líneas divisorias entre pares.
   - Footprint: TerminalBlock de resorte 8 posiciones, pitch 3.5mm, entrada horizontal.

2. **Esquema de serigrafía para la bornera J2:**
   | Pin | Net | Etiqueta serigrafía |
   |-----|-----|---------------------|
   | 1 | MOT_FL_A | FL+ |
   | 2 | MOT_FL_B | FL- |
   | 3 | MOT_FR_A | FR+ |
   | 4 | MOT_FR_B | FR- |
   | 5 | MOT_RL_A | RL+ |
   | 6 | MOT_RL_B | RL- |
   | 7 | MOT_RR_A | RR+ |
   | 8 | MOT_RR_B | RR- |

3. **Pines duplicados del TB6612 verificados:** el símbolo de KiCad agrupa internamente AO1(1,2), AO2(5,6), BO1(11,12), BO2(7,8). No requiere conexión manual de duplicados.

4. **Net de potencia protegida:** unificado como `+VBAT_P` en todas las hojas (corregido desde +VDC que se usó por error inicialmente).

5. **STBY compartido:** un solo pull-up R8 (10k) para ambos TB6612, ya que comparten la red STBY.

### Errores resueltos durante el ERC

- **Nomenclatura de nets locales:** corregido esquema M1/M2 (M1 = TB6612 #1, M2 = TB6612 #2). Inicialmente había mezcla M1/M2/M3/M4 inconsistente.
- **Tipo de Global Labels:** las etiquetas en la Hoja MCU deben ser tipo **Output** (el MCU genera). En MOTOR son tipo **Input** (reciben). Un error de tipo causaba "pin not connected".

### Capacitores de filtro de motor

- C25-C28 (100nF/50V cerámico) entre los dos terminales de cada motor.
- Decisión: 1 capacitor por motor (no 3) — suficiente para TRL 5-6 en interior. Si se detecta ruido en campo, agregar 2 más por motor en v2.0.

### Componentes acumulados de la Hoja MOTOR

| Ref | Componente | Valor/Modelo |
|-----|------------|--------------|
| U1 | 74HC595 | SOIC-16 |
| U2 | TB6612FNG | SSOP-24 (motores FL/FR) |
| U3 | TB6612FNG | SSOP-24 (motores RL/RR) |
| J2 | Bornera resorte 8P | pitch 3.5mm horizontal |
| C8 | 100nF | desacople HC595 |
| C19, C22 | 100µF | bulk VM de cada TB6612 |
| C20, C23 | 100nF | filtro VM de cada TB6612 |
| C21, C24 | 100nF | desacople VCC de cada TB6612 |
| C25-C28 | 100nF/50V | filtro de motor |
| R8 | 10kΩ | pull-up STBY (compartido) |

### Próximo paso
Hoja SENSOR — MPU-6050 + 2× VL53L0X + divisor ADC de batería.

