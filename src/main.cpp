#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include "Credentials.h" 


#include "Adafruit_SHT31.h"         // temperature and humidity
#include "Adafruit_LTR329_LTR303.h" // light
#include <MQTT.h>
#include <Adafruit_Sensor.h>
#include <Pushsafer.h>
#include <ArduinoJson.h>


#define HALL_SENSOR_PIN 1

//global variables

Adafruit_SHT31 sht31 = Adafruit_SHT31();
Adafruit_LTR329 ltr = Adafruit_LTR329();

const char* ssid = SSID;   // your network SSID (name) 
const char* password = PASSWORD;   // your network password

const char mqtt_username[] = MQTT_USERNAME;
const char mqtt_password[] = MQTT_PASSWORD;
const char mqtt_server[]   = MQTT_SERVER;



WiFiClient networkClient;
MQTTClient mqttClient;

const char* PushsaferKey = PUSHSAFER_KEY;   // Private Key: http://pushsafer.com
Pushsafer pushsafer(PushsaferKey, networkClient);

unsigned long lastMillis = 0;

// global variables for sensor data
unsigned int temperature = 0;
unsigned int humidity = 0;
unsigned int light = 0;
unsigned int lux_value;

bool temperatureThresholdExceeded = false;
bool humidityThresholdExceeded = false;
bool lightThresholdExceeded = false;

// variables for controlling the push-notifications
bool temperatureAlertSent = false;
bool humidityAlertSent = false;
bool lightAlertSent = false;

unsigned long startTimeLightThresholdExceeded;
unsigned long startTimeTemperatureThresholdExceeded;
unsigned long startTimeHumidityThresholdExceeded;

// door sensor 


bool isMagnetRemoved = false;
bool isTheDoorClosed = false;
String doorState = "";
// int hallVal = 0;

void configureHallSensor(){
  pinMode( LED_BUILTIN, OUTPUT );
  pinMode( HALL_SENSOR_PIN, INPUT );
}

//initiating the sensors

void initSHT(){
  Serial.println("SHT31 test");
  if (! sht31.begin(0x44)) {   // Set to 0x45 for alternate i2c addr
    Serial.println("Couldn't find SHT31");
    while (1) delay(1);
  }

  Serial.print("Heater Enabled State: ");
  if (sht31.isHeaterEnabled())
    Serial.println("ENABLED");
  else
    Serial.println("DISABLED");
}

void initLTR(){
  if ( ! ltr.begin() ) {
    Serial.println("Couldn't find LTR sensor!");
    while (1) delay(10);
  }
  // Setul LTR sensor (see advanced demo for all options!
  Serial.println("Found LTR sensor!");
  ltr.setGain(LTR3XX_GAIN_4);
  ltr.setIntegrationTime(LTR3XX_INTEGTIME_50);
  ltr.setMeasurementRate(LTR3XX_MEASRATE_50);

}

//connections

void connectToWifi(){
  Serial.print("\nConnecting to Wifi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nWifi Connected!");
}

void stayConnectedToWifi(){
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Wifi connection lost");
    // strip.setPixelColor(0, 255, 0, 0); // red LED to show connection problem
    // strip.show(); // Update LED with new contents

    // if connection is lost, wait 3 seconds and try connecting again
    while (WiFi.status() != WL_CONNECTED) {
      delay(3000);
      Serial.print(".");
      WiFi.reconnect();
    }
    Serial.print("Wifi connection regained!");
  }
}

void stayConnectedToMqtt(){
  if( !mqttClient.connected() )
  {
    // Setup and connect the the MQTT broker
    mqttClient.begin(mqtt_server, networkClient);

    String clientId = "ESP32Client-"; // Create a random client ID
      clientId += String(random(0xffff), HEX);

    Serial.print("\nConnecting to MQTT...");
    while (!mqttClient.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      Serial.print(".");
      delay(1000);
    }
    Serial.println("\nMQTT Connected!");
  } else {
    // Make the library maintain our connection to the server
    mqttClient.loop();
  }
}

void setup() {
  Serial.begin(115200);  //Initialize serial

  initSHT();
  initLTR();
  configureHallSensor();

  connectToWifi();

}


