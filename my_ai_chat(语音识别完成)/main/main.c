/* LCD GUI  example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "audio_idf_version.h"
#include "esp_wifi.h"
#include "esp_peripherals.h"
#include "periph_button.h"
#include "periph_wifi.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <string.h>
#include "esp_timer.h"
#include "driver/ledc.h"
#include "esp_lcd_touch_ft5x06.h"
#include "es8311.h"
#include "volcengine_vtt.h"

// 要连接的WIFI
#define ESP_WIFI_STA_SSID "Redmi"
#define ESP_WIFI_STA_PASSWD "qq2213216806"

static const char *TAG = "AI_CHAT_EXAMPLE";

void WIFI_CallBack(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
	static uint8_t connect_count = 0;
	// WIFI 启动成功
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		ESP_LOGI("WIFI_EVENT", "WIFI_EVENT_STA_START");
		ESP_ERROR_CHECK(esp_wifi_connect());
	}
	// WIFI 连接失败
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		ESP_LOGI("WIFI_EVENT", "WIFI_EVENT_STA_DISCONNECTED");
		connect_count++;
		if (connect_count < 6)
		{
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			ESP_ERROR_CHECK(esp_wifi_connect());
		}
		else
		{
			ESP_LOGI("WIFI_EVENT", "WIFI_EVENT_STA_DISCONNECTED 10 times");
		}
	}
	// WIFI 连接成功(获取到了IP)
	if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
		ESP_LOGI("WIFI_EVENT", "WIFI_EVENT_STA_GOT_IP");
		ip_event_got_ip_t *info = (ip_event_got_ip_t *)event_data;
		ESP_LOGI("WIFI_EVENT", "got ip:" IPSTR "", IP2STR(&info->ip_info.ip));
	}
}

// wifi初始化
static void wifi_sta_init(void)
{
	ESP_ERROR_CHECK(esp_netif_init());

	// 注册事件(wifi启动成功)
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, WIFI_CallBack, NULL, NULL));
	// 注册事件(wifi连接失败)
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, WIFI_CallBack, NULL, NULL));
	// 注册事件(wifi连接失败)
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, WIFI_CallBack, NULL, NULL));

	// 初始化STA设备
	esp_netif_create_default_wifi_sta();

	/*Initialize WiFi */
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	// WIFI_INIT_CONFIG_DEFAULT 是一个默认配置的宏

	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	//----------------配置阶段-------------------
	// 初始化WIFI设备( 为 WiFi 驱动初始化 WiFi 分配资源，如 WiFi 控制结构、RX/TX 缓冲区、WiFi NVS 结构等，这个 WiFi 也启动 WiFi 任务。必须先调用此API，然后才能调用所有其他WiFi API)
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	// STA详细配置
	wifi_config_t sta_config = {
			.sta = {
					.ssid = ESP_WIFI_STA_SSID,
					.password = ESP_WIFI_STA_PASSWD,
					.bssid_set = false,
			},
	};
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

	//----------------启动阶段-------------------
	ESP_ERROR_CHECK(esp_wifi_start());

	//----------------配置省电模式-------------------
	// 不省电(数据传输会更快)
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}


void app_main(void)
{
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

    /*******  初始化ES8311音频芯片 (注意：这个里面会初始化I2C 后面初始化触摸屏就不用再初始化I2C了)  ********/
    ESP_LOGI(TAG, "[ 2 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);
    es8311_codec_set_voice_volume(100); // 最大音量
    es8311_pa_power(false);  // 关闭声音

	// 创建默认事件循环
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// 配置启动WIFI
	wifi_sta_init();

	// 等待wifi连接成功(暂时这样处理)
	vTaskDelay(5000 / portTICK_PERIOD_MS);

	// Initialize peripherals management
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    //periph_cfg.task_stack = 8*1024;
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

	// Initialize Button peripheral
    periph_button_cfg_t btn_cfg = {
        .gpio_mask = (1ULL << get_input_mode_id()) | (1ULL << get_input_rec_id()),
    };
    esp_periph_handle_t button_handle = periph_button_init(&btn_cfg);

    // button peripheral
    esp_periph_start(set, button_handle);


	/*管道测试*/
	//  语音转文字 初始化
    Volcengine_vtt_config_t vtt_config = {
        .record_sample_rates = 16000,
        .encoding = ENCODING_LINEAR16,
    };
    Volcengine_vtt_handle_t vtt = Volcengine_Vtt_Init(&vtt_config);

	// 监听“流”
    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from the pipeline");
    Volcengine_Vtt_set_listener(vtt, evt);

	ESP_LOGI(TAG, "[4.2] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);
	
    while (1)
    {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) != ESP_OK) {
            ESP_LOGW(TAG, "[ * ] Event process failed: src_type:%d, source:%p cmd:%d, data:%p, data_len:%d",
                     msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);
            continue;
        }

        ESP_LOGI(TAG, "[ * ] Event received: src_type:%d, source:%p cmd:%d, data:%p, data_len:%d",
                 msg.source_type, msg.source, msg.cmd, msg.data, msg.data_len);

        //按键情况判断
        if (msg.source_type != PERIPH_ID_BUTTON) {
            continue; //不是按键类型忽略本次事件
        }

        if ((int)msg.data == get_input_mode_id()) {
            break;   
        }
            //这里的按键框架类似于Linux的input子系统
        if ((int)msg.data != get_input_rec_id()) {
            continue; //不是录制按钮则 忽略本次事件
        }

        
        if (msg.cmd == PERIPH_BUTTON_PRESSED) {
			printf("按键按下\n");
			Volcengine_Vtt_start(vtt);
           
        } else if (msg.cmd == PERIPH_BUTTON_RELEASE || msg.cmd == PERIPH_BUTTON_LONG_RELEASE)
		{
            printf("按键松开\n");
			Volcengine_Vtt_stop(vtt);
        }

    }
    ESP_LOGI(TAG, "[ 6 ] Stop audio_pipeline");
    vTaskDelete(NULL);
}
        
	
