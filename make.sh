#!/bin/bash

#Arduino IDE
ARDUINO_VERSION_TO_USE="1.8.16"

# Use this ESP32 compiler version
ESP32_TOOLCHAIN_VERSION_TO_USE="2.0.1"

# Install the following, additional libraries
ARDUINO_LIBRARIES_TO_USE="MQTT:2.5.0"
    
# Values for boards are derived from boards.txt (/tmp/arduino/portable/packages/esp32/hardware/esp32/1.0.3/) file
BOARD="esp32:esp32:esp32:PSRAM=disabled,PartitionScheme=default,CPUFreq=240,FlashMode=qio,FlashFreq=80,FlashSize=4M,DebugLevel=none"

#IDE folder
IDE_FOLDER="/tmp/arduino"
BUILD_FOLDER="build"

export PATH=$PATH:$IDE_FOLDER

#download, unpack and install Arduino-IDE + esp32 compiler + libs
if [ ! -d $IDE_FOLDER ]; then
	if [ ! -r arduino-$ARDUINO_VERSION_TO_USE-linux64.tar.xz ]; then
		wget https://downloads.arduino.cc/arduino-$ARDUINO_VERSION_TO_USE-linux64.tar.xz || exit 1
	fi

	tar xf arduino-$ARDUINO_VERSION_TO_USE-linux64.tar.xz || exit 1
	mv arduino-$ARDUINO_VERSION_TO_USE $IDE_FOLDER || exit 1
	
	#prepare toolchain
	arduino --preferences-file $IDE_FOLDER/portable/preferences.txt --pref "sketchbook.path=." --save-prefs
	arduino --preferences-file $IDE_FOLDER/portable/preferences.txt --pref "build.path=$BUILD_FOLDER" --save-prefs
	arduino --preferences-file $IDE_FOLDER/portable/preferences.txt --pref "boardsmanager.additional.urls=https://github.com/espressif/arduino-esp32/releases/download/$ESP32_TOOLCHAIN_VERSION_TO_USE/package_esp32_index.json" --save-prefs
	arduino --preferences-file $IDE_FOLDER/portable/preferences.txt --install-boards esp32:esp32
	arduino --preferences-file $IDE_FOLDER/portable/preferences.txt --install-library $ARDUINO_LIBRARIES_TO_USE
	arduino --preferences-file $IDE_FOLDER/portable/preferences.txt --pref "compiler.warning_level=all" --save-prefs
fi

#compile the INO file
arduino --preferences-file $IDE_FOLDER/portable/preferences.txt --verify --board $BOARD *.ino

#rm -rf $IDE_FOLDER

exit 0
