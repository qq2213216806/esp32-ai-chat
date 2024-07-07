#ifndef __VOLCENGINE_VTT_H
#define __VOLCENGINE_VTT_H



#include "esp_err.h"
#include "audio_event_iface.h"


#define DEFAULT_VTT_BUFFER_SIZE (8192)

/**
 * audio encoding
 */
typedef enum {
    ENCODING_LINEAR16 = 0,  /*!< Speech-to-Text audio encoding PCM 16-bit mono */
} Volcengine_vtt_encoding_t;

typedef struct Volcengine_vtt* Volcengine_vtt_handle_t;
typedef void (*Volcengine_vtt_event_handle_t)(Volcengine_vtt_handle_t vtt);

/**
 * baidu Cloud Speech-to-Text configurations
 */
typedef struct {
    int record_sample_rates;            /*!< Audio recording sample rate */
    Volcengine_vtt_encoding_t encoding;      /*!< Audio encoding */
    int buffer_size;                    /*!< Processing buffer size */
    Volcengine_vtt_event_handle_t on_begin;  /*!< Begin send audio data to server */
} Volcengine_vtt_config_t;

Volcengine_vtt_handle_t Volcengine_Vtt_Init(Volcengine_vtt_config_t *config);
esp_err_t Volcengine_Vtt_start(Volcengine_vtt_handle_t vtt);
char *Volcengine_Vtt_stop(Volcengine_vtt_handle_t vtt);
esp_err_t Volcengine_Vtt_destroy(Volcengine_vtt_handle_t vtt);
esp_err_t Volcengine_Vtt_set_listener(Volcengine_vtt_handle_t vtt, audio_event_iface_handle_t listener);

#endif /*__VOLCENGINE_VTT_H*/
