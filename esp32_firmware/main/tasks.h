#ifndef TASKS_H
#define TASKS_H

#include <stdint.h>
#include <stdbool.h>

// --- Estructura de Estado Global ---
typedef struct {
    float sample_rate_dhz; // 5, 10, 20, 50 dHz
    bool temperature;
    bool humidity;
    bool pressure;
} app_config_t;

void tasks_init(void);

// Getters con mutex
void tasks_get_config(app_config_t* config);
uint32_t dHz_to_ms(uint8_t dHz);
uint32_t tasks_get_freq_ms(void);

// SETTERS con mutex
void tasks_set_freq(float freq_dhz);
void tasks_set_metrics(bool temp, bool hum, bool press);


#endif 