# Irrigation Controller
Simple, minimalistic Arduino sketch for eight irrigation valves

<img src="pictures/IMG_1156.GIF" width="240">

Features
--------
 * Arduino-ESP32
 * MQTT client uses TLS and checks the Root-CA
 * Can use many WiFi SSIDs
 * Watchdog switches valves off after a long time if no other command was send
 * Only one valve active at a time, this prevents pressure loss
 * Accepts OTA updates via HTTP+username+password
 
