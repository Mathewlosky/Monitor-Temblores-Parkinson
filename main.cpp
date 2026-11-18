#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include "config.h"
#include "apwifieeprommode.h"
#include "mpu6050.h"
#include "fft.h"
#include "web.h"

// ---------------- Pines ----------------
const int PIN_LED_ROJO = 12;
const int PIN_LED_VERDE = 27;
const int PIN_LED_AMARILLO = 14;

// ---------------- Umbrales (en g) ----------------
// Movimiento "grande y lento" (brazo, levantarse, caminar): se mide con la senal
// pasabajos (<2 Hz), asi los taps/temblor de ~5 Hz casi no cuentan.
const float UMBRAL_MOVIMIENTO = 0.20f;          // g: enciende el amarillo (tap: <0.05, movimiento: >0.34)
const float UMBRAL_MOVIMIENTO_FIN = 0.06f;      // g: histeresis para volver a reposo
const float ALFA_LP = 0.118f;                   // pasabajos 1er orden, fc = 2 Hz (x2 en cascada)
const float ALFA_BASE = 0.019f;                 // linea base lenta, fc = 0.3 Hz
const unsigned long HOLD_MOVIMIENTO = 1000;     // ms que se mantiene el amarillo
const float UMBRAL_TEMBLOR_RMS = 0.012f;        // RMS en banda 3.5-7.5 Hz (ruido en mesa: 0.004-0.009)
const float UMBRAL_RATIO = 0.40f;               // fraccion de energia en banda (mov. voluntario: 0.03-0.14)
const float TOLERANCIA_FREC = 0.25f;            // Hz, por la interpolacion del pico
const int VENTANAS_TEMBLOR = 3;                 // 3 ventanas con solape = ~5 s

// ---------------- Tiempos ----------------
const unsigned long INTERVALO = 10000;          // 100 Hz (us)
const unsigned long DURACION_ALERTA = 5000;
const unsigned long INTERVALO_PARPADEO = 200;
const unsigned long REINTENTO_ERROR = 3000;

unsigned long tiempoAnterior = 0;
unsigned long tiempoInicioTemblor = 0;
unsigned long tiempoUltimoParpadeo = 0;
unsigned long tiempoUltimoMovimiento = 0;
unsigned long tiempoError = 0;

// ---------------- Objetos ----------------
MPU6050 sensor;
AnalizadorFFT fftX, fftY, fftZ;

// ---------------- Estados ----------------
enum Estado {
    ST_INICIO,
    ST_REPOSO,
    ST_MOVIMIENTO,
    ST_POSIBLE_TEMBLOR,
    ST_ERROR
};

Estado estadoActual = ST_INICIO;

int contadorTemblor = 0;
float sumaMov = 0;
int nMov = 0;
int fallosLectura = 0;

// Filtros de movimiento lento (por eje)
float lp1[3] = {0, 0, 0};
float lp2[3] = {0, 0, 0};
float baseLenta[3] = {0, 0, 0};
float maxMovLento = 0;
float maxMovLentoPrev = 0;   // la ventana FFT abarca 2 bloques de 128 muestras

void iniciarFiltroMov(float x, float y, float z) {
    float v[3] = {x, y, z};
    for (int i = 0; i < 3; i++) {
        lp1[i] = lp2[i] = baseLenta[i] = v[i];
    }
    maxMovLento = 0;
    maxMovLentoPrev = 0;
}

// Devuelve cuanto se aparta (en g) la senal lenta de su linea base, sumando los 3 ejes
float movimientoLento(float x, float y, float z) {
    float v[3] = {x, y, z};
    float suma = 0;
    for (int i = 0; i < 3; i++) {
        lp1[i] += ALFA_LP * (v[i] - lp1[i]);
        lp2[i] += ALFA_LP * (lp1[i] - lp2[i]);
        baseLenta[i] += ALFA_BASE * (lp2[i] - baseLenta[i]);
        suma += fabsf(lp2[i] - baseLenta[i]);
    }
    return suma;
}

void setLeds(bool r, bool v, bool a) {
    digitalWrite(PIN_LED_ROJO, r);
    digitalWrite(PIN_LED_VERDE, v);
    digitalWrite(PIN_LED_AMARILLO, a);
}

