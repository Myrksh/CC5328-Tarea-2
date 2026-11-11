#include "tasks.h"
#include "bme688.h"
#include "protocol.h"
#include "comandos.h"


#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/uart.h>
#include <esp_log.h>

//static const char *TAG = "TASKS";

#define MAX_SAMPLES 300

static app_config_t g_config = {
    .sample_rate_dhz = 10,

    .t_avg = false,
    .t_avg_w = 10,

    .t_peak = false,
    .t_peak_start_time = 0,
    .t_peak_threashold = 28,

    .temperature = true,
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

void tasks_set_mean(bool on, uint8_t time) {
    if (config_mutex != NULL) xSemaphoreTake(config_mutex, portMAX_DELAY);
    g_config.t_avg = on;
    g_config.t_avg_w = time;
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
}

void tasks_set_peak(bool on, float threshold) {
    if (config_mutex != NULL) xSemaphoreTake(config_mutex, portMAX_DELAY);
    g_config.t_peak = on;
    g_config.t_peak_start_time = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
}

uint32_t dHz_to_ms(uint8_t dHz) {return (10000.0f / dHz);}

double subtarea_promedio(double last_temperature, double* temps, uint16_t* temp_i, uint16_t* sample_count, uint8_t w) {
    temps[temp_i] = last_temperature;
    if (sample_count < MAX_SAMPLES) *sample_count++; 
    temp_i = (temp_i + 1) % MAX_SAMPLES;

    uint16_t needed_samples = w * freq_hz / 10.0f;
    if (needed_samples == 0) needed_samples = 1;

    uint16_t count_to_avg = (sample_count < needed_samples) ? sample_count : needed_samples;

    double sum = 0.0;
    for (uint16_t i = 0; i < count_to_avg; i++) {
        int idx = (int)head - 1 - (int)i;
        if (idx < 0) idx += MAX_SAMPLES;
        sum += temps[idx];
    }

    return sum / count_to_avg;
}

void subtarea_picos() {
    
}

// Tarea 1
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


// tarea 2: Adquisicion y Emision del BME688 
static void tarea_adquisicion(void *arg) {
    (void)arg;
    measurement_t lecturas;
    app_config_t config;
    TickType_t ultimo_ciclo = xTaskGetTickCount();

    bool must_use_temp = false;

    // Promedio
    double temps[300];
    uint16_t W, sample_count = 0;
    bool avg_complete = false;
    double temp_avg_c = 0.0;

    while (1) {
        tasks_get_config(&config);
        must_use_temp = config.t_avg || config.t_peak;

        // settear medicion en el BME688
        bme_set_metrics(config.temperature || must_use_temp, config.humidity, config.pressure);
        // obtener datos
        bme_get_measurements(&lecturas);

        // Promedio
        if (config.t_avg) lecturas.temp_avg_c = subtarea_promedio(lecturas.temperature, temps, last_temp_i, samples);
        //if (config.t_peak) lecturas.temp_peak = subtarea_picos(lecturas.temperature);

        protocolo_enviar_telemetria(&lecturas, &config);

        vTaskDelayUntil(&ultimo_ciclo, pdMS_TO_TICKS(dHz_to_ms(config.sample_rate_dhz)));
    }
}

// --- Inicializador Global ---
void tasks_init(void) {
    config_mutex = xSemaphoreCreateMutex();

    // Lanza ambas tareas concurrentes en FreeRTOS
    xTaskCreate(tarea_comandos, "t_comandos", 4096, NULL, 10, NULL);
    xTaskCreate(tarea_adquisicion, "t_adquisicion", 4096, NULL, 10, NULL);
}