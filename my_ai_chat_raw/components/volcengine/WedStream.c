#include "WedStream.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include <sys/socket.h>
#include "audio_mem.h"
#include "esp_websocket_client.h"
#include "raw_stream.h"
static const char *TAG = "WEDSTREAM";
#define  WEDSTREAM_STREAM_BUFFER_SIZE (2048)

// WebSocket客户端事件处理 主要负责管理与服务器连接的回调函数
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
	esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    WedStream_t *wedstream = (WedStream_t*)handler_args;
	WedStream_event_msg_t msg;
    msg.event_id = (WedStream_event_id_t)event_id;
    msg.wedstream = wedstream;
    msg.user_data = wedstream->user_data;
    msg.buffer = malloc(data->data_len);
    memcpy(msg.buffer, data->data_ptr,data->data_len);
    msg.buffer_len = data->data_len;
    msg.el = NULL; //音频元素上下文，但是这里并不需要
    msg.op_code = data->op_code;
	if(wedstream->hook)
	{
		wedstream->hook(&msg);
	}
    free(msg.buffer);
    /*二级关闭处理  避免没有显性的调用 WedStream_able_to_close*/
    if(wedstream->shutdown_sema != NULL && data->op_code == 0x09)
    {
        ESP_LOGW(TAG, "Please call WedStream_able_to_close in your program");
        xSemaphoreGive(wedstream->shutdown_sema);
    }    
}

static int dispatch_hook(audio_element_handle_t self, WedStream_event_id_t type, void *buffer, int buffer_len)
{
    WedStream_t *wedstream = (WedStream_t *)audio_element_getdata(self);
    WedStream_event_msg_t msg;
    msg.event_id = type;
    msg.wedstream = wedstream;
    msg.user_data = wedstream->user_data;
    msg.buffer = buffer;
    msg.buffer_len = buffer_len;
    msg.el = self;
    msg.op_code = 15; //该操作码,无用
    if (wedstream->hook) {
        return wedstream->hook(&msg);
    }
    return ESP_OK;
}

static esp_err_t _WedStream_open(audio_element_handle_t self)
{
    WedStream_t *wedstream = (WedStream_t *)audio_element_getdata(self);
    char *uri = NULL;
    audio_element_info_t info;
    ESP_LOGD(TAG, "_WedStream_open");

     if (wedstream->client) {
        //esp_websocket_client_close(wedstream->client,portMAX_DELAY);
        esp_websocket_client_destroy(wedstream->client);
        wedstream->client = NULL;
    }

    if (wedstream->is_open)
    {
        ESP_LOGE(TAG, "already opened");
        return ESP_OK;
    }
    wedstream->_errno = 0;
    audio_element_getinfo(self, &info);

    uri = audio_element_get_uri(self);
    if (uri == NULL) {
        ESP_LOGE(TAG, "Error open connection, uri = NULL");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "URI=%s", uri);

    if(wedstream->client == NULL)
    {  
        /*wedstream 客户端初始化 但是不打开 start*/
        esp_websocket_client_config_t client_cfg = {
            .uri = uri,
            .reconnect_timeout_ms = 1000,
            .buffer_size = WEDSTREAM_STREAM_BUFFER_SIZE,
        };
        printf("config.uri:%s\n",client_cfg.uri);
        // 创建WebSocket客户端
        wedstream->client = esp_websocket_client_init(&client_cfg);
        AUDIO_MEM_CHECK(TAG, wedstream->client, return ESP_ERR_NO_MEM);
        if(wedstream->header_key != NULL && wedstream->header_value != NULL)
        {
            esp_websocket_client_append_header(wedstream->client,wedstream->header_key,wedstream->header_value);
        }
        // 注册事件
        //wedstream->user_data = config->user_data;
        esp_websocket_register_events(wedstream->client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)wedstream);
        /* wedstream 客户端初始化 end */
    }
    if(esp_websocket_client_is_connected(wedstream->client) != true)
    {
        esp_websocket_client_start(wedstream->client);
        while (esp_websocket_client_is_connected(wedstream->client) != true)
        {
            vTaskDelay(1);
        }
        ESP_LOGI(TAG, "client Client successfully open");
    }
    
    wedstream->is_open = true;
    audio_element_report_codec_fmt(self);
	return ESP_OK;
}

static esp_err_t _WedStream_close(audio_element_handle_t self)
{
	WedStream_t *wedstream = (WedStream_t *)audio_element_getdata(self);
    ESP_LOGD(TAG, "_wedstream_close");
    if (wedstream->is_open) {
        wedstream->is_open = false;
        do {
            if (wedstream->stream_type != AUDIO_STREAM_WRITER) {
                break;
            }
            /*调用一些钩子操作*/
        } while (0);
    }

    if (AEL_STATE_PAUSED != audio_element_get_state(self)) {
        audio_element_report_pos(self);
        audio_element_set_byte_pos(self, 0);
    }

    // 创建信号量           //为等待服务器最后一条消息而设置
	wedstream->shutdown_sema = xSemaphoreCreateBinary();
    dispatch_hook(self,WEDSTREAM_EVENT_BEFORE_CLOSE,NULL,0);
    xSemaphoreTake(wedstream->shutdown_sema,portMAX_DELAY);

    if (wedstream->client) {
        //esp_websocket_client_close(wedstream->client,portMAX_DELAY);
        esp_websocket_client_destroy(wedstream->client);
        wedstream->client = NULL;
        ESP_LOGI(TAG, "client Client successfully closed");
        vSemaphoreDelete(wedstream->shutdown_sema);
        wedstream->shutdown_sema = NULL;
    }

	return ESP_OK;
}
static int _WedStream_read(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    // WedStream_t *http = (WedStream_t *)audio_element_getdata(self);
    audio_element_info_t info;
    audio_element_getinfo(self, &info);
    int wrlen = dispatch_hook(self, WEDSTREAM_EVENT_DATA_AUDIO_READ, buffer, len);
    int rlen = wrlen;
    
    if (rlen <= 0) {
        ESP_LOGE(TAG, "Failed to process user callback");
    } else {
        audio_element_update_byte_pos(self, rlen);
    }
    ESP_LOGD(TAG, "req lengh=%d, read=%d, pos=%d/%d", len, rlen, (int)info.byte_pos, (int)info.total_bytes);
    return rlen;
}

