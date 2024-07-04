idf_component_register(SRCS "WedStream.c" "uuid.c" "volcengine_vtt.c" 
                    INCLUDE_DIRS "include"
                    REQUIRES "audio_pipeline" "esp_wifi" "nvs_flash" "esp_http_client" "audio_stream" 
                                 "json"  "audio_hal" "esp_websocket_client")
