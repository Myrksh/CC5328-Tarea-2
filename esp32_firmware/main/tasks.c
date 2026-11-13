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
#include <esp_wifi.h>
#include <esp_err.h>

/*  Tareas principales que se ejecutan en el esp32  */

#define MAX_SAMPLES 300 // Se explica mas adelante

/*  Configuracion inicial/global que tendra el esp32. 
    Ya que habran dos threads escribiendo en simultaneo,
    se debe ocupar mutex para su cambio.  
    Documentación del type en tasks.h  */
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

static SemaphoreHandle_t config_mutex = NULL; // Mutex de g_config

/*  Getters y Setters de la config global. Los nombres de las func. lo dicen todo  */
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

/*  Funcion auxiliar, ya que se transmite y guardan los Hz en deciHercios*/
uint32_t dHz_to_ms(uint8_t dHz) {return (10000.0f / dHz);}


/*  Subtarea para calcular el promedio de temperaturas.
    @param *cfg         configuracion global
    @param *lecturas    estructura en donde escribir el promedio
    @param *temps       lista de temperaturas
    @param last_temp_i  indice de la última temperatura en la lista
    
    Calcula el promedio de todas las mediciones de temperatura a los ultimos w segundos
    De haber menos, promedia los registros que haya.  */
static void subtarea_promedio(const app_config_t *cfg, 
                              measurement_t *lecturas, 
                              const double *temps, 
                              uint16_t last_temp_i) {

    uint16_t sample_count = cfg->sample_count;      // Cantidad de lecturas que temps tiene. 
    uint8_t w = cfg->t_avg_w;                       // Ventana w en segundos
    float freq_hz = cfg->sample_rate_dhz / 10.0f;   // Los hz de muestreo. Importante!

    // Si hay 5 hz, se deben promediar 5 muestras para cada segundo, de aho la formula
    uint16_t needed_samples = w * freq_hz;
    if (needed_samples == 0) needed_samples = 1; // porsiacaso

    uint16_t count_to_avg; // Muestras a promediar
    // De haber suficientes muestras, se avisa que el promedio esta completio
    if (needed_samples <= sample_count) {
        lecturas->avg_complete = true;
        count_to_avg = needed_samples;
    } else {
        lecturas->avg_complete = false;
        count_to_avg = sample_count;
    }

    double sum = 0.0;
    // for de derecha a izq. De topar, sigue por la derecha.
    for (uint16_t i = 0; i < count_to_avg; i++) {
        int idx = (int)last_temp_i - (int)i;
        if (idx < 0) idx += MAX_SAMPLES;
        sum += temps[idx];
    }

    lecturas->temp_avg_c = sum / count_to_avg; // Resultado
}

/*  Subtarea para calcular picos de temperatura.
    @param *cfg         configuracion global
    @param *lecturas    estructura en donde escribir los picos
    @param *temps       lista de temperaturas
    @param last_temp_i  indice de la última temperatura en la lista
    
    Calcula si hay un pico de temperatura respecto a tres condiciones:
    Un valor supere un umbral, valor anterior y valor siguiente.
    También, la variacion debe ser de mas de dos decimales, o se descarta
    De haber menos de tres lecturas, retorna, evidentemente  */
