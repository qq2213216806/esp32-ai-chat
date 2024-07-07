#include "volcengine_common.h"

Header_Type generate_header( uint8_t version,
    Message_Type message_type,
    Message_Type_Specific_Flags specific_flags,
    Message_Serialization_method serial_method,
    Message_Compression_Type compression,
    uint8_t reserved_data)
{
    Header_Type header;
    uint8_t header_size = DEFAULT_HEADER_SIZE;
    header.versio_and_header_size = ( (version << 4) | (header_size) );
    header.message_type_and_specific_flags = ( (message_type << 4) | (specific_flags) );
    header.serial_method_and_compression_type = ( (serial_method << 4) | (compression) );
    header.reserved = reserved_data;
    return header;
}

// 
Header_Type generate_full_default_header(void)
{
    Header_Type header = generate_header(PROTOCOL_VERSION,CLIENT_FULL_REQUEST,NO_SEQUENCE,JSON,NO_COMPRESSION,0x00);
    return header;
}

Header_Type generate_audio_default_header(void)
{
    Header_Type header = generate_header(PROTOCOL_VERSION,CLIENT_AUDIO_ONLY_REQUEST,NO_SEQUENCE,NO_SERIALIZATION,NO_COMPRESSION,0x00);
    return header;
}

Header_Type generate_last_audio_default_header(void)
{
    Header_Type header = generate_header(PROTOCOL_VERSION,CLIENT_AUDIO_ONLY_REQUEST,NEG_SEQUENCE,NO_SERIALIZATION,NO_COMPRESSION,0x00);
    return header;
}

Message_Type  Get_Header_Message_type(Header_Type header)
{
    return (Message_Type)(header.message_type_and_specific_flags >> 4);
}
//小端转大端
int clac_payload_size(int data_len)
{
	char *data = (char *)&data_len;  //int转char
	//小端转大端
	int result = (data[0] <<24) | (data[1] <<16) | (data[2] << 8) | data[3];  
	data = (char *)&result;
	return result;
}

uint8_t  Get_Header_Protocol_version(Header_Type header)
{
    return (uint8_t)(header.versio_and_header_size >> 4);
}
uint8_t Get_Header_size(Header_Type header)
{
    return (uint8_t)(header.versio_and_header_size & 0xf);
}
Message_Type_Specific_Flags Get_Header_Message_type_specific_flags(Header_Type header)
{
    return (Message_Type_Specific_Flags)(header.message_type_and_specific_flags & 0xf);
}
Message_Serialization_method  Get_Header_Message_serialization_method(Header_Type header)
{
    return (Message_Serialization_method)(header.serial_method_and_compression_type >>4);
}
Message_Compression_Type Get_Header_Message_Compression_Type(Header_Type header)
{
    return (Message_Compression_Type)(header.serial_method_and_compression_type &0xf);
}