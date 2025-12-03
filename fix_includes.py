#!/usr/bin/env python3
with open('src/main.cpp', 'r') as f:
    lines = f.readlines()

# Replace first 7 lines with correct order
new_header = [
    '#include <Arduino.h>\n',
    '#include <WiFi.h>\n',
    '#include <ESPmDNS.h>\n',
    '#include <WiFiUdp.h>\n',
    '#include <ArduinoOTA.h>\n',
    '#include <Preferences.h>\n',
    '#include <Update.h>\n'
]

# Write back
with open('src/main.cpp', 'w') as f:
    f.writelines(new_header)
    f.writelines(lines[7:])

print('File updated successfully')