// ---------------- WiFi en otra tarea (nucleo 0) ----------------
void tareaWifi(void *param) {

    // El AP y la pagina de monitoreo quedan siempre activos
    Serial.println("Iniciando AP...");
    initAP("espgroup5", "contrasena", registrarRutasWeb);

    // Ademas, si hay una red guardada, se conecta a ella (tambien sirve la pagina)
    Serial.println("Buscando redes guardadas...");
    if (lastRed()) {
        Serial.println("WiFi conectado a la red guardada");
    }

    for (;;) {
        loopAP();
        vTaskDelay(2 / portTICK_PERIOD_MS);
    }
}

// ---------------- Analisis de una ventana ----------------
void evaluarVentana() {

    fftX.calcular();
    fftY.calcular();
    fftZ.calcular();

    float potBanda = fftX.obtenerPotenciaBanda() + fftY.obtenerPotenciaBanda() + fftZ.obtenerPotenciaBanda();
    float potTotal = fftX.obtenerPotenciaTotal() + fftY.obtenerPotenciaTotal() + fftZ.obtenerPotenciaTotal();

    float rms = sqrtf(potBanda);
    float ratio = (potTotal > 1e-9f) ? potBanda / potTotal : 0;

    AnalizadorFFT *dom = &fftX;
    if (fftY.obtenerAmplitud() > dom->obtenerAmplitud()) dom = &fftY;
    if (fftZ.obtenerAmplitud() > dom->obtenerAmplitud()) dom = &fftZ;
    float frecuencia = dom->obtenerFrecuencia();

    float movProm = (nMov > 0) ? sumaMov / nMov : 0;
    sumaMov = 0;
    nMov = 0;

    // Movimiento lento maximo en toda la ventana (los 2 bloques de 128 muestras)
    float movLentoVentana = max(maxMovLento, maxMovLentoPrev);
    maxMovLentoPrev = maxMovLento;
    maxMovLento = 0;

    bool compatible =
        frecuencia >= (BANDA_MIN - TOLERANCIA_FREC) &&
        frecuencia <= (BANDA_MAX + TOLERANCIA_FREC) &&
        rms >= UMBRAL_TEMBLOR_RMS &&
        ratio >= UMBRAL_RATIO &&
        movLentoVentana < UMBRAL_MOVIMIENTO;       // sin movimiento grande (brazo, caminar)

    if (estadoActual != ST_POSIBLE_TEMBLOR) {
        contadorTemblor = compatible ? contadorTemblor + 1 : 0;
    }

    Serial.printf("Frec: %.2f Hz | RMS: %.4f g | Ratio: %.2f | MovProm: %.4f g | MovLentoMax: %.3f g | Compat: %d\n",
                  frecuencia, rms, ratio, movProm, movLentoVentana, contadorTemblor);

    // Publicar para la pagina web
    int16_t esp[WEB_BINES];
    for (int k = 0; k < WEB_BINES; k++) {
        float bx = fftX.amplitudBin(k + 1);
        float by = fftY.amplitudBin(k + 1);
        float bz = fftZ.amplitudBin(k + 1);
        esp[k] = (int16_t)(sqrtf(bx * bx + by * by + bz * bz) * 1000.0f);
    }
    portENTER_CRITICAL(&webMux);
    dw.ventana++;
    dw.frec = frecuencia;
    dw.rms = rms;
    dw.ratio = ratio;
    dw.movLento = movLentoVentana;
    dw.movProm = movProm;
    dw.compat = contadorTemblor;
    memcpy(dw.espectro, esp, sizeof(esp));
    portEXIT_CRITICAL(&webMux);

    if (estadoActual != ST_POSIBLE_TEMBLOR && contadorTemblor >= VENTANAS_TEMBLOR) {
        Serial.println("POSIBLE TEMBLOR DETECTADO");
        estadoActual = ST_POSIBLE_TEMBLOR;
        tiempoInicioTemblor = millis();
        tiempoUltimoParpadeo = millis();
        contadorTemblor = 0;
    }
}