unsigned int getIlluminance(double CH0, double CH1){
  unsigned int lux = 0;

  byte integrationTime = 0x01;
	byte gain = 0x00;
  double ratio;
  uint ALS_GAIN[8] = {1, 2, 4, 8, 1, 1, 48, 96};
  float ALS_INT[8] = {1.0, 0.5, 2.0, 4.0, 1.5, 2.5, 3.0, 3.5};

  if ((CH0 == 0xFFFF) || (CH1 == 0xFFFF))
  {
      lux = 0.0;
      return lux;
  }

  ratio = CH1 / (CH0 + CH1);

  if (ratio < 0.45)
		{
			lux = ((1.7743 * CH0) + (1.1059 * CH1)) / ALS_GAIN[gain] / ALS_INT[integrationTime];
		}

  else if (ratio < 0.64)
  {
    lux = ((4.2785 * CH0) - (1.9548 * CH1)) / ALS_GAIN[gain] / ALS_INT[integrationTime];
  }

  else if (ratio < 0.85)
  {
    lux = ((0.5926 * CH0) + (0.1185 * CH1)) / ALS_GAIN[gain] / ALS_INT[integrationTime];
  }

  else{
    lux = 0;
  }
    
  return lux;

  // if(lux < 10000 || lux > 120000){
  //         notifyLight(lux);
  //       }
}

void readTemperature(){
  temperature = sht31.readTemperature();
    if (! isnan(temperature)) {  // check if 'is not a number'
      Serial.print("Temp *C = "); Serial.print(temperature); Serial.print("\t\t");
    } else { 
      Serial.println("Failed to read temperature");
    }
}

void readHumidity(){
  humidity = sht31.readHumidity();
  if (! isnan(humidity)) {  // check if 'is not a number'
    Serial.print("Hum. % = "); Serial.print(humidity); Serial.print("\t\t");
  } else { 
    Serial.println("Failed to read humidity");
  }
}

void readLight(){
  uint16_t visible_plus_ir, infrared;
    if (ltr.newDataAvailable()) {
      bool valid = ltr.readBothChannels(visible_plus_ir, infrared);
      if (valid) {
        Serial.println("\tCH0 Visible + IR: ");
        Serial.print(visible_plus_ir);
        Serial.println("\tCH1 Infrared: ");
        Serial.print(infrared);
        lux_value = getIlluminance(visible_plus_ir, infrared);
        Serial.println("\tIlluminance: " + String(lux_value) + " lux");
        
      }
    }
    Serial.println("");
}

void publishDataToMqtt(String greenhouseId, String temperature, String humidity, String lightInLux, String doorState){
  //String mqttTopic = "students/soso012/greenhouseData/id:" + greenhouseId + "";
  String mqttTopic = "greenhouseData/id:" + greenhouseId + "";
  String mqttTopic2 = "greenhouseData/id:" + greenhouseId + "/doorState";
  String jsonFormat = "{\"temperature\":" + temperature + ",\"humidity\":" + humidity + ",\"illuminance\":" + lightInLux + "}";
  String jsonFormat2 = "{\"door_state\":\"" + doorState + "\"}";
  mqttClient.publish(mqttTopic, jsonFormat);
  mqttClient.publish(mqttTopic2, jsonFormat2);
    Serial.print("Sent to MQTT: ");
  Serial.println(jsonFormat + jsonFormat2);
}

void sendPushNotification(String title, String message){
  pushsafer.debug = true;
  struct PushSaferInput input;
  input.message = message;
  input.title = title;
  input.sound = "1";
  input.vibration = "1";
  input.icon = "1";
  input.iconcolor = "#FFCCCC";
  input.priority = "1";
  input.device = "a";
  input.url = "https://www.pushsafer.com";
  input.urlTitle = "Open Pushsafer.com";
  input.picture = "";
  input.picture2 = "";
  input.picture3 = "";
  input.time2live = "";
  input.retry = "";
  input.expire = "";
  input.confirm = "";
  input.answer = "";
  input.answeroptions = "";
  input.answerforce = "";
  Serial.println(pushsafer.sendEvent(input));
  Serial.println("Sent");
}

