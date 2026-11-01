/* sensors.h — las fórmulas que simulan los sensores.
 *   1) A·sin(2πft)                — armónica simple
 *   2) A·cos(2πf1t)·sin(2πf2t)    — modulada en amplitud (AM)
 *   3) A·[sin(2πft) + cos(4πft)]  — multicomponente
 *
 * Las frecuencias internas de síntesis son fijas (decisión de diseño):
 * f = 1 Hz para 1) y 3); f1 = 1 Hz y f2 = 10 Hz para 2).
 * La FRECUENCIA del protocolo configura la fs de MUESTREO por eje
 * (50..1000 Hz), no la frecuencia de la señal.
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

typedef enum {
    SENSOR_EJE_X = 0,
    SENSOR_EJE_Y,
    SENSOR_EJE_Z,
    SENSOR_EJES /* cantidad de ejes */
} sensor_eje_t;

/* Funciones de vibración del enunciado (valores = codificación en el
 * comando FUNCION,<eje>,<n>) */
typedef enum {
    SENSOR_FUNCION_ARMONICA = 1,      /* A·sin(2πft) */
    SENSOR_FUNCION_MODULADA = 2,      /* A·cos(2πf1t)·sin(2πf2t) */
    SENSOR_FUNCION_MULTICOMPONENTE = 3 /* A·[sin(2πft) + cos(4πft)] */
} sensor_funcion_t;

/* Límites de configuración (enunciado 2.1.1) */
#define SENSOR_AMPLITUD_MIN_G 4
#define SENSOR_AMPLITUD_MAX_G 16
#define SENSOR_FS_MIN_HZ 50
#define SENSOR_FS_MAX_HZ 1000

/* Frecuencias internas de síntesis en Hz, fijas */
#define SENSOR_F_HZ 1.0f
#define SENSOR_F1_HZ 1.0f
#define SENSOR_F2_HZ 10.0f

/* Configuración de un eje */
typedef struct {
    sensor_funcion_t funcion;
    uint8_t amplitud_g; /* g: 4, 8 o 16 */
    uint16_t fs_hz;     /* Hz: 50..1000 */
} sensor_config_t;

/* Default de configuración: función 1, 4 g, 100 Hz */
sensor_config_t sensor_config_default(void);

/* Aplican un cambio de config al eje bajo mutex; la task lo toma en
 * su siguiente tick. */
void sensors_fijar_funcion(sensor_eje_t eje, sensor_funcion_t funcion);
void sensors_fijar_amplitud(sensor_eje_t eje, uint8_t amplitud_g);
void sensors_fijar_fs(sensor_eje_t eje, uint16_t fs_hz);

/* Defaults en los 3 ejes y fase reiniciada (sin reiniciar la MCU). */
void sensors_reiniciar(void);

/* Crea las 3 tasks de muestreo (X, Y y Z); cada una emite por tick
 * ACELEROMETRO,<eje>,<valor>*CK vía protocolo (protocol.h). */
void sensors_init(void);

#endif /* SENSORS_H */
