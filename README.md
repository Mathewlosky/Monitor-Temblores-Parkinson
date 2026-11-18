# Monitor-Temblores-Parkinson
Este proyecto presenta el diseño de un monitor inercial vestible de muñeca basado en el microcontrolador ESP-32 y la IMU MPU-6050, capaz de capturar cinemática continua a 100 Hz, aislar espectralmente el temblor patológico (3.5–7.5 Hz) mediante procesamiento en el borde y servir telemetría en tiempo real a través de un portal cautivo Wi-Fi.
# Monitor Portátil de Temblores para Parkinson y Temblor Esencial

**Autores:** Christian Morante, Felix Pereira  
**Institución:** Escuela Superior Politécnica del Litoral (ESPOL) – FIEC  

---

## 1. Introducción y Objetivos
La evaluación clínica tradicional de la enfermedad de Parkinson y el temblor esencial depende de diarios subjetivos llenados por el paciente, los cuales presentan omisiones y baja precisión temporal para correlacionar los episodios motores con la medicación (levodopa). Este proyecto implementa un dispositivo vestible de muñeca basado en **ESP-32** e **IMU MPU-6050** ($25.95 USD) que captura cinemática continua a 100 Hz, aísla espectralmente el temblor patológico (3.5–7.5 Hz) mediante procesamiento en el borde (*Edge Computing*) y sirve telemetría en tiempo real a través de un portal cautivo Wi-Fi.

* **Objetivo General:** Diseñar e implementar un prototipo portátil de bajo costo para detectar, clasificar y cuantificar objetivamente episodios de temblor.
* **Objetivos Específicos:**
  1. Capturar aceleración triaxial determinista a 100 Hz mediante bus I2C a 400 kHz.
  2. Discriminar temblor patológico de movimientos voluntarios mediante filtrado digital y FFT embebida.
  3. Proveer retroalimentación visual local (semáforo LED) y visualización web en vivo sin requerir internet externo.

---

## 2. Alcance y Limitaciones

| ¿Qué SÍ resuelve el sistema? (Alcance) | ¿Qué NO resuelve el sistema? (Limitaciones) |
| :--- | :--- |
| Detección continua de temblor en reposo (3.5–7.5 Hz) con cálculo de frecuencia dominante, potencia RMS y razón de energía. | No emite diagnósticos médicos definitivos ni sustituye la valoración clínica del neurólogo. |
| Filtrado de movimientos voluntarios gruesos (caminar, mover el brazo) y cancelación de gravedad estática. | No cuantifica bradicinesia, rigidez muscular ni alteraciones de la marcha. |
| Alertas visuales inmediatas mediante semáforo LED (Verde, Amarillo, Rojo). | No sincroniza datos en servidores públicos en la nube. |
| Portal web autónomo (AP Wi-Fi `espgroup5`) con gráficas HTML5 Canvas y reconexión vía EEPROM. | Prototipo actual montado en protoboard (sin encapsulado hermético IP67 ni PCB SMD final). |

---

## 3. Arquitectura y Diagramas del Diseño

### 3.1 Diagrama de Contexto
![Diagrama de Contexto](docs/img/diag1_contexto.png)

* **Paciente / Cuidador:** Porta el sensor en la muñeca y recibe señalización visual directa mediante LEDs.
* **Entorno Físico:** Introduce gravedad estática ($1g$), gestos voluntarios y alimentación USB de 5 V.
* **Especialista Médico:** Accede vía navegador (`192.168.4.1`) para observar métricas en vivo y configurar credenciales Wi-Fi.

### 3.2 Diagrama de Bloques de Hardware y Software
![Diagrama de Bloques](docs/img/diag2_bloques.png)

### 3.3 Máquina de Estados Finitos (FSM)
![Máquina de Estados](docs/img/diag3_fsm.png)

---

## 4. Estructura del Código Fuente (`src/`)

* [`src/config.h`](src/config.h): Parámetros metrológicos ($F_s = 100\text{ Hz}$, ventana de 256 muestras, solape del 50%, banda clínica 3.5–7.5 Hz).
* [`src/mpu6050.h`](src/mpu6050.h): Inicialización de registros I2C, filtro paso bajo hardware (DLPF ~20 Hz) y lectura atómica por ráfaga de 6 bytes.
* [`src/fft.h`](src/fft.h): Sustracción dinámica de componente DC (gravedad), ventana de Hamming e interpolación parabólica de pico sub-bin.
* [`src/main.cpp`](src/main.cpp): Gestión dual-core con **FreeRTOS** (Core 0 para Wi-Fi/Web, Core 1 para muestreo a 100 Hz), filtro de movimiento lento ($<2\text{ Hz}$) y validación por 3 ventanas consecutivas ($\sim 5\text{ s}$).
* [`src/web.h`](src/web.h) & [`src/apwifieeprommode.h`](src/apwifieeprommode.h): Servidor web embebido en `PROGMEM`, sincronización *thread-safe* mediante spinlock (`webMux`) y almacenamiento no volátil en EEPROM.

### Fragmento clave: Criterio Clínico Multivariable (`src/main.cpp`)
```cpp
bool compatible =
    frecuencia >= (BANDA_MIN - TOLERANCIA_FREC) &&
    frecuencia <= (BANDA_MAX + TOLERANCIA_FREC) &&
    rms >= UMBRAL_TEMBLOR_RMS &&         // >= 0.012 g
    ratio >= UMBRAL_RATIO &&             // >= 0.40 (40% de energía en banda)
    movLentoVentana < UMBRAL_MOVIMIENTO; // < 0.20 g (descarta movimiento voluntario)