static void subtarea_picos(const app_config_t *cfg, 
                           measurement_t *lecturas, 
                           const double *temps, 
                           uint16_t last_temp_i) {

        uint16_t sample_count = cfg->sample_count;      // Cantidad de lecturas que temps tiene. 
        float peak_threshold = cfg->t_peak_threshold;   // Umbral a vencer

        if (sample_count < 3) return;   // Se requieren al menos 3 muestras
        float peak_delta = 100.0;       // Variacion de 2 decimales (centesimas)

        // Indices, evitando salirse de los bordes
        int i_k_2 = ((int)last_temp_i - 2 + MAX_SAMPLES) % MAX_SAMPLES;
        int i_k_1 = ((int)last_temp_i - 1 + MAX_SAMPLES) % MAX_SAMPLES;
        int i_cur = ((int)last_temp_i + MAX_SAMPLES) % MAX_SAMPLES;

        // Valores de temperatura
        double tempk_2 = temps[i_k_2];
        double tempk_1 = temps[i_k_1];
        double current_temp = temps[i_cur];

        // Redondea a la centesima.
        current_temp = round(current_temp * peak_delta) / peak_delta;
        tempk_2 = round(tempk_2 * peak_delta) / peak_delta;
        tempk_1 = round(tempk_1 * peak_delta) / peak_delta;

        // condiciones
        if (tempk_1 > tempk_2 && tempk_1 > current_temp && tempk_1 > peak_threshold) {
            lecturas->has_t_peak = true;
            lecturas->temp_peak = tempk_1;
            lecturas->temp_peak_time = lecturas->prev_temp_time - cfg->start_time;
            lecturas->temp_peak_threshold = peak_threshold;
        } else lecturas->has_t_peak = false;

        lecturas->prev_temp_time = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount()); // Se guarda el tiempo de la ultima temp
}

/*  Tarea principal - recibir comandos
    Ejecutada en un thread aparte, se encarga de recibir comandos desde la GUI
    Viene desde la tarea 1, por lo que no se detalla tanto su uso.  */
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

/*  Tarea principal - adquirir mediciones 
    Ejecutada en un thread aparte, se encarga de recibir las mediciones del bme688, procesarlas y enviarlas
    Además, de estar activada la temperatura, registra las mediciones en una lista.*/
static void tarea_adquisicion(void *arg) {
    (void)arg;
    // Inicializacion de estructuras
    app_config_t config;
    measurement_t lecturas; lecturas.seq = 0;
    TickType_t ultimo_ciclo = xTaskGetTickCount();

    // Lista de temperaturas. Al llenarse, reemplaza las últimas
    static double temps[MAX_SAMPLES];   // Pensando en W<=60 y hz<=5, lo máximo a almacenar es 300
    static uint16_t temps_index = 0;    // Indice a llenar

    bool must_use_temp = false;
    
    while (1) {
        tasks_get_config(&config);  // Se obtiene la configuracion global (con mutex)

        // Variable interna que indica si se debe medir temperatura. Tanto le medicion de picos y promedios
        // la ocupan, aunque de no estar activo config.temperature, no se enviara la medicion actual.
        // Tambien medir presion ocupa temperatura, pero eso esta en la config de bme688.c
        must_use_temp = config.t_avg || config.t_peak || config.temperature;

        bme_set_metrics(must_use_temp, config.humidity, config.pressure);   // Setea los valores a medir en el bme
        bme_get_measurements(&lecturas);                                    // Y los lee hacia lecturas

        // De leer temperatura, aprovecha y guarda los datos en la lista. (aun si peak y promedios no estan activados)
        if (must_use_temp) {
            temps[temps_index] = lecturas.temperature;
            temps_index = (temps_index + 1) % MAX_SAMPLES;
            if (config.sample_count < MAX_SAMPLES) tasks_increment_sample_count(); // No importa tanto el get desincronizado.
        }

        // Ahora, de estar activos el promedio y los picos, se llama a su subfuncion correspondiente
        if (config.t_avg) subtarea_promedio(&config, &lecturas, temps, temps_index-1);
        if (config.t_peak) subtarea_picos(&config, &lecturas, temps, temps_index-1);

        protocolo_enviar_telemetria(&lecturas, &config); // Finalmente, se envia la info

        lecturas.seq++;
        vTaskDelayUntil(&ultimo_ciclo, pdMS_TO_TICKS(dHz_to_ms(config.sample_rate_dhz))); // Se espera delay de los Hz
    }
}

// --- Inicializador Global ---
void tasks_init(void) {
    if (esp_wifi_stop() == ESP_OK) esp_wifi_deinit()

    config_mutex = xSemaphoreCreateMutex();

    // Lanza ambas tareas concurrentes en FreeRTOS
    xTaskCreate(tarea_comandos, "t_comandos", 4096, NULL, 10, NULL);
    xTaskCreate(tarea_adquisicion, "t_adquisicion", 4096, NULL, 10, NULL);
}