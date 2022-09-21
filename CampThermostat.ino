#include "settings.h"
#include "secret.h"
#include "arduino-timer.h"
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>


#include <Adafruit_Sensor.h>
#include <DHT.h>

void callback(char * topic, byte * payload, unsigned int length);

int thermostatState = LOW;
int relay1State = LOW;

WiFiClient wifiClient;
PubSubClient client(mqtt_server, mqttPort, callback, wifiClient);

DHT dht(DHTPIN, DHTTYPE);

auto timer = timer_create_default();
auto printtimer = timer_create_default();
auto badtemptimer = timer_create_default();
bool timerActive = false;
bool pulse = 100;
float newT = 0;
bool sendTemp = false;
bool badTemp = false;
bool badTempTimerActive = false;


bool badtempshut_off(void *argument /* optional argument given to in/at/every */) {
    if (!badTemp) {
      Serial.println("bad temp Shut off");
      digitalWrite(RELAY_PIN_1, HIGH);
      digitalWrite(LED_BUILTIN, HIGH);
      thermostatState = LOW;
      client.publish(ThermostatStateTopic, "0", true);
      relay1State = LOW;
      client.publish(relayStateTopic, String(relay1State).c_str(), true);
      client.publish(ThermostatCommandTopic, "0", true);
      badTempTimerActive = false;
      Serial.println("Update temp");
      client.publish(TempTopic, String(newT).c_str(), true);
      return false; // to repeat the action - false to stop
    }
}

bool shut_off(void *argument /* optional argument given to in/at/every */) {
    Serial.println("Shut off");
    digitalWrite(RELAY_PIN_1, HIGH);
    digitalWrite(LED_BUILTIN, HIGH);
    thermostatState = LOW;
    relay1State = LOW;
    timerActive = false;
    return false; // to repeat the action - false to stop
}

bool update_temp(void *argument /* optional argument given to in/at/every */) {
    Serial.println("Update temp");
    client.publish(TempTopic, String(newT).c_str(), true);
    return true; // to repeat the action - false to stop
}

bool print_temp(void *argument /* optional argument given to in/at/every */) {
    Serial.print("Temp: ");
    Serial.println(newT);
    return true; // to repeat the action - false to stop
}


void setup() {
  //initialize the switch as an output and set to LOW (off)
  pinMode(RELAY_PIN_1, OUTPUT);                 // Relay Switch 1
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(RELAY_PIN_1, HIGH);
  digitalWrite(LED_BUILTIN, HIGH);

  dht.begin();
  newT = ( dht.readTemperature() * 1.8 ) + 32;
  
  Serial.begin(115200);                         // Start the serial line for debugging
  delay(100);

  client.setKeepAlive(MQTT_KEEPALIVE);                      //increase keepalive timout to use less data

  WiFi.begin(ssid, password);                   // Start wifi subsystem  
  reconnectWifi();                              // Attempt to connect to the WIFI network and then connect to the MQTT server
  delay(2000);                                  // Wait a bit before starting the main loop

  auto task = printtimer.every(5000, print_temp);
}


