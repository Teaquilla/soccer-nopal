idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES ps3 nvs_flash esp_wifi esp_http_server driver esp_timer esp_hw_support esp_system
)
