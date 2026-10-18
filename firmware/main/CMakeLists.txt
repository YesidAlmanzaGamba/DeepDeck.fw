idf_component_register(SRCS  "spiffs.c" "main.c" "deepdeck_tasks.c" "keymap.c" "matrix.c" "keypress_handles.c" "server.c" "wifi_handles.c" "serial_transport.c" "config_api.c" "config_task.c"
                    INCLUDE_DIRS "."
                    REQUIRES "driver" "vfs" "json" "esp_timer" "spiffs" "mdns" "esp_http_server" "mqtt_dd" "battery" "ble" "oled" "nvsfuncs" "plugins" "rotary_encoder" "rgb_led" "menu" "bus" "uuid_master") 


                    spiffs_create_partition_image(www_0 ../spiffs_image FLASH_IN_PROJECT)
