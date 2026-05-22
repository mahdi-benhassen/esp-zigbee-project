| Supported Targets | ESP32-H2 | ESP32-C6 | ESP32-C5 |
| ----------------- | -------- | -------- | -------- |

# Light Sensor Example

This example demonstrates how to configure a Home Automation light sensor on a Zigbee End Device.

## Hardware Required

* One 802.15.4 enabled development board (e.g., ESP32-H2 or ESP32-C6) running this example.
* A second board running as a Zigbee Coordinator (e.g. Gateway)

## Configure the project

Before project configuration and build, make sure to set the correct chip target using `idf.py set-target TARGET` command.

## Erase the NVRAM

Before flash it to the board, it is recommended to erase NVRAM if user doesn't want to keep the previous examples or other projects stored info
using `idf.py -p PORT erase-flash`

## Build and Flash

Build the project, flash it to the board, and start the monitor tool to view the serial output by running `idf.py -p PORT flash monitor`.

(To exit the serial monitor, type ``Ctrl-]``.)

## Application Functions

- When the program starts, the board, acting as a Zigbee End Device with the `Home Automation Light Sensor` function, will attempt to detect an available Zigbee network.

- The board updates the illuminance attribute of the `Illuminance Measurement` cluster based on the simulated lux value.

- By clicking the `BOOT` button on this board, the board will actively report the current measured illuminance to the bound device.
