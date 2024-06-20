#include "stdio.h"
// needed for int to str conversion. fucking C
#include "stdlib.h"

#include "wifi.h"
#include "comms.h"
#include "sensor.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"

static const char *TAG = "Project INDRA";

/*---------------------------------------------------------------
        Motor Macros
---------------------------------------------------------------*/
#define MOTOR 21

/*---------------------------------------------------------------
        ADC General Macros
---------------------------------------------------------------*/
// ADC2 Channels
#define SENS_01 ADC_CHANNEL_0 // GPIO4
#define SENS_02 ADC_CHANNEL_3 // GPIO15
#define SENS_03 ADC_CHANNEL_4 // GPIO13
#define SENS_04 ADC_CHANNEL_5 // GPIO12
#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_UNIT ADC_UNIT_2

#define NUM_OF_SENSORS 1

static int adc_raw[NUM_OF_SENSORS];
static int voltage[NUM_OF_SENSORS];

/*---------------------------------------------------------------
        ADC Calibration Vars
---------------------------------------------------------------*/
adc_oneshot_unit_handle_t adc_handle;
adc_cali_handle_t adc_cali_handle;
bool do_calibration2 = 0;

/*---------------------------------------------------------------
        Sensor Thresholds
---------------------------------------------------------------*/
#define MOISTURE_THRESHOLD_LOW 1000  // Example threshold for turning the motor on
#define MOISTURE_THRESHOLD_HIGH 2000 // Example threshold for turning the motor off


/*---------------------------------------------------------------
       Function to control the motor based on sensor value
---------------------------------------------------------------*/
void control_motor_based_on_moisture(int sensor_value)
{
    if (sensor_value < MOISTURE_THRESHOLD_LOW)
    {
        ESP_LOGI(TAG, "Moisture level low (%d), turning on water", sensor_value);
        gpio_set_level(MOTOR, 1);
    }
    else if (sensor_value > MOISTURE_THRESHOLD_HIGH)
    {
        ESP_LOGI(TAG, "Moisture level high (%d), turning off water", sensor_value);
        gpio_set_level(MOTOR, 0);
    }
}

/*---------------------------------------------------------------
       Sensor Reading and Motor Control Task
---------------------------------------------------------------*/
void sensor_task(void *pvParameter)
{
    while (1)
    {
        // Read sensor value
        if (do_calibration2)
        {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_raw[0], &voltage[0]));
        }
        else
        {
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, SENS_01, &adc_raw[0]));
        }

        ESP_LOGI(TAG, "Moisture level: %d", adc_raw[0]);
        control_motor_based_on_moisture(adc_raw[0]);

        vTaskDelay(5000 / portTICK_PERIOD_MS); // Adjust the delay as needed
    }
}

/*---------------------------------------------------------------
       MQTT Event Handler
---------------------------------------------------------------*/

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    // problem for future me
    // option 1: define the full event handler right here
    // option 2: define handler in main.c

    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        // subscribe to /indratest/motor/
        // should be qos2 - exactly 1ce
        msg_id = esp_mqtt_client_subscribe(client, "indratest/commands", 2);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        // ADC Tear Down
        ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
        if (do_calibration2)
        {
            adc_calibration_deinit(adc_cali_handle);
        }
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        // triggered when message is received on the subscribed topic
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        if (strncmp(event->topic, "indratest/commands", event->topic_len) == 0)
        {
            if (strncmp(event->data, "WATER_ON", event->data_len) == 0)
            {
                ESP_LOGI(TAG, "Turning on Water");
                gpio_set_level(MOTOR, 1);
            }
            else if (strncmp(event->data, "WATER_OFF", event->data_len) == 0)
            {
                ESP_LOGI(TAG, "Turning off Water");
                gpio_set_level(MOTOR, 0);
            }
            else if (strncmp(event->data, "MOISTURE_GET", event->data_len) == 0)
            {
                // errors unless sensor is actually connected !
                ESP_LOGI(TAG, "Checking soil moisture");
                vTaskDelay(500 / portTICK_PERIOD_MS); // Adjust the delay as needed
                if (do_calibration2)
                {
                    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_raw[0], &voltage[0]));
                }
                else
                {
                    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, SENS_01, &adc_raw[0]));
                }
                // str convert
                int length = snprintf(NULL, 0, "%d", adc_raw[0]);
                char *val = malloc(length + 1);
                snprintf(val, length + 1, "%d", adc_raw[0]);

                msg_id = esp_mqtt_client_publish(client, "indra/moisture", val, 0, 0, 0);
                ESP_LOGI(TAG, "sent SOIL MOISTURE successful, msg_id=%d", msg_id);
                ESP_LOGI(TAG, "sent SOIL MOISTURE successful, data=%s", val);

                free(val); // thank you C !!
            }
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

// ---------------------------------------------------------------
// ---------------------------------------------------------------

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

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    // temp soln till i move the error handling of wifi connection to main.c from inside func
    // vTaskDelay(10000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    // ADC Stuff
    adc_handle = adc_init(ADC_UNIT, SENS_01, ADC_ATTEN);
    do_calibration2 = adc_calibration_init(ADC_UNIT, SENS_01, ADC_ATTEN, &adc_cali_handle);

    // motor stuff
    gpio_reset_pin(MOTOR);
    gpio_set_direction(MOTOR, GPIO_MODE_OUTPUT);

    xTaskCreate(sensor_task, "auto function", 4096, NULL, 5, NULL);

    mqtt_app_start(mqtt_event_handler);
}
