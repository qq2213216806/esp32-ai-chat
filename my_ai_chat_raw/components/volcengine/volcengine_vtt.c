#include "volcengine_vtt.h"
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
#include "WedStream.h"
#include "esp_websocket_client.h"
#include "mp3_decoder.h"
#include "cJSON.h"
#include "uuid.h"

static const char *TAG = "Volcengine_VTT";


#define Volcengine_VTT_URI    "wss://openspeech.bytedance.com/api/v2/asr"
#define Volcengine_VTT_TASK_STACK  (8*1024)

typedef struct Volcengine_vtt {
    audio_pipeline_handle_t pipeline;
    char                    *send_buffer;
    char                    *recv_buffer;
    audio_element_handle_t  i2s_reader;
    audio_element_handle_t  WedStream_writer;
    int                     sample_rates;
    int                     buffer_size;
    Volcengine_vtt_encoding_t    encoding;
    char                    *response_text;
    Volcengine_vtt_event_handle_t on_begin;
    bool                    is_begin; //判断是否是第一次发
} Volcengine_vtt_t;

char ask_text[256]={0};  // 存储提问的文字 

static void  vtt_construct_full_json(char *out_string)
{    
    uuid_t uu;
    char uu_str[UUID_STR_LEN];
    cJSON *root = cJSON_CreateObject();  
  
    // 添加app对象  
    cJSON *app = cJSON_CreateObject();  
    cJSON_AddItemToObject(root, "app", app);  
    // app的子元素  
    cJSON_AddStringToObject(app, "appid", vlocegine_appid); // 这里假设appid、token、cluster为空字符串  
    cJSON_AddStringToObject(app, "token", vlocegine_token);  
    cJSON_AddStringToObject(app, "cluster",vtt_cluster);  
  
    // 添加user对象  
    cJSON *user = cJSON_CreateObject();  
    cJSON_AddItemToObject(root, "user", user);  
    // user的子元素  
    cJSON_AddStringToObject(user, "uid","esp32_c3");  
    
    // 添加audio对象  
    cJSON *audio = cJSON_CreateObject();  
    cJSON_AddItemToObject(root, "audio", audio);  
    // audio的子元素  
    cJSON_AddStringToObject(audio, "format", audio_format);  
    cJSON_AddNumberToObject(audio, "rate", 16000);  
    cJSON_AddNumberToObject(audio, "bits", 16);  
    cJSON_AddNumberToObject(audio, "channel", 1);  
    cJSON_AddStringToObject(audio, "language", "zh-CN");  
  
    // 添加request对象  
    cJSON *request = cJSON_CreateObject();  
    cJSON_AddItemToObject(root, "request", request);  
     
    //生成uuid
    uuid_generate(uu);
    ESP_LOGI(TAG, "uuid generated:");
    ESP_LOG_BUFFER_HEXDUMP(TAG, uu, sizeof(uuid_t), ESP_LOG_WARN);
    uuid_unparse(uu, uu_str);
    ESP_LOGI(TAG, "uuid parsed: %s", uu_str);
	
    // request的子元素 
    cJSON_AddStringToObject(request, "reqid", uu_str);  
    cJSON_AddStringToObject(request, "workflow", "audio_in,resample,partition,vad,fe,decode,itn,nlu_punctuate");  
    cJSON_AddNumberToObject(request, "sequence", 1);  
    cJSON_AddNumberToObject(request, "nbest", 1);  
    cJSON_AddBoolToObject(request, "show_utterances", 0); // true is 1 in cJSON  
  
    // 将JSON对象转换为字符串  
    char *json_string = cJSON_Print(root);  
    
    // 打印JSON字符串  
    //printf("%s\n", json_string);  
    strcpy(out_string,json_string);

    // 释放内存  
    cJSON_Delete(root);  
    free(json_string);  
}