// ---------------- Muestreo a 100 Hz ----------------
void muestrear() {

    if ((unsigned long)(micros() - tiempoAnterior) < INTERVALO) return;

    tiempoAnterior += INTERVALO;

    if ((unsigned long)(micros() - tiempoAnterior) > 5 * INTERVALO) {
        tiempoAnterior = micros();
    }

    if (!sensor.leerAceleracion()) {
        if (++fallosLectura >= 50) {
            Serial.println("Sensor sin respuesta");
            estadoActual = ST_ERROR;
            tiempoError = millis();
        }
        return;
    }
    fallosLectura = 0;

    bool completo = fftX.agregarMuestra(sensor.obtenerAX());
    fftY.agregarMuestra(sensor.obtenerAY());
    fftZ.agregarMuestra(sensor.obtenerAZ());
    webPushMuestra(sensor.obtenerAX(), sensor.obtenerAY(), sensor.obtenerAZ());

    float mov = sensor.detectarMovimiento();
    sumaMov += mov;
    nMov++;

    // Amarillo con retencion: se mantiene HOLD_MOVIMIENTO ms tras el ultimo pico
    unsigned long ahora = millis();

    float movLento = movimientoLento(sensor.obtenerAX(), sensor.obtenerAY(), sensor.obtenerAZ());
    if (movLento > maxMovLento) maxMovLento = movLento;

    if (movLento > UMBRAL_MOVIMIENTO) {
        tiempoUltimoMovimiento = ahora;
        if (estadoActual == ST_REPOSO) estadoActual = ST_MOVIMIENTO;
    } else if (estadoActual == ST_MOVIMIENTO) {
        if (movLento > UMBRAL_MOVIMIENTO_FIN) {
            tiempoUltimoMovimiento = ahora;          // aun en la zona de histeresis
        } else if (ahora - tiempoUltimoMovimiento >= HOLD_MOVIMIENTO) {
            estadoActual = ST_REPOSO;
        }
    }

    if (completo) {
        evaluarVentana();
    }
}

// ---------------- LEDs ----------------
void actualizarLeds() {

    switch (estadoActual) {

        case ST_REPOSO:
            setLeds(LOW, HIGH, LOW);
            break;

        case ST_MOVIMIENTO:
            setLeds(LOW, LOW, HIGH);
            break;

        case ST_POSIBLE_TEMBLOR: {
            unsigned long t = millis();

            if (t - tiempoInicioTemblor >= DURACION_ALERTA) {
                Serial.println("Fin de alerta");
                contadorTemblor = 0;
                estadoActual = ST_REPOSO;
                setLeds(LOW, HIGH, LOW);
            } else {
                digitalWrite(PIN_LED_VERDE, LOW);
                digitalWrite(PIN_LED_AMARILLO, LOW);

                if (t - tiempoUltimoParpadeo >= INTERVALO_PARPADEO) {
                    tiempoUltimoParpadeo = t;
                    digitalWrite(PIN_LED_ROJO, !digitalRead(PIN_LED_ROJO));
                }
            }
            break;
        }

        default:
            break;
    }
}

void setup() {

    Serial.begin(115200);

    pinMode(PIN_LED_ROJO, OUTPUT);
    pinMode(PIN_LED_VERDE, OUTPUT);
    pinMode(PIN_LED_AMARILLO, OUTPUT);
    setLeds(LOW, LOW, LOW);

    Wire.begin(21, 22);
    Wire.setClock(400000);

    EEPROM.begin(512);

    Serial.println("Iniciando sistema...");

    // Umbrales que la pagina dibuja como lineas de referencia
    dw.uRms = UMBRAL_TEMBLOR_RMS;
    dw.uRatio = UMBRAL_RATIO;
    dw.uMov = UMBRAL_MOVIMIENTO;
    dw.uN = VENTANAS_TEMBLOR;

    xTaskCreatePinnedToCore(tareaWifi, "wifi", 12288, NULL, 1, NULL, 0);
}

void loop() {

    dw.estado = (uint8_t)estadoActual;

    switch (estadoActual) {

        case ST_INICIO:

            Serial.println("Inicializando MPU6050...");

            if (sensor.iniciar()) {
                Serial.println("MPU6050 iniciado");

                sensor.leerAceleracion();
                sensor.detectarMovimiento();
                iniciarFiltroMov(sensor.obtenerAX(), sensor.obtenerAY(), sensor.obtenerAZ());

                fftX.reiniciar();
                fftY.reiniciar();
                fftZ.reiniciar();
                sumaMov = 0;
                nMov = 0;
                fallosLectura = 0;
                contadorTemblor = 0;

                tiempoAnterior = micros();
                estadoActual = ST_REPOSO;
            } else {
                Serial.println("Error MPU6050");
                estadoActual = ST_ERROR;
                tiempoError = millis();
            }
            break;

        case ST_ERROR:

            setLeds(HIGH, HIGH, HIGH);

            if (millis() - tiempoError >= REINTENTO_ERROR) {
                estadoActual = ST_INICIO;
            }
            break;

        default:
            muestrear();
            actualizarLeds();
            break;
    }
}
