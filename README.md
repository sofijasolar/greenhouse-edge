# greenhouse-edge

## Greenhouse Monitoring – Edge solution.

Handles sensor data collection and MQTT communication for greenhouse monitoring, providing real-time insights on temperature, humidity, light, and door status. The project is written in C++ using the PlatformIO IDE with Arduino framework.

## Installation

Create a Credentials.h file in 'src' folder. Define variables for your:
 - SSID (WiFi)
 - PASSWORD (WiFi password)
 - MQTT_USERNAME 
 - MQTT_PASSWORD
 - MQTT_SERVER 
 - and PUSHSAFER_KEY (if you want to be notified using PushSafer).

## Run

- `pio run --target upload`
- `pio device monitor`


