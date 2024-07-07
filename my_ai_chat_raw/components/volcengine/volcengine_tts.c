
#include "volcengine_tts.h"
#include "volcengine_common.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "audio_hal.h"
#include "i2s_stream.h"
#include "esp_websocket_client.h"
#include "mp3_decoder.h"
#include "cJSON.h"
#include "uuid.h"
#include "raw_stream.h"
#include "base64.h"

static const char *TAG = "Volcengine_TTS";
#define VOLCENGINE_TTS_TASK_STACK (8*1024)
#define VOLCENGINE_TTS_URI "wss://openspeech.bytedance.com/api/v1/tts/ws_binary"


typedef struct Volcengine_tts {
    audio_pipeline_handle_t pipeline;
    esp_websocket_client_handle_t client;
    audio_element_handle_t  i2s_writer;
    audio_element_handle_t  raw_writer;
    audio_element_handle_t  mp3_decoder;
    int                     buffer_size;
    char                    *buffer;
    char                    *text;
    int                     sample_rate;
    TTS_Mes_Type            message;
} Volcengine_tts_t;


static void  tts_construct_full_json(char *out_string,char *text,const char *voice_type)
{
    // 创建根对象  
     uuid_t uu;
    char uu_str[UUID_STR_LEN];
    cJSON *root = cJSON_CreateObject();  
  
    // 添加app对象  
    cJSON *app = cJSON_CreateObject();  
    cJSON_AddItemToObject(root, "app", app); 
    //app的子元素 
    cJSON_AddStringToObject(app, "appid", vlocegine_appid);  
    cJSON_AddStringToObject(app, "token", vlocegine_token);  
    cJSON_AddStringToObject(app, "cluster",tts_cluster);  
  
    // 添加user对象  
    cJSON *user = cJSON_CreateObject();  
    cJSON_AddItemToObject(root, "user", user);  
    cJSON_AddStringToObject(user, "uid", "esp32_c3");  
  
    // 添加audio对象  
    cJSON *audio = cJSON_CreateObject();  
    cJSON_AddItemToObject(root, "audio", audio);  
    cJSON_AddStringToObject(audio, "voice_type", voice_type);  
    cJSON_AddStringToObject(audio, "encoding", "mp3");  
    cJSON_AddNumberToObject(audio, "compression_rate", 1);  
    cJSON_AddNumberToObject(audio, "rate", 16000);  
    cJSON_AddNumberToObject(audio, "speed_ratio", 1.0);  
    cJSON_AddNumberToObject(audio, "volume_ratio", 1.0);  
    cJSON_AddNumberToObject(audio, "pitch_ratio", 1.0);  
    cJSON_AddStringToObject(audio, "emotion", "happy");  
    cJSON_AddStringToObject(audio, "language", "cn");  
  
    //生成uuid
    uuid_generate(uu);
    ESP_LOGI(TAG, "uuid generated:");
    ESP_LOG_BUFFER_HEXDUMP(TAG, uu, sizeof(uuid_t), ESP_LOG_WARN);
    uuid_unparse(uu, uu_str);
    ESP_LOGI(TAG, "uuid parsed: %s", uu_str);

    // 添加request对象  
    cJSON *request = cJSON_CreateObject();  
    cJSON_AddItemToObject(root, "request", request);  
    cJSON_AddStringToObject(request, "reqid", uu_str);  
    cJSON_AddStringToObject(request, "text", text);  
    cJSON_AddStringToObject(request, "text_type", "plain");  
    cJSON_AddStringToObject(request, "operation", "submit");   //流式 
    cJSON_AddStringToObject(request, "silence_duration", "125");  
    cJSON_AddStringToObject(request, "with_frontend", "1");  
    cJSON_AddStringToObject(request, "frontend_type", "unitTson");  
    cJSON_AddStringToObject(request, "pure_english_opt", "1");  
  
    // 将JSON对象转换为字符串  
    char *json_string = cJSON_Print(root);  
    // 打印JSON字符串  
    //printf("%s\n", json_string);  
    strcpy(out_string,json_string);
    // 释放内存  
    cJSON_Delete(root);  
    free(json_string);  
}

