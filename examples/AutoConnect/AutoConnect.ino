/****************************************************************************************************************************
  AutoConnect.ino
  For ESP8266 / ESP32 boards
  
  ESP_WiFiManager is a library for the ESP8266/ESP32 platform (https://github.com/esp8266/Arduino) to enable easy
  configuration and reconfiguration of WiFi credentials using a Captive Portal.
  
  Modified from Tzapu https://github.com/tzapu/WiFiManager
  and from Ken Taylor https://github.com/kentaylor
  
  Built by Khoi Hoang https://github.com/khoih-prog/ESP_WiFiManager
  Licensed under MIT license
  Version: 1.0.11

   Version Modified By   Date      Comments
   ------- -----------  ---------- -----------
    1.0.0   K Hoang      07/10/2019 Initial coding
    1.0.1   K Hoang      13/12/2019 Fix bug. Add features. Add support for ESP32
    1.0.2   K Hoang      19/12/2019 Fix bug thatkeeps ConfigPortal in endless loop if Portal/Router SSID or Password is NULL.
    1.0.3   K Hoang      05/01/2020 Option not displaying AvailablePages in Info page. Enhance README.md. Modify examples
    1.0.4   K Hoang      07/01/2020 Add RFC952 setHostname feature.
    1.0.5   K Hoang      15/01/2020 Add configurable DNS feature. Thanks to @Amorphous of https://community.blynk.cc
    1.0.6   K Hoang      03/02/2020 Add support for ArduinoJson version 6.0.0+ ( tested with v6.14.1 )
    1.0.7   K Hoang      13/04/2020 Reduce start time, fix SPIFFS bug in examples, update README.md
    1.0.8   K Hoang      10/06/2020 Fix STAstaticIP issue. Restructure code. Add LittleFS support for ESP8266 core 2.7.1+
    1.0.9   K Hoang      29/07/2020 Fix ESP32 STAstaticIP bug. Permit changing from DHCP <-> static IP using Config Portal.
                                    Add, enhance examples (fix MDNS for ESP32)
    1.0.10  K Hoang      08/08/2020 Add more features to Config Portal. Use random WiFi AP channel to avoid conflict.
    1.0.11  K Hoang      17/08/2020 Add CORS feature. Fix bug in softAP, autoConnect, resetSettings.
 *****************************************************************************************************************************/
#if !( defined(ESP8266) ||  defined(ESP32) )
  #error This code is intended to run on the ESP8266 or ESP32 platform! Please check your Tools->Board setting.
#endif

// Use from 0 to 4. Higher number, more debugging messages and memory usage.
#define _WIFIMGR_LOGLEVEL_    3

//Ported to ESP32
#ifdef ESP32
  #include <esp_wifi.h>
  #include <WiFi.h>
  #include <WiFiClient.h>
  
  #define ESP_getChipId()   ((uint32_t)ESP.getEfuseMac())
  
  #define LED_ON      HIGH
  #define LED_OFF     LOW
#else
  #include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
  //needed for library
  #include <DNSServer.h>
  #include <ESP8266WebServer.h>
  
  #define ESP_getChipId()   (ESP.getChipId())
  
  #define LED_ON      LOW
  #define LED_OFF     HIGH
#endif

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;

// Use false if you don't like to display Available Pages in Information Page of Config Portal
// Comment out or use true to display Available Pages in Information Page of Config Portal
// Must be placed before #include <ESP_WiFiManager.h>
#define USE_AVAILABLE_PAGES     false

// From v1.0.10 to permit disable/enable StaticIP configuration in Config Portal from sketch. Valid only if DHCP is used.
// You'll loose the feature of dynamically changing from DHCP to static IP, or vice versa
// You have to explicitly specify false to disable the feature.
//#define USE_STATIC_IP_CONFIG_IN_CP          false

// Use false to disable NTP config. Advisable when using Cellphone, Tablet to access Config Portal.
// See Issue 23: On Android phone ConfigPortal is unresponsive (https://github.com/khoih-prog/ESP_WiFiManager/issues/23)
#define USE_ESP_WIFIMANAGER_NTP     false

// Use true to enable CloudFlare NTP service. System can hang if you don't have Internet access while accessing CloudFlare
// See Issue #21: CloudFlare link in the default portal (https://github.com/khoih-prog/ESP_WiFiManager/issues/21)
#define USE_CLOUDFLARE_NTP          false

// New in v1.0.11
#define USING_CORS_FEATURE          true
//////

// Use USE_DHCP_IP == true for dynamic DHCP IP, false to use static IP which you have to change accordingly to your network
#if (defined(USE_STATIC_IP_CONFIG_IN_CP) && !USE_STATIC_IP_CONFIG_IN_CP)
  // Force DHCP to be true
  #if defined(USE_DHCP_IP)
    #undef USE_DHCP_IP
  #endif
  #define USE_DHCP_IP     true
#else
  // You can select DHCP or Static IP here
  //#define USE_DHCP_IP     true
  #define USE_DHCP_IP     false
#endif

