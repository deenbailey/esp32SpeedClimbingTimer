# esp32SpeedClimbingTimer
An ESP32 based timer for speed climbing competitions


#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <FastLED.h>
#include <DNSServer.h>

Here are the package versions:

#include <WiFi.h>
name=WiFi
version=3.3.0
author=Hristo Gochkov
maintainer=Hristo Gochkov <hristo@espressif.com>
sentence=Enables network connection (local and Internet) using the ESP32 built-in WiFi.
paragraph=With this library you can instantiate Servers, Clients and send/receive UDP packets through WiFi. The shield can connect either to open or encrypted networks. The IP address can be assigned statically or through a DHCP. The library can also manage DNS.
category=Communication
url=
architectures=esp32


#include <WebServer.h>
name=WebServer
version=3.3.0
author=Ivan Grokhotkov
maintainer=Ivan Grokhtkov <ivan@esp8266.com>
sentence=Simple web server library
paragraph=The library supports HTTP GET and POST requests, provides argument parsing, handles one client at a time.
category=Communication
url=
architectures=esp32



#include <ArduinoJson.h>
name=ArduinoJson
version=7.4.2
author=Benoit Blanchon <blog.benoitblanchon.fr>
maintainer=Benoit Blanchon <blog.benoitblanchon.fr>
sentence=A simple and efficient JSON library for embedded C++.
paragraph=⭐ 6953 stars on GitHub! Supports serialization, deserialization, MessagePack, streams, filtering, and more. Fully tested and documented.
category=Data Pro[Uploading library.properties…]()
cessing
url=https://arduinojson.org/?utm_source=meta&utm_medium=library.properties
architectures=*
repository=https://github.com/bblanchon/ArduinoJson.git
license=MIT



#include <WebSocketsServer.h>
name=WebSockets
version=2.7.0
author=Markus Sattler
maintainer=Markus Sattler
sentence=WebSockets for Arduino (Server + Client)
paragraph=use 2.x.x for ESP and 1.3 for AVR
category=Communication
url=https://github.com/Links2004/arduinoWebSockets
architectures=*


#include <FastLED.h>
name=FastLED
version=3.10.2
author=Daniel Garcia
maintainer=Daniel Garcia <dgarcia@fastled.io>
sentence=Multi-platform library for controlling dozens of different types of LEDs along with optimized math, effect, and noise functions.
paragraph=FastLED is a fast, efficient, easy-to-use Arduino library for programming addressable LED strips and pixels such as WS2810, WS2811, LPD8806, Neopixel and more. FastLED also provides high-level math functions that can be used for generative art and graphics.
category=Display
url=https://github.com/FastLED/FastLED
architectures=*
includes=FastLED.h


#include <DNSServer.h>
name=DNSServer
version=3.3.0
author=Kristijan Novoselić
maintainer=Kristijan Novoselić, <kristijan.novoselic@gmail.com>
sentence=A simple DNS server for ESP32.
paragraph=This library implements a simple DNS server.
category=Communication
url=
architectures=esp32
