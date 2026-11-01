/* Contrato: líneas ASCII terminadas en '\n', argumentos separados por comas,
 * checksum XOR de 2 dígitos hexa mayúsculas tras '*'.
 * Cuerpos inválidos o demasiado largos se descartan en silencio.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Puerto usado por el protocolo (consola UART0) */
#define PROTOCOLO_UART_PORT 0

/* Configura UART0 (TX GPIO1 / RX GPIO3, 115200 8N1), instala el driver,
 * crea el mutex TX y emite un banner mínimo en el arranque. */
void protocolo_init(void);

/* Baudio del arranque */
#define PROTOCOLO_BAUDIOS_DEFAULT 115200

/* Cambia el baudio del UART0 en caliente (hot-swap); lo transmitido
 * después de la llamada sale a la velocidad nueva. */
void protocolo_fijar_baudios(uint32_t baudios);

/* Transmite <datos>*CK\n: armado usando un cursor y varios memcpy+len.
 * Evita dataraces entre tasks usando un mutex; len no incluye '\0' final. */
void protocolo_enviar_frame(const char *datos, size_t len);

/* Conveniencia para cuerpos terminados en '\0': delega en
 * protocolo_enviar_frame() midiendo con strlen. */
void protocolo_enviar_linea(const char *cuerpo);

/* Checksum con XOR de todos los bytes del cuerpo (sin '*' ni checksum). */
uint8_t protocolo_checksum(const char *cuerpo);

/* Agrega al cursor un entero en milésimas con formato d.ddd (para valores). */
void protocolo_agregar_milig(char *buf, size_t *len, int32_t milig);

/* Agrega al cursor un entero con signo (dígitos en reversa). */
void protocolo_agregar_entero(char *buf, size_t *len, int32_t valor);

#endif /* PROTOCOL_H */
