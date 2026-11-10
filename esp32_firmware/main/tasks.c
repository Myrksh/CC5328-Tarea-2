#include "tasks.h"
#include "bme688.h"
#include "protocol.h"
#include "comandos.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/uart.h>
#include <esp_log.h>

static const char *TAG = "TASKS";

// --- Estructura de Estado Global ---
typedef struct {
    float sample_rate_dhz; // 5, 10, 20, 50 dHz
    uint8_t metric_mask; // primeros tres bits indican temp, humedad y press
} app_config_t;

static app_config_t g_config = {
    .sample_rate_dhz = 10,
    .metric_mask = 0b111,
};

static SemaphoreHandle_t config_mutex = NULL;

// GETTERS SETTERS
uint8_t tasks_get_metric_mask(void) {
    uint8_t mask;
    if (config_mutex != NULL) xSemaphoreTake(config_mutex, portMAX_DELAY);
    mask = g_config.metric_mask;
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
    return mask;
}

uint32_t tasks_get_freq_ms(void) {
    float freq = 10;
    if (config_mutex != NULL) xSemaphoreTake(config_mutex, portMAX_DELAY);
    freq = g_config.sample_rate_dhz;
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
    return (uint32_t)(100.0f / freq);
}

void tasks_set_freq(float freq_dhz) {
    if (config_mutex != NULL) xSemaphoreTake(config_mutex, portMAX_DELAY);
    g_config.sample_rate_dhz = freq_dhz;
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
}

void tasks_set_metrics(uint8_t mask) {
    if (config_mutex != NULL) xSemaphoreTake(config_mutex, portMAX_DELAY);
    g_config.metric_mask = mask;
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
}

// Tarea 1
/*
static void tarea_comandos(void *arg) {
    (void)arg;
    uint8_t chunk[32];
    char acumulado[96];
    size_t n = 0;

    while (1) {
        int leidos = uart_read_bytes(PROTOCOLO_UART_PORT, chunk, sizeof(chunk), pdMS_TO_TICKS(20));
        for (int i = 0; i < leidos; i++) {
            uint8_t b = chunk[i];
            if (b == '\n' || b == '\r') {
                if (n > 0) {
                    acumulado[n] = '\0';
                    procesar(acumulado); // Parsea y llama a tasks_set_*
                    n = 0;
                }
            } else if (n < sizeof(acumulado) - 1) {
                acumulado[n++] = (char)b;
            }
        }
    }
}
*/

// tarea 2: Adquisicion y Emision del BME688 
static void tarea_adquisicion(void *arg) {
    (void)arg;
    bme_measurement_t lecturas;
    TickType_t ultimo_ciclo = xTaskGetTickCount();

    while (1) {
        uint8_t mascara = tasks_get_metric_mask();
        uint32_t periodo_ms = tasks_get_freq_ms();

        // settear medicion en el BME688
        bme_set_metrics(mascara);

        // obtener datos y enviarlos
        bme_get_measurements(&lecturas, mascara);
        //protocolo_enviar_telemetria(&lecturas, mascara);
        printf("TEMP:\t%.2f degC\tPRESS:\t%.2f hPa\tHUM:\t%.2f %%RH\n", 
            lecturas.temperature, 
            lecturas.pressure, 
            lecturas.humidity);

        vTaskDelayUntil(&ultimo_ciclo, pdMS_TO_TICKS(periodo_ms));
    }
}

// --- Inicializador Global ---
void tasks_init(void) {
    config_mutex = xSemaphoreCreateMutex();

    // Lanza ambas tareas concurrentes en FreeRTOS
    //xTaskCreate(tarea_comandos, "t_comandos", 4096, NULL, 10, NULL);
    xTaskCreate(tarea_adquisicion, "t_adquisicion", 4096, NULL, 10, NULL);
}