/* comandos.h — recepción y aplicación de comandos del contrato:
 * INICIAR, FUNCION, AMPLITUD, FRECUENCIA, PERIODO, BAUDIOS.
 */
#ifndef COMANDOS_H
#define COMANDOS_H

/* Acumula hasta '\n', valida el checksum y responde OK/ERROR. */
void comandos_init(void);

#endif /* COMANDOS_H */
