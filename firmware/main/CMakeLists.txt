idf_component_register(SRCS  "main.c" "deepdeck_tasks.c" "keymap.c" "matrix.c" "keypress_handles.c" "wifi_handles.c" "serial_transport.c" "config_api.c" "config_task.c"
                    INCLUDE_DIRS "."
                    REQUIRES "driver" "vfs" "json" "esp_timer" "esp_wifi" "nvs_flash" "app_update" "bootloader_support" "mbedtls" "battery" "ble" "oled" "nvsfuncs" "plugins" "rotary_encoder" "rgb_led" "menu" "bus" "uuid_master") 