#if ( USE_DHCP_IP || ( defined(USE_STATIC_IP_CONFIG_IN_CP) && !USE_STATIC_IP_CONFIG_IN_CP ) )
// Use DHCP
  #warning Using DHCP IP
  IPAddress stationIP   = IPAddress(0, 0, 0, 0);
  IPAddress gatewayIP   = IPAddress(192, 168, 2, 1);
  IPAddress netMask     = IPAddress(255, 255, 255, 0);
#else
  // Use static IP
  #warning Using static IP
  
  #ifdef ESP32
    IPAddress stationIP   = IPAddress(192, 168, 2, 232);
  #else
    IPAddress stationIP   = IPAddress(192, 168, 2, 186);
  #endif
  
  IPAddress gatewayIP   = IPAddress(192, 168, 2, 1);
  IPAddress netMask     = IPAddress(255, 255, 255, 0);
#endif

#define USE_CONFIGURABLE_DNS      true

IPAddress dns1IP      = gatewayIP;
IPAddress dns2IP      = IPAddress(8, 8, 8, 8);

#include <ESP_WiFiManager.h>              //https://github.com/khoih-prog/ESP_WiFiManager

void heartBeatPrint(void)
{
  static int num = 1;

  if (WiFi.status() == WL_CONNECTED)
    Serial.print("H");        // H means connected to WiFi
  else
    Serial.print("F");        // F means not connected to WiFi

  if (num == 80)
  {
    Serial.println();
    num = 1;
  }
  else if (num++ % 10 == 0)
  {
    Serial.print(" ");
  }
}

void check_status()
{
  static ulong checkstatus_timeout = 0;

#define HEARTBEAT_INTERVAL    10000L
  // Print hearbeat every HEARTBEAT_INTERVAL (10) seconds.
  if ((millis() > checkstatus_timeout) || (checkstatus_timeout == 0))
  {
    heartBeatPrint();
    checkstatus_timeout = millis() + HEARTBEAT_INTERVAL;
  }
}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("\nStarting AutoConnectAP on " + String(ARDUINO_BOARD));

  // Use this to default DHCP hostname to ESP8266-XXXXXX or ESP32-XXXXXX
  //ESP_WiFiManager ESP_wifiManager;
  // Use this to personalize DHCP hostname (RFC952 conformed)
  ESP_WiFiManager ESP_wifiManager("AutoConnectAP");

  ESP_wifiManager.setDebugOutput(true);

  //set custom ip for portal
  ESP_wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 100, 1), IPAddress(192, 168, 100, 1), IPAddress(255, 255, 255, 0));

  ESP_wifiManager.setMinimumSignalQuality(-1);

  // From v1.0.10 only
  // Set config portal channel, default = 1. Use 0 => random channel from 1-13
  ESP_wifiManager.setConfigPortalChannel(0);
  //////
  
#if !USE_DHCP_IP    
  #if USE_CONFIGURABLE_DNS  
    // Set static IP, Gateway, Subnetmask, DNS1 and DNS2. New in v1.0.5
    ESP_wifiManager.setSTAStaticIPConfig(stationIP, gatewayIP, netMask, dns1IP, dns2IP);  
  #else
    // Set static IP, Gateway, Subnetmask, Use auto DNS1 and DNS2.
    ESP_wifiManager.setSTAStaticIPConfig(stationIP, gatewayIP, netMask);
  #endif 
#endif  

  // We can't use WiFi.SSID() in ESP32 as it's only valid after connected.
  // SSID and Password stored in ESP32 wifi_ap_record_t and wifi_config_t are also cleared in reboot
  // Have to create a new function to store in EEPROM/SPIFFS for this purpose
  Router_SSID = ESP_wifiManager.WiFi_SSID();
  Router_Pass = ESP_wifiManager.WiFi_Pass();

  //Remove this line if you do not want to see WiFi password printed
  Serial.println("Stored: SSID = " + Router_SSID + ", Pass = " + Router_Pass);

  if (Router_SSID != "")
  {
    ESP_wifiManager.setConfigPortalTimeout(60); //If no access point name has been previously entered disable timeout.
    Serial.println("Got stored Credentials. Timeout 60s");
  }
  else
  {
    Serial.println("No stored Credentials. No timeout");
  }

  String chipID = String(ESP_getChipId(), HEX);
  chipID.toUpperCase();

  // SSID and PW for Config Portal
  String AP_SSID = "ESP_" + chipID + "_AutoConnectAP";
  String AP_PASS = "MyESP_" + chipID;

  // Get Router SSID and PASS from EEPROM, then open Config portal AP named "ESP_XXXXXX_AutoConnectAP" and PW "MyESP_XXXXXX"
  // 1) If got stored Credentials, Config portal timeout is 60s
  // 2) If no stored Credentials, stay in Config portal until get WiFi Credentials
  ESP_wifiManager.autoConnect(AP_SSID.c_str(), AP_PASS.c_str());
  //or use this for Config portal AP named "ESP_XXXXXX" and NULL password
  //ESP_wifiManager.autoConnect();

  //if you get here you have connected to the WiFi
  Serial.println("WiFi connected");
}

void loop()
{
  // put your main code here, to run repeatedly
  check_status();
}
