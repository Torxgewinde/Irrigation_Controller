
#include <map>
#include <string>
#include <iterator>

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

WiFiMulti wifiMulti;

WebServer server(80);

Ticker ValveWatchdog;
#define MAX_VALVE_ON_TIME_IN_S 60*60
unsigned long ValveWatchdogFeedingTime = 0;

#define STATUS_PUSH_INTERVAL_IN_MS 1000*300

/****************************************************************************************************************/
std::map<String, String> WiFi_map = {
  {"YOUR WIFI 1", "yourwifipassword"},
  {"YOUR WIFI 2", "yourwifipassword"},
  {"YOUR WIFI 3", "yourwifipassword"}
};

String UpdateUsername = "Nutzername";
String UpdatePassword = "Passwort";

String MQTTServerName = "mqttserver.lan";
uint16_t MQTTPort = 8883;
String MQTTUsername = "username";
String MQTTPassword = "supersecret";
String MQTTDeviceName = "IrrigationController";
String MQTTRootTopic = "garden/irrigation";
String MQTTRootCA = "-----BEGIN CERTIFICATE-----\n" \
                    "1234567890123456789012345678901234567890123456789012345678901234\n" \
                    "1234567890123456789012345678901234567890123456789012345678901234\n" \
                    "1234567890123456789012345678901234567890123456789012345678901234\n" \
                    "1234567890123456789012345678901234567890123456789012345678901234\n" \
                    "1234567890123456789012345678901234567890123456789012345678901234\n" \
                    "1234567890123456789012345678901234567890123456789012345678901234\n" \
                    "1234567890123456789012345678901234567890123456789012345678901234\n" \
                    "1234567890123456789012345678901234567890123456789012345678901234\n" \
                    ...
                    "123456=\n" \
                    "-----END CERTIFICATE-----\n";

std::map<String, uint8_t> io_map = {
  {"Relais #1", 13},
  {"Relais #2", 22},
  {"Relais #3", 14},
  {"Relais #4", 27},
  {"Relais #5", 26},
  {"Relais #6", 25},
  {"Relais #7", 33},
  {"Relais #8", 32}
};

void setValve(String valveName, String state);
String getValve(String valveName);

std::map<String, std::function<void(String)>> topic_map = {
  { MQTTRootTopic+"/relay/0/set", [](String payload) { setValve("Relais #1", payload);} },
  { MQTTRootTopic+"/relay/1/set", [](String payload) { setValve("Relais #2", payload);} },
  { MQTTRootTopic+"/relay/2/set", [](String payload) { setValve("Relais #3", payload);} },
  { MQTTRootTopic+"/relay/3/set", [](String payload) { setValve("Relais #4", payload);} },
  { MQTTRootTopic+"/relay/4/set", [](String payload) { setValve("Relais #5", payload);} },
  { MQTTRootTopic+"/relay/5/set", [](String payload) { setValve("Relais #6", payload);} },
  { MQTTRootTopic+"/relay/6/set", [](String payload) { setValve("Relais #7", payload);} },
  { MQTTRootTopic+"/relay/7/set", [](String payload) { setValve("Relais #8", payload);} },
  { MQTTRootTopic+"/relay/0/get", [](String payload) { MQTTClient.publish(MQTTRootTopic+"/relay/0", getValve("Relais #1"), false, 2);} },
  { MQTTRootTopic+"/relay/1/get", [](String payload) { MQTTClient.publish(MQTTRootTopic+"/relay/1", getValve("Relais #2"), false, 2);} },
  { MQTTRootTopic+"/relay/2/get", [](String payload) { MQTTClient.publish(MQTTRootTopic+"/relay/2", getValve("Relais #3"), false, 2);} },
  { MQTTRootTopic+"/relay/3/get", [](String payload) { MQTTClient.publish(MQTTRootTopic+"/relay/3", getValve("Relais #4"), false, 2);} },
  { MQTTRootTopic+"/relay/4/get", [](String payload) { MQTTClient.publish(MQTTRootTopic+"/relay/4", getValve("Relais #5"), false, 2);} },
  { MQTTRootTopic+"/relay/5/get", [](String payload) { MQTTClient.publish(MQTTRootTopic+"/relay/5", getValve("Relais #6"), false, 2);} },
  { MQTTRootTopic+"/relay/6/get", [](String payload) { MQTTClient.publish(MQTTRootTopic+"/relay/6", getValve("Relais #7"), false, 2);} },
  { MQTTRootTopic+"/relay/7/get", [](String payload) { MQTTClient.publish(MQTTRootTopic+"/relay/7", getValve("Relais #8"), false, 2);} }
};
/****************************************************************************************************************/



/******************************************************************************
Description.: write a log message
Input Value.: String with the log message
Return Value: -
******************************************************************************/
void Log(String text) {
  Serial.println(text);
}