static int _WedStream_write(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    WedStream_t *wedstream = (WedStream_t *)audio_element_getdata(self);
    int wrlen = dispatch_hook(self, WEDSTREAM_EVENT_DATA_AUDIO_WRITE, buffer, len);
    if (wrlen < 0) {
        ESP_LOGE(TAG, "Failed to process user callback");
        return ESP_FAIL;
    }
    if (wrlen > 0) {
        return wrlen;
    }
    /*如果钩子被调用，这个if不会被进入*/
    if ((wrlen = esp_websocket_client_send_text(wedstream->client,buffer,len,portMAX_DELAY) ) <= 0) {
        ESP_LOGE(TAG, "Failed to write data to WedStream");
    }
    return wrlen;
}


static int _WedStream_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    int r_size = audio_element_input(self, in_buffer, in_len); //读环形缓冲区
    if (audio_element_is_stopping(self) == true) {
        ESP_LOGW(TAG, "No output due to stopping");
       // return AEL_IO_ABORT;
    } 
    int w_size = 0;
    if (r_size > 0) {
        w_size = audio_element_output(self, in_buffer, r_size);
        audio_element_multi_output(self, in_buffer, r_size, 0);
    }
     else {
        w_size = r_size;
    }

    return w_size;
	
}
static esp_err_t _WedStream_destroy(audio_element_handle_t self)
{
	WedStream_t *wedstream = (WedStream_t *)audio_element_getdata(self);
    audio_free(wedstream->header_key);
    audio_free(wedstream->header_value);
	audio_free(wedstream);
    return ESP_OK;
}
audio_element_handle_t WedStream_init(WedStream_cfg_t *config)
{
	audio_element_handle_t el;
    WedStream_t *wedstream = calloc(1, sizeof(WedStream_t));
    AUDIO_MEM_CHECK(TAG, wedstream, return NULL);

   /* 音频元素初始化配置 start*/
    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.open = _WedStream_open;
    cfg.close = _WedStream_close;
    cfg.process = _WedStream_process;
    cfg.destroy = _WedStream_destroy;
    cfg.task_stack = config->task_stack;
    cfg.task_prio = config->task_prio;
    cfg.task_core = config->task_core;
    cfg.stack_in_ext = config->stack_in_ext;
    cfg.out_rb_size = config->out_rb_size;
    cfg.multi_out_rb_num = config->multi_out_num;
    cfg.tag = "WedStream";

    wedstream->type = config->type;
    wedstream->hook = config->event_handle;
    wedstream->stream_type = config->type;
    wedstream->user_data = config->user_data;
    //wedstream->cert_pem = config->cert_pem;
   // wedstream->user_agent = config->user_agent;

    if (config->type == AUDIO_STREAM_READER) {
        cfg.read = _WedStream_read;
    } else if (config->type == AUDIO_STREAM_WRITER) {
        cfg.write = _WedStream_write;
    }
   // wedstream->request_range_size = config->request_range_size;
    if (config->request_size) {
        cfg.buffer_len = config->request_size;
    }

	el = audio_element_init(&cfg);
	AUDIO_MEM_CHECK(TAG, el, {
        audio_free(wedstream);
        return NULL;
    });
	audio_element_setdata(el, wedstream);
    audio_element_set_uri(el,config->uri);  //添加uri
	/*音频元素初始化配置 end*/
   return el;
}
esp_err_t WedStream_append_header(audio_element_handle_t el, const char *key, const char *value)
{
    WedStream_t *wedstream = (WedStream_t *)audio_element_getdata(el);
    if(wedstream->header_key != NULL)
    {
        free(wedstream->header_key);
        wedstream->header_key = NULL;
    }
    if(wedstream->header_value != NULL)
    {
        free(wedstream->header_value);
        wedstream->header_value = NULL;
    }

    wedstream->header_key = malloc(strlen(key)+1);     //加1，补'0'
    wedstream->header_value = malloc(strlen(value)+1); //加1，补'0'
	strcpy(wedstream->header_key,key);
    strcpy(wedstream->header_value,value);
    return ESP_OK;
}

int WedStream_client_send_bin(WedStream_t *wedstream,const char*data,int len,TickType_t timeout)
{
    esp_websocket_client_handle_t client =(esp_websocket_client_handle_t )wedstream->client;
    return esp_websocket_client_send_bin(client,data,len,portMAX_DELAY);
}

esp_err_t WedStream_able_to_close(WedStream_t *wedstream)
{
     //为等待服务器最后一条消息而设置
    if (wedstream->shutdown_sema == NULL)
    {
       ESP_LOGW(TAG, "shutdown_sema not created");
       return ESP_FAIL;
    }
    xSemaphoreGive(wedstream->shutdown_sema);
    return ESP_OK;
}