static int parse_json(char *json,char *response_text,int *sequence)
{
    cJSON *root = cJSON_Parse(json);
    if(root == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();  
        if (error_ptr != NULL) 
        {  
           ESP_LOGW(TAG, "Error before: %s\n", error_ptr); 
        } 
        return ESP_FAIL; 
    }
    //提取出
    cJSON *sequence_item = cJSON_GetObjectItem(root, "sequence");
    if (sequence_item!= NULL && cJSON_IsNumber(sequence_item)) {
        *sequence = sequence_item->valueint;
        printf("sequence: %d\n", *sequence);
    } else {
        printf("sequence not found or not a number\n");
    }


    // 提取result数组  
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");  
    if (!cJSON_IsArray(result))
    {  
        ESP_LOGW(TAG, "Error: result is not an array\n");
        cJSON_Delete(root);  
        return ESP_FAIL;  
    }  
    // 提取result数组的第一个元素  
    cJSON *firstResult = cJSON_GetArrayItem(result, 0);  
    if (!cJSON_IsObject(firstResult)) 
    {  
        ESP_LOGW(TAG, "Error: first result is not an object\n"); 
        cJSON_Delete(root);  
        return ESP_FAIL;  
    }  
  
    // 提取text字段  
    cJSON *text = cJSON_GetObjectItemCaseSensitive(firstResult, "text");  
    if (!cJSON_IsString(text) || (text->valuestring == NULL))
    {  
       
        cJSON_Delete(root);  
        return ESP_FAIL;  
    }  
    strcpy(response_text ,text->valuestring);
    return ESP_OK;
}


int parse_response_mes(char *mes,Mes_Type *response_mes)
{
    char *ptr = mes;
    int error_code = 1000;
    //header 部分
    Header_Type header;
    header.versio_and_header_size = mes[0];
    header.message_type_and_specific_flags = mes[1];
    header.serial_method_and_compression_type = mes[2];
    header.reserved = mes[3];
    response_mes->header = header;

    //playload 部分
    uint8_t header_size = (mes[0] & 0x0f)<<2;
    int payload_offset = 0;
    int payload_size = 0;

    if(Get_Header_Message_type(header) == SERVER_FULL_RESPONSE)
    {   
        payload_offset = header_size;  //过滤header_extensions
        //大端转小端
        payload_size = mes[payload_offset+3] | mes[payload_offset+2] << 8 | mes[payload_offset+1]<<16 | mes[payload_offset] << 24;
        response_mes->playload_size = payload_size;
        
        ptr +=  payload_offset + 4;
        //strcpy(response_mes->playload,ptr);
        response_mes->playload = ptr; //playload 将会与vtt-buffer 共用同一块内存
        error_code = 1000;
        
    }else if(Get_Header_Message_type(header) == SERVER_ERROR_RESPONSE  )
    {
        payload_offset = header_size+4;  //过滤header_extensions
        payload_size = mes[payload_offset+3] | mes[payload_offset+2] << 8 | mes[payload_offset+1]<<16 | mes[payload_offset] << 24;
        response_mes->playload_size = payload_size;
        ptr +=  payload_offset + 4;
        //strcpy(response_mes->playload,ptr);
        response_mes->playload = ptr; //playload 将会与vtt-buffer 共用同一块内存
        error_code = mes[header_size+3] | mes[header_size+2] << 8 | mes[header_size+1]<<16 | mes[header_size] << 24;
    }
    return error_code;
}


