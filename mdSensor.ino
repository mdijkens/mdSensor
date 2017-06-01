/*
	mdSensor - Copyright maurits@dijkens.com
	- Schematic: https://easyeda.com/normal/Schematic-17021e93dfe04dacb1a7bc42ed2b4cc4
	- Send battery, temperature and humidity every hour to data.sparkfun
	- Optimized for minimum poewr consumption; runs 9 months on single 18650 cell
	- Runs ~11sec, then deep sleep for ~68min
	- Stores statistics in RTC-memory
	- Connects to one of pre-defined WiFi networks
	- Remote updates via webserver
*/

#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include "DHT.h"

#define       SLEEP_SEC         4097    // Seconds sleep (max: 4097s ~ 68m19s)
#define       RETRY_SEC         4096    // Seconds sleep when errors/timeouts
// #define       DEBUG_SERIAL

#ifdef DEBUG_SERIAL
 #include <SoftwareSerial.h>
 #define S_PRINT(x)    Serial.print (x)
 #define S_PRINTLN(x)  Serial.println (x)
 #define S_WRITE(x)    Serial.write (x)
 #define S_PRINTF      Serial.printf
#else
 #define S_PRINT(x)
 #define S_PRINTLN(x) 
 #define S_WRITE(x)
 #define S_PRINTF(format, args...) ((void)0)
#endif
#define       LED_OFF           HIGH
#define       LED_ON            LOW
#define       BLUE_LED          2       // GPIO2 pin for ESP-12E blue led
#define       DHTTYPE           DHT22   // DHT11 type
#define       DHTDATA           12		  // DHT11 data pin
#define       DHTPOWER          13      // DHT11 power pin

struct {
  char          id[9];        //  9 bytes
  uint16_t      wakeups ;     //  2 bytes
  uint32_t      awakems;      //  4 bytes
} rtcData;                    //=15 bytes
float         temperature;
float         humidity;
float         battery;
unsigned long dhtStart;
int           sleepsec = SLEEP_SEC;
DHT           dht(DHTDATA, DHTTYPE);

void setup(void)
{
  #ifdef DEBUG_SERIAL
    Serial.begin(74880);
    delay(10);
    S_PRINTLN();
//    Serial.setDebugOutput(true);
  #endif
  setPin(DHTPOWER, HIGH);
  setPin(BLUE_LED, LED_OFF);
  setPin(DHTDATA, HIGH);
  
  dht.begin();
  dhtStart = millis();
  ESP.rtcUserMemoryRead(100, (uint32_t*) &rtcData, sizeof(rtcData));
  yield();
  if (strcmp(rtcData.id, "mdSensor") != 0) {
    strcpy(rtcData.id, "mdSensor");
    rtcData.wakeups = 0;
    rtcData.awakems = 0;
    S_PRINTLN("rtcData initialized");
  }
  else
    S_PRINTF("rtcData wakeups:%d awakems:%d\r\n", rtcData.wakeups, rtcData.awakems);
  if (wifiNetwork()) {
    webUpdater();
//    digitalWrite(BLUE_LED, LED_ON);
    if (!getSensorData())
      sleepsec = RETRY_SEC;
//    digitalWrite(BLUE_LED, LED_OFF);
    if (!sendSensorData())
      sleepsec = RETRY_SEC;
  }
  else
     sleepsec = RETRY_SEC;
  setPin(DHTPOWER, LOW);
  S_PRINTF("########### DEEP-SLEEP FOR %d SECONDS ###########\r\n", sleepsec);
  rtcData.wakeups++;
  S_PRINTF("millis: %d\r\n", millis());
  rtcData.awakems += millis() + 160;  // 160ms init (oscilloscope)
  ESP.rtcUserMemoryWrite(100, (uint32_t*) &rtcData, sizeof(rtcData));
  yield();
//   for deepSleep connect GPIO16 <=> RST (without: sketch not started after awake)
//   However on NodeMCU.... No USB upload possible. Use 1K ohm resistor between D0 and RST
  ESP.deepSleep(sleepsec * 1048320, WAKE_RF_DEFAULT); // max 4294967295us ~ 4097s actually 68m17s
}