void notifyTemperature(int temperature){
  String message;
  String title;
  
  if(!temperatureThresholdExceeded){
    if(temperature > 30){
        message = "The temperature is too high";
        title = "Too hot";
    }else if(temperature < 20){
        message = "The temperature is too low";
        title = "Too cold";
    }
    sendPushNotification(title, message);
    temperatureAlertSent = true;
    startTimeTemperatureThresholdExceeded = millis();
  }
  if ( temperatureAlertSent && (millis() - startTimeTemperatureThresholdExceeded > 120000)){
    message = "The temperature has been too high or low for 2 minutes straight.";
    title = "Bad temperature for 2 min";
    sendPushNotification(title, message);
    //startTimeTemperatureTresholdExceeded = lastMillis;
    temperatureAlertSent = true;
    temperatureThresholdExceeded = false;
  }
  
  
}
void notifyHumidity(int humidity){
  String message;
  String title;

  if(!humidityThresholdExceeded){
    if(humidity < 50){
        message = "The humidity is too low";
        title = "Too dry";
    }
    sendPushNotification(title, message);
    humidityAlertSent = true;
    startTimeHumidityThresholdExceeded = millis();
  }
  if ( humidityAlertSent && (millis() - startTimeHumidityThresholdExceeded > 120000)){
    message = "The humidity has been too low for 2 minutes straight.";
    title = "Bad humidity for 2 min";
    sendPushNotification(title, message);
    humidityAlertSent = false;
    humidityThresholdExceeded = true;
  }
}
void notifyLight(int light){
  String message;
  String title;

   if (!lightThresholdExceeded) {
    if (light < 10000) {
      message = "The light is too low";
      title = "Too dark";
    } else if (light > 120000) {
      message = "The light is too strong";
      title = "Too strong light";
    }
    sendPushNotification(title, message);
    lightAlertSent = true;
    startTimeLightThresholdExceeded = millis();
  }

  if (lightAlertSent && (millis() - startTimeLightThresholdExceeded > 120000)) {
    message = "The light has been too weak or strong for 2 minutes straight.";
    title = "Bad light for 2 min";
    sendPushNotification(title, message);
    lightAlertSent = false;
    lightThresholdExceeded = true;
  }
}

void checkTemperature(){
  if(temperature >= 30 || temperature < 20){
    notifyTemperature(temperature);
    temperatureThresholdExceeded = true;
  }
  else{
    temperatureThresholdExceeded = false;
  }
}
void checkHumidity(){
  if (humidity < 50){
    notifyHumidity(humidity);
    humidityThresholdExceeded = true;
  }
  else{
    humidityThresholdExceeded = false;
  }
}
void checkLight(){
  if (lux_value < 10000 || lux_value > 120000) {
    notifyLight(lux_value);
    lightThresholdExceeded = true;
  } else {
    lightThresholdExceeded = false;
  }
}

void checkDoorState(){
  //hallVal = hallRead();

  isMagnetRemoved = digitalRead( HALL_SENSOR_PIN );
  isTheDoorClosed = !isMagnetRemoved;
  Serial.println(isMagnetRemoved);
  digitalWrite( LED_BUILTIN, isMagnetRemoved ); // light when open
  if (isMagnetRemoved == 1 ){
    Serial.println("The Door is open");
    doorState = "open";
  }else{
    Serial.println("The Door is closed");
    doorState = "closed";
    //isMagnetRemoved = false;
  }
}

void loop() {
  stayConnectedToWifi();
  stayConnectedToMqtt();

  readTemperature();
  readHumidity();
  readLight();

  checkDoorState();

  //checkTemperature();
  //checkHumidity();
  //checkLight();

  //publish a message every 10 seconds
  if (millis() - lastMillis > 10000) {
    lastMillis = millis();

    bool sensorReadingDidWork = true; // fix

    if(sensorReadingDidWork){
      publishDataToMqtt(String(1), String(temperature), String(humidity), String(lux_value), doorState); //finish
      //publishDataToMqtt(String(2), String(temperature), String(humidity), String(lux_value)); //finish
      //publishDataToMqtt(String(3), String(temperature), String(humidity), String(lux_value)); //finish
    }

  }

  delay(5000);

}