void loop() {  
  if (WiFi.status() != WL_CONNECTED) {          // Reconnect if connection is lost
    if (!timerActive) {
      sendTemp = false;
      timer.cancel();
      Serial.println("timers canceled");
      Serial.println("Shut off timer activated");
      auto task = timer.in(SHUT_OFF_TIME, shut_off);
      timerActive = true;
    } 
    reconnectWifi();
  } else if (!client.connected()) {
    if (!timerActive) {
      sendTemp = false;
      timer.cancel();
      Serial.println("timers canceled");
      Serial.println("Shut off timer activated");
      auto task = timer.in(SHUT_OFF_TIME, shut_off);
      timerActive = true;
    }
    reconnectMQTT();
  } else {

    newT = ( dht.readTemperature() * 1.8 ) + 32;
//    Serial.print("Temp: ");
//    Serial.println(newT);
    
    
    if (timerActive) {
      timer.cancel();
      timerActive = false;
      sendTemp = false;
      Serial.println("timers canceled");
    }    

    
    client.loop();                              // Maintain MQTT connection   
    delay(10);                                  // MUST delay to allow ESP8266 WIFI functions to run

    
    if (thermostatState) {
      digitalWrite(LED_BUILTIN, LOW);

      if (newT < -50 || newT > 150 || isnan(newT)) { 
        if (!badTempTimerActive){
          badTemp = true;
          Serial.println("bad temp timer activated");
          auto task = badtemptimer.in(SHUT_OFF_TIME, badtempshut_off);
          badTempTimerActive = true;
        }
      } else {
        badTemp = false;
        if (badTempTimerActive) {
          badtemptimer.cancel();
          badTempTimerActive = false;
          Serial.println("badtemptimer canceled");
        }
      }

      
      if ( !sendTemp ) {
        Serial.println("temp update timer started");
        Serial.println("Update temp");
        client.publish(TempTopic, String(newT).c_str(), true);
        auto task = timer.every(TEMP_UPDATE_TIME, update_temp);
        sendTemp = true;
      }
      
      if (newT < tempSetting - tempTolerance) {
        if (!relay1State ) {
          digitalWrite(RELAY_PIN_1, LOW);
          relay1State = HIGH;
          client.publish(relayStateTopic, String(relay1State).c_str(), true);
          Serial.println("temp below turn on relay");
          Serial.println("Update temp");
          client.publish(TempTopic, String(newT).c_str(), true);
        }
      } else if ( newT > tempSetting + tempTolerance) {
        if ( relay1State ) {
          digitalWrite(RELAY_PIN_1, HIGH);
          relay1State = LOW;
          client.publish(relayStateTopic, String(relay1State).c_str(), true);
          Serial.println("temp above turn off relay");
          Serial.println("Update temp");
          client.publish(TempTopic, String(newT).c_str(), true);
        }
      }
    } else {
      digitalWrite(LED_BUILTIN, HIGH);
      if ( relay1State ) {
        digitalWrite(RELAY_PIN_1, HIGH);
        relay1State = LOW;
        client.publish(relayStateTopic, String(relay1State).c_str(), true);
        Serial.println("Thermostate state off turn off relay");
      }
      if ( sendTemp ) {
        sendTemp = false;
        timer.cancel();
        Serial.println("timers canceled");
      }
    }
    
   
  }

  timer.tick();
  badtemptimer.tick();
  printtimer.tick();
}

void callback(char * topic, byte * payload, unsigned int length) {
  String topicStr = topic;                                // Convert topic to string to make it easier to work with
  Serial.print("Callback update. ");
  Serial.print("Topic: ");
  Serial.print(topicStr);                               // Note:  the "topic" value gets overwritten everytime it receives confirmation (callback) message from MQTT
  Serial.print(" Payload: ");
  Serial.println(payload[0]); 

  if (topicStr == ThermostatCommandTopic) {    
    if (payload[0] == '1') {                              // Turn the switch on if the payload is '1' and publish to the MQTT server a confirmation message
      thermostatState = HIGH;
      client.publish(ThermostatStateTopic, "1", true);
    }    
    else if (payload[0] == '0') {                         // Turn the switch off if the payload is '0' and publish to the MQTT server a confirmation message
      thermostatState = LOW;
      client.publish(ThermostatStateTopic, "0", true);
    }
  }
   
}

void reconnectWifi() {
  Serial.println("");
  Serial.println("Wifi status = ");
  Serial.println(WiFi.status());  
  if (WiFi.status() != WL_CONNECTED) {                    // Attempt to connect to the wifi if connection is lost
    Serial.println("Connecting to ");
    Serial.println(ssid);
        
    while (WiFi.status() != WL_CONNECTED) {               // Loop while we wait for connection
      delay(1000);
      timer.tick();
      badtemptimer.tick();
      printtimer.tick();
    }

    Serial.println("");
    Serial.println("WiFi connected");
    reconnectMQTT();
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

void reconnectMQTT() {
  delay(1000);  
  if (WiFi.status() == WL_CONNECTED) {                    // Make sure we are connected to WIFI before attemping to reconnect to MQTT   
    Serial.println("Attempting MQTT connection..."); 
    while (!client.connected()) {                         // Loop until we're reconnected to the MQTT server
             
       
      if (client.connect(mqttClientId, mqttUser, mqttPassword)) {         // Delete "mqtt_username", and "mqtt_password" here if you are not using any
        Serial.println("\tMQTT Connected");
        client.subscribe(ThermostatCommandTopic);                           // If connected, subscribe to the topic(s) we want to be notified about
        if (thermostatState) {
          client.publish(ThermostatStateTopic, "1", true);
        } else {
          client.publish(ThermostatStateTopic, "0", true);
        }
        client.publish(relayStateTopic, String(relay1State).c_str(), true);
      }
      timer.tick();
      badtemptimer.tick();
      printtimer.tick();
      delay(1000);
    }
  } else {
    Serial.println("Wifi is not connected");
  }
}
