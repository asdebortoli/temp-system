idf_component_register(SRCS "main.c"
                            "sensors.c"
                            "state_machine.c"
                            "net.c"
                            "storage.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_adc esp_timer esp_driver_rmt)