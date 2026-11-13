#ifndef BME688_H
#define BME688_H

#include "tasks.h"
#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <driver/i2c_master.h>

/*  Funcion de inicializacion del bme688. Setea los puertos y el canal de la comunicacion i2c  
    También desactiva la medicion de gas por defecto*/
void bme_init(void);

/*  Funcion que setea las metricas a leer. 
    @param temp   temperatura
    @param hum    humedad
    @param press  presion
    Renueva los valores internos de medicion y transmite por i2c  */
void bme_set_metrics(bool temp, bool hum, bool press);

/*  Funcion principal para obtener lecturas de las metricas.
    @param measurements   Estructura measurement_t en donde escribir las mediciones
    En primera instancia calibra. Luego, intenta obtener mediciones una cantidad de tries (20)  
    Finalmente, dependiendo si esta activa la metrica, la procesa, la calibra y retorna  
    Las operaciones que hace son del manual y del codigo del aux...  */
void bme_get_measurements(measurement_t* measurements);

#endif