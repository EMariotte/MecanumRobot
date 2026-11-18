#pragma once

// ============================================================
//  BUS I2C — SDA GPIO4 (D2), SCL GPIO5 (D1)
//  INA226 hoy; MPU-6050 y VL53L0X ×2 cuando lleguen los sensores.
// ============================================================

void i2cIniciar();
void i2cEscanear();   // imprime los dispositivos encontrados
