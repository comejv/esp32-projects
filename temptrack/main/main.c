#include <inttypes.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_spiffs.h>
#include <time.h>
#include "ds18x20.h"

#define TOUCH0 GPIO_NUM_13
#define GPIO_DS28B20 GPIO_NUM_25
#define GPIO_DHT11_1 GPIO_NUM_32
#define GPIO_DHT11_2 GPIO_NUM_33
#define SECOND (1000 * portTICK_PERIOD_MS)

static const int MAX_SENSORS = 2;
static const int RESCAN_INTERVAL = 8;

static const char *TAG = "temptrack";

static const char *sensor_type(uint8_t family_id)
{
    switch (family_id)
    {
    case DS18X20_FAMILY_DS18S20:
        return "DS18S20";
    case DS18X20_FAMILY_DS1822:
        return "DS1822";
    case DS18X20_FAMILY_DS18B20:
        return "DS18B20";
    case DS18X20_FAMILY_MAX31850:
        return "MAX31850";
    }
    return "Unknown";
}

void ds18x20_read(void *pvParameters)
{
    FILE *file = (FILE *)pvParameters;
    fprintf(file, "date, ds18_1, ds18_2,\n");

    onewire_addr_t addrs[MAX_SENSORS];
    float temps[MAX_SENSORS];
    size_t sensor_count = 0;

    gpio_set_pull_mode((gpio_num_t)GPIO_DS28B20, GPIO_PULLUP_ONLY);

    esp_err_t res;
    time_t now;
    while (1)
    {
        // Every RESCAN_INTERVAL samples, check to see if the sensors connected
        // to our bus have changed.
        res = ds18x20_scan_devices((gpio_num_t)GPIO_DS28B20, addrs, MAX_SENSORS, &sensor_count);
        if (res != ESP_OK)
        {
            ESP_LOGE(TAG, "Sensors scan error %d (%s)", res, esp_err_to_name(res));
            continue;
        }

        if (!sensor_count)
        {
            ESP_LOGW(TAG, "No sensors detected!");
            continue;
        }

        ESP_LOGI(TAG, "%d sensors detected", sensor_count);

        // If there were more sensors found than we have space to handle,
        // just report the first MAX_SENSORS..
        if (sensor_count > MAX_SENSORS)
            sensor_count = MAX_SENSORS;

        time(&now);

        // Do a number of temperature samples, and print the results.
        for (int i = 0; i < RESCAN_INTERVAL; i++)
        {
            ESP_LOGI(TAG, "Measuring...");

            res = ds18x20_measure_and_read_multi((gpio_num_t)GPIO_DS28B20, addrs, sensor_count, temps);
            if (res != ESP_OK)
            {
                ESP_LOGE(TAG, "Sensors read error %d (%s)", res, esp_err_to_name(res));
                continue;
            }

            fprintf(file, "%s, ", ctime(&now));

            for (int j = 0; j < sensor_count; j++)
            {
                float temp_c = temps[j];
                float temp_f = (temp_c * 1.8) + 32;
                ESP_LOGI(TAG, "Sensor %08" PRIx32 "%08" PRIx32 " (%s) reports %.3f°C (%.3f°F)",
                         (uint32_t)(addrs[j] >> 32), (uint32_t)addrs[j],
                         sensor_type(addrs[j]),
                         temp_c, temp_f);

                fprintf(file, "%f, ", temp_f);
            }
            fprintf(file, "\n");

            // Wait for a little bit between each sample (note that the
            // ds18x20_measure_and_read_multi operation already takes at
            // least 750ms to run, so this is on top of that delay).
            vTaskDelay(5 * SECOND);
        }
    }
}

void dht11_read(void *pvParameters)
{
    // FILE *file = (FILE *)pvParameters;
    // fprintf(file, "date, dht11_1_temp, dht11_1_hum, dht11_2_temp, dht11_2_hum,\n");
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

    FILE *file_ds18 = fopen("/spiffs/ds18data.csv", "r+");
    if (file_ds18 == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }

    FILE *file_dht11 = fopen("/spiffs/dht11data.csv", "r+");
    if (file_dht11 == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }

    // Log files to console
    char line[256];
    while (fgets(line, sizeof(line), file_ds18))
    {
        printf("%s", line);
    }
    while (fgets(line, sizeof(line), file_dht11))
    {
        printf("%s", line);
    }
    rewind(file_ds18);
    rewind(file_dht11);

    xTaskCreate(&ds18x20_read, TAG, configMINIMAL_STACK_SIZE * 4, (void *)file_ds18, 5, NULL);
    xTaskCreate(&dht11_read, TAG, configMINIMAL_STACK_SIZE * 4, (void *)file_dht11, 5, NULL);

    fclose(file_ds18);
    fclose(file_dht11);

    esp_vfs_spiffs_unregister(NULL);
}