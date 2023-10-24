#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "ds18x20.h"
#include "dht.h"
#include "esp_sleep.h"

#define TOUCH0 GPIO_NUM_13
#define GPIO_DS28B20 GPIO_NUM_25
#define GPIO_DHT11_1 GPIO_NUM_32
#define GPIO_DHT11_2 GPIO_NUM_33
#define SECOND (1000 * portTICK_PERIOD_MS)

static const int MAX_SENSORS = 2;

static const char *TAG = "temptrack";

void ds18x20_read(FILE *file)
{

    onewire_addr_t addrs[MAX_SENSORS];
    float temps[MAX_SENSORS];
    size_t sensor_count = 0;

    gpio_set_pull_mode((gpio_num_t)GPIO_DS28B20, GPIO_PULLUP_ONLY);

    esp_err_t res;
    time_t now;
    time(&now);
    // Check to see if the sensors connected
    // to our bus have changed.
    res = ds18x20_scan_devices((gpio_num_t)GPIO_DS28B20, addrs, MAX_SENSORS, &sensor_count);
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Sensors scan error %d (%s)", res, esp_err_to_name(res));
        return;
    }

    if (!sensor_count)
    {
        ESP_LOGW(TAG, "No sensors detected!");
        return;
    }

    ESP_LOGI(TAG, "%d sensors detected", sensor_count);

    // If there were more sensors found than we have space to handle,
    // just report the first MAX_SENSORS..
    if (sensor_count > MAX_SENSORS)
        sensor_count = MAX_SENSORS;

    ESP_LOGI(TAG, "Measuring...");

    res = ds18x20_measure_and_read_multi((gpio_num_t)GPIO_DS28B20, addrs, sensor_count, temps);
    if (res != ESP_OK)
    {
        ESP_LOGE(TAG, "Sensors read error %d (%s)", res, esp_err_to_name(res));
        return;
    }

    fprintf(file, "%ld, ", (long)now);

    for (int i = 0; i < sensor_count; i++)
    {
        float temp_c = temps[i];
        ESP_LOGI(TAG, "%ld : DS18B20_%d reports %.3f°C",
                 (long)now,
                 i,
                 temp_c);

        fprintf(file, "%.2f, ", temp_c);
    }
    fprintf(file, "\n");
}

void dht11_read(FILE *file)
{
    // date, dht11_1_temp, dht11_1_hum, dht11_2_temp, dht11_2_hum
    float temperature, humidity;
    time_t now;
    time(&now);

    // DHT11_1
    esp_err_t ret = dht_read_float_data(DHT_TYPE_DHT11, (gpio_num_t)GPIO_DHT11_1, &humidity, &temperature);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not read data from sensor");
        return;
    }
    ESP_LOGI(TAG, "%ld : DHT11_1 reports %f°C, %f%% humidity",
             (long)now,
             temperature,
             humidity);
    fprintf(file, "%ld, %f, %f, ", (long)now, temperature, humidity);

    // DHT11_2
    ret = dht_read_float_data(DHT_TYPE_DHT11, (gpio_num_t)GPIO_DHT11_2, &humidity, &temperature);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not read data from sensor");
        return;
    }
    ESP_LOGI(TAG, "%ld : DHT11_2 reports %f°C, %f%% humidity",
             (long)now,
             temperature,
             humidity);
    fprintf(file, "%f, %f\n", temperature, humidity);

    return;
}

void app_main()
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t config = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&config);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "SPIFFS mounted");

    FILE *file_ds18 = fopen("/spiffs/ds18data.csv", "r+");
    if (file_ds18 == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }

    // FILE *file_dht11 = fopen("/spiffs/dht11data.csv", "r+");
    // if (file_dht11 == NULL)
    // {
    //     ESP_LOGE(TAG, "Failed to open file for reading");
    //     return;
    // }

    // Log files to console
    char line[256];
    ESP_LOGI(TAG, "DS18 LOG :");
    while (fgets(line, sizeof(line), file_ds18) != NULL)
    {
        printf("%s", line);
    }
    // ESP_LOGI(TAG, "DHT LOG :");
    // while (fgets(line, sizeof(line), file_dht11) != NULL)
    // {
    //     printf("%s", line);
    // }

    ds18x20_read(file_ds18);
    // dht11_read(file_dht11); // PROBLEM IN PHASE B

    fclose(file_ds18);
    // fclose(file_dht11);

    // esp_vfs_spiffs_unregister(NULL);

    esp_deep_sleep(240 * 1000000);
}