/* Arranque: protocolo UART0 + síntesis de señales + ambiente + comandos */
#include "protocol.h"
#include "sensors.h"
#include "ambiente.h"
#include "comandos.h"

void app_main(void) {
    protocolo_init();
    sensors_init();
    ambiente_init();
    comandos_init();
}
