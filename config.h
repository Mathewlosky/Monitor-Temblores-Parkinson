#ifndef CONFIG_H
#define CONFIG_H

// Muestreo
#define FRECUENCIA_MUESTREO 100
#define MUESTRAS 256
#define PASO_VENTANA 128   // solape 50 %: evalua cada 128 muestras (1.28 s)

// Banda del temblor (Hz)
#define BANDA_MIN 3.5f
#define BANDA_MAX 7.5f

// Banda total para calcular la razón de energía (Hz)
#define TOTAL_MIN 0.5f
#define TOTAL_MAX 15.0f

// MPU6050 en rango +-2 g
#define LSB_POR_G 16384.0f

#endif
