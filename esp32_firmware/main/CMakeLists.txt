idf_component_register(
SRCS "main.c" "protocol.c" "sensors.c" "ambiente.c" "comandos.c"
REQUIRES esp_driver_uart esp_driver_gpio
INCLUDE_DIRS "."
)