static esp_err_t _VTT_WedStream_event_handle(WedStream_event_msg_t *msg)
{
   // esp_websocket_client_handle_t client = (esp_websocket_client_handle_t)msg->Wed_client;
    WedStream_t *wedstream = (WedStream_t *)msg->wedstream;
    Volcengine_vtt_t *vtt = (Volcengine_vtt_t *)msg->user_data;
    if (msg->event_id == WEDSTREAM_EVENT_CONNECTED)
    {
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        vtt->is_begin = true;
        return ESP_OK;
    }
    if (msg->event_id == WEDSTREAM_EVENT_DISCONNECTED)
    {
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        return ESP_OK;
    }
    //收到服务器的数据
    if (msg->event_id == WEDSTREAM_EVENT_DATA_SERVER)
    {
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_RECV_SERVER_DATA");
        ESP_LOGI(TAG, "Received opcode=%d", msg->op_code);
		if (msg->op_code == 0x08)
		{
			ESP_LOGW(TAG, "Received closed message");
		}else if(msg->op_code == 0x09)
        {
            ESP_LOGI(TAG, "Received ping");
        }
		else
		{
            memcpy(vtt->recv_buffer,msg->buffer,msg->buffer_len);
            vtt->recv_buffer[msg->buffer_len] = '\0'; //补'0'
            //开始解析
            Mes_Type message;
            int error = parse_response_mes(vtt->recv_buffer,&message);
            if (error == 1000)
            {
               printf("消息正常可以 json解析\n");
               printf("消息长度:%d\n",message.playload_size);
               printf("消息：%s\n",message.playload);
               int sequence;
               error =  parse_json(message.playload,ask_text,&sequence);
               if (sequence < 0)
               {  
                   //收到最后一条消息
                   WedStream_able_to_close(wedstream); //通知客户端可以关闭，注:接收最后一数据之后调用，通知可以客户端本次连接完成
               }
               if (error == ESP_OK && sequence < 0)
               {
                    vtt->response_text = ask_text;
                    printf("response_text:%s\n",vtt->response_text);
               }
            }else
            {
                printf("错误消息 错误码:%d\n",error);
                printf("消息：%s\n",message.playload);
                WedStream_able_to_close(wedstream);
            }
            
        
		}

        return msg->buffer_len; 
    }
    
    //收到I2S流的数据
    if (msg->event_id == WEDSTREAM_EVENT_DATA_AUDIO_READ)
    {
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_RECV_DATA_AUDIO_READ");
       
        return msg->buffer_len;
    }
    if (msg->event_id == WEDSTREAM_EVENT_DATA_AUDIO_WRITE)
    {
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_RECV_DATA_AUDIO_WRITE");
         //判断是不是第一次发送
        if( vtt->is_begin == true)
        {
            memset(vtt->send_buffer,0,vtt->buffer_size);
           
            Header_Type header = generate_full_default_header(); //生成full client request 的头
            vtt_construct_full_json(vtt->send_buffer+8);     //paylaod消息体,从第8个字节位置写入
            int payload_len = strlen(vtt->send_buffer+8); //计算消息体大小
            int len = sizeof(header)+4+payload_len;

            memcpy(vtt->send_buffer,&header,sizeof(header));
            payload_len =  clac_payload_size(payload_len); //小端转大端
            memcpy(vtt->send_buffer+4,&payload_len,4);
           // esp_websocket_client_send_bin(client,vtt->buffer,len,portMAX_DELAY);
           // esp_websocket_client_send_bin(client,vtt->send_buffer,len,portMAX_DELAY);
           WedStream_client_send_bin(wedstream,vtt->send_buffer,len,portMAX_DELAY);

            vtt->is_begin = false;
        }else
        {
            memset(vtt->send_buffer,0,vtt->buffer_size);
            //发音频数据
            Header_Type header = generate_audio_default_header(); //生成Audio only client request的头
            int payload_len = clac_payload_size(msg->buffer_len);
            int len = sizeof(header)+4+msg->buffer_len;
            memcpy(vtt->send_buffer,&header,sizeof(header));
            memcpy(vtt->send_buffer+4,&payload_len,4);
            memcpy(vtt->send_buffer+8,msg->buffer,msg->buffer_len);
            //esp_websocket_client_send_bin(client,vtt->send_buffer,len,portMAX_DELAY);
            WedStream_client_send_bin(wedstream,vtt->send_buffer,len,portMAX_DELAY);
        }
        
        return msg->buffer_len;
    }
    if (msg->event_id == WEDSTREAM_EVENT_BEFORE_CLOSE)
    {
        /*发送结束消息*/
        printf("发送结束块\n");
        memset(vtt->send_buffer,0,vtt->buffer_size);
        Header_Type header = generate_last_audio_default_header();
        int payload_len =  strlen("\r\n");
        payload_len = clac_payload_size(payload_len); //小端转大端
        memcpy(vtt->send_buffer,&header,sizeof(header));
        memcpy(vtt->send_buffer+4,&payload_len,4);
        memcpy(vtt->send_buffer+8,"\r\n",strlen("\r\n"));
        int len = sizeof(header)+4+strlen("\r\n");
       // esp_websocket_client_send_bin(client,vtt->buffer,len,portMAX_DELAY);
       // esp_websocket_client_send_bin(client,vtt->send_buffer,len,portMAX_DELAY);
        WedStream_client_send_bin(wedstream,vtt->send_buffer,len,portMAX_DELAY);

        return ESP_OK;
    }
    

    return ESP_OK;
}

