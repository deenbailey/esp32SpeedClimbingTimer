# esp32SpeedClimbingTimer

An ESP32 based timer for speed climbing competitions.

---

## Required Libraries

```cpp
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <FastLED.h>
#include <DNSServer.h>
```

## ESP32 Arduino Core

- **Version:** 3.3.6
- **Repository:** [https://github.com/espressif/arduino-esp32](https://github.com/espressif/arduino-esp32)

---

## Library Versions

### WiFi `v3.3.0`
- **Author:** Hristo Gochkov
- **Description:** Enables network connection using the ESP32 built-in WiFi. Supports Servers, Clients, and UDP packets. IP address can be assigned statically or via DHCP. Includes DNS management.
- **Architecture:** esp32

---

### WebServer `v3.3.0`
- **Author:** Ivan Grokhotkov
- **Description:** Simple web server library. Supports HTTP GET and POST requests, argument parsing, and handles one client at a time.
- **Architecture:** esp32

---

### ArduinoJson `v7.4.2`
- **Author:** Benoit Blanchon
- **Description:** A simple and efficient JSON library for embedded C++. Supports serialization, deserialization, MessagePack, streams, filtering, and more.
- **Repository:** [https://github.com/bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson)
- **License:** MIT
- **Architecture:** *

---

### WebSockets `v2.7.0`
- **Author:** Markus Sattler
- **Description:** WebSockets for Arduino (Server + Client). Use v2.x.x for ESP, v1.3 for AVR.
- **Repository:** [https://github.com/Links2004/arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets)
- **Architecture:** *

---

### FastLED `v3.10.2`
- **Author:** Daniel Garcia
- **Description:** Multi-platform library for controlling dozens of different types of LEDs (WS2810, WS2811, LPD8806, Neopixel, and more). Also provides high-level math functions for generative art and graphics.
- **Repository:** [https://github.com/FastLED/FastLED](https://github.com/FastLED/FastLED)
- **Architecture:** *

---

### DNSServer `v3.3.0`
- **Author:** Kristijan Novoselić
- **Description:** A simple DNS server for ESP32.
- **Architecture:** esp32
