#ifndef BME688_H
#define BME688_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <driver/i2c_master.h>

typedef struct {
    float sample_rate_hz;  
    bool enable_temp;      
    bool enable_hum;       
    bool enable_pres;      
} bme_config;

typedef struct {
  uint16_t par_t1;
  int16_t par_t2;
  int8_t par_t3;
  uint16_t par_p1;
  int16_t par_p2;
  int8_t par_p3;
  int16_t par_p4;
  int16_t par_p5;
  int8_t par_p6;
  int8_t par_p7;
  int16_t par_p8;
  int16_t par_p9;
  uint8_t par_p10;
  uint16_t par_h1;
  uint16_t par_h2;
  int8_t par_h3;
  int8_t par_h4;
  int8_t par_h5;
  uint8_t par_h6;
  int8_t par_h7;
} calibrations;

void bme_set_config(bme_config* cfg);

void bme_init();

void register_metrics(void);

#endif