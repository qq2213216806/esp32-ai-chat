#ifndef __WED_STREAM_H
#define __WED_STREAM_H

#include "audio_error.h"
#include "audio_element.h"
#include "audio_common.h"
#include "esp_websocket_client.h"

typedef enum
{
    WEDSTREAM_EVENT_ERROR = 0,      /*!< This event occurs when there are any errors during execution */
    WEDSTREAM_EVENT_CONNECTED,      /*!< Once the Websocket has been connected to the server, no data exchange has been performed */
    WEDSTREAM_EVENT_DISCONNECTED,   /*!< The connection has been disconnected */
    WEDSTREAM_EVENT_DATA_SERVER,    /*服务器发来的数据 !< When receiving data from the server, possibly multiple portions of the packet */
    WEDSTREAM_EVENT_CLOSED,         /*!< The connection has been closed cleanly */
    WEDSTREAM_EVENT_BEFORE_CONNECT, /*!< The event occurs before connecting */
    WEDSTREAM_EVENT_MAX,
    WEDSTREAM_EVENT_DATA_AUDIO_READ,     /* recv audio data  */
    WEDSTREAM_EVENT_DATA_AUDIO_WRITE,
    WEDSTREAM_EVENT_BEFORE_CLOSE,       /*流关闭前*/  
}WedStream_event_id_t;

typedef struct WedStream_event_msg{
    WedStream_event_id_t    event_id;       /*!< Event ID */
    void                    *wedstream;   /*!< Reference to HTTP Client using by this HTTP Stream */
   const char               *buffer;        /*!< Reference to Buffer using by the Audio Element */
    int                     buffer_len;     /*!< Length of buffer */
    void                    *user_data;     /*!< User data context, from `http_stream_cfg_t` */
    audio_element_handle_t  el;             /*!< Audio element context */
     uint8_t                 op_code;                        /*!< Received opcode */
}WedStream_event_msg_t;

typedef int (*WedStream_event_handle_t)(WedStream_event_msg_t *msg);

typedef struct WedStream
{
    audio_stream_type_t             type;
    bool                            is_open;
    esp_websocket_client_handle_t   client;
    WedStream_event_handle_t        hook;
    audio_stream_type_t             stream_type;
    void                            *user_data;
	char                             *uri;
    int                             _errno;            /* errno code for http */
    int                             connect_times;     /* max reconnect times */
    const char                     *cert_pem;
    int                             request_range_size;
    int64_t                         request_range_end;
    bool                            is_last_range;
    const char                      *user_agent;
    char                      *header_key;
    char                      *header_value;
    SemaphoreHandle_t           shutdown_sema;
}WedStream_t;


typedef struct WedStream_config_t
{
    audio_stream_type_t         type;
    const char    *uri;
    WedStream_event_handle_t    event_handle;           /*!< The hook function for HTTP Stream */ 
    void                        *user_data;              /*客户端的用户数据*/
    int                         out_rb_size;            /*!< Size of output ringbuffer */
    int                         task_stack;             /*!< Task stack size */
    int                         task_core;              /*!< Task running in core (0 or 1) */
    int                         task_prio;              /*!< Task priority (based on freeRTOS priority) */
    bool                        stack_in_ext;           /*!< Try to allocate stack in external memory */
    int                         multi_out_num;      
    int                         request_size;
}WedStream_cfg_t;

audio_element_handle_t WedStream_init(WedStream_cfg_t *config);
esp_err_t WedStream_append_header(audio_element_handle_t el, const char *key, const char *value);

/*
通知客户端可以关闭，注:接收最后一包数据之后调用，通知客户端本次连接完成,可以关闭客户端
注：如果不调用WedStream_able_to_close, 客户端永不会关闭。 
*/
esp_err_t WedStream_able_to_close(WedStream_t *wedstream);
int WedStream_client_send_bin(WedStream_t *wedstream,const char*data,int len,TickType_t timeout);
//int WedStream_client_send_text(WedStream_t *wedstream,const char*data,int len,TickType_t timeout);
#endif /*__WED_STREAM_H*/
