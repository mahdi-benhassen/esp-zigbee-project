/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "alarm_timer.h"
#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "ezbee/zcl/cluster/carbon_dioxide_measurement_desc.h"
#include "ezbee/zcl/cluster/analog_input_desc.h"
#include "gas_sensor.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"

static const char *TAG = "ESP_ZIGBEE_GAS_SENSOR";

#define I2C_SDA_PIN          GPIO_NUM_10
#define I2C_SCL_PIN          GPIO_NUM_11
#define AMMONIA_ADC_PIN      GPIO_NUM_1  // ADC1_CH0
#define FAN_RELAY_PIN        GPIO_NUM_3

#define SCD4X_I2C_ADDR       0x62

static float s_co2_ppm = 400.0f;       // Fresh air base
static float s_ammonia_ppm = 5.0f;     // Clean coop base
static uint8_t s_fan_state = 0;        // Off

static i2c_master_dev_handle_t s_scd4x_handle = NULL;
static adc_oneshot_unit_handle_t s_adc_handle = NULL;

// CRC-8 calculation helper for SCD4x
static uint8_t scd4x_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// Initialize I2C and SCD4x sensor
static esp_err_t init_scd4x_sensor(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SCD4X_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(bus_handle, &dev_config, &s_scd4x_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SCD4x device to I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    // Start periodic measurement on SCD4x (command 0x21B1)
    uint8_t cmd_start[2] = {0x21, 0xB1};
    err = i2c_master_transmit(s_scd4x_handle, cmd_start, 2, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send Start Periodic Measurement command to SCD4x: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "SCD4x CO2 sensor initialized successfully on I2C (SDA:%d, SCL:%d)", I2C_SDA_PIN, I2C_SCL_PIN);
    return ESP_OK;
}

// Read CO2 concentration from SCD4x
static esp_err_t read_scd4x_co2(float *co2_val)
{
    if (!s_scd4x_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    // Command to read measurement: 0xEC05
    uint8_t cmd_read[2] = {0xEC, 0x05};
    uint8_t rx_data[9] = {0};
    
    // Transmit command and receive 9 bytes of response
    esp_err_t err = i2c_master_transmit_receive(s_scd4x_handle, cmd_read, 2, rx_data, 9, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C transfer error reading SCD4x: %s", esp_err_to_name(err));
        return err;
    }

    // Validate CO2 data CRC (bytes 0, 1 vs byte 2)
    if (scd4x_crc8(rx_data, 2) != rx_data[2]) {
        ESP_LOGE(TAG, "SCD4x CO2 CRC mismatch!");
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t co2_raw = (rx_data[0] << 8) | rx_data[1];
    *co2_val = (float)co2_raw;
    return ESP_OK;
}

// Initialize ADC for Ammonia sensor
static esp_err_t init_ammonia_adc(void)
{
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_config, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    // GPIO1 is ADC1_CH0
    err = adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_0, &chan_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Ammonia sensor ADC initialized on GPIO%d (ADC1_CH0)", AMMONIA_ADC_PIN);
    return ESP_OK;
}

// Read Ammonia level from MQ-137 / electrochemical sensor
static esp_err_t read_ammonia_ppm(float *ammonia_val)
{
    if (!s_adc_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    int raw_val = 0;
    esp_err_t err = adc_oneshot_read(s_adc_handle, ADC_CHANNEL_0, &raw_val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read raw ADC: %s", esp_err_to_name(err));
        return err;
    }

    // Convert raw ADC (0-4095) to equivalent Ammonia PPM.
    // Assuming a linear 0-3.3V sensor output for 0-100 PPM NH3:
    // Voltage = raw_val * 3.3 / 4095.0
    // PPM = Voltage * (100 / 3.3) = raw_val * 100.0 / 4095.0
    *ammonia_val = ((float)raw_val * 100.0f) / 4095.0f;
    return ESP_OK;
}

// Initialize Ventilation Fan relay GPIO pin
static void init_fan_relay(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FAN_RELAY_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(FAN_RELAY_PIN, s_fan_state);
    ESP_LOGI(TAG, "Ventilation Fan Relay initialized on GPIO%d (initial state: %s)", FAN_RELAY_PIN, s_fan_state ? "ON" : "OFF");
}

static void real_gas_sensor_task(void *pvParameters)
{
    while (1) {
        float co2_read = 0.0f;
        float ammonia_read = 0.0f;

        /* 1. Read real Carbon Dioxide levels from SCD4x */
        if (read_scd4x_co2(&co2_read) == ESP_OK) {
            s_co2_ppm = co2_read;
        } else {
            ESP_LOGW(TAG, "Failed to read CO2 sensor, using last value: %.1f PPM", s_co2_ppm);
        }

        /* 2. Read real Ammonia level from ADC */
        if (read_ammonia_ppm(&ammonia_read) == ESP_OK) {
            s_ammonia_ppm = ammonia_read;
        } else {
            ESP_LOGW(TAG, "Failed to read Ammonia sensor, using last value: %.1f PPM", s_ammonia_ppm);
        }

        /* 3. Automatic Ventilation Actuator Logic */
        uint8_t target_fan_state = s_fan_state;
        if (s_ammonia_ppm > 25.0f || s_co2_ppm > 1500.0f) {
            target_fan_state = 1; // Turn fan ON
            ESP_LOGW(TAG, "Safety limit exceeded! (CO2: %.1f PPM, NH3: %.1f PPM). Triggering ventilation fan!", s_co2_ppm, s_ammonia_ppm);
        } else if (s_ammonia_ppm < 15.0f && s_co2_ppm < 1000.0f) {
            target_fan_state = 0; // Turn fan OFF
        }

        if (target_fan_state != s_fan_state) {
            s_fan_state = target_fan_state;
            gpio_set_level(FAN_RELAY_PIN, s_fan_state);
            ESP_LOGI(TAG, "Local Actuator: Ventilation Fan is now %s", s_fan_state ? "ON" : "OFF");
            
            // Update ZCL On/Off attribute
            esp_zigbee_lock_acquire(portMAX_DELAY);
            ezb_zcl_set_attr_value(ESP_ZIGBEE_HA_GAS_SENSOR_EP_ID,
                                   EZB_ZCL_CLUSTER_ID_ON_OFF,
                                   EZB_ZCL_CLUSTER_SERVER,
                                   EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                                   EZB_ZCL_STD_MANUF_CODE,
                                   (uint8_t *)&s_fan_state,
                                   false);
            esp_zigbee_lock_release();

            // Report the Fan State
            ezb_zcl_report_attr_cmd_t report_fan = {
                .cmd_ctrl = {
                    .fc.direction = EZB_ZCL_CMD_DIRECTION_TO_CLI,
                    .dst_addr.addr_mode = EZB_ADDR_MODE_NONE,
                    .src_ep = ESP_ZIGBEE_HA_GAS_SENSOR_EP_ID,
                    .cluster_id = EZB_ZCL_CLUSTER_ID_ON_OFF,
                },
                .payload = {
                    .attr_id = EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                }
            };
            esp_zigbee_lock_acquire(portMAX_DELAY);
            ezb_zcl_report_attr_cmd_req(&report_fan);
            esp_zigbee_lock_release();
        }

        ESP_LOGI(TAG, "Poultry Gas Telemetry: CO2 = %.1f PPM | Ammonia = %.1f PPM | Fan = %s",
                 s_co2_ppm, s_ammonia_ppm, s_fan_state ? "ON" : "OFF");

        /* 4. Update and Report CO2 MeasuredValue attribute */
        esp_zigbee_lock_acquire(portMAX_DELAY);
        ezb_zcl_set_attr_value(ESP_ZIGBEE_HA_GAS_SENSOR_EP_ID,
                               EZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT,
                               EZB_ZCL_CLUSTER_SERVER,
                               EZB_ZCL_ATTR_CARBON_DIOXIDE_MEASUREMENT_MEASURED_VALUE_ID,
                               EZB_ZCL_STD_MANUF_CODE,
                               (uint8_t *)&s_co2_ppm,
                               false);
        esp_zigbee_lock_release();

        ezb_zcl_report_attr_cmd_t report_co2 = {
            .cmd_ctrl = {
                .fc.direction = EZB_ZCL_CMD_DIRECTION_TO_CLI,
                .dst_addr.addr_mode = EZB_ADDR_MODE_NONE,
                .src_ep = ESP_ZIGBEE_HA_GAS_SENSOR_EP_ID,
                .cluster_id = EZB_ZCL_CLUSTER_ID_CARBON_DIOXIDE_MEASUREMENT,
            },
            .payload = {
                .attr_id = EZB_ZCL_ATTR_CARBON_DIOXIDE_MEASUREMENT_MEASURED_VALUE_ID,
            }
        };
        esp_zigbee_lock_acquire(portMAX_DELAY);
        ezb_zcl_report_attr_cmd_req(&report_co2);
        esp_zigbee_lock_release();

        /* 5. Update and Report Ammonia PresentValue attribute (Analog Input) */
        esp_zigbee_lock_acquire(portMAX_DELAY);
        ezb_zcl_set_attr_value(ESP_ZIGBEE_HA_GAS_SENSOR_EP_ID,
                               EZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
                               EZB_ZCL_CLUSTER_SERVER,
                               EZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
                               EZB_ZCL_STD_MANUF_CODE,
                               (uint8_t *)&s_ammonia_ppm,
                               false);
        esp_zigbee_lock_release();

        ezb_zcl_report_attr_cmd_t report_ammonia = {
            .cmd_ctrl = {
                .fc.direction = EZB_ZCL_CMD_DIRECTION_TO_CLI,
                .dst_addr.addr_mode = EZB_ADDR_MODE_NONE,
                .src_ep = ESP_ZIGBEE_HA_GAS_SENSOR_EP_ID,
                .cluster_id = EZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
            },
            .payload = {
                .attr_id = EZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
            }
        };
        esp_zigbee_lock_acquire(portMAX_DELAY);
        ezb_zcl_report_attr_cmd_req(&report_ammonia);
        esp_zigbee_lock_release();

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void deferred_driver_init(void)
{
    ESP_LOGI(TAG, "Initializing gas sensor drivers");
    
    // Initialize Ventilation Fan relay GPIO
    init_fan_relay();

    // Initialize Sensirion SCD4x I2C CO2 sensor
    if (init_scd4x_sensor() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize real SCD4x CO2 sensor");
    }

    // Initialize Ammonia ADC sensor
    if (init_ammonia_adc() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize real Ammonia ADC sensor");
    }

    xTaskCreate(real_gas_sensor_task, "real_gas_sensor", 4096, NULL, 5, NULL);
}

static void esp_zigbee_alarm_bdb_commissioning(alarm_timer_arg_t arg)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    (void)ezb_bdb_start_top_level_commissioning(arg);
    esp_zigbee_lock_release();
}

static bool esp_zigbee_app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);

    switch (signal_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            deferred_driver_init();
            ESP_LOGI(TAG, "Device started up in%s factory-reset mode", ezb_bdb_is_factory_new() ? "" : " non");
            if (ezb_bdb_is_factory_new()) {
                ESP_ERROR_CHECK(ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING));
            } else {
                ESP_LOGI(TAG, "Device rejoined network successfully");
            }
        } else {
            ESP_LOGW(TAG, "The %s failed with status(0x%02x), retrying in 1s", ezb_app_signal_to_string(signal_type), status);
            alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_INITIALIZATION, 1000);
        }
    } break;
    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ezb_extpanid_t extended_pan_id;
            ezb_nwk_get_extended_panid(&extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully: PAN ID(0x%04hx, EXT: 0x%llx), Channel(%d), Short Address(0x%04hx)",
                     ezb_nwk_get_panid(), extended_pan_id.u64, ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
        } else {
            ESP_LOGW(TAG, "Failed to join network with status(0x%02x), retrying steering", status);
            alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
    } break;
    default:
        ESP_LOGI(TAG, "Zigbee APP Signal: %s(type: 0x%02x)", ezb_app_signal_to_string(signal_type), signal_type);
        break;
    }
    return true;
}