/******************************************************************************
Description.: write a log message
Input Value.: String with the log message
Return Value: -
******************************************************************************/
void PushStatusViaMQTT() {
  int i=0;
  
  for(auto it = io_map.begin(); it != io_map.end(); it++, i++) {
    MQTTClient.publish(MQTTRootTopic+"/relay/"+String(i), getValve(it->first), false, 2);
  }

  MQTTClient.publish(MQTTRootTopic+"/uptime", String(millis()), false, 2);
  MQTTClient.publish(MQTTRootTopic+"/FreeHeap", String(ESP.getFreeHeap()), false, 2);
  MQTTClient.publish(MQTTRootTopic+"/ValveWatchdogLastFed", String(millis() - ValveWatchdogFeedingTime), false, 2);
}

/******************************************************************************
Description.: set a valve to open or close, switch all other valves to off
Input Value.: ValveName as defined in io_map
              state is either "on" for flow or "off" for no-flow, other strings
              default to no-flow
Return Value: -
******************************************************************************/
void setValve(String valveName, String state){
  bool relaisstate = (state == "on") ? FLOW : NOFLOW;

  auto it = io_map.find(valveName);

  // valve name not found, leave this function
  if(it == io_map.end()) {
    Log("unknown valveName: "+ valveName);
    return;
  }

  int i=0;
  for(auto jt = io_map.begin(); jt != io_map.end(); i++, jt++) {
    FeedValveWatchdog();
    
    // check if iterators are identical, if yes set it to the desired state which might be "on" or "off"
    if ( jt == it ) {
      digitalWrite(jt->second, relaisstate);
      MQTTClient.publish(MQTTRootTopic+"/relay/"+String(i), (state == "on") ? state : "off", false, 2);
      continue;
    }

    // set all other valves to off, inform if a state change was necessary
    if( digitalRead(jt->second) == FLOW ) {
      digitalWrite(jt->second, NOFLOW);
      MQTTClient.publish(MQTTRootTopic+"/relay/"+String(i), "off", false, 2);
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
  ValveWatchdog.attach(MAX_VALVE_ON_TIME_IN_S, [](){setValve("Relais #1", "off");});
  ValveWatchdogFeedingTime = millis();
}

/******************************************************************************
Description.: get a valve state
Input Value.: ValveName as defined in io_map
Return Value: either "on", "off", "unknown"
******************************************************************************/
String getValve(String valveName){
  bool relaisstate;
  
  auto it = io_map.find(valveName);
  if(it != io_map.end()) {
    
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
  
  while (!MQTTClient.connect(MQTTDeviceName.c_str(), MQTTUsername.c_str(), MQTTPassword.c_str())) {
    Log("could not connect to MQTT server");
    return;
  }

  for(auto it = topic_map.begin(); it != topic_map.end(); it++) {
    MQTTClient.subscribe(it->first, 2);
  }
}

/******************************************************************************
Description.: called for mqtt messages this device receives
Input Value.: topic and payload are passed as strings
Return Value: -
******************************************************************************/
void MQTT_messageReceived(String &topic, String &payload) {
  Log("incoming: " + topic + " - " + payload);

  auto it = topic_map.find(topic);
  if (it != topic_map.end()) {
    it->second(payload);
  }
}

/******************************************************************************
Description.: setup is called after powering the device on
Input Value.: -
Return Value: -
******************************************************************************/
void setup() {
  Serial.begin(115200);
  Log("Irrigation Controller: " __DATE__ ", " __TIME__);

  //switch all relais off
  for(auto i = io_map.begin(); i != io_map.end(); i++) {
    Log("Set GPIO "+ String(i->second) +" as output for " + i->first);
    pinMode(i->second, OUTPUT);
    //digitalWrite(i->second, FLOW);
    //delay(100);
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

  server.begin();

  //establish connection to MQTT server, use the ROOT-CA to authenticate
  //"net" is instanciated from a class that uses ciphers
  net.setCACert(MQTTRootCA.c_str());
  MQTTClient.begin(MQTTServerName.c_str(), MQTTPort, net);
  MQTTClient.onMessage(MQTT_messageReceived);
}

/******************************************************************************
Description.: this is the main loop
Input Value.: -
Return Value: -
******************************************************************************/
void loop() {
  static unsigned long then = STATUS_PUSH_INTERVAL_IN_MS;

  //give these tasks CPU cycles
  wifiMulti.run();
  server.handleClient();
  MQTTClient.loop();

  //if MQTT lost connection re-establish it
  if (!MQTTClient.connected()) {
    MQTT_connect();
  }

  if( millis()-then >= STATUS_PUSH_INTERVAL_IN_MS ) {
    then = millis();
    PushStatusViaMQTT();
  }
}
