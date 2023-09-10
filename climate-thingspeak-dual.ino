/*********
  Rui Santos
  Complete project details at https://randomnerdtutorials.com/esp8266-dht11dht22-temperature-and-humidity-web-server-with-arduino-ide/
*********/

// Import required libraries

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include "ThingSpeak.h"

// Replace with your network credentials
const char* ssid = "foxwood"; // "agcan"; 
const char* password = "9059010281905";

// Thingspeak channel and key
unsigned long myChannelNumber = 992128;
const char * myWriteAPIKey = "0TA6CXFX2K7SXLKB";

WiFiClient  client;

// 4 pin, no board
#define INSIDE_DHTPIN 4     // Digital pin connected to the DHT sensor

// 3 pin with board
// https://projecthub.arduino.cc/arcaegecengiz/using-dht11-12f621
// https://www.circuitbasics.com/wp-content/uploads/2015/12/DHT11-Pinout-for-three-pin-and-four-pin-types-2.jpg
#define OUTSIDE_DHTPIN 5

// Uncomment the type of sensor in use:
#define DHTTYPE    DHT11     // DHT 11
//#define DHTTYPE    DHT22     // DHT 22 (AM2302)
//#define DHTTYPE    DHT21     // DHT 21 (AM2301)

DHT in_dht(INSIDE_DHTPIN, DHTTYPE);
DHT out_dht(OUTSIDE_DHTPIN, DHTTYPE);

// current temperature & humidity, updated in loop()
float in_t = 0.0;
float in_h = 0.0;
float out_t = 0.0;
float out_h = 0.0;

// Generally, you should use "unsigned long" for variables that hold time
// The value will quickly become too large for an int to store
unsigned long previousSensorMillis = 0;    // will store last time DHT was updated
unsigned long previousUploadMillis = 0;    // last time data was uploaded

// Updates DHT readings every 1 seconds
const long sensorIntervalMillis = 1000;
// Upload every hour
unsigned long uploadIntervalMillis = 1000 * 60 *60;  


// Create AsyncWebServer object on port 80
AsyncWebServer server(80);


const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <style>
    html {
     font-family: Arial;
     display: inline-block;
     margin: 0px auto;
     text-align: center;
    }
    h2 { font-size: 3.0rem; }
    h3 { font-size: 2.0rem; }
    p { font-size: 2.0rem; }
    .units { font-size: 1.2rem; }
    .dht-labels{
      font-size: 1.5rem;
      vertical-align:middle;
      padding-bottom: 15px;
    }
  </style>
</head>
<body>
  <h2>Foxwood Climate</h2>
  <h3>Inside</h3>

  <p>
    <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
    <span class="dht-labels">Temperature</span> 
    <span>%IN_TEMPERATURE%</span>
    <sup class="units">&deg;C</sup>
  </p>

  <p>
    <i class="fas fa-tint" style="color:#00add6;"></i> 
    <span class="dht-labels">Humidity</span>
    <span>%IN_HUMIDITY%</span>
    <sup class="units">&#37;</sup>
  </p>
  
  <h3>Outside</h3>
  <p>
    <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
    <span class="dht-labels">Temperature</span> 
    <span>%OUT_TEMPERATURE%</span>
    <sup class="units">&deg;C</sup>
  </p>

  <p>
    <i class="fas fa-tint" style="color:#00add6;"></i> 
    <span class="dht-labels">Humidity</span>
    <span>%OUT_HUMIDITY%</span>
    <sup class="units">&#37;</sup>
  </p>
  
</body>
<script>
setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("temperature").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/temperature", true);
  xhttp.send();
}, 10000 ) ;

setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("humidity").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/humidity", true);
  xhttp.send();
}, 10000 ) ;
</script>
</html>)rawliteral";

void log(String message) {
  Serial.println(message);
  delay(500);
}

// Replaces placeholder with DHT values
String processor(const String& var){
  Serial.print("var: ");
  Serial.println(var);
  if(var == "IN_TEMPERATURE"){
    return String(in_t);
  } else {
    if(var == "IN_HUMIDITY"){
      return String(in_h);
    } else {
      if(var == "OUT_TEMPERATURE"){
        return String(out_t);
      } else {
        if(var == "OUT_HUMIDITY"){
          return String(out_h);
        }
      }
    }
  }
  return String();
}

void ensureWifi() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("\Waiting for WiFi, ssid=");
    Serial.println(ssid);
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(5000);
    }
    Serial.print("\nConnected to Wifi. IP Address=");
    Serial.println(WiFi.localIP());
  }
}

void connectToWifi() {
  // Connect or reconnect to WiFi
  Serial.print("\nConnecting to WiFi, ssid=");
  Serial.print(ssid);
  Serial.print(", password=");
  Serial.println(password);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password); // Connect to WPA/WPA2 network. Change this line if using open or WEP network
  ensureWifi();
}

// Update the t+h readings but do not reset the sensorIntervalMillis
void refreshReadings() {
    // TODO refactor into functions

    // INSIDE
    // Read temperature as Celsius (the default)
    log("1");
    float newT = in_dht.readTemperature();
    log("1.1  ");
    // Read temperature as Fahrenheit (isFahrenheit = true)
    // float newT = in_dht.readTemperature(true);
    // if temperature read failed, don't change t value
    if (isnan(newT)) {
      log("2");
      Serial.println("Failed to read from DHT sensor!");
    } else {
      log("3");
      in_t = newT;
      log("4");
      Serial.print("inside temperature: ");
      Serial.println(in_t);
      log("5");
    }
    // Read Humidity
    log("6");
    float newH = in_dht.readHumidity();
    log("7");
    // if humidity read failed, don't change h value 
    if (isnan(newH)) {
      log("8");
      Serial.println("Failed to read from DHT sensor!");
      log("9");
    } else {
      log("10");
      in_h = newH;
      Serial.print("inside humidity: ");
      Serial.println(in_h);
      log("11");
    } 

    // OUTSIE
    log("o.1");
    newT = out_dht.readTemperature();
    log("o.1.1  ");
    // Read temperature as Fahrenheit (isFahrenheit = true)
    // float newT = in_dht.readTemperature(true);
    // if temperature read failed, don't change t value
    if (isnan(newT)) {
      log("o.2");
      Serial.println("Failed to read from outside DHT sensor!");
    } else {
      log("o.3");
      out_t = newT;
      log("o.4");
      Serial.print("outside temperature: ");
      Serial.println(out_t);
      log("o.5");
    }
    // Read Humidity
    log("o.6");
    newH = out_dht.readHumidity();
    log("o.7");
    // if humidity read failed, don't change h value 
    if (isnan(newH)) {
      log("o.8");
      Serial.println("Failed to read from DHT sensor!");
      log("o.9");
    } else {
      log("o.10");
      out_h = newH;
      Serial.print("outside humidity: ");
      Serial.println(out_h);
      log("o.11");
    } 
}


// Update the t+h readings if it is time
void updateReadings() {
  unsigned long currentSensorMillis = millis();
  if (currentSensorMillis - previousSensorMillis >= sensorIntervalMillis ) {
    // save the last time you updated the DHT values and refresh
    previousSensorMillis = currentSensorMillis;
    refreshReadings();
  }
}

 
void refreshData() {

  ensureWifi();
  
  Serial.print("upload inside temperature: ");
  Serial.println(in_t);
  Serial.print("upload inside humidity: ");
  Serial.println(in_h);
  Serial.print("upload outside temperature: ");
  Serial.println(out_t);
  Serial.print("upload outside humidity: ");
  Serial.println(out_h);

            
  // Upload temp and humidity
  ThingSpeak.setField(1,in_t); // inside temp
  ThingSpeak.setField(2,in_h); // inside humidity
  ThingSpeak.setField(3,out_t); // outside temp
  ThingSpeak.setField(4,out_h); // outside humidity
  
  int httpCode = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  
  if (httpCode == 200) {
    Serial.println("Channel write successful.");
  } else {
    Serial.println("Problem writing to channel. HTTP error code " + String(httpCode));
  }
}

void uploadData() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousUploadMillis < uploadIntervalMillis) {
    return;
  }
  previousUploadMillis=currentMillis;
  refreshData();
}


void setup(){
  Serial.println("Setup...");
  // Serial port for debugging purposes
  Serial.begin(115200);
  in_dht.begin();
  out_dht.begin();
  // wait 1 sec for DHT to be stable (Section 4. Power and Pin, https://www.mouser.com/datasheet/2/758/DHT11-Technical-Data-Sheet-Translated-Version-1143054.pdf)
  delay(1000);
  
  connectToWifi();

  // Read initial t+h values
  previousSensorMillis = millis();  // now
  refreshReadings(); // force refresh to ensure non-zero

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", String(in_t).c_str());
  });
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", String(in_h).c_str());
  });

//  server.on("/refresh", HTTP_GET, [](AsyncWebServerRequest *request){
//    Serial.println("/refresh");
//    refreshReadings();
//    String response = String(t) + String(", ") + String(h);
//    Serial.println(response.c_str());
//    delay(1000);
//    request->send_P(200, "text/plain", "ok");
//  });

  // Start server
  server.begin();
 
  ThingSpeak.begin(client);

  refreshData();
}


void loop(){ 
//  in_t = 1.0;
//  in_h = 2.0;
//  out_t = 3.0;
//  out_h = 4.0;
  updateReadings();
  uploadData(); 
}
