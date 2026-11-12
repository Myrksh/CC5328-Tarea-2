#include "tasks.h"
#include "bme688.h"
#include "protocol.h"
#include "comandos.h"

#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/uart.h>
#include <esp_log.h>

//static const char *TAG = "TASKS";

#define MAX_SAMPLES 300

static app_config_t g_config = {
    .sample_rate_dhz = 10,

    .sample_count = 0,
    .t_avg = false,
    .t_avg_w = 10,

    .start_time = 0,
    .t_peak = false,
    .t_peak_threshold = 28,

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
    g_config.sample_count = 0;
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
    g_config.t_peak_threshold = threshold;
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
}

void tasks_set_start_time(void) {
    if (config_mutex != NULL) xSemaphoreTake(config_mutex, portMAX_DELAY);
    g_config.start_time = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
}

void tasks_increment_sample_count(void) {
    if (config_mutex != NULL) xSemaphoreTake(config_mutex, portMAX_DELAY);
    g_config.sample_count++;
    if (config_mutex != NULL) xSemaphoreGive(config_mutex);
}

uint32_t dHz_to_ms(uint8_t dHz) {return (10000.0f / dHz);}

static void subtarea_promedio(const app_config_t *cfg, 
                              measurement_t *lecturas, 
                              const double *temps, 
                              uint16_t temps_index, 
                              uint16_t sample_count) {

    uint8_t w = cfg->t_avg_w;
    float freq_hz = cfg->sample_rate_dhz / 10.0f;

    uint16_t needed_samples = w * freq_hz;
    if (needed_samples == 0) needed_samples = 1;
    if (needed_samples <= sample_count) lecturas->avg_complete = true;
    else lecturas->avg_complete = false;

    uint16_t count_to_avg = (sample_count < needed_samples) ? sample_count : needed_samples;

    double sum = 0.0;
    for (uint16_t i = 0; i < count_to_avg; i++) {
        int idx = (int)temps_index - 1 - (int)i;
        if (idx < 0) idx += MAX_SAMPLES;
        sum += temps[idx];
    }

    lecturas->temp_avg_c = sum / count_to_avg;
}

static void subtarea_picos(const app_config_t *cfg, 
                           measurement_t *lecturas, 
                           const double *temps, 
                           uint16_t temps_index, 
                           uint16_t sample_count) {
        // Se requieren al menos 3 muestras
        if (sample_count < 3) return;
        float peak_delta = 100.0; // Para 2 decimales (centésimas)

        float peak_threshold = cfg->t_peak_threshold;

        int i_k_2 = ((int)temps_index - 3 + MAX_SAMPLES) % MAX_SAMPLES;
        int i_k_1 = ((int)temps_index - 2 + MAX_SAMPLES) % MAX_SAMPLES;
        int i_cur = ((int)temps_index - 1 + MAX_SAMPLES) % MAX_SAMPLES;

        double tempk_2 = temps[i_k_2];
        double tempk_1 = temps[i_k_1];
        double current_temp = temps[i_cur];

        current_temp = round(current_temp * peak_delta) / peak_delta;
        tempk_2 = round(tempk_2 * peak_delta) / peak_delta;
        tempk_1 = round(tempk_1 * peak_delta) / peak_delta;

        // condiciones
        if (tempk_1 > tempk_2 && tempk_1 > current_temp && tempk_1 > peak_threshold) {
            lecturas->has_t_peak = true;
            lecturas->temp_peak = tempk_1;
            lecturas->temp_peak_time = lecturas->prev_temp_time - cfg->start_time;
            lecturas->temp_peak_threshold = peak_threshold;
        }

        lecturas->prev_temp_time = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
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

    // Buffer circular mantenido localmente en la tarea
    static double temps[MAX_SAMPLES];
    static uint16_t temps_index = 0;

    bool must_use_temp = false;
    
    while (1) {
        tasks_get_config(&config);
        must_use_temp = config.t_avg || config.t_peak || config.temperature;

        // settear medicion en el BME688
        bme_set_metrics(must_use_temp, config.humidity, config.pressure);
        // obtener datos
        bme_get_measurements(&lecturas);
        lecturas.has_t_peak = false;

        // Promedio
        if (must_use_temp) {
            temps[temps_index] = lecturas.temperature;
            temps_index = (temps_index + 1) % MAX_SAMPLES;
            if (config.sample_count < MAX_SAMPLES) tasks_increment_sample_count(); // No importa tanto el get desincronizado.
        }
        if (config.t_avg) subtarea_promedio(&config, &lecturas, temps, temps_index, config.sample_count);
        if (config.t_peak) subtarea_picos(&config, &lecturas, temps, temps_index, config.sample_count);

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