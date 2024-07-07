#ifndef __VOLCENGINE_TTS_H
#define __VOLCENGINE_TTS_H

#include "esp_err.h"
#include "audio_event_iface.h"
#include "volcengine_common.h"

#define  DEFAULT_TTS_BUFFER_SIZE     (2048)
#define  DEFAULT_TTS_AUDIO_DATA_SIZE (2048)

typedef struct Volcengine_tts* Volcengine_tts_handle_t;

typedef struct {
    const char *api_key;
    //const char *lang_code;
    int playback_sample_rate;
    int buffer_size;
} Volcengine_tts_config_t;

typedef struct 
{
   Header_Type header;
   int         sequence;
   int         payload_size;
   char        *payload;  
} TTS_Mes_Type;







/**
 * @brief      Initialize Baidu Cloud Text-to-Speech, this function will return a Text-to-Speech context
 *
 * @param      config  The configuration
 *
 * @return     The Text-to-Speech context
 */
Volcengine_tts_handle_t Volcengine_tts_init(Volcengine_tts_config_t *config);

/**
 * @brief      Start sending text to Baidu Cloud Text-to-Speech and play audio received
 *
 * @param[in]  tts        The Text-to-Speech context
 * @param[in]  text       The text
 * @param[in]  lang_code  The language code
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 */
esp_err_t Volcengine_tts_start(Volcengine_tts_handle_t tts, const char *text);

/**
 * @brief      Stop playing audio from Baidu Cloud Text-to-Speech
 *
 * @param[in]  tts   The Text-to-Speech context
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 */
esp_err_t Volcengine_tts_stop(Volcengine_tts_handle_t tts);

/**
 * @brief      Register listener for the Text-to-Speech context
 *
 * @param[in]  tts       The Text-to-Speech context
 * @param[in]  listener  The listener
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 */
esp_err_t Volcengine_tts_set_listener(Volcengine_tts_handle_t tts, audio_event_iface_handle_t listener);

/**
 * @brief      Cleanup the Text-to-Speech object
 *
 * @param[in]  tts   The Text-to-Speech context
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL
 */
esp_err_t Volcengine_tts_destroy(Volcengine_tts_handle_t tts);


#endif 
