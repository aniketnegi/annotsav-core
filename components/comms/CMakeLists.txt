idf_component_register(SRCS "comms.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_event esp_netif mqtt)
