# mdSensor
ESP8266 with DHT22 based sensor

I created this project to install a battery powered sensor on my boat.
The main focus was on minimal power consumption
The device wakes up every 68 minutes (maximum deep sleep) and then runs on average 11 seconds before going in deep sleep again.
Power consumption is ~78mA awake and ~16uA in deep sleep.
It runs ~ 9 months on a single 18650 2600mAh battery