static void zcl_core_set_attr_value_handler(ezb_zcl_set_attr_value_message_t *message)
{
    ESP_RETURN_ON_FALSE(message, , TAG, "message is empty");
    ESP_LOGI(TAG, "ZCL SetAttributeValue message for endpoint(%d) cluster(0x%04x) with status(0x%02x)",
             message->info.dst_ep, message->info.cluster_id, message->info.status);

    if (message->info.cluster_id == EZB_ZCL_CLUSTER_ID_ON_OFF) {
        uint8_t command_state = *(uint8_t *)message->in.attribute.data.value;
        s_fan_state = command_state;
        gpio_set_level(FAN_RELAY_PIN, s_fan_state);
        ESP_LOGI(TAG, "Actuator: Ventilation Fan turned %s via ZCL write", s_fan_state ? "ON" : "OFF");
    }
}

static void esp_zigbee_zcl_core_action_handler(ezb_zcl_core_action_callback_id_t callback_id, void *message)
{
    switch (callback_id) {
    case EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID:
        zcl_core_set_attr_value_handler(message);
        break;
    default:
        ESP_LOGW(TAG, "ZCL Core Action callback ID: 0x%04lx", callback_id);
        break;
    }
}

esp_err_t esp_zigbee_create_zha_gas_sensor_device(void)
{
    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    ezb_af_ep_config_t ep_config = {
        .ep_id = ESP_ZIGBEE_HA_GAS_SENSOR_EP_ID,
        .app_profile_id = EZB_AF_HA_PROFILE_ID,
        .app_device_id = EZB_ZHA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    ezb_af_ep_desc_t ep_desc = ezb_af_create_endpoint_desc(&ep_config);

    /* 1. Basic Cluster Server */
    ezb_zcl_cluster_desc_t basic_desc = ezb_zcl_basic_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    ezb_zcl_basic_cluster_desc_add_attr(basic_desc, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)ESP_MANUFACTURER_NAME);
    ezb_zcl_basic_cluster_desc_add_attr(basic_desc, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)ESP_MODEL_IDENTIFIER);
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, basic_desc));

    /* 2. Identify Cluster Server */
    ezb_zcl_cluster_desc_t identify_desc = ezb_zcl_identify_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, identify_desc));

    /* 3. Carbon Dioxide Measurement Cluster Server */
    ezb_zcl_carbon_dioxide_measurement_cluster_server_config_t co2_cfg = {
        .measured_value = 400.0f,
        .min_measured_value = 0.0f,
        .max_measured_value = 10000.0f,
    };
    ezb_zcl_cluster_desc_t co2_desc = ezb_zcl_carbon_dioxide_measurement_create_cluster_desc(&co2_cfg, EZB_ZCL_CLUSTER_SERVER);
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, co2_desc));

    /* 4. Analog Input Cluster Server (representing Ammonia NH3) */
    ezb_zcl_analog_input_cluster_server_config_t ammonia_cfg = {
        .out_of_service = false,
        .present_value = 5.0f,
        .status_flags = 0,
    };
    ezb_zcl_cluster_desc_t ammonia_desc = ezb_zcl_analog_input_create_cluster_desc(&ammonia_cfg, EZB_ZCL_CLUSTER_SERVER);
    
    // Set application type to PPM
    uint32_t app_type = EZB_ZCL_ANALOG_INPUT_APPLICATION_TYPE_PPM;
    ESP_ERROR_CHECK(ezb_zcl_analog_input_cluster_desc_add_attr(ammonia_desc, EZB_ZCL_ATTR_ANALOG_INPUT_APPLICATION_TYPE_ID, &app_type));
    
    // Set units to PPM (value: 96)
    uint16_t units = 96;
    ESP_ERROR_CHECK(ezb_zcl_analog_input_cluster_desc_add_attr(ammonia_desc, EZB_ZCL_ATTR_ANALOG_INPUT_ENGINEERING_UNITS_ID, &units));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, ammonia_desc));

    /* 5. On/Off Cluster Server (representing local ventilation actuator) */
    ezb_zcl_cluster_desc_t fan_desc = ezb_zcl_on_off_create_cluster_desc(NULL, EZB_ZCL_CLUSTER_SERVER);
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, fan_desc));

    /* Register ep to device and device to stack */
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(dev_desc, ep_desc));
    ESP_ERROR_CHECK(ezb_af_device_desc_register(dev_desc));

    ezb_zcl_core_action_handler_register(esp_zigbee_zcl_core_action_handler);

    return ESP_OK;
}

static void esp_zigbee_stack_main_task(void *pvParameters)
{
    esp_zigbee_config_t zigbee_config = ESP_ZIGBEE_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_zigbee_init(&zigbee_config));

    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(ESP_ZIGBEE_PRIMARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(ESP_ZIGBEE_SECONDARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(esp_zigbee_app_signal_handler));

    ESP_ERROR_CHECK(esp_zigbee_create_zha_gas_sensor_device());

    ESP_ERROR_CHECK(esp_zigbee_start(false));

    esp_zigbee_launch_mainloop();

    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(nvs_flash_init_partition(ESP_ZIGBEE_STORAGE_PARTITION_NAME));

    ESP_LOGI(TAG, "Starting Smart Poultry Ammonia/CO2 Gas Sensor & Actuator Node");
    xTaskCreate(esp_zigbee_stack_main_task, "Zigbee_main", 4096 * 2, NULL, 5, NULL);
}