static int tts_parse_response_mes(char *mes, TTS_Mes_Type *response_mes)
{
    char *ptr = mes;
    //header 部分
    Header_Type header;
    header.versio_and_header_size = mes[0];
    header.message_type_and_specific_flags = mes[1];
    header.serial_method_and_compression_type = mes[2];
    header.reserved = mes[3];
    response_mes->header = header;

    //解析头
    uint8_t versio = Get_Header_Protocol_version(header);
    uint8_t Header_size = Get_Header_size(header);
    Message_Type  message_type = Get_Header_Message_type(header);
    Message_Type_Specific_Flags   message_type_specific_flags =  Get_Header_Message_type_specific_flags(header);
    Message_Serialization_method serialization_method = Get_Header_Message_serialization_method(header);
    Message_Compression_Type message_compression = Get_Header_Message_Compression_Type(header);

    printf("Protocol version:%02x\n",versio);
    printf("Header_size:%d\n",Header_size);
    printf("message_type:%02x\n",message_type);
    printf("Message_Type_Specific_Flags:%02x\n",message_type_specific_flags);
    printf("Message_Serialization_method:%02x\n",serialization_method);
    printf("Message_Compression_Type:%02x\n",message_compression);

    //playload 部分
    uint8_t header_size = (mes[0] & 0x0f)<<2;
    int payload_offset = 0;
    int payload_size = 0;
    int sequence = 0;

    if (Header_size != 1)
        printf("Have Header extensions\n");
    if (message_type == 0xb)  //audio-only server response
    {   
        if (message_type_specific_flags == 0)  //no sequence number as ACK
        {
            printf("Payload size: 0");
            return ESP_FAIL;
        }else
        {
           payload_offset = header_size;  //过滤header_extensions
           sequence = mes[payload_offset+3] | mes[payload_offset+2] << 8 | mes[payload_offset+1]<<16 | mes[payload_offset] << 24;
           response_mes->sequence = sequence;

           payload_size =  mes[payload_offset+7] | mes[payload_offset+6] << 8 | mes[payload_offset+5]<<16 | mes[payload_offset+4] << 24; 
           response_mes->payload_size = payload_size;
           printf("sequence:%d\n",sequence);
           printf("payload_size:%d\n",payload_size);
           ptr += payload_offset + 8;
           response_mes->payload = ptr;
           return ESP_OK;
        }
    }else if(message_type == 0xf)
    {
        payload_offset = header_size+4;  //过滤header_extensions
        int error = mes[header_size+3] | mes[header_size+2] << 8 | mes[header_size+1]<<16 | mes[header_size] << 24;
        payload_size = mes[payload_offset+3] | mes[payload_offset+2] << 8 | mes[payload_offset+1]<<16 | mes[payload_offset] << 24;
        response_mes->payload_size = payload_size;
        ptr +=  payload_offset + 4;
        //strcpy(response_mes->playload,ptr);
        response_mes->payload = ptr; //playload 将会与vtt-buffer 共用同一块内存

        printf("Error message code: %d",error);
        printf("Error message size: %d",response_mes->payload_size);
        printf("     Error message: %s",response_mes->payload);
        return error;
    }else if(message_type == 0xc)
    {
        payload_offset = header_size;  //过滤header_extensions
        payload_size = mes[payload_offset+3] | mes[payload_offset+2] << 8 | mes[payload_offset+1]<<16 | mes[payload_offset] << 24;
        response_mes->payload_size = payload_size;
        ptr += payload_offset + 4;
        response_mes->payload = ptr;
        printf("Frontend message: %s\n",response_mes->payload);
        return ESP_FAIL;
    }else
    {
        printf("undefined message type!");
        return ESP_FAIL;
    }
    return ESP_FAIL;
}


// WebSocket客户端事件处理
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
	esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    Volcengine_tts_t *tts = (Volcengine_tts_t *) handler_args;
	switch (event_id)
	{
	// 连接成功
	case WEBSOCKET_EVENT_CONNECTED:
		ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        /*发送语音合成请求*/
         memset(tts->buffer,0,tts->buffer_size);
        Header_Type header = generate_full_default_header(); //生成full client request 的头
        tts_construct_full_json(tts->buffer+8,tts->text,tts_voice_type);     //paylaod消息体,从第8个字节位置写入
        int payload_len = strlen(tts->buffer+8); //计算消息体大小
        int len = sizeof(header)+4+payload_len;

        memcpy(tts->buffer,&header,sizeof(header));
        payload_len =  clac_payload_size(payload_len); //小端转大端
        memcpy(tts->buffer+4,&payload_len,4);
        esp_websocket_client_send_bin(tts->client,tts->buffer,len,portMAX_DELAY);
        
		break;
	// 连接断开
	case WEBSOCKET_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
		break;
	// 收到数据
	case WEBSOCKET_EVENT_DATA:
		ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
		ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
		if (data->op_code == 0x08 )
		{
			ESP_LOGW(TAG, "Received closed message");
		}
		else if(data->op_code == 0x09)
        {
            ESP_LOGI(TAG, "Received ping");
        }else
		{
			//ESP_LOGW(TAG, "Received=%.*s\n\n", data->data_len, (char *)(data->data_ptr)+8);
            //判断是不是消息的起点
            
            if (data->payload_offset == 0)
            {
                //解析数据
                memset(tts->buffer,0,tts->buffer_size);
                memcpy(tts->buffer,data->data_ptr,data->data_len);
                TTS_Mes_Type message;
                int error = tts_parse_response_mes(tts->buffer,&message);
                tts->message = message;
                if (error == ESP_OK)
                {
                    /*解析成功  开始写入raw的环形缓冲区*/
                    raw_stream_write(tts->raw_writer,message.payload,message.payload_size);
                }
            }else
            {
                /*直接发送数据*/
                if (Get_Header_Message_type(tts->message.header) == 0xb)   /*只有音频数据*/
                {
                    /* code */
                    memset(tts->buffer,0,tts->buffer_size);
                    memcpy(tts->buffer,data->data_ptr,data->data_len);
                    raw_stream_write(tts->raw_writer,tts->buffer,data->data_len);
                }
                
            }
                


		}

		ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);
        break;
	// 错误
	case WEBSOCKET_EVENT_ERROR:
		ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
		if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT)
		{
			
		}
		break;
	}
}



