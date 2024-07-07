#ifndef __VOLCENGINE_COMMON_H
#define __VOLCENGINE_COMMON_H

#include "esp_err.h"
#include "audio_event_iface.h"

/*宏定义列表*/
#define vlocegine_appid    "3342807223"                                   //项目的 appid
#define vlocegine_token    "su0lR53n7I6Z1PEzgxtKMy9eskBKPTX7"               // 项目的 token
#define vtt_cluster        "volcengine_streaming_common"             // 语音识别请求的集群
#define tts_cluster        "volcano_tts"
#define tts_voice_type     "BV700_streaming"

#define audio_format  "raw"




#define     PROTOCOL_VERSION     0b0001    //协议版本号
#define     DEFAULT_HEADER_SIZE  0b0001    //

#define     PROTOCOL_VERSION_BITS   4
#define     HEADER_BITS             4
#define     MESSAGE_TYPE_BITS       4
#define     MESSAGE_TYPE_SPECIFIC_FLAGS_BITS  4
#define     MESSAGE_SERIALIZATION_BITS        4
#define     MESSAGE_COMPRESSION_BITS          4
#define     RESERVED_BITS                     8

//Message Type 消息类型
typedef enum 
{
    CLIENT_FULL_REQUEST = 0b0001,         //端上发送包含请求参数的 full client request
    CLIENT_AUDIO_ONLY_REQUEST = 0b0010,   //端上发送包含音频数据的 audio only request
    SERVER_FULL_RESPONSE = 0b1001,        //服务端下发包含识别结果的 full server response  
    SERVER_ACK = 0b1011,                  //服务端下发包 Audio-only server response 
    SERVER_ERROR_RESPONSE = 0b1111        //服务端处理错误时下发的消息类型（如无效的消息格式，不支持的序列化方法等）
}Message_Type;

// Message Type Specific Flags 消息类型的补充信息
typedef enum
{
    NO_SEQUENCE = 0b0000,    //full client request 或包含非最后一包音频数据的 audio only request 中设置
    POS_SEQUENCE = 0b0001,   
    NEG_SEQUENCE = 0b0010,   //包含最后一包音频数据的 audio only request 中设置
    NEG_SEQUENCE_1 = 0b0011
}Message_Type_Specific_Flags;

// Message Serialization // 消息序列化方法
typedef enum
{
    NO_SERIALIZATION = 0b0000,  //无序列化
    JSON = 0b0001,              //JSON 格式    
    THRIFT = 0b0011,
    CUSTOM_TYPE = 0b1111
}Message_Serialization_method;

//Message Compression  消息压缩方式
typedef enum
{
    NO_COMPRESSION = 0b0000,       //不压缩
    GZIP = 0b0001,                 //GZIP压缩 
    CUSTOM_COMPRESSION = 0b1111    //自定义压缩
}Message_Compression_Type;

typedef struct
{
    uint8_t versio_and_header_size;
    uint8_t message_type_and_specific_flags;
    uint8_t serial_method_and_compression_type;
    uint8_t reserved;
}Header_Type;  

typedef struct
{
    Header_Type header;
    int playload_size;    //大端模式
    char *playload;
}Mes_Type;

Header_Type generate_header( uint8_t version,
    Message_Type message_type,
    Message_Type_Specific_Flags specific_flags,
    Message_Serialization_method serial_method,
    Message_Compression_Type compression,
    uint8_t reserved_data);

Header_Type generate_full_default_header(void);
Header_Type generate_audio_default_header(void);
Header_Type generate_last_audio_default_header(void);
Message_Type  Get_Header_Message_type(Header_Type header);
int clac_payload_size(int data_len);

uint8_t  Get_Header_Protocol_version(Header_Type header);
uint8_t Get_Header_size(Header_Type header);
Message_Type_Specific_Flags Get_Header_Message_type_specific_flags(Header_Type header);
Message_Serialization_method  Get_Header_Message_serialization_method(Header_Type header);
Message_Compression_Type Get_Header_Message_Compression_Type(Header_Type header);


#endif
