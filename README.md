# ESP Zigbee Gateway & NCP Node

CI/CD managed builds for the **ESP Zigbee Gateway** and **ESP Zigbee NCP** examples,
targeting M5Stack **CoreS3** (ESP32-S3) + **Module Gateway H2** (ESP32-H2).

![Build Status](https://github.com/YOUR_USERNAME/YOUR_REPO/actions/workflows/build.yml/badge.svg)

---

## Hardware

| Board | Chip | Role |
|---|---|---|
| CoreS3 | ESP32-S3 | Gateway host / NCP host |
| Module Gateway H2 | ESP32-H2 | RCP (Gateway mode) / NCP (Node mode) |
| ESP32 Downloader | — | Flashing tool |

---

## Repository Structure

```
esp-zigbee-project/
├── .github/
│   └── workflows/
│       ├── build.yml       ← CI: builds all 4 firmware on every push/PR
│       └── release.yml     ← Release: packages & publishes .bin zips on version tag
├── config/
│   ├── gateway/
│   │   └── sdkconfig.defaults   ← Gateway pin/WiFi config (CoreS3)
│   ├── rcp/
│   │   └── sdkconfig.defaults   ← RCP config (ESP32-H2)
│   ├── ncp/
│   │   └── sdkconfig.defaults   ← NCP pin config (ESP32-H2)
│   └── host/
│       └── sdkconfig.defaults   ← Host pin config (CoreS3)
└── README.md
```

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
git clone https://github.com/YOUR_USERNAME/esp-zigbee-project.git
cd esp-zigbee-project
```

Or start from scratch:

```bash
mkdir esp-zigbee-project && cd esp-zigbee-project
git init
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/esp-zigbee-project.git
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
Watch it at: `https://github.com/YOUR_USERNAME/esp-zigbee-project/actions`

### Step 5 — Publish a release

```bash
git tag v1.0.0
git push origin v1.0.0
```

This triggers `release.yml`, which builds all four firmware packages and attaches
them as `.zip` files to a GitHub Release. Download from the **Releases** page.

---

## CI/CD Workflows

### `build.yml` — Runs on every push / pull request

```
push to main/develop
│
├── build-rcp    (ESP32-H2)   ─────────────────────────────────────────►  ✅ artifact: rcp-firmware
│                                                                          │
└── build-gateway (ESP32-S3)  needs: build-rcp ──────────────────────►  ✅ artifact: gateway-firmware

├── build-ncp    (ESP32-H2)   ─────────────────────────────────────────►  ✅ artifact: ncp-firmware
│                                                                          │
└── build-host   (ESP32-S3)   needs: build-ncp ──────────────────────►  ✅ artifact: host-firmware
```

### `release.yml` — Runs on `git tag v*.*.*`

Same jobs, but also:
- Packages binaries into `.zip` files per firmware type
- Creates a GitHub Release with all zips attached
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

> Requires ESP-IDF v5.3.1. Run `. ./export.sh` first.

### Gateway (2 chips)

```bash
# 1. Flash RCP onto Module Gateway H2
cd $IDF_PATH/examples/openthread/ot_rcp
idf.py set-target esp32h2
idf.py build
idf.py erase_flash flash --port /dev/ttyUSB0   # H2 port

# 2. Flash Gateway onto CoreS3
cd esp-zigbee-sdk/examples/esp_zigbee_gateway
idf.py set-target esp32s3
idf.py build
idf.py erase_flash flash --port /dev/ttyUSB1   # S3 port
```

### NCP Node (2 chips)

```bash
# 1. Flash NCP onto Module Gateway H2
cd esp-zigbee-sdk/examples/esp_zigbee_ncp
idf.py set-target esp32h2
idf.py build
idf.py erase_flash flash --port /dev/ttyUSB0

# 2. Flash Host onto CoreS3
cd esp-zigbee-sdk/examples/esp_zigbee_host
idf.py set-target esp32s3
idf.py build
idf.py erase_flash flash --port /dev/ttyUSB1
```

### Monitor logs

```bash
idf.py monitor --port /dev/ttyUSB1    # 115200 bps
```

Expected Gateway log output:
- RCP firmware version check ✅
- Wi-Fi connected ✅
- Zigbee network created ✅
- Network open for device joining ✅

Expected NCP log output:
- Zigbee stack initialized (NCP side) ✅
- Host connected to NCP ✅
- Zigbee network created in Coordinator mode ✅

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

Both workflows pin to **ESP-IDF v5.3.1** as recommended by the M5Stack tutorials.
To upgrade, change the `IDF_VERSION` env variable at the top of each workflow file.

---

## References

- [ESP-Zigbee-SDK](https://github.com/espressif/esp-zigbee-sdk)
- [ESP-IDF](https://github.com/espressif/esp-idf)
- [espressif/esp-idf-ci-action](https://github.com/espressif/esp-idf-ci-action)
- M5Stack Module Gateway H2 Tutorial (zigbee_gateway.pdf / zigbee_ncp.pdf)
