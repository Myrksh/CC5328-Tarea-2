#ifndef TASKS_H
#define TASKS_H

#include <stdint.h>
#include <stdbool.h>

/* Estructura que guarda la configuracion del esp32 global */
typedef struct {
    uint32_t start_time;        // Tiempo en el que se inicia la conexion con la GUI
    uint8_t sample_rate_dhz;    // Frecuencia de muestreo en decihercios 5, 10, 20, 50 dHz
    
    uint16_t sample_count;      // Cantidad de muestras de temperatura en la lista. Es global ya que al cambiar hz, debe reiniciarse

    bool t_avg;                 // Indica si esta activa la medicion de promedio
    uint8_t t_avg_w;            // Ventana w en segundos del promedio
    
    bool t_peak;                // Indica si esta activa la medicion de picos
    float t_peak_threshold;     // El umbral de temperatura a superar
    
    bool temperature;           // Indica si esta activa la medicion de temperatura actual
    bool humidity;              // Indica si esta activa la medicion de humedad actual
    bool pressure;              // Indica si esta activa la medicion de presion actual
} app_config_t;

/* Estructura que guarda los datos a enviar a la gui. Contiene las lecturas al instante del sensor */
typedef struct {
    uint32_t seq;               // Numero de secuencia

    double temperature;         // Lectura instantanea de temperatura
    double humidity;            // Lectura instantanea de humedad
    double pressure;            // Lectura instantanea de presion

    double temp_avg_c;          // Lectura de promedio de temperatura
    bool avg_complete;          // Indica si el promedio esta completo (incompleto si no hay suf. mediciones)

    bool has_t_peak;            // Indica si hay un pico de temperatura
    double temp_peak;           // Temperatura del pico
    uint32_t temp_peak_time;    // Tiempo en el que se registro
    float temp_peak_threshold;  // Umbral de temperatura que el pico supero

    uint32_t prev_temp_time;    // Tiempo de la ultima medicion de temperatura
} measurement_t;

// Getters con mutex
void tasks_get_config(app_config_t* config);
uint32_t dHz_to_ms(uint8_t dHz);

// SETTERS con mutex
void tasks_set_mean(bool on, uint8_t time);
void tasks_set_peak(bool on, float threshold);
void tasks_set_freq(float freq_dhz);
void tasks_set_metrics(bool temp, bool hum, bool press);
void tasks_set_start_time(void);

// Incializador
void tasks_init(void);

#endif 