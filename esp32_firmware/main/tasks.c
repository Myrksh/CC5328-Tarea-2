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

static app_config_t g_config = {
    .sample_rate_dhz = 10,
    .temperature = false,
    .humidity = true,
    .pressure = true,
};

static SemaphoreHandle_t config_mutex = NULL;

// GETTERS SETTERS
void tasks_get_config(app_config_t* config) {
    if (config_mutex != NULL) xSemaphoreTake(config_mutex, portMAX_DELAY);
    *config = g_config;
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
}

void tasks_set_freq(float freq_dhz) {
    if (config_mutex != NULL) xSemaphoreTake(config_mutex, portMAX_DELAY);
    g_config.sample_rate_dhz = freq_dhz;
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
}

void tasks_set_metrics(bool temp, bool hum, bool press) {
    if (config_mutex != NULL) xSemaphoreTake(config_mutex, portMAX_DELAY);
    g_config.temperature = temp;
    g_config.humidity = hum;
    g_config.pressure = press;
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
}

uint32_t dHz_to_ms(uint8_t dHz) {return (10000.0f / dHz);}

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
    app_config_t config;
    TickType_t ultimo_ciclo = xTaskGetTickCount();

    while (1) {
        tasks_get_config(&config);

        // settear medicion en el BME688
        bme_set_metrics(config.temperature, config.humidity, config.pressure);

        // obtener datos y enviarlos
        bme_get_measurements(&lecturas);
        //protocolo_enviar_telemetria(&lecturas, mascara);

        if (config.temperature) printf("TEMP:\t%.2f degC\t", lecturas.temperature);
        if (config.humidity)    printf("HUM:\t%.2f %%RH\t", lecturas.humidity);
        if (config.pressure)    printf("PRESS:\t%.2f hPa\t", lecturas.pressure);
        printf("\n");

        vTaskDelayUntil(&ultimo_ciclo, pdMS_TO_TICKS(dHz_to_ms(config.sample_rate_dhz)));
    }
}

// --- Inicializador Global ---
void tasks_init(void) {
    config_mutex = xSemaphoreCreateMutex();

    // Lanza ambas tareas concurrentes en FreeRTOS
    //xTaskCreate(tarea_comandos, "t_comandos", 4096, NULL, 10, NULL);
    xTaskCreate(tarea_adquisicion, "t_adquisicion", 4096, NULL, 10, NULL);
}