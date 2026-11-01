/* Recibe comandos por UART0 y los aplica.
 * Lectura en bloques con timeout corto, patrón de
 * embeb-uart-usb/echo/main/main.c (repo del auxiliar):
 * acumulación hasta '\n', checksum obligatorio — línea corrupta
 * o desconocida se descarta en silencio.
 */
#include "comandos.h"
#include "protocol.h"
#include "sensors.h"
#include "ambiente.h"

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

/* Detalle "<eje>,<valor>" terminado en '\0' */
static void detalle_eje(char *d, size_t *len, char eje, uint32_t valor) {
    d[(*len)++] = eje;
    d[(*len)++] = ',';
    protocolo_agregar_entero(d, len, (int32_t)valor);
    d[*len] = '\0';
}

/* Detalle "<valor>" terminado en '\0' */
static void detalle_num(char *d, size_t *len, uint32_t valor) {
    protocolo_agregar_entero(d, len, (int32_t)valor);
    d[*len] = '\0';
}

/* Lee "<eje>,<valor>" a partir del prefijo ya matcheado */
static bool leer_eje_valor(const char *cmd, size_t prefijo, char *eje,
                           uint32_t *valor) {
    const char *p = cmd + prefijo;
    if ((p[0] != 'X' && p[0] != 'Y' && p[0] != 'Z') || p[1] != ',') {
        return false;
    }
    *eje = p[0];
    return parsear_uint(p + 2, valor);
}

/* Cuerpo ya con checksum validado */
static void aplicar(const char *cmd) {
    char eje;
    uint32_t valor;
    char d[16];
    size_t dl = 0;

    if (strncmp(cmd, "INICIAR", 7) == 0 && cmd[7] == '\0') {
        sensors_reiniciar();
        ambiente_fijar_periodo(30);
        protocolo_fijar_baudios(PROTOCOLO_BAUDIOS_DEFAULT); /* y el baudio vuelve al de arranque */
        responder("OK,INICIAR", "tarea1-v1.0");
        return;
    }
    if (strncmp(cmd, "FUNCION,", 8) == 0) {
        if (!leer_eje_valor(cmd, 8, &eje, &valor)) {
            responder("ERROR,FUNCION", "formato invalido");
        } else if (valor < 1 || valor > 3) {
            responder("ERROR,FUNCION", "modo invalido");
        } else {
            sensors_fijar_funcion((sensor_eje_t)(eje - 'X'),
                                  (sensor_funcion_t)valor);
            detalle_eje(d, &dl, eje, valor);
            responder("OK,FUNCION", d);
        }
        return;
    }
    if (strncmp(cmd, "AMPLITUD,", 9) == 0) {
        if (!leer_eje_valor(cmd, 9, &eje, &valor)) {
            responder("ERROR,AMPLITUD", "formato invalido");
        } else if (valor != 4 && valor != 8 && valor != 16) {
            responder("ERROR,AMPLITUD", "amplitud invalida");
        } else {
            sensors_fijar_amplitud((sensor_eje_t)(eje - 'X'), (uint8_t)valor);
            detalle_eje(d, &dl, eje, valor);
            responder("OK,AMPLITUD", d);
        }
        return;
    }
    if (strncmp(cmd, "FRECUENCIA,", 11) == 0) {
        if (!leer_eje_valor(cmd, 11, &eje, &valor)) {
            responder("ERROR,FRECUENCIA", "formato invalido");
        } else if (valor != 50 && valor != 100 && valor != 200
                   && valor != 500 && valor != 1000) {
            responder("ERROR,FRECUENCIA", "fs invalida");
        } else {
            sensors_fijar_fs((sensor_eje_t)(eje - 'X'), (uint16_t)valor);
            detalle_eje(d, &dl, eje, valor);
            responder("OK,FRECUENCIA", d);
        }
        return;
    }
    if (strncmp(cmd, "PERIODO,", 8) == 0) {
        if (!parsear_uint(cmd + 8, &valor) || (valor != 30 && valor != 60)) {
            responder("ERROR,PERIODO", "periodo invalido");
        } else {
            ambiente_fijar_periodo(valor);
            detalle_num(d, &dl, valor);
            responder("OK,PERIODO", d);
        }
        return;
    }
    if (strncmp(cmd, "BAUDIOS,", 8) == 0) {
        if (!parsear_uint(cmd + 8, &valor)
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

/* Verifica el checksum; falla → descarte silencioso */
static void procesar(char *cuerpo) {
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

static void tarea_comandos(void *arg) {
    (void)arg;
    uint8_t chunk[32];
    char acumulado[COMANDO_MAX];
    size_t n = 0;
    bool desbordado = false;
    while (1) {
        int leidos = uart_read_bytes(UART_PORT_NUM, chunk, sizeof(chunk),
                                     pdMS_TO_TICKS(20));
        for (int i = 0; i < leidos; i++) {
            uint8_t b = chunk[i];
            if (b == '\n' || b == '\r') {
                desbordado = false;
                if (n > 0) {
                    acumulado[n] = '\0';
                    procesar(acumulado);
                    n = 0;
                }
            } else if (!desbordado) {
                acumulado[n++] = (char)b;
                if (n >= COMANDO_MAX) {
                    desbordado = true; /* línea excesiva: descarte silencioso */
                }
            }
        }
        if (leidos <= 0) {
            if (n > 0 && !desbordado) {
                /* silencio también cierra la línea: tolera paste
                 * sin '\n' y Enter que llega tarde */
                acumulado[n] = '\0';
                procesar(acumulado);
            }
            n = 0;
            desbordado = false;
        }
    }
}

void comandos_init(void) {
    xTaskCreate(tarea_comandos, "comandos", 4096, NULL, 10, NULL);
}
