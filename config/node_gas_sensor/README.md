| Supported Targets | ESP32-H2 | ESP32-C6 | ESP32-C5 |
| ----------------- | -------- | -------- | -------- |

# Smart Poultry Ammonia & CO2 Gas Sensor Example

This example configures a Zigbee End Device that exposes a **Carbon Dioxide
Measurement** cluster and an **Analog Input** cluster (Ammonia, in PPM), and
drives a local ventilation fan through an **On/Off** cluster. It is designed for
smart-poultry air-quality monitoring with automatic ventilation actuation.

## Hardware Required

* One 802.15.4 enabled development board (e.g., ESP32-H2) running this example.
* A second board running as a Zigbee Coordinator (e.g. Gateway).
* **Sensirion SCD4x** CO2 sensor on I2C (SDA = GPIO10, SCL = GPIO11, address 0x62).
* **MQ-137 / electrochemical** Ammonia sensor on ADC1_CH0 (GPIO1).
* **Fan relay** on GPIO3 (active-high digital output).

## Pin Mapping

| Function | GPIO | Notes |
| --- | --- | --- |
| SCD4x I2C SDA | 10 | I2C_NUM_0 |
| SCD4x I2C SCL | 11 | I2C_NUM_0 |
| Ammonia ADC | 1 | ADC1_CH0, 12-bit, 12 dB attenuation |
| Fan relay | 3 | GPIO output |

## Configure the project

Before project configuration and build, make sure to set the correct chip target
using `idf.py set-target TARGET` command.

## Erase the NVRAM

Before flashing it to the board, it is recommended to erase NVRAM if you don't
want to keep the previous examples or other projects stored info using
`idf.py -p PORT erase-flash`.

## Build and Flash

Build the project, flash it to the board, and start the monitor tool to view the
serial output by running `idf.py -p PORT flash monitor`.

(To exit the serial monitor, type ``Ctrl-]``.)

## Application Functions

- On startup the board initializes the SCD4x CO2 sensor (I2C), the Ammonia ADC,
  and the fan relay GPIO, then joins a Zigbee network as an End Device.
- Every 5 seconds it reads CO2 (PPM) and Ammonia (PPM) and updates the
  `Carbon Dioxide Measurement` and `Analog Input` cluster attributes, then
  reports them to the bound coordinator.
- **Automatic ventilation:** the fan turns ON when Ammonia > 25 PPM or CO2 >
  1500 PPM, and turns OFF when Ammonia < 15 PPM and CO2 < 1000 PPM. The fan
  state is reflected in the local `On/Off` cluster and reported on change.
- The fan can also be toggled remotely by writing the `On/Off` cluster attribute
  from the coordinator.

```
I (xxx) ESP_ZIGBEE_GAS_SENSOR: SCD4x CO2 sensor initialized successfully on I2C (SDA:10, SCL:11)
I (xxx) ESP_ZIGBEE_GAS_SENSOR: Ammonia sensor ADC initialized on GPIO1 (ADC1_CH0)
I (xxx) ESP_ZIGBEE_GAS_SENSOR: Ventilation Fan Relay initialized on GPIO3 (initial state: OFF)
I (xxx) ESP_ZIGBEE_GAS_SENSOR: Joined network successfully: PAN ID(0x3e40, ...), Channel(13), Short Address(0xd9bc)
I (xxx) ESP_ZIGBEE_GAS_SENSOR: Poultry Gas Telemetry: CO2 = 612.0 PPM | Ammonia = 8.3 PPM | Fan = OFF
```

## Troubleshooting

For any technical queries, please open an
[issue](https://github.com/espressif/esp-zigbee-sdk/issues) on GitHub. We will
get back to you soon.
