# ESP Zigbee Gateway & Sensor Nodes

CI/CD managed builds for the **ESP Zigbee Gateway**, **Temperature Sensor Node**, **Light Sensor Node**, and **Ammonia/CO2 Gas Sensor Node** examples,
targeting M5Stack **CoreS3** (ESP32-S3) + **Module Gateway H2** (ESP32-H2).

![Build Status](https://github.com/mahdi-benhassen/esp-zigbee-project/actions/workflows/build.yml/badge.svg)

---

## Hardware

| Board | Chip | Role |
|---|---|---|
| CoreS3 | ESP32-S3 | Gateway host |
| Module Gateway H2 | ESP32-H2 | RCP (Gateway mode) / Standalone Sensor Nodes (End Device) |
| ESP32 Downloader | — | Flashing tool |

---

## Repository Structure

```
esp-zigbee-project/
├── .github/
│   └── workflows/
│       ├── build.yml       ← CI: builds all firmware on every push/PR
│       └── release.yml     ← Release: packages & publishes .bin zips on version tag
├── config/
│   ├── gateway/
│   │   └── sdkconfig.defaults      ← Gateway pin/WiFi config (CoreS3)
│   ├── rcp/
│   │   └── sdkconfig.defaults      ← RCP config (ESP32-H2)
│   ├── node_temp_sensor/           ← Standalone IDF project (ESP32-H2)
│   │   ├── CMakeLists.txt
│   │   ├── partitions.csv
│   │   ├── sdkconfig.defaults
│   │   └── main/                   ← Temperature sensor application source
│   ├── node_light_sensor/          ← Standalone IDF project (ESP32-H2)
│   │   ├── CMakeLists.txt
│   │   ├── partitions.csv
│   │   ├── sdkconfig.defaults
│   │   └── main/                   ← Light sensor application source
│   └── node_gas_sensor/            ← Standalone IDF project (ESP32-H2)
│       ├── CMakeLists.txt
│       ├── partitions.csv
│       ├── sdkconfig.defaults
│       └── main/                   ← Ammonia/CO2 gas sensor & fan actuator source
├── docs/                           ← Reference PDFs (gateway / border router tutorials)
└── README.md
```

> **Note:** the node projects reference shared utility components (`utils/switch_driver`,
> `utils/temp_sensor_driver`, `utils/alarm_timer`) via relative paths. The `utils/` directory
> is gitignored and is copied from the ESP-Zigbee-SDK at build time (CI does this
> automatically — see [Flashing Firmware Locally](#flashing-firmware-locally) for local setup).

---

## Quick Start — Create & Push to GitHub

### Step 1 — Create a new GitHub repository

1. Go to https://github.com/new
2. Name it e.g. `esp-zigbee-project`
3. Leave it **empty** (no README, no .gitignore)
4. Click **Create repository**

### Step 2 — Clone this template locally

```bash
# Clone this repo
git clone https://github.com/mahdi-benhassen/esp-zigbee-project.git
cd esp-zigbee-project
```

Or start from scratch:

```bash
mkdir esp-zigbee-project && cd esp-zigbee-project
git init
git branch -M main
git remote add origin https://github.com/mahdi-benhassen/esp-zigbee-project.git
```

### Step 3 — Add your Wi-Fi credentials (Gateway only)

Edit `config/gateway/sdkconfig.defaults` and replace the placeholder values:

```
CONFIG_EXAMPLE_WIFI_SSID="MyHomeNetwork"
CONFIG_EXAMPLE_WIFI_PASSWORD="MyPassword123"
```

> **Security tip:** Do NOT commit real credentials to a public repo.
> Use a GitHub Secret instead — see [Using Secrets](#using-secrets-for-wifi-credentials) below.

### Step 4 — Commit and push

```bash
git add .
git commit -m "Initial commit: Zigbee GW + NCP workflows"
git push -u origin main
```

GitHub Actions will automatically trigger the `build.yml` workflow.
Watch it at: `https://github.com/mahdi-benhassen/esp-zigbee-project/actions`

### Step 5 — Publish a release

```bash
git tag v1.0.0
git push origin v1.0.0
```

This triggers `release.yml`, which builds all five firmware packages and attaches
them as `.zip` files to a GitHub Release. Download from the **Releases** page.

---

## CI/CD Workflows

### `build.yml` — Runs on every push / pull request

A single `build-all` job runs every build step sequentially in one container
(`espressif/idf:release-v5.3`), then uploads a combined artifact:

```
build-all (ubuntu-latest, container: espressif/idf:release-v5.3)
│
├── Clone ESP-Zigbee-SDK + prepare utils/
├── Build RCP            (ESP32-H2)  ►  build_artifacts/rcp/
├── Build Gateway        (ESP32-S3)  ►  build_artifacts/gateway/
├── Build Node Temp      (ESP32-H2)  ►  build_artifacts/node_temp_sensor/
├── Build Node Light     (ESP32-H2)  ►  build_artifacts/node_light_sensor/
└── Build Node Gas       (ESP32-H2)  ►  build_artifacts/node_gas_sensor/
                                        │
                                        └── upload-artifact: zigbee-firmware-binaries
```

### `release.yml` — Runs on `git tag v*.*.*`

Same build steps as `build.yml`, but also:
- Packages each firmware's binaries into a `.zip` file
- Uploads the zips as the `release-zips` artifact
- A second `publish-release` job creates a GitHub Release with all five zips attached
- Auto-generates release notes from commits

---

## Pin Wiring Reference

### Gateway Mode

| Signal | CoreS3 Pin | Module Gateway H2 Pin |
|---|---|---|
| UART TX → RCP RX | GPIO 10 | GPIO 17 |
| UART RX ← RCP TX | GPIO 17 | GPIO 10 |
| RCP Reset | GPIO 7 | RST |
| RCP Boot | GPIO 18 | BOOT |

### NCP Mode

| Signal | CoreS3 (HOST) Pin | Module Gateway H2 (NCP) Pin |
|---|---|---|
| UART TX → NCP RX | GPIO 17 | GPIO 23 |
| UART RX ← NCP TX | GPIO 10 | GPIO 24 |

---

## Flashing Firmware Locally

> Requires ESP-IDF v5.3+. Run `. ./export.sh` first.

> **Note — Sensor Nodes only:** the node projects under `config/node_*/` reference
> shared utility components (`utils/switch_driver`, `utils/temp_sensor_driver`,
> `utils/alarm_timer`) via relative paths. These `utils/` are gitignored, so copy
> them from the ESP-Zigbee-SDK before building a node locally:
> ```bash
> git clone --recursive https://github.com/espressif/esp-zigbee-sdk.git
> cp -r esp-zigbee-sdk/examples/utils ./utils
> ```

### Gateway (2 chips)

```bash
# 1. Flash RCP onto Module Gateway H2
cd $IDF_PATH/examples/openthread/ot_rcp
idf.py set-target esp32h2
idf.py build
idf.py erase_flash flash --port /dev/ttyUSB0   # H2 port

# 2. Flash Gateway onto CoreS3
cd esp-zigbee-sdk/examples/zigbee_gateway
idf.py set-target esp32s3
idf.py build
idf.py erase_flash flash --port /dev/ttyUSB1   # S3 port
```

### Sensor Nodes (ESP32-H2 Standalone)

#### Temperature Sensor Node
```bash
# Flash Temperature Sensor onto Module Gateway H2
cd config/node_temp_sensor
idf.py set-target esp32h2
idf.py build
idf.py erase_flash flash --port /dev/ttyUSB0
```

#### Light Sensor Node
```bash
# Flash Light Sensor onto Module Gateway H2
cd config/node_light_sensor
idf.py set-target esp32h2
idf.py build
idf.py erase_flash flash --port /dev/ttyUSB0
```

#### Gas (Ammonia/CO2) Sensor & Actuator Node
```bash
# Flash Gas Sensor onto Module Gateway H2
cd config/node_gas_sensor
idf.py set-target esp32h2
idf.py build
idf.py erase_flash flash --port /dev/ttyUSB0
```

### Monitor logs

```bash
idf.py monitor --port /dev/ttyUSB0    # 115200 bps
```

Expected Gateway log output:
- RCP firmware version check ✅
- Wi-Fi connected ✅
- Zigbee network created ✅
- Network open for device joining ✅

Expected Sensor Node log output:
- Start ESP Zigbee Stack ✅
- Initialize Zigbee stack ✅
- Joined network successfully ✅
- Simulated Sensor updates or Sent ZCL Report Attribute request ✅

---

## Using Secrets for Wi-Fi Credentials

Instead of hardcoding credentials, store them as GitHub Secrets:

1. Go to your repo → **Settings** → **Secrets and variables** → **Actions**
2. Click **New repository secret**
3. Add `WIFI_SSID` and `WIFI_PASSWORD`

Then update `build.yml` and `release.yml` to inject them at build time:

```yaml
- name: Inject Wi-Fi credentials
  run: |
    sed -i 's/CONFIG_EXAMPLE_WIFI_SSID=.*/CONFIG_EXAMPLE_WIFI_SSID="${{ secrets.WIFI_SSID }}"/' \
      esp-zigbee-sdk/examples/esp_zigbee_gateway/sdkconfig.defaults
    sed -i 's/CONFIG_EXAMPLE_WIFI_PASSWORD=.*/CONFIG_EXAMPLE_WIFI_PASSWORD="${{ secrets.WIFI_PASSWORD }}"/' \
      esp-zigbee-sdk/examples/esp_zigbee_gateway/sdkconfig.defaults
```

Add this step **before** the `esp-idf-ci-action` step in the gateway job.

---

## ESP-IDF Version

Both workflows use the **ESP-IDF v5.3** release line via the
`espressif/idf:release-v5.3` container image. For local development, install
ESP-IDF v5.3 or newer. To upgrade the CI, change the `container:` image in each
workflow file.

---

## References

- [ESP-Zigbee-SDK](https://github.com/espressif/esp-zigbee-sdk)
- [ESP-IDF](https://github.com/espressif/esp-idf)
- [espressif/esp-idf-ci-action](https://github.com/espressif/esp-idf-ci-action)
- M5Stack Module Gateway H2 Tutorial (zigbee_gateway.pdf / zigbee_ncp.pdf)
