/* init UART: patrón literal de embeb-uart-usb/echo/main/main.c, el repo del auxiliar.
 * Armado de línea: patrón cursor (memcpy + len) de
 * embeb-uart-usb/multiplex/main/main.c
 */
#include "protocol.h"
#include "tasks.h"

#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/uart.h>

#define UART_PORT_NUM PROTOCOLO_UART_PORT

/* 4096: ráfagas largas mientras se cambia de baudio */
#define RX_BUF_SIZE 4096

/* Línea completa = cuerpo + *CK + \n */
#define LINEA_MAX 128

static SemaphoreHandle_t tx_mutex;

/* Dígitos hexa mayúsculas para armar el checksum */
static const char HEXA[] = "0123456789ABCDEF";

/* Escritura cruda bajo mutex; única vía de salida por UART0 */
static void enviar_raw(const char *datos, size_t len) {
    if (tx_mutex == NULL) { /* defensa: init no llamado aún */
        uart_write_bytes(UART_PORT_NUM, datos, len);
        return;
    }
    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return; /* canal ocupado: descarte silencioso */
    }
    uart_write_bytes(UART_PORT_NUM, datos, len);
    xSemaphoreGive(tx_mutex);
}

uint8_t protocolo_checksum(const char *cuerpo) {
    uint8_t ck = 0;
    while (*cuerpo) {
        ck ^= (uint8_t)*cuerpo++;
    }
    return ck;
}

void protocolo_enviar_frame(const char *datos, size_t len) {
    if (datos == NULL || len == 0 || len > LINEA_MAX - 4) {
        return; /* cuerpo ausente o demasiado largo */
    }

    /* XOR de los bytes del cuerpo */
    uint8_t ck = 0;
    for (size_t i = 0; i < len; i++) {
        ck ^= (uint8_t)datos[i];
    }

    /* Cursor: cuerpo, '*', 2 dígitos hexa, '\n' */
    char linea[LINEA_MAX];
    size_t n = 0;
    memcpy(linea + n, datos, len);
    n += len;
    linea[n++] = '*';
    linea[n++] = HEXA[(ck >> 4) & 0xF];
    linea[n++] = HEXA[ck & 0xF];
    linea[n++] = '\n';

    enviar_raw(linea, n);
}

void protocolo_enviar_linea(const char *cuerpo) {
    if (cuerpo == NULL) {
        return;
    }
    protocolo_enviar_frame(cuerpo, strlen(cuerpo));
}



/* Agrega el numero val al cursos con tantos decimales se requiera 
 * parte entera por dígitos en reversa, decimales variables. 
 * reemplaza a protocolo_agrega_milig*/
void protocolo_agregar_float(char *buf, size_t *len, double val, uint8_t decimales) {
    uint32_t factor = 1;
    for (int i = 0; i<decimales; i++) factor*= 10;
    
    int32_t escalado = (int32_t)lround(val * (double)factor);
    if (escalado < 0) {
        buf[(*len)++] = '-';
        escalado = -escalado;
    }
    uint32_t entero = (uint32_t)escalado / factor;
    uint32_t frac = (uint32_t)escalado % factor;
    char digitos[10];
    size_t nd = 0;
    do {
        digitos[nd++] = (char)('0' + entero % 10);
        entero /= 10;
    } while (entero != 0);
    while (nd != 0) {
        buf[(*len)++] = digitos[--nd];
    }
    
    // Nuevo manejo para multiples decimales
    if (decimales > 0) {
        buf[(*len)++] = '.';
        for (int i = (int)decimales - 1; i >= 0; i--) {
            factor /= 10;
            buf[(*len)++] = (char)('0' + (frac / factor) % 10);
        }
    }
}



/* Agrega un entero con signo al cursor; dígitos en reversa. */
void protocolo_agregar_entero(char *buf, size_t *len, int32_t valor) {
    uint32_t sin_signo = (valor < 0) ? (uint32_t)(-valor) : (uint32_t)valor;
    if (valor < 0) {
        buf[(*len)++] = '-';
    }
    char digitos[10];
    size_t nd = 0;
    do {
        digitos[nd++] = (char)('0' + sin_signo % 10);
        sin_signo /= 10;
    } while (sin_signo != 0);
    while (nd != 0) {
        buf[(*len)++] = digitos[--nd];
    }
}

