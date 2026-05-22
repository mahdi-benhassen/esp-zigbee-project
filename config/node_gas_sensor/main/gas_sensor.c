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
#include "switch_driver.h"
#include "alarm_timer.h"
#include "esp_zigbee.h"
#include "ezbee/zha.h"
#include "ezbee/zcl/cluster/carbon_dioxide_measurement_desc.h"
#include "ezbee/zcl/cluster/analog_input_desc.h"
#include "gas_sensor.h"
#include <math.h>

static const char *TAG = "ESP_ZIGBEE_GAS_SENSOR";

/* Default values for our simulated sensors */
static float s_co2_ppm = 400.0f;       // Fresh air base
static float s_ammonia_ppm = 5.0f;     // Clean coop base
static uint8_t s_fan_state = 0;        // Off

static void simulated_gas_sensor_task(void *pvParameters)
{
    bool co2_increasing = true;
    bool ammonia_increasing = true;

    while (1) {
        /* 1. Simulate Carbon Dioxide levels (400 PPM to 2000 PPM) */
        if (co2_increasing) {
            s_co2_ppm += 100.0f;
            if (s_co2_ppm >= 2000.0f) {
                co2_increasing = false;
            }
        } else {
            s_co2_ppm -= 100.0f;
            if (s_co2_ppm <= 400.0f) {
                co2_increasing = true;
            }
        }

        /* 2. Simulate Ammonia NH3 levels (5 PPM to 40 PPM) */
        if (ammonia_increasing) {
            s_ammonia_ppm += 2.5f;
            if (s_ammonia_ppm >= 40.0f) {
                ammonia_increasing = false;
            }
        } else {
            s_ammonia_ppm -= 2.5f;
            if (s_ammonia_ppm <= 5.0f) {
                ammonia_increasing = true;
            }
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
    xTaskCreate(simulated_gas_sensor_task, "sim_gas_sensor", 4096, NULL, 5, NULL);
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

    ESP_LOGI(TAG, "Starting Smart Poultry Ammonia/CO2 Gas Sensor & Actuator Node");
    xTaskCreate(esp_zigbee_stack_main_task, "Zigbee_main", 4096 * 2, NULL, 5, NULL);
}
