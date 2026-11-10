#ifndef TASKS_H
#define TASKS_H

#include <stdint.h>
#include <stdbool.h>


void tasks_init(void);

// Getters con mutex
uint8_t tasks_get_metric_mask(void);
uint32_t tasks_get_freq_ms(void);

// SETTERS con mutex
void tasks_set_freq(float freq_dhz);
void tasks_set_metrics(uint8_t mask);


#endif 