esp_err_t Volcengine_Vtt_start(Volcengine_vtt_handle_t vtt)
{
    audio_pipeline_reset_items_state(vtt->pipeline);
    audio_pipeline_reset_ringbuffer(vtt->pipeline);
    audio_pipeline_run(vtt->pipeline);
    return ESP_OK;
}

char *Volcengine_Vtt_stop(Volcengine_vtt_handle_t vtt)
{
    audio_pipeline_stop(vtt->pipeline);
    audio_pipeline_wait_for_stop(vtt->pipeline);

    return vtt->response_text;
}

esp_err_t Volcengine_Vtt_destroy(Volcengine_vtt_handle_t vtt)
{
    if (vtt == NULL) {
        return ESP_FAIL;
    }
    audio_pipeline_stop(vtt->pipeline);
    audio_pipeline_wait_for_stop(vtt->pipeline);
    audio_pipeline_terminate(vtt->pipeline);
    audio_pipeline_remove_listener(vtt->pipeline);
    audio_pipeline_deinit(vtt->pipeline);
    free(vtt->send_buffer);
    free(vtt->recv_buffer);
    free(vtt);
    return ESP_OK;
}

esp_err_t Volcengine_Vtt_set_listener(Volcengine_vtt_handle_t vtt, audio_event_iface_handle_t listener)
{
    if (listener) {
        audio_pipeline_set_listener(vtt->pipeline, listener);
    }
    return ESP_OK;
}

Volcengine_vtt_handle_t Volcengine_Vtt_Init(Volcengine_vtt_config_t *config)
{
    // 管道配置
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    Volcengine_vtt_t *vtt = calloc(1, sizeof(Volcengine_vtt_t));
    AUDIO_MEM_CHECK(TAG, vtt, return NULL);
    vtt->pipeline = audio_pipeline_init(&pipeline_cfg);

    vtt->buffer_size = config->buffer_size;
    if (vtt->buffer_size <= 0) {
        vtt->buffer_size = DEFAULT_VTT_BUFFER_SIZE;
    }
    vtt->send_buffer = malloc(vtt->buffer_size);
    vtt->recv_buffer = malloc(vtt->buffer_size);
    AUDIO_MEM_CHECK(TAG, vtt->send_buffer, goto exit_vtt_init);
    AUDIO_MEM_CHECK(TAG, vtt->recv_buffer, goto exit_vtt_init);
    // I2S流配置
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT_WITH_PARA(0, 16000, 16, AUDIO_STREAM_READER);
    i2s_cfg.std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO;
    i2s_cfg.std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    vtt->i2s_reader = i2s_stream_init(&i2s_cfg);

    //WedStream流配置
    WedStream_cfg_t wedstream_cfg ={
        .type = AUDIO_STREAM_WRITER,
        .uri = Volcengine_VTT_URI,
        .event_handle =  _VTT_WedStream_event_handle,
        .user_data = vtt,
        .task_stack = Volcengine_VTT_TASK_STACK,
    };
    vtt->WedStream_writer = WedStream_init(&wedstream_cfg); 
    sprintf(vtt->send_buffer,"Bearer; %s",vlocegine_token);
    WedStream_append_header(vtt->WedStream_writer,"Authorization",vtt->send_buffer); //添加鉴权头
    vtt->sample_rates = config->record_sample_rates;
    vtt->encoding = config->encoding;
    vtt->on_begin = config->on_begin;
    vtt->is_begin = true;

    // 把 I2S流 和 HTTP流 注册到管道 并链接起来
     audio_pipeline_register(vtt->pipeline, vtt->WedStream_writer,  "vtt_WedStream");
      audio_pipeline_register(vtt->pipeline, vtt->i2s_reader,         "vtt_i2s");
    const char *link_tag[2] = {"vtt_i2s", "vtt_WedStream"};
    audio_pipeline_link(vtt->pipeline, &link_tag[0], 2);
  // const char *link_tag[1] = {"vtt_WedStream"};
  //  audio_pipeline_link(vtt->pipeline, &link_tag[0], 1);

    // 设置I2S采样率 位数 等参数
    i2s_stream_set_clk(vtt->i2s_reader, config->record_sample_rates, 16, 1);

    return vtt;

exit_vtt_init:
    Volcengine_Vtt_destroy(vtt);
    return NULL;
}

