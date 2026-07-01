idf_component_register(SRCS "main.c"
                            "sensors.c"
                            "state_machine.c"
                            "net.c"
                            "storage.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_adc esp_timer esp_driver_rmt
                                esp_wifi esp_event esp_netif nvs_flash
                                mqtt esp-tls json
                       EMBED_TXTFILES "certs/broker_ca.pem")
