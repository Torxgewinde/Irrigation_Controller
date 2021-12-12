
/*******************************************************************************
#                                                                              #
#     A minimal firmware for ESP32, MQTT and TLS                               #
#                                                                              #
#                                                                              #
#      Copyright (C) 2021 Tom Stöveken                                         #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; version 2 of the License.                      #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
********************************************************************************/

#include <map>
#include <string>
#include <iterator>
#include <deque>

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <MQTT.h>
#include <WebServer.h>
#include <Update.h>
#include <Ticker.h>

// the board energizes relais coil on LOW which results in an opened valve
#define FLOW    LOW
#define NOFLOW  HIGH

WiFiClientSecure net;
MQTTClient MQTTClient;

// queue to publish states later, not from within the receive routine
struct MQTTMessageQueueItem {
  String topic;
  String message;
  bool retained;
  int qos;
};
std::deque<struct MQTTMessageQueueItem> MQTTMessageQueue;
unsigned long MQTTDroppedMessages = 0;

WiFiMulti wifiMulti;

WebServer server(80);

Ticker ValveWatchdog;
#define MAX_VALVE_ON_TIME_IN_S 60*60
unsigned long ValveWatchdogFeedingTime = 0;

#define STATUS_PUSH_INTERVAL_IN_MS 1000*60

/****************************************************************************************************************/
std::map<String, String> WiFi_map = {
  {"YOUR WIFI 1", "yourwifipassword"},
  {"YOUR WIFI 2", "yourwifipassword"},
  {"YOUR WIFI 3", "yourwifipassword"}
};

String UpdateUsername = "Nutzername";
String UpdatePassword = "Passwort";

//https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
//configure DST, Timezone and NTP server
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER "your_timeserver.tld"

String MQTTServerName = "mqttserver.lan";
uint16_t MQTTPort = 8883;
String MQTTUsername = "username";
String MQTTPassword = "supersecret";
String MQTTDeviceName = "IrrigationController";
String MQTTRootTopic = "garden/irrigation";
const char MQTTRootCA[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
1234567890123456789012345678901234567890123456789012345678901234
1234567890123456789012345678901234567890123456789012345678901234
1234567890123456789012345678901234567890123456789012345678901234
1234567890123456789012345678901234567890123456789012345678901234
1234567890123456789012345678901234567890123456789012345678901234
1234567890123456789012345678901234567890123456789012345678901234
1234567890123456789012345678901234567890123456789012345678901234
1234567890123456789012345678901234567890123456789012345678901234
1234567890123456789012345678901234567890123456789012345678901234
1234567890123456789012345678901234567890123456789012345678901234
1234567890123456789012345678901234567890123456789012345678901234
...
1234567890123456789012345678901234567890123456789012345678901234
1234567890123456789012345678901234567890123456789012345678901234
123456=
-----END CERTIFICATE-----
)CERT";

//define outputs of device: "name", GPIO
std::map<String, int> outputs = {
  {"0", 13},
  {"1", 22},
  {"2", 14},
  {"3", 27},
  {"4", 26},
  {"5", 25},
  {"6", 33},
  {"7", 32},
};

/******************************************************************************
Description.: write a log message
Input Value.: String with the log message
Return Value: -
******************************************************************************/
void Log(String text) {
  Serial.println(text);
}

/******************************************************************************
Description.: publish whole status via MQTT
Input Value.: -
Return Value: -
******************************************************************************/
void PushStatusViaMQTT() {
  for(auto i = outputs.begin(); i != outputs.end(); i++) {
    MQTTClient.publish(
      MQTTRootTopic + "/relay/" + i->first,
      getValve(i->first),
      true, 2);
  }

  MQTTClient.publish(MQTTRootTopic+"/status1",
  "{"
    "\"FreeHeap\":"+String(ESP.getFreeHeap())+", "+
    "\"ValveWatchdogLastFed\":"+String(millis() - ValveWatchdogFeedingTime)+", "+
    "\"uptime\":"+String(millis())+
  "}", true, 2);
  
  MQTTClient.publish(MQTTRootTopic+"/status2",
  "{"
    "\"RSSI\":"+String(WiFi.RSSI())+", "+
    "\"MQTTDroppedMessages\":"+String(MQTTDroppedMessages)+
  "}", true, 2);
}

