#include <Wire.h>
#include "config.h"
#include "bus_i2c.h"

static const char *nombreI2C(uint8_t dir) {
  switch (dir) {
    case 0x29: return "VL53L0X (dirección por defecto)";
    case 0x30: return "VL53L0X #1 (reasignado)";
    case 0x40: return "INA226";
    case 0x68: return "MPU-6050";
    default:   return "desconocido";
  }
}

void i2cIniciar() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_FRECUENCIA);
}

void i2cEscanear() {
  LOG("Escaneo del bus I2C (SDA=D2, SCL=D1):\n");
  int encontrados = 0;
  for (uint8_t dir = 1; dir < 127; dir++) {
    Wire.beginTransmission(dir);
    if (Wire.endTransmission() == 0) {
      LOG("   0x%02X  %s\n", dir, nombreI2C(dir));
      encontrados++;
    }
  }
  if (encontrados == 0) LOG("   (ningún dispositivo — revisar SDA, SCL, VCC y GND)\n");
}
