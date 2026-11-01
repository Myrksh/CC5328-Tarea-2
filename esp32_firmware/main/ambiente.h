 /* temperatura 15.0–30.0 °C y humedad relativa 20–40 %. */
#ifndef AMBIENTE_H
#define AMBIENTE_H

#include <stdint.h>

/* Crea la task que emite AMBIENTE,<°C>,<%HR>*CK cada 30 s
 * vía protocolo (protocol.h). */
void ambiente_init(void);

/* Cambia el periodo de emisión en segundos (30 o 60). */
void ambiente_fijar_periodo(uint32_t segundos);

#endif /* AMBIENTE_H */
