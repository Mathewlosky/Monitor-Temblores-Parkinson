#ifndef FFT_H
#define FFT_H

#include <Arduino.h>
#include <arduinoFFT.h>   // version 1.x: lib_deps = kosme/arduinoFFT @ ^1.6.1
#include "config.h"

class AnalizadorFFT {

private:

    float datos[MUESTRAS];        // buffer circular con las ultimas 256 muestras
    int cabeza = 0;
    int llenas = 0;
    int desdeUltimo = 0;

    double vReal[MUESTRAS];
    double vImag[MUESTRAS];

    arduinoFFT FFT;

    float frecuenciaPico = 0;     // Hz (interpolada)
    float amplitudPico = 0;       // g (pico)
    float potenciaBanda = 0;      // g^2 en 3.5-7.5 Hz
    float potenciaTotal = 0;      // g^2 en 0.5-15 Hz

    static constexpr double GANANCIA_VENTANA = 0.54;   // Hamming

public:

    AnalizadorFFT() :
        FFT(vReal, vImag, MUESTRAS, FRECUENCIA_MUESTREO) {
        reiniciar();
    }

    // Devuelve true cada PASO_VENTANA muestras, una vez lleno el buffer
    bool agregarMuestra(float dato) {
        datos[cabeza] = dato;
        cabeza = (cabeza + 1) % MUESTRAS;
        if (llenas < MUESTRAS) llenas++;
        desdeUltimo++;
        return llenas >= MUESTRAS && desdeUltimo >= PASO_VENTANA;
    }

    void calcular() {

        desdeUltimo = 0;

        // Copiar en orden cronologico y quitar la media (gravedad)
        double media = 0;
        for (int i = 0; i < MUESTRAS; i++) {
            vReal[i] = datos[(cabeza + i) % MUESTRAS];
            vImag[i] = 0;
            media += vReal[i];
        }
        media /= MUESTRAS;
        for (int i = 0; i < MUESTRAS; i++) vReal[i] -= media;

        FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFT.Compute(FFT_FORWARD);
        FFT.ComplexToMagnitude();

        potenciaBanda = 0;
        potenciaTotal = 0;
        amplitudPico = 0;
        frecuenciaPico = 0;

        const double resolucion = (double)FRECUENCIA_MUESTREO / MUESTRAS;
        int iPico = -1;

        for (int i = 1; i < MUESTRAS / 2; i++) {

            double f = i * resolucion;
            if (f < TOTAL_MIN || f > TOTAL_MAX) continue;

            double amp = 2.0 * vReal[i] / (MUESTRAS * GANANCIA_VENTANA);
            double p = amp * amp / 2.0;

            potenciaTotal += p;

            if (f >= BANDA_MIN && f <= BANDA_MAX) {
                potenciaBanda += p;
                if (amp > amplitudPico) {
                    amplitudPico = amp;
                    iPico = i;
                }
            }
        }

        if (iPico > 1 && iPico < MUESTRAS / 2 - 1) {
            double a = vReal[iPico - 1];
            double b = vReal[iPico];
            double c = vReal[iPico + 1];
            double den = a - 2 * b + c;
            double delta = (fabs(den) > 1e-12) ? 0.5 * (a - c) / den : 0;
            frecuenciaPico = (iPico + delta) * resolucion;
        } else if (iPico > 0) {
            frecuenciaPico = iPico * resolucion;
        }
    }

    float obtenerFrecuencia()    { return frecuenciaPico; }
    float obtenerAmplitud()      { return amplitudPico; }
    float obtenerPotenciaBanda() { return potenciaBanda; }
    float obtenerPotenciaTotal() { return potenciaTotal; }

    // Amplitud pico (g) del bin i, valida despues de calcular()
    float amplitudBin(int i) {
        if (i < 1 || i >= MUESTRAS / 2) return 0;
        return (float)(2.0 * vReal[i] / (MUESTRAS * GANANCIA_VENTANA));
    }

    void reiniciar() {
        cabeza = 0;
        llenas = 0;
        desdeUltimo = 0;
        for (int i = 0; i < MUESTRAS; i++) {
            datos[i] = 0;
            vReal[i] = 0;
            vImag[i] = 0;
        }
    }
};

#endif
