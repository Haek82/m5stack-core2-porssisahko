# M5Stack Core2 Porssisahko - Electricity Price Display

Real-time Finnish electricity spot price display for the M5Stack Core2. Fetches 15-minute interval pricing data from the [Porssisahko.net](https://api.porssisahko.net) API and visualizes today's and tomorrow's prices on the built-in 320x240 LCD.

## Features

- **Two display pages** — tap the touchscreen to toggle between:
  - **Chart view** — current price + 24h bar chart with tomorrow's forecast
  - **Big price view** — full-screen current price for at-a-glance reading
- **Color-coded prices** (green / yellow / red) based on configurable thresholds
- **15-minute granularity** for the current price slot
- **Tomorrow's prices** shown alongside today when available (typically after 14:00 Finnish time)
- **Automatic refresh** — display updates every 60 s, data re-fetched every hour
- **NTP time sync** with Helsinki timezone and DST support

## Hardware

- [M5Stack Core2](https://docs.m5stack.com/en/core/core2) (ESP32, 320x240 LCD, WiFi)

## Prerequisites

- [PlatformIO](https://platformio.org/) (standalone CLI or VSCode extension)
- A 2.4 GHz WiFi network

## Getting Started

1. **Clone the repository**

   ```bash
   git clone https://github.com/<your-user>/m5stack-core2-porssisahko.git
   cd m5stack-core2-porssisahko
   ```

2. **Configure WiFi credentials**

   Edit `include/config.h` and set your network name and password:

   ```cpp
   #define WIFI_SSID     "MyNetwork"
   #define WIFI_PASSWORD "MyPassword"
   ```

3. **Build and upload**

   ```bash
   pio run -t upload
   ```

4. **Monitor serial output** (optional)

   ```bash
   pio device monitor
   ```

## Configuration

All user-configurable settings are in [`include/config.h`](include/config.h):

| Define | Default | Description |
|---|---|---|
| `WIFI_SSID` | `"SSID"` | WiFi network name |
| `WIFI_PASSWORD` | `"PASSWORD"` | WiFi password |
| `PRICE_LOW` | `10.0` | Threshold below which price is shown in **green** (c/kWh) |
| `PRICE_HIGH` | `20.0` | Threshold above which price is shown in **red** (c/kWh) |
| `FETCH_INTERVAL_MS` | `3600000` | How often to re-fetch prices from the API (ms) |

Prices between `PRICE_LOW` and `PRICE_HIGH` are shown in yellow/amber.

## Display Pages

Tap anywhere on the touchscreen to toggle between the two pages.

### Chart View (default)

```
+----------------------------------+
| PORSSISAHKO  FI           14:35  |  <- header + clock
|----------------------------------|
|          12.34                   |  <- current 15-min price (color-coded)
|                        c/kWh    |
|          klo 14:15               |  <- current slot label
|----------------------------------|
| 20|  ██                          |  <- bar chart (today + tomorrow)
| 10|  ████ ██   ██     | ██ ████ |     white bar = current hour
|  0|████████████████████|█████████|     vertical line = day separator
|   00  06  12  18  00  06  12  18 |  <- hour labels
+----------------------------------+
```

### Big Price View

```
+----------------------------------+
| PORSSISAHKO  FI           14:35  |  <- header + clock
|----------------------------------|
|                                  |
|           12.34                  |  <- large price (color-coded)
|                                  |
|           c/kWh                  |
|                                  |
|          klo 14:15               |  <- current slot label
|                                  |
+----------------------------------+
```

## Project Structure

```
m5stack-core2-porssisahko/
├── include/
│   └── config.h          # WiFi credentials and price thresholds
├── src/
│   └── main.cpp          # Application source code
├── platformio.ini        # PlatformIO build configuration
└── README.md
```

## Dependencies

Managed automatically by PlatformIO:

| Library | Version | Purpose |
|---|---|---|
| [M5Unified](https://github.com/m5stack/M5Unified) | >= 0.2.2 | Hardware abstraction (display, touch, buttons) |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | >= 7.2.0 | JSON parsing for API responses |

## API

The application fetches data from:

```
GET https://api.porssisahko.net/v2/latest-prices.json
```

Response format:

```json
{
  "prices": [
    { "price": 5.42, "startDate": "2025-02-24T22:00:00.000Z" },
    { "price": 4.87, "startDate": "2025-02-24T22:15:00.000Z" }
  ]
}
```

- `price` — electricity price in c/kWh
- `startDate` — start of the 15-minute slot in UTC (ISO 8601)

## License

This project does not currently include a license file. Please add one if you plan to distribute the code.