bool getSensorData()
{
// Reading temperature or humidity takes about 250 milliseconds!
  bool ret = true;
  while (abs(millis() - dhtStart) < 2000)
    yield();
  humidity = dht.readHumidity();
  if (isnan(humidity)) {
    humidity = -99.9;
//    ret = false;
  }
  temperature = dht.readTemperature();  // in Celsius (the default)
  if (isnan(temperature)) {
    temperature = -99.9;
//    ret = false;
  }
  battery = readADC_dcm();
  S_PRINT("Humidity:    "); S_PRINTLN(humidity);
  S_PRINT("Temperature: "); S_PRINTLN(temperature);
  S_PRINT("Battery:     "); S_PRINTLN(battery);
  return ret;
}

float readADC_dcm() {
  uint32_t adc = 0;
  for (int i=0; i<256; i++)
     adc = adc + analogRead(A0);
  adc = adc >> 4; // 0 - 16384
  // Vtout = 1.0605V (max)
  // Vmax = Vtout*(d1+d2)/d2
  // divider 1501K:119K   Vmax=14.44V => 0.0008822F * adc
  if (adc > 7820) // correction > 6.90V
    return 0.000876F * adc;
  else
    return 0.0008822F * adc;
}

bool sendSensorData()
{
  S_PRINTLN("Connect data.sparkfun.com");
  WiFiClient client;
  if (!client.connect("data.sparkfun.com", 80)) {
    S_PRINTLN("connection failed");
    return false;
  }
  String url;
  if (sleepsec > RETRY_SEC) {
    url = "/input/yourchannel?private_key=yourkey";
    url += "&temperature=";
    url += temperature;
    url += "&humidity=";
    url += humidity;
    url += "&battery=";
    url += battery;
    url += "&wakeups=";
    url += rtcData.wakeups;
    url += "&awakems=";
    url += rtcData.awakems;
    S_PRINTF("URL: %s\r\n", url.c_str());
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: data.sparkfun.com\r\n" + 
                 "Connection: close\r\n\r\n");
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 15000) {
        S_PRINTLN(">>> Client Timeout !");
        client.stop();
        return false;
      }
    }
  }
  while(client.available()){
    String line = client.readStringUntil('\r');
    S_PRINT(line);
  }
  S_PRINTLN();
  S_PRINTLN("closing connection");
  return true;
}

void loop(void)
{
}

  
bool wifiNetwork()
{
  if (wifiConnect("",""))
    return true;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(10);
  int n = WiFi.scanNetworks();
  if (n == 0)
    S_PRINTLN("No networks found");
  else
  {
    S_PRINTF("%d Networks found\r\n",n);
    for (int i = 0; i < n; ++i)
    {
      S_PRINTF("%02d %s (%d)%s\r\n", i+1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), (WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*");
      if (WiFi.SSID(i) == "ssid1")
        return wifiConnect("ssid1", "passkey");
      else if (WiFi.SSID(i) == "ssid2")
        return wifiConnect("ssid2", "passkey");
      else if (WiFi.SSID(i) == "ssid3")
        return wifiConnect("ssid3", "passkey");
    }
  }
  return false;
}

bool wifiConnect(const String &ssid, const String &psk)
{
  WiFi.setOutputPower(20.5);
  if (ssid != "") {
    S_PRINTF("Connect to %s ", ssid.c_str());
    WiFi.begin(ssid.c_str(), psk.c_str());
  }
  else
    S_PRINTF("Reconnect to %s ", WiFi.SSID().c_str());
  for (int tries = 0; tries < 100; tries++) {
    if (WiFi.status() == WL_CONNECTED)
      break;
    S_PRINT('.');
    delay(100);
  }
  if(WiFi.status() != WL_CONNECTED) {
    S_PRINTLN("FAIL");
    return false;
  }
  else {
    S_PRINTLN("OK");
    S_PRINTLN(WiFi.localIP());
    return true;
  }
}

void webUpdater() {
  S_PRINT("HTTP Sketch Update... ");
  t_httpUpdate_return ret = ESPhttpUpdate.update("yourupdateserver", yourport, "/espupdate.php");
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      S_PRINTF("HTTP_UPDATE_FAILED Error (%d): %s\r\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      S_PRINTLN("HTTP_UPDATE_NO_UPDATES");
      break;
    case HTTP_UPDATE_OK:
      S_PRINTLN("HTTP_UPDATE_OK");
      break;
  }
}

void setPin(int gpio, int value) {
  pinMode(gpio, OUTPUT);
  digitalWrite(gpio, value);
  S_PRINT("setPin GPIO");
  S_PRINT(gpio);
  S_PRINT(":");
  S_PRINTLN(value);
}
