/* Sintetiza las señales de vibración por eje y las emite como
 * ACELEROMETRO,<eje>,<valor>*CK.
 * Fórmulas (Tarea1_CC5328_v2.md 2.1.1):
 *   1) A·sin(2πft)
 *   2) A·cos(2πf1t)·sin(2πf2t)
 *   3) (2A/2)·[sin(2πft) + cos(4πft)] = A·[sin(2πft) + cos(4πft)]
 */
#include "sensors.h"
#include "protocol.h"

#include <math.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#define PI 3.14159265358979f

/* Etiqueta de dato en el contrato */
#define ETIQUETA "ACELEROMETRO,"
#define ETIQUETA_LEN (sizeof(ETIQUETA) - 1)

/* Configuración por eje y fase de cada task */
static sensor_config_t config[SENSOR_EJES];
static uint32_t muestra[SENSOR_EJES];

/* Protege config + muestra; las secciones críticas son cortas */
static SemaphoreHandle_t config_mutex;

/* Default: función 1, 4 g, 100 Hz */
sensor_config_t sensor_config_default(void) {
    return (sensor_config_t){
        .funcion = SENSOR_FUNCION_ARMONICA,
        .amplitud_g = 4,
        .fs_hz = 100,
    };
}

/* Evalúa la función del eje en la muestra n; t = n/fs */
static float valor_funcion(const sensor_config_t *c, uint32_t n) {
    float t = (float)n / (float)c->fs_hz;
    if (c->funcion == SENSOR_FUNCION_MODULADA) {
        return (float)c->amplitud_g
               * cosf(2.0f * PI * SENSOR_F1_HZ * t)
               * sinf(2.0f * PI * SENSOR_F2_HZ * t);
    }
    if (c->funcion == SENSOR_FUNCION_MULTICOMPONENTE) {
        /* cos(4πft) = cos(2π·2f·t) */
        return (float)c->amplitud_g
               * (sinf(2.0f * PI * SENSOR_F_HZ * t)
                  + cosf(2.0f * PI * 2.0f * SENSOR_F_HZ * t));
    }
    /* función 1 (armónica simple) es el default */
    return (float)c->amplitud_g * sinf(2.0f * PI * SENSOR_F_HZ * t);
}

/* arg = eje; task creation y stack de embeb-uart-usb/echo/main/main.c */
static void tarea_sensor(void *arg) {
    sensor_eje_t eje = (sensor_eje_t)(size_t)arg;
    static const char EJES[] = {'X', 'Y', 'Z'};
    TickType_t ultimo = xTaskGetTickCount();
    char buf[24];
    while (1) {
        /* snapshot corto: config y fase juntas bajo mutex */
        xSemaphoreTake(config_mutex, portMAX_DELAY);
        sensor_config_t c = config[eje];
        uint32_t n = muestra[eje]++;
        xSemaphoreGive(config_mutex);

        float valor = valor_funcion(&c, n);
        size_t len = 0;
        memcpy(buf + len, ETIQUETA, ETIQUETA_LEN);
        len += ETIQUETA_LEN;
        buf[len++] = EJES[eje];
        buf[len++] = ',';
        protocolo_agregar_milig(buf, &len, lroundf(valor * 1000.0f));
        protocolo_enviar_frame(buf, len);
        vTaskDelayUntil(&ultimo, pdMS_TO_TICKS(1000 / c.fs_hz));
    }
}

/* Default de configuración y 3 tasks (X, Y y Z) */
void sensors_init(void) {
    config_mutex = xSemaphoreCreateMutex();
    for (size_t i = 0; i < SENSOR_EJES; i++) {
        config[i] = sensor_config_default();
    }
    for (size_t i = 0; i < SENSOR_EJES; i++) {
        xTaskCreate(tarea_sensor, "sensor", 4096, (void *)(size_t)i, 10, NULL);
    }
}

void sensors_fijar_funcion(sensor_eje_t eje, sensor_funcion_t funcion) {
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    config[eje].funcion = funcion;
    xSemaphoreGive(config_mutex);
}

void sensors_fijar_amplitud(sensor_eje_t eje, uint8_t amplitud_g) {
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    config[eje].amplitud_g = amplitud_g;
    xSemaphoreGive(config_mutex);
}

void sensors_fijar_fs(sensor_eje_t eje, uint16_t fs_hz) {
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    config[eje].fs_hz = fs_hz;
    xSemaphoreGive(config_mutex);
}

void sensors_reiniciar(void) {
    xSemaphoreTake(config_mutex, portMAX_DELAY);
    for (size_t i = 0; i < SENSOR_EJES; i++) {
        config[i] = sensor_config_default();
        muestra[i] = 0;
    }
    xSemaphoreGive(config_mutex);
}