/* Concatena ",<etiqueta><valor_float>" al cursor con la cantidad de decimales indicada.*/
static void protocolo_concat_measure(char *buf, size_t *len, const char *etiqueta, double valor, uint8_t decimales) {
    buf[(*len)++] = ',';
    size_t etq_len = strlen(etiqueta);
    memcpy(buf + *len, etiqueta, etq_len);
    *len += etq_len;
    protocolo_agregar_float(buf, len, valor, decimales);
}

/* Concatena ",<etiqueta><valor_entero>" al cursor (SEQ, bool, etc). */
static void protocolo_concat_int(char *buf, size_t *len, const char *etiqueta, int32_t valor) {
    buf[(*len)++] = ',';
    size_t etq_len = strlen(etiqueta);
    memcpy(buf + *len, etiqueta, etq_len);
    *len += etq_len;
    protocolo_agregar_entero(buf, len, valor);
}

/*  Funcion que envia las mediciones que correspondan dependiendo de las metricas globales
    @param *mediciones  estructura en donde se guardan las mediciones
    @param *cfg         estructura de configuracion
    Dependiendo de la configuracion, arma un buffer, concatenando etiquetas+valor
    Finalmente enviar el frame completo.  */
void protocolo_enviar_telemetria(const measurement_t *mediciones, const app_config_t *cfg) {
    char buf[LINEA_MAX];
    size_t len = 0;

    // Encabezado
    memcpy(buf, "DATO", 4);
    len += 4;

    // Numero de secuencia
    protocolo_concat_int(buf, &len, "SEQ:", (int32_t)mediciones->seq);

    // metricas
    if (cfg->temperature) protocolo_concat_measure(buf, &len, "TEMP:", mediciones->temperature, 2);
    if (cfg->humidity)    protocolo_concat_measure(buf, &len, "HUM:", mediciones->humidity, 2);
    if (cfg->pressure)    protocolo_concat_measure(buf, &len, "PRES:", mediciones->pressure, 2);
    if (cfg->t_avg) {
        protocolo_concat_measure(buf, &len, "TEMP_AVG_C:", mediciones->temp_avg_c, 2);
        protocolo_concat_int(buf, &len, "TEMP_AVG_OK:", mediciones->avg_complete ? 1 : 0);
    }
    if (cfg->t_peak && mediciones->has_t_peak) {
        protocolo_concat_measure(buf, &len, "PICO:", mediciones->temp_peak, 2);
        protocolo_concat_measure(buf, &len, "UMBRAL:", mediciones->temp_peak_threshold, 1);
        protocolo_concat_int(buf, &len, "PICO_T:", (int32_t)mediciones->temp_peak_time);
    }

    // 6. Enviar paquete completo con checksum *CK\n
    protocolo_enviar_frame(buf, len);
}

/* Cambia el baudio del UART0 en caliente (hot-swap); lo transmitido
* después de la llamada sale a la velocidad nueva. */
void protocolo_fijar_baudios(uint32_t baudios) {
    uart_set_baudrate(UART_PORT_NUM, baudios);
}

/* Esta función es para probar que todo funcione. */
static void banner(void) {
    enviar_raw("tarea-1 listo (version 1.0)\n",
        sizeof("tarea-1 listo (version 1.0)\n") - 1);
    }
    
    void protocolo_init(void) {
        static const uart_config_t uart_config = {
            .baud_rate  = 115200,
            .data_bits  = UART_DATA_8_BITS,
            .parity     = UART_PARITY_DISABLE,
            .stop_bits  = UART_STOP_BITS_1,
            .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            uart_driver_install(UART_PORT_NUM, RX_BUF_SIZE, 0, 0, NULL, 0));
            ESP_ERROR_CHECK_WITHOUT_ABORT(
                uart_param_config(UART_PORT_NUM, &uart_config));
                ESP_ERROR_CHECK_WITHOUT_ABORT(
                    uart_set_pin(UART_PORT_NUM, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
                    
                    tx_mutex = xSemaphoreCreateMutex();
                    
                    banner();
                }
