/* Recibe comandos por UART0 y los aplica.
 * Lectura en bloques con timeout corto, patrón de
 * embeb-uart-usb/echo/main/main.c (repo del auxiliar):
 * acumulación hasta '\n', checksum obligatorio — línea corrupta
 * o desconocida se descarta en silencio.
 */
#include "comandos.h"
#include "protocol.h"
#include "bme688.h"
#include "tasks.h"

#include <stdint.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>

#define UART_PORT_NUM PROTOCOLO_UART_PORT

/* Línea larga de más: descarte silencioso */
#define COMANDO_MAX 96

static bool parsear_uint(const char *s, uint32_t *out) {
    if (*s == '\0') {
        return false;
    }
    uint32_t v = 0;
    while (*s != '\0') {
        if (*s < '0' || *s > '9') {
            return false;
        }
        v = v * 10 + (uint32_t)(*s - '0');
        if (v > 999999) { /* 6 dígitos alcanza para BAUDIOS 921600 */
            return false;
        }
        s++;
    }
    *out = v;
    return true;
}

/* Detalle "<valor>" terminado en '\0' */
static void detalle_num(char *d, size_t *len, uint32_t valor) {
    protocolo_agregar_entero(d, len, (int32_t)valor);
    d[*len] = '\0';
}

/* 0..15, o -1 si no es dígito hexa (acepta minúsculas) */
static int hexval(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/* Responde con el cuerpo armado <cmd>,<detalle>; el checksum y el
 * '\n' los agrega protocolo_enviar_linea. */
static void responder(const char *cmd, const char *detalle) {
    char resp[48];
    size_t len = 0;
    size_t nc = strlen(cmd);
    memcpy(resp + len, cmd, nc);
    len += nc;
    resp[len++] = ',';
    size_t nd = strlen(detalle);
    memcpy(resp + len, detalle, nd);
    len += nd;
    resp[len] = '\0';
    protocolo_enviar_linea(resp);
}


/* Cuerpo ya con checksum validado */
static void aplicar(const char *cmd) {
    uint32_t valor;
    char d[16];
    size_t dl = 0;
    uint8_t aux = 0;

    if (strncmp(cmd, "INICIAR", 7) == 0 && cmd[7] == '\0') {
        tasks_set_metrics(true, true, true);
        tasks_set_freq(10);
        tasks_set_mean(false, 10);
        tasks_set_peak(false, 28.0f);
        protocolo_fijar_baudios(PROTOCOLO_BAUDIOS_DEFAULT); /* y el baudio vuelve al de arranque */
        tasks_set_start_time(); // Ahora se le agrega el timestamp el momento que inicio la conexion !!!
        responder("OK,INICIAR", "tarea2-v1.0");
        return;
    }
    /*  Comando para setear frecuencia de muestreo  */
    //  FRECUENCIA EN DECIHERCIOS, PARA EVITAR EL FLOAT
    if (strncmp(cmd, "FRECUENCIA,", 11) == 0) {
        if (!parsear_uint(cmd + 11, &valor)) {
            responder("ERROR,FRECUENCIA", "formato invalido");
        } else if (valor != 5 && valor != 10 && valor != 20 && valor != 50) {
            responder("ERROR,FRECUENCIA", "frecuencia invalida");
        } else {
            tasks_set_freq(valor);
            responder("OK,FRECUENCIA", d);
        }
        return;
    }
    if (strncmp(cmd, "CONFIG,", 7) == 0){
        aux = 7;
        
        /*  Comando para setear las metricas a medir. Para evitar hacer una por cada metrica,
            junte todo en un numero "mascara". Por ejemplo, 5 -> 0b101 activa temperatura y presion */
        if (strncmp(cmd + aux, "METRICAS,", 9) == 0) {
            if (!parsear_uint(cmd + aux + 9, &valor)) {
                responder("ERROR,METRICAS", "formato invalido");
            } else if (valor < 1 || valor > 7) {
                responder("ERROR,METRICAS", "modo invalido");
            } else {
                bool enable_press = valor & 0b1;
                bool enable_hum   = valor >> 1 & 0b1;
                bool enable_temp  = valor >> 2 & 0b1;
    
                tasks_set_metrics(enable_temp, enable_hum, enable_press);
                responder("OK,METRICAS", d);
            }
            return;
        }
    
        /*  Comando para activar/desactivar el promedio. Recibe dos parametros:
            act -> si se debe activar
             w  -> la ventana en segundos a utilizar.  */
        if (strncmp(cmd + aux, "PROMEDIO,", 9) == 0) {
            int act, w;
            if (sscanf(cmd + aux + 9, "%d,%d", &act, &w) == 2) {
                if ((act == 0 || act == 1) && (w == 5 || w == 10 || w == 30 || w == 60)) {
                    tasks_set_mean(act, w);
                    responder("OK,PROMEDIO", cmd + aux + 9);
                    return;
                }
            }
            responder("ERROR,PROMEDIO", "parametros invalidos");
            return;
        }
    
        /*  Comando para activar/desactivar la funcion picos. Recibe dos parametros:
            act    -> si se debe activar
            umbral -> cual es el umbral de temperatura */
        if (strncmp(cmd + aux, "PICOS,", 6) == 0) {
            int act;
            float umbral;
            if (sscanf(cmd + aux + 6, "%d,%f", &act, &umbral) == 2) {
                if ((act == 0 || act == 1) && (-100.0f <= umbral && umbral <= 100.0)) {
                    tasks_set_peak(act, umbral);
                    responder("OK,PICOS", cmd + aux + 6);
                    return;
                }
            }
            responder("ERROR,PICOS", "parametros invalidos");
            return;
        }
        /*  Comando para cambiar baudios. De la tarea 1, no se cambio nada  */
        if (strncmp(cmd + aux, "BAUDIOS,", 8) == 0) {
            if (!parsear_uint(cmd + aux + 8, &valor)
                || (valor != 115200 && valor != 230400 && valor != 460800
                    && valor != 921600)) {
                responder("ERROR,BAUDIOS", "valor invalido");
                return;
            }
            /* el OK sale al baudio viejo; el cambio aplica 50 ms después,
             * cuando el receptor ya terminó de leer la respuesta */
            detalle_num(d, &dl, valor);
            responder("OK,BAUDIOS", d);
            vTaskDelay(pdMS_TO_TICKS(50));
            protocolo_fijar_baudios(valor);
            return;
        }
        responder("ERROR,desconocido", "sin soporte");
    }
}

/* Verifica el checksum; falla → descarte silencioso */
void procesar(char *cuerpo) {
    char *star = strchr(cuerpo, '*');
    if (star == NULL || star[3] != '\0') {
        return; /* sin checksum o basura al final */
    }
    int c1 = hexval(star[1]);
    int c2 = hexval(star[2]);
    if (c1 < 0 || c2 < 0) {
        return;
    }
    *star = '\0';
    if (protocolo_checksum(cuerpo) != (uint8_t)(c1 * 16 + c2)) {
        return;
    }
    aplicar(cuerpo);
}

