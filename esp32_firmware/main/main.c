/* Arranque: protocolo UART0 + bme688 + tareas */
#include "protocol.h"
#include "bme688.h"
#include "tasks.h"

void app_main(void) {
    esp_wifi_stop()
    protocolo_init();
    bme_init();
    tasks_init();
}
