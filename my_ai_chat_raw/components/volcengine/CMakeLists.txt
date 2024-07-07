idf_component_register(SRCS "volcengine_common.c" "WedStream.c" "uuid.c" "volcengine_vtt.c" "volcengine_common.c" "volcengine_tts.c"
                    INCLUDE_DIRS "include" "D:/Espressif/frameworks/esp-idf-v5.1.4/components/wpa_supplicant/src/utils"
                    REQUIRES "audio_pipeline" "esp_wifi" "nvs_flash" "esp_http_client" "audio_stream" 
                                 "json"  "audio_hal" "esp_websocket_client" "wpa_supplicant")
