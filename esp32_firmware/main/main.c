/* Arranque: protocolo UART0 + síntesis de señales + ambiente + comandos */
#include "protocol.h"
#include "comandos.h"
#include "bme688.h"
#include "tasks.h"

void app_main(void) {
    protocolo_init();
    comandos_init();
    bme_init();
    tasks_init();
}
