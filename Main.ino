#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <Stepper.h>
#include "DHTesp.h"

#define WATCHDOG_TIMEOUT_S 5
#define rainAnalog 35
#define rainDigital 34
#define LDRDigital 32
#define LDRAnalog 33
#define DHTpin 17
#define IN1 19
#define IN2 18
#define IN3 5
#define IN4 21

const int stepsPerRevolution = 2048;
DHTesp dht;
Stepper myStepper(stepsPerRevolution, IN1, IN3, IN2, IN4);
hw_timer_t * watchDogTimer = NULL;
char* ssid = "SSID";
char* password = "PASSWORD";
const char* serverName = "SERVERNAME";
int prevState = 0;
int stateMotor;
int statRain = 0;
int statPrev = 0;

void IRAM_ATTR watchDogInterrupt(){
  Serial.println("reboot");
  ESP.restart();
}

void watchDogRefresh(){
  timerWrite(watchDogTimer, 0);
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password); 
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  dht.setup(DHTpin, DHTesp::DHT11);
  pinMode(rainDigital, INPUT);
  pinMode(LDRDigital, INPUT);
  myStepper.setSpeed(10);
  xTaskCreate(
    read_sensor,
    "Baca Sensor",
    10000,
    NULL,
    1,
    NULL
  );
  xTaskCreatePinnedToCore(
    motor_bergerak,
    "Motor Stepper",
    5000,
    NULL,
    1,
    NULL,
    1
  );
  watchDogTimer = timerBegin(2, 80, true);
  timerAttachInterrupt(watchDogTimer, &watchDogInterrupt, true);
  timerAlarmWrite(watchDogTimer, WATCHDOG_TIMEOUT_S * 1000000, false);
  timerAlarmEnable(watchDogTimer);
  Serial.println("Connected to the WiFi network");
}

const char* root_ca= \
"-----BEGIN CERTIFICATE-----\n" \
//Here The Certificate
"-----END CERTIFICATE-----\n";

void loop(){
  watchDogRefresh();
}

void kirimState(int states_of_rain){
  HTTPClient http;
  http.begin(serverName+'/ENDPOINT', root_ca);
  http.addHeader("Content-Type", "application/json");
  if(states_of_rain == 0 && prevState == 1){
    http.POST("{\"state\":\"Masuk\"}");
  }else if(states_of_rain == 1){
    http.POST("{\"state\":\"Keluar\"}");
  }
  http.end();
}

void read_sensor(void * parameter) {
  int rainAnalogVal = analogRead(rainAnalog);
  int rainDigitalVal = digitalRead(rainDigital);
  int LDRAnalogVal = analogRead(LDRAnalog);
  int LDRDigitalVal = digitalRead(LDRDigital);
  float h = dht.getHumidity();
  float t = dht.getTemperature();
  String ket;
  if(rainDigitalVal == 0 && LDRDigitalVal == 1){
    ket = "Hujan";
    statRain = 1;
  }else{
    ket = "Cerah";
    statRain = 0;
  }
  for(;;){
    if ((WiFi.status() == WL_CONNECTED)) { //Check the current connection status
      HTTPClient http;
      http.begin(serverName, root_ca);
      http.addHeader("Content-Type", "application/json");
      t = dht.getTemperature();
      h = dht.getHumidity();
      rainAnalogVal = analogRead(rainAnalog);
      LDRAnalogVal = analogRead(LDRAnalog);
      if(rainDigitalVal == 0 && LDRDigitalVal == 1){
        ket = "Hujan";
        statRain = 1;
      }else{
        ket = "Cerah";
        statRain = 0;
      }
      kirimState(statRain);
      int httpResponseCode = http.POST("{\"suhu\":\"" + String(t) + "\",\"hum\":\"" + String(h) + "\",\"raindrop\":\"" + String(rainAnalogVal) + "\",\"cahaya\":\"" + String(LDRAnalogVal) + "\",\"keterangan\":\"" + ket + "\"}");
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      http.end();
    }
    vTaskDelay(180000/portTICK_PERIOD_MS);
  }
}

String httpGETRequest(char* server) {
  HTTPClient http;


  http.begin(server, root_ca);


  int httpResponseCode = http.GET();

  String payload = "{}"; 

  if (httpResponseCode>0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }

  http.end();

  return payload;
}

void motor_bergerak(void * parameter){
  for(;;){
    String sensorReadings = httpGETRequest(serverName);
    JSONVar myObject = JSON.parse(sensorReadings);
  

    if (JSON.typeof(myObject) == "undefined") {
      Serial.println("Parsing input failed!");
      return;
    }

    JSONVar keys = myObject.keys();
    for (int i = 0; i < keys.length(); i++) {
      JSONVar value = myObject[keys[i]];
      if(i == 1)stateMotor = int(value);
    }
    if(stateMotor == 1 && prevState == 0 || statRain == 1 && statPrev == 0){
      myStepper.step(stepsPerRevolution);
      prevState = stateMotor;
      statPrev = statRain;
    }
    else if(stateMotor == 0 && prevState == 1 || statRain == 0 && statPrev == 1){
      myStepper.step(-stepsPerRevolution);
      prevState = stateMotor;
      statRain = statRain;
    }
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}