Volcengine_tts_handle_t Volcengine_tts_init(Volcengine_tts_config_t *config)
{
    // 管道设置
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    Volcengine_tts_t *tts = calloc(1, sizeof(Volcengine_tts_t));
    AUDIO_MEM_CHECK(TAG, tts, return NULL);

    tts->pipeline = audio_pipeline_init(&pipeline_cfg);

    tts->buffer_size = config->buffer_size;
    if (tts->buffer_size <= 0) {
        tts->buffer_size = DEFAULT_TTS_BUFFER_SIZE;
    }
    tts->buffer = malloc(tts->buffer_size);
    AUDIO_MEM_CHECK(TAG, tts->buffer, goto exit_tts_init);


    tts->sample_rate = config->playback_sample_rate;

    // I2S流设置
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(0, 16000, 16, AUDIO_STREAM_WRITER);
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    tts->i2s_writer = i2s_stream_init(&i2s_cfg);

    // raw流设置
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    tts->raw_writer = raw_stream_init(&raw_cfg);

    // MP3流设置
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    tts->mp3_decoder = mp3_decoder_init(&mp3_cfg);

    audio_pipeline_register(tts->pipeline, tts->raw_writer,         "tts_raw");
    audio_pipeline_register(tts->pipeline, tts->mp3_decoder,        "tts_mp3");
    audio_pipeline_register(tts->pipeline, tts->i2s_writer,         "tts_i2s");
    const char *link_tag[3] = {"tts_raw", "tts_mp3", "tts_i2s"};
    audio_pipeline_link(tts->pipeline, &link_tag[0], 3);

    // I2S流采样率 位数等设置
    i2s_stream_set_clk(tts->i2s_writer, config->playback_sample_rate, 16, 1);
    return tts;
exit_tts_init:
    Volcengine_tts_destroy(tts);
    return NULL;
}

esp_err_t Volcengine_tts_destroy(Volcengine_tts_handle_t tts)
{
    if (tts == NULL) {
        return ESP_FAIL;
    }
    audio_pipeline_stop(tts->pipeline);
    audio_pipeline_wait_for_stop(tts->pipeline);
    audio_pipeline_terminate(tts->pipeline);
    audio_pipeline_remove_listener(tts->pipeline);
    audio_pipeline_deinit(tts->pipeline);
    free(tts->buffer);

    return ESP_OK;
}

esp_err_t Volcengine_tts_set_listener(Volcengine_tts_handle_t tts, audio_event_iface_handle_t listener)
{
    if (listener) {
        audio_pipeline_set_listener(tts->pipeline, listener);
    }
    return ESP_OK;
}

esp_err_t Volcengine_tts_start(Volcengine_tts_handle_t tts, const char *text)
{
    free(tts->text);
    tts->text = strdup(text);
    if (tts->text == NULL) {
        ESP_LOGE(TAG, "Error no mem");
        return ESP_ERR_NO_MEM;
    }
    /*创建wedsocket 客户端*/
    esp_websocket_client_config_t websocket_cfg = {};
    websocket_cfg.uri = VOLCENGINE_TTS_URI;
    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);
    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    memset(tts->buffer,0,tts->buffer_size);
    sprintf(tts->buffer,"Bearer; %s",vlocegine_token);
    esp_websocket_client_append_header(client,"Authorization",tts->buffer);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)tts);
    esp_websocket_client_start(client);
    tts->client = client;
    
    /*开启管道*/
    audio_pipeline_reset_items_state(tts->pipeline);
    audio_pipeline_reset_ringbuffer(tts->pipeline);
    audio_pipeline_run(tts->pipeline);
    return ESP_OK;
}

esp_err_t Volcengine_tts_stop(Volcengine_tts_handle_t tts)
{
    /*管道停止*/
    audio_pipeline_stop(tts->pipeline);
    audio_pipeline_wait_for_stop(tts->pipeline);

    /*注销wedsocket客户端*/
    // 关闭WebSocket客户端
	esp_websocket_client_close(tts->client, portMAX_DELAY);
	ESP_LOGI(TAG, "Websocket Stopped");
	// 销毁WebSocket客户端
	esp_websocket_client_destroy(tts->client);
    tts->client = NULL;

    ESP_LOGD(TAG, "TTS Stopped");

    return ESP_OK;
}