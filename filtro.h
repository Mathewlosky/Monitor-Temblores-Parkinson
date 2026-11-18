
#ifndef FILTRO_H
#define FILTRO_H

class FiltroBanda {
private:
    float entradaAnterior = 0;
    float salidaHP = 0;
    float salidaLP = 0;

    // Coeficientes para Fs = 100 Hz
    // Pasaaltos: 3.5 Hz
    const float a = 0.8197;

    // Pasabajos: 7.5 Hz
    const float alpha = 0.3202;

public:
    float actualizar(float entrada) {

        // Filtro pasaaltos
        float hp = a * (salidaHP + entrada - entradaAnterior);

        entradaAnterior = entrada;
        salidaHP = hp;

        // Filtro pasabajos
        salidaLP = salidaLP + alpha * (hp - salidaLP);

        return salidaLP;
    }
};

#endif
