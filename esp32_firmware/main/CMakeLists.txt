idf_component_register(
SRCS "main.c" "protocol.c" "comandos.c" "bme688.c" "tasks.c"
REQUIRES esp_driver_uart esp_driver_gpio esp_driver_i2c
INCLUDE_DIRS "."
)
