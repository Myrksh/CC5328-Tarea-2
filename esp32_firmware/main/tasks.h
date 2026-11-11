#ifndef TASKS_H
#define TASKS_H

#include <stdint.h>
#include <stdbool.h>

// --- Estructura de Estado Global ---
typedef struct {
    float sample_rate_dhz; // 5, 10, 20, 50 dHz

    bool t_avg;
    uint8_t t_avg_w;

    bool t_peak;
    uint32_t t_peak_start_time;
    float t_peak_threshold;

    bool temperature;
    bool humidity;
    bool pressure;
} app_config_t;

typedef struct {
    double pressure;
    double humidity;
    double temperature;

    double temp_avg_c;

    bool has_t_peak;
    double temp_peak;
    uint32_t temp_peak_time;
    float temp_peak_threshold;
} measurement_t;

void tasks_init(void);

// Getters con mutex
void tasks_get_config(app_config_t* config);
uint32_t dHz_to_ms(uint8_t dHz);
uint32_t tasks_get_freq_ms(void);

// SETTERS con mutex
void tasks_set_mean(bool on, uint8_t time);
void tasks_set_peak(bool on, float threshold);
void tasks_set_freq(float freq_dhz);
void tasks_set_metrics(bool temp, bool hum, bool press);


#endif 