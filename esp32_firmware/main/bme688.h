#ifndef BME688_H
#define BME688_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <driver/i2c_master.h>

void register_metrics(void);

#endif