/******************************************************************************
Description.: set a valve to open or close, switch all other valves to off
Input Value.: ValveName as defined in outputs
              state is either "on" for flow or "off" for no-flow, other strings
              default to no-flow
Return Value: -
******************************************************************************/
void setValve(String valveName, String state){
  bool relaisstate = (state == "on") ? FLOW : NOFLOW;

  auto it = outputs.find(valveName);

  // valve name not found, leave this function
  if(it == outputs.end()) {
    Log("unknown valveName: "+ valveName);
    return;
  }

  for(auto jt = outputs.begin(); jt != outputs.end(); jt++) {
    FeedValveWatchdog();
    
    // check if iterators are identical, if yes set it to the desired state which might be "on" or "off"
    if ( jt == it ) {
      digitalWrite(jt->second, relaisstate);

      struct MQTTMessageQueueItem a;
      a.message  = (state == "on") ? state : "off";
      a.topic    = MQTTRootTopic + "/relay/" + it->first;
      a.retained = true;
      a.qos      = 2;

      MQTTMessageQueue.push_back(a);
      
      continue;
    }

    // set all other valves to off, inform if a state change was necessary
    if( digitalRead(jt->second) == FLOW ) {
      digitalWrite(jt->second, NOFLOW);

      struct MQTTMessageQueueItem a;
      a.message  = "off";
      a.topic    = MQTTRootTopic + "/relay/" + jt->first;
      a.retained = true;
      a.qos      = 2;

      MQTTMessageQueue.push_back(a);
    }
  }
}

/******************************************************************************
Description.: Watchdog forces valves to off-state if no other command was received
Input Value.: -
Return Value: -
******************************************************************************/
void FeedValveWatchdog(){
  ValveWatchdog.detach();
  ValveWatchdog.attach(MAX_VALVE_ON_TIME_IN_S, [](){setValve("0", "off");});
  ValveWatchdogFeedingTime = millis();
}

/******************************************************************************
Description.: get a valve state
Input Value.: ValveName as defined in outputs
Return Value: either "on", "off", "unknown"
******************************************************************************/
String getValve(String valveName){
  bool relaisstate;

  auto it = outputs.find(valveName);
  if(it != outputs.end()) {
    relaisstate = !digitalRead(it->second);
    Log("valve: "+ valveName +" is "+ relaisstate);
    return (relaisstate)?"on":"off";
  } else {
    Log("unknown valve: "+ valveName);
    return "unknown";  
  }
}

/******************************************************************************
Description.: connect to MQTT server
Input Value.: -
Return Value: -
******************************************************************************/
void MQTT_connect() {
  if(wifiMulti.run() != WL_CONNECTED) {
    Log("WiFi not connected, not trying to establish MQTT connection");
    return;
  }
  
  //set last-will-testament, must be set before connecting
  MQTTClient.setWill(String(MQTTRootTopic+"/LWT").c_str(), "offline", true, 2);
  
  while (!MQTTClient.connect(MQTTDeviceName.c_str(), MQTTUsername.c_str(), MQTTPassword.c_str())) {
    Log("could not connect to MQTT server");
    return;
  }
  Log("connected to MQTT server");
  
  //announce that this device is connected
  MQTTClient.publish(MQTTRootTopic+"/LWT", "online", true, 2);

  for(auto i = outputs.begin(); i != outputs.end(); i++) {
    MQTTClient.subscribe(MQTTRootTopic+"/relay/"+i->first+"/set", 2);
  }
  
  Log("subscribed to all topics defined in topic_map");
}

/******************************************************************************
Description.: called for mqtt messages this device receives
Input Value.: topic and payload are passed as strings
Return Value: -
******************************************************************************/
void MQTT_messageReceived(String &topic, String &payload) {
  Log("incoming: " + topic + " - " + payload);

  if( MQTTMessageQueue.size() > 5 ) {
    Log("rate of MQTT messages is very high, dropping messages!");
    MQTTDroppedMessages++;
    return;
  }

  for(auto i = outputs.begin(); i != outputs.end(); i++) {
    if(!topic.equals(MQTTRootTopic+"/relay/"+ i->first +"/set")) {
        continue;
    }
    
    setValve(i->first, payload);
  }
}

/******************************************************************************
Description.: overrule the default startup delay for NTP
              (defined as weak function).
Input Value.: -
Return Value: -
******************************************************************************/
uint32_t sntp_startup_delay_MS_rfc_not_less_than_60000 () {
  return 0;
}

/******************************************************************************
Description.: setup is called after powering the device on
Input Value.: -
Return Value: -
******************************************************************************/
void setup() {
  Serial.begin(115200);
  
  Log("");
  Log("Irrigation Controller: " __DATE__ ", " __TIME__);

  for(auto i = outputs.begin(); i != outputs.end(); i++) {     
    pinMode(i->second, OUTPUT);
    digitalWrite(i->second, NOFLOW);
  }

  //allow for many WiFi APs
  for(auto i = WiFi_map.begin(); i != WiFi_map.end(); i++) {
    wifiMulti.addAP((i->first).c_str(), (i->second).c_str());
  }

  //try to connect
  if(wifiMulti.run() == WL_CONNECTED) {
    Log("WiFi OK");
    Log(WiFi.localIP().toString());
  }

  //allow an authenticated HTTP upload with new firmware images
  //others who already know the WiFi keys can sniff the password
  server.on("/update", HTTP_POST, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");

      Log((Update.hasError()) ? "FAIL" : "OK");
      Log("restarting now");
      ESP.restart();
    }, []() {
      if (!server.authenticate(UpdateUsername.c_str(), UpdatePassword.c_str())) {
        Log("Wrong Authentication");
        return server.requestAuthentication(DIGEST_AUTH);
      }
    
      HTTPUpload& upload = server.upload();
      
      if (upload.status == UPLOAD_FILE_START) {
        Log("Update begins");
        Update.begin();
      }
      
      if (upload.status == UPLOAD_FILE_WRITE) {
        Log("Writing "+ String(upload.currentSize) + " bytes");
        Update.write(upload.buf, upload.currentSize);
      }
      
      if (upload.status == UPLOAD_FILE_END) {
        Log("Update ends");
        Update.end(true);
      }     
      });
      
  server.on("/", []() {
    struct tm tm;
    static char buf[26];
    time_t now = time(&now);
    localtime_r(&now, &tm);
    strftime (buf, sizeof(buf), R"(["%T","%d.%m.%Y"])", &tm);
    server.send(200, "application/json", buf);
  });

  server.begin();
  
  //get the current time to check certs for expiry
  configTime(0, 0, NTP_SERVER);
  setenv("TZ", TIMEZONE, 1);

  time_t now = time(nullptr);
  while (now < 3600) {
    delay(10);
    now = time(nullptr);
  }
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Log(asctime(&timeinfo));

  //establish connection to MQTT server, use the ROOT-CA to authenticate
  //"net" is instanciated from a class that uses ciphers
  net.setCACert(MQTTRootCA);
  //DO NOT USE THIS COMMAND: net.setInsecure();
  
  MQTTClient.begin(MQTTServerName.c_str(), MQTTPort, net);
  MQTTClient.onMessage(MQTT_messageReceived);
  
  Log("Setup done!");
}

/******************************************************************************
Description.: this is the main loop
Input Value.: -
Return Value: -
******************************************************************************/
void loop() {
  static unsigned long then = STATUS_PUSH_INTERVAL_IN_MS;

  //handle WiFi connection and loss of connection
  if(wifiMulti.run() != WL_CONNECTED) {
    Log("WiFi NOT ok, retrying");
    //esp_wifi_internal_set_fix_rate(WIFI_IF_STA, true, WIFI_PHY_RATE_6M);
    //esp_wifi_set_ps(WIFI_PS_NONE);
    for(int i=0; i<10; i++) {
      delay(5000);
      if(wifiMulti.run() == WL_CONNECTED) {
        //now the connection seems to be OK again, just leave this loop()
        return;
      }
    }
    Log("WiFi still NOT ok, tried several times, resetting the whole board");
    ESP.restart();
  }
  
  //handle webserver task
  server.handleClient();
  
  //handle MQTT task
  MQTTClient.loop();
  
  //publish changes if there are any
  while( !MQTTMessageQueue.empty() ) {
    auto j = MQTTMessageQueue.front();
    MQTTClient.publish(j.topic, j.message, true, 2);
    MQTTMessageQueue.pop_front();
  }

  //if MQTT lost connection re-establish it
  if (!MQTTClient.connected()) {
    Log("Establish MQTT connection");
    MQTT_connect();
    PushStatusViaMQTT();
    then = millis();
  }

  if( millis()-then >= STATUS_PUSH_INTERVAL_IN_MS ) {
    then = millis();
    PushStatusViaMQTT();
  }
}
