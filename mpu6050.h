#ifndef MPU6050_H
#define MPU6050_H

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

#define MPU_ADDR 0x68

class MPU6050 {
private:
    float ax = 0, ay = 0, az = 0;           // en g
    float ax_ant = 0, ay_ant = 0, az_ant = 0;

    bool escribirRegistro(uint8_t reg, uint8_t valor) {
        Wire.beginTransmission(MPU_ADDR);
        Wire.write(reg);
        Wire.write(valor);
        return Wire.endTransmission() == 0;
    }

public:

    bool iniciar() {

        // Despertar
        if (!escribirRegistro(0x6B, 0x00)) return false;

        // Verificar que responde (WHO_AM_I)
        Wire.beginTransmission(MPU_ADDR);
        Wire.write(0x75);
        if (Wire.endTransmission(false) != 0) return false;
        Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1, (uint8_t)true);
        if (Wire.available() < 1) return false;
        uint8_t id = Wire.read();
        if (id == 0x00 || id == 0xFF) return false;   // acepta clones

        // Filtro digital ~20 Hz
        if (!escribirRegistro(0x1A, 0x04)) return false;

        // Frecuencia de muestreo: 1000/(1+9) = 100 Hz
        if (!escribirRegistro(0x19, 0x09)) return false;

        // Rango +-2 g
        if (!escribirRegistro(0x1C, 0x00)) return false;

        return true;
    }

    bool leerAceleracion() {
        Wire.beginTransmission(MPU_ADDR);
        Wire.write(0x3B);
        if (Wire.endTransmission(false) != 0) return false;

        Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6, (uint8_t)true);

        if (Wire.available() < 6) return false;

        // Cada Wire.read() en su propia linea: el orden de evaluacion de
        // (Wire.read() << 8) | Wire.read() NO esta garantizado en C++.
        uint8_t xh = Wire.read(), xl = Wire.read();
        uint8_t yh = Wire.read(), yl = Wire.read();
        uint8_t zh = Wire.read(), zl = Wire.read();

        ax = (int16_t)((xh << 8) | xl) / LSB_POR_G;
        ay = (int16_t)((yh << 8) | yl) / LSB_POR_G;
        az = (int16_t)((zh << 8) | zl) / LSB_POR_G;

        return true;
    }

    // Suma del cambio absoluto entre muestras (en g)
    float detectarMovimiento() {
        float mov = fabsf(ax - ax_ant) + fabsf(ay - ay_ant) + fabsf(az - az_ant);
        ax_ant = ax;
        ay_ant = ay;
        az_ant = az;
        return mov;
    }

    float obtenerAX() { return ax; }
    float obtenerAY() { return ay; }
    float obtenerAZ() { return az; }
};

#endif
