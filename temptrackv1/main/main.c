#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "sdkconfig.h"
#include "dht_espidf.h"
#include "esp_spiffs.h"
#include "esp_vfs.h"
#include "ds18b20.h"

static const char *TAG = "main";

#define DHT_GPIO_1 26
#define DHT_GPIO_2 27
#define DS18B20_GPIO 4
#define MAX_DEVICES 2
#define SECOND 1000 / portTICK_PERIOD_MS

void app_main(void)
{
    // Reading requires a delay before starting
    vTaskDelay(2 * SECOND);

    // SPIFFS init
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    FILE *f = fopen("/spiffs/data.csv", "r+");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL)
    {
        printf(line);
    }

    // DS18B20 init
    // Create a 1-Wire bus, using the RMT timeslot driver
    OneWireBus *owb;
    owb_rmt_driver_info rmt_driver_info;
    owb = owb_rmt_initialize(&rmt_driver_info, (gpio_num_t)DS18B20_GPIO, RMT_CHANNEL_1, RMT_CHANNEL_0);
    owb_use_crc(owb, true); // enable CRC check for ROM code

    // Find all connected devices
    printf("Find devices:\n");
    OneWireBus_ROMCode device_rom_codes[MAX_DEVICES] = {0};
    int num_devices = 0;
    OneWireBus_SearchState search_state = {0};
    bool found = false;
    owb_search_first(owb, &search_state, &found);
    while (found)
    {
        char rom_code_s[17];
        owb_string_from_rom_code(search_state.rom_code, rom_code_s, sizeof(rom_code_s));
        printf("  %d : %s\n", num_devices, rom_code_s);
        device_rom_codes[num_devices] = search_state.rom_code;
        ++num_devices;
        owb_search_next(owb, &search_state, &found);
    }
    printf("Found %d device%s\n", num_devices, num_devices == 1 ? "" : "s");

    // Create DS18B20 devices on the 1-Wire bus
    DS18B20_Info *devices[MAX_DEVICES] = {0};
    for (int i = 0; i < num_devices; ++i)
    {
        DS18B20_Info *ds18b20_info = ds18b20_malloc(); // heap allocation
        devices[i] = ds18b20_info;

        if (num_devices == 1)
        {
            printf("Single device optimisations enabled\n");
            ds18b20_init_solo(ds18b20_info, owb); // only one device on bus
        }
        else
        {
            ds18b20_init(ds18b20_info, owb, device_rom_codes[i]); // associate with bus and device
        }
        ds18b20_use_crc(ds18b20_info, true); // enable CRC check on all reads
        ds18b20_set_resolution(ds18b20_info, DS18B20_RESOLUTION_12_BIT);
    }

    int errors_count[MAX_DEVICES] = {0};
    int sample_count = 0;

    // DHT init
    time_t now;
    struct dht_reading dht_data1;
    struct dht_reading dht_data2;
    dht_result_t res1;
    dht_result_t res2;

    while (1)
    {
        // res1 = read_dht_sensor_data((gpio_num_t)DHT_GPIO_1, DHT11, &dht_data1);
        // res2 = read_dht_sensor_data((gpio_num_t)DHT_GPIO_2, DHT11, &dht_data2);

        // if (res1 != DHT_OK || res2 != DHT_OK)
        // {
        //     ESP_LOGW(TAG, "DHT sensor reading failed");
        // }
        // else if (dht_data1.humidity == 0 || dht_data2.humidity == 0)
        // {
        //     ESP_LOGW(TAG, "Lu valeur nulle");
        // }
        // else
        // {
        //     // Get current time
        //     time(&now);
        //     // Log to spiffs
        //     fprintf(f, "1, %s, %f, %f,\n", ctime(&now), dht_data1.temperature, dht_data1.humidity);
        //     fprintf(f, "2, %s, %f, %f,\n", ctime(&now), dht_data2.temperature, dht_data2.humidity);
        //     // Log to serial
        //     ESP_LOGI(TAG, "1 - %s%f°C / %f hum\n", ctime(&now), dht_data1.temperature, dht_data1.humidity);
        //     ESP_LOGI(TAG, "2 - %s%f°C / %f hum\n", ctime(&now), dht_data2.temperature, dht_data2.humidity);
        // }

        ds18b20_convert_all(owb);

        // In this application all devices use the same resolution,
        // so use the first device to determine the delay
        ds18b20_wait_for_conversion(devices[0]);

        // Read the results immediately after conversion otherwise it may fail
        // (using printf before reading may take too long)
        float readings[MAX_DEVICES] = {0};
        DS18B20_ERROR errors[MAX_DEVICES] = {0};

        for (int i = 0; i < num_devices; ++i)
        {
            errors[i] = ds18b20_read_temp(devices[i], &readings[i]);
        }

        for (int i = 0; i < num_devices; ++i)
        {
            if (errors[i] != DS18B20_OK)
            {
                ++errors_count[i];
            }
            time(&now);
            fprintf(f, "%d, %s, %.1f,\n", i, ctime(&now), readings[i]);
            printf("  %d: %.1f    %d errors\n", i, readings[i], errors_count[i]);
        }

        vTaskDelay(1200 * SECOND);
    }

    fclose(f);
    // clean up dynamically allocated data
    for (int i = 0; i < num_devices; ++i)
    {
        ds18b20_free(&devices[i]);
    }
    owb_uninitialize(owb);

    esp_vfs_spiffs_unregister(NULL);
}