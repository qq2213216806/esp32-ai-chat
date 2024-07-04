/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "sdkconfig.h"
#include "audio_common.h"
#include "audio_hal.h"
#include "minimax_chat.h"
#include "cJSON.h"

static const char *TAG = "MINIMAX_CHAT";

extern const char * minimax_key;

#define PSOT_DATA    "{\
\"model\":\"abab5.5s-chat\",\"tokens_to_generate\": 256,\"temperature\":0.7,\"top_p\":0.7,\"plugins\":[],\"sample_messages\":[],\
\"reply_constraints\":{\"sender_type\":\"BOT\",\"sender_name\":\"小美\"},\
\"bot_setting\":[{\
\"bot_name\":\"小美\",\
\"content\":\"小美，性别女，年龄22岁，在校大学生，性格活泼可爱，说话幽默风趣，擅长撩男生，喜欢美食，爱好旅行，是个话痨。\\n\"}],\
\"messages\":[{\"sender_type\":\"USER\",\"sender_name\":\"靓仔\",\"text\":\"%s\"}]\
}"


#define MAX_CHAT_BUFFER (2048)
char minimax_content[2048]={0};

char *minimax_chat(const char *text)
{
    char *response_text = NULL;
    char *post_buffer = NULL;
    char *data_buf = NULL; 

    esp_http_client_config_t config = {
        .url = "https://api.minimax.chat/v1/text/chatcompletion_pro?GroupId=xxx",  // 这里替换成自己的GroupId
        .buffer_size_tx = 1024  // 默认是512 minimax_key很长 512不够 这里改成1024
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    int post_len = asprintf(&post_buffer, PSOT_DATA, text);
    
    if (post_buffer == NULL) {
        goto exit_translate;
    }

    // POST
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", minimax_key);

    if (esp_http_client_open(client, post_len) != ESP_OK) {
        ESP_LOGE(TAG, "Error opening connection");
        goto exit_translate;
    }
    int write_len = esp_http_client_write(client, post_buffer, post_len);
    ESP_LOGI(TAG, "Need to write %d, written %d", post_len, write_len);

    int data_length = esp_http_client_fetch_headers(client);
    if (data_length <= 0) {
        data_length = MAX_CHAT_BUFFER;
    }
    data_buf = malloc(data_length + 1);
    if(data_buf == NULL) {
        goto exit_translate;
    }
    data_buf[data_length] = '\0';
    int rlen = esp_http_client_read(client, data_buf, data_length);
    data_buf[rlen] = '\0';
    ESP_LOGI(TAG, "read = %s", data_buf);

    cJSON *root = cJSON_Parse(data_buf);
    int created = cJSON_GetObjectItem(root,"created")->valueint;
    if(created != 0)
    {
        char *reply = cJSON_GetObjectItem(root,"reply")->valuestring;
        strcpy(minimax_content, reply);
        response_text = minimax_content;
        ESP_LOGI(TAG, "response_text:%s", response_text);
    }

    cJSON_Delete(root);

exit_translate:
    free(post_buffer);
    free(data_buf);
    esp_http_client_cleanup(client);

    return response_text;
}


