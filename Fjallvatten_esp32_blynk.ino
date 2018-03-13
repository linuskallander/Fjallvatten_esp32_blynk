/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial

//#include <SPI.h>
#include "RTClib.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

// You should get Auth Token in the Blynk App.
// Go to the Project Settings (nut icon).
char auth[] = "721dbb4ddb714f71835d52ec2626cb56";

// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = "Sambokojan";
char pass[] = "abc123def";

BlynkTimer timer;
RTC_DS3231 rtc;
WidgetLED valveVLED1(V7);
WidgetLED valveVLED2(V8);
WidgetLED errorLED1(V6);
WidgetLED errorLED2(V5);
WidgetTerminal terminal(V20);

// PINS
#define SOIL_SENS_PIN_1 A7
#define SOIL_SENS_PIN_2 A6
#define SENSORS_VCC 23 // powers soilsensors and rainsensor
#define RAIN_SENS_PIN 19
#define VALVE_PIN_1 12
#define VALVE_PIN_2 14
#define VALVE_LEDPIN_1 32
#define VALVE_LEDPIN_2 33
#define ERROR_LED1_PIN 25
#define ERROR_LED2_PIN 26
#define MANUAL_VALVE_PIN_1 18
#define MANUAL_VALVE_PIN_2 4
#define PAUSE_PIN 27
#define LED_BUILTIN 2


// SETTINGS
int hourLaunch = 05; // Hour when automated watering should start
int minuteLaunch = 30;
int limitWater = 0; // Set to 1 if there is a water restriction in Gnesta.
int nightModeHourStart = 21; //No extra watering during night hours
int nightModeHourEnd = 7;

int moistlevelExtreme = 10;
int moistLevelLow = 30;
int moistLevelScheduleCycleOff = 50;
int moistLevelExtraCycleOff = 40;

int maxCycle = 30;  //Maximum time of watercycle
int pauseTime = 60; // duration to pause on force pause, in minutes.

//VARIABLES
int sensorCycle;
bool rainSens; // HIGH means it's not raining
bool errorValve1;
bool errorValve2;
bool pauseWaterCycle = false;
int pausendH = 0;
int pausendM = 0;
int appSwitch1;
int appSwitch2;
int appSwitchLastState1;
int appSwitchLastState2;
int switchState1 = false;
int switchState2 = false;

int lastSwitchState1;
int lastSwitchState2;
bool valveState1;
bool valveState2;

int soilSens1 = 0;
int soilSens2 = 0; // variables to store the value coming from the soil humitidy sensor


char daysOfTheWeek[7][12] = {"Söndag", "Måndag", "Tisdag", "Onsdag", "Torsdag", "Fredag", "Lördag"};

// This function will run every time Blynk connection is established
BLYNK_CONNECTED() {
  // Request Blynk server to re-send latest values for all pins
  Blynk.syncAll();
}

BLYNK_WRITE(V0)
{moistLevelScheduleCycleOff = param.asInt();}

BLYNK_WRITE(V1)
{moistLevelLow = param.asInt();}

BLYNK_WRITE(V2)
{moistlevelExtreme = param.asInt();}

BLYNK_WRITE(V3)
{moistLevelExtraCycleOff = param.asInt();}

BLYNK_WRITE(V13)
{appSwitch1 = param.asInt();}

BLYNK_WRITE(V14)
{appSwitch2 = param.asInt();}

BLYNK_WRITE(V20)
{
  terminal.flush();
}

void sendStatsToServer();

void runAutonomously();

void runManually();

void setup()
{
  #ifndef ESP8266
    while (!Serial); // for Leonardo/Micro/Zero
  #endif
  // Debug console
  Serial.begin(9600);

  Blynk.begin(auth, ssid, pass);
  // You can also specify server:
  //Blynk.begin(auth, ssid, pass, "blynk-cloud.com", 8442);
  //Blynk.begin(auth, ssid, pass, IPAddress(192,168,1,100), 8442);

  // Setup a function to be called every second
  timer.setInterval(1000L, sendStatsToServer);
  timer.setInterval(1000L, runAutonomously);
  // timer.setInterval(10L, runManually);



    pinMode(SENSORS_VCC, OUTPUT);
    pinMode(SOIL_SENS_PIN_1, INPUT);
    pinMode(SOIL_SENS_PIN_2, INPUT);

    pinMode(ERROR_LED1_PIN, OUTPUT);
    pinMode(ERROR_LED2_PIN, OUTPUT);
    pinMode(VALVE_PIN_1, OUTPUT);
    pinMode(VALVE_PIN_2, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(VALVE_LEDPIN_1, OUTPUT);
    pinMode(VALVE_LEDPIN_2, OUTPUT);

    pinMode(RAIN_SENS_PIN, INPUT);
    pinMode(MANUAL_VALVE_PIN_1, INPUT);
    pinMode(MANUAL_VALVE_PIN_2, INPUT);
    pinMode(PAUSE_PIN, INPUT);

    lastSwitchState1 = digitalRead(MANUAL_VALVE_PIN_1);
    lastSwitchState2 = digitalRead(MANUAL_VALVE_PIN_2);
    Blynk.virtualWrite(V13, LOW);
    appSwitch1 = 0;
    Blynk.virtualWrite(V14, LOW);
    appSwitch2 = 0;
    digitalWrite(VALVE_PIN_1, LOW);
    digitalWrite(VALVE_PIN_2, LOW);



    if (! rtc.begin()) {
      Serial.println("Couldn't find RTC");
      while (1);
    }
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, lets set the time!");
      // following line sets the RTC to the date &amp; time this sketch was compiled
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      // This line sets the RTC with an explicit date &amp; time, for example to set
      // January 21, 2014 at 3am you would call:
      // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
    }

    DateTime now = rtc.now();
}

void loop()
{
  Blynk.run();
  timer.run(); // Initiates BlynkTimer
  runManually();

}

void sendStatsToServer() {
  readSensors();
  digitalWrite(LED_BUILTIN, HIGH);
  Blynk.virtualWrite(V10, soilSens1);
  Blynk.virtualWrite(V11, soilSens2);
  if(valveState1){Blynk.virtualWrite(V15,1);}
  if(valveState2){Blynk.virtualWrite(V16,1);}
  delay(20);
  digitalWrite(LED_BUILTIN, LOW);
}

void runAutonomously() {

  DateTime now = rtc.now();
  readSensors();

  //If the time is right, it's not raining and no errors are
  //reported, start scheduled watering
  if (rainSens == HIGH || !errorValve2 || !errorValve1) {
  //checkTime();
  // Scheduled watering

    if (now.hour() == hourLaunch && now.minute() == minuteLaunch) {
      WaterCycle(1); // Initiate water cycle (on schedule)
      }
     else {

      // If we are not in a scheduled cycle, do this:

      if (limitWater == 0) { //If there is a limitation on water, only water on schedule
        if (soilSens1 > moistlevelExtreme || soilSens2 > moistlevelExtreme) {
         if (now.hour() > nightModeHourEnd && now.hour() < nightModeHourStart) { // Don't panic-water during the night.
            WaterCycle(0); // Initiate water cycle
         }
        }
      }
     }
    //rainmode:
    }
}

void runManually() {
  // Manual switches
  if (appSwitch1 == 1){
    if (!valveState1){ //If virtual button turned on and valve is closed...
      valveControll("open", 1);
      printTimeToTerminal();
      terminal.println("Ventil 1 öppnad via app");
    }
  } else {
    if (valveState1){
      valveControll("close", 1);
      printTimeToTerminal();
      terminal.println("Ventil 1 stängd via app");
    }
  }

  if (digitalRead(MANUAL_VALVE_PIN_1) != lastSwitchState1){
    if (!valveState1) {
      valveControll("open", 1);
      Blynk.virtualWrite(V13, HIGH); //Set virtual button to ON
      appSwitch1 = 1;
      lastSwitchState1 = digitalRead(MANUAL_VALVE_PIN_1);
      printTimeToTerminal();
      terminal.println("Ventil 1 öppnad manuellt");

    }
    else {
      valveControll("close", 1);
      Blynk.virtualWrite(V13, LOW);
      appSwitch1 = 0;
      lastSwitchState1 = digitalRead(MANUAL_VALVE_PIN_1);
      printTimeToTerminal();
      terminal.println("Ventil 1 stängd manuellt");
    }
  }


  if (appSwitch2 == 1){
    if (!valveState2){ //If virtual button turned on and valve is closed...
      valveControll("open", 2);
      printTimeToTerminal();
      terminal.println("Ventil 2 öppnad via app");
    }
  } else {
    if (valveState2){
      valveControll("close", 2);
      printTimeToTerminal();
      terminal.println("Ventil 2 stängd via app");
    }
  }

  if (digitalRead(MANUAL_VALVE_PIN_2) != lastSwitchState2){
    if (!valveState2) {
      valveControll("open", 2);
      Blynk.virtualWrite(V14, HIGH); //Set virtual button to ON
      appSwitch2 = 1;
      lastSwitchState2 = digitalRead(MANUAL_VALVE_PIN_2);
      printTimeToTerminal();
      terminal.println("Ventil 2 öppnad manuellt");
    }
    else {
      valveControll("close", 2);
      Blynk.virtualWrite(V14, LOW);
      appSwitch2 = 0;
      lastSwitchState2 = digitalRead(MANUAL_VALVE_PIN_2);
      printTimeToTerminal();
      terminal.println("Ventil 2 stängd manuellt");
    }
  }
    terminal.flush();
}

void valveControll(String direction, int valve){
  switch (valve) {
    case 1:
      if (direction == "open"){
        digitalWrite(VALVE_PIN_1, HIGH); //open valve
        digitalWrite(VALVE_LEDPIN_1, HIGH); //turn on physical LED
        valveVLED1.on(); //Turn on virtual LED
        valveState1 = true; //Flag to mark valve as open
        //Blynk.virtualWrite(V15,1);
      }
      else {
        digitalWrite(VALVE_PIN_1, LOW);
        valveVLED1.off();
        digitalWrite(VALVE_LEDPIN_1, LOW);
        valveState1 = false;
        //Blynk.virtualWrite(V15,0);
      }
      break;
    case 2:
      if (direction == "open"){
        digitalWrite(VALVE_PIN_2, HIGH); //open valve
        digitalWrite(VALVE_LEDPIN_2, HIGH); //turn on physical LED
        valveVLED2.on(); //Turn on virtual LED
        valveState2 = true; //Flag to mark valve as open
        Blynk.virtualWrite(V16,1);
      }
      else {
        digitalWrite(VALVE_PIN_2, LOW);
        valveVLED2.off();
        digitalWrite(VALVE_LEDPIN_2, LOW);
        valveState2 = false;
        Blynk.virtualWrite(V16,0);
      }
      break;
    default:
      terminal.println("DEBUG: Valve action called without specified valve id.");
  }

}

void WaterCycle(bool scheduled) {
  DateTime now = rtc.now();
  int endMoist;
  int forceEnd = 0;
  int soilsSensStartingValue;
  int thresholdMoist;

  if (scheduled == 0) {
      thresholdMoist = moistlevelExtreme;
      endMoist = moistLevelExtraCycleOff;
    } else {
      thresholdMoist = moistLevelLow;
      endMoist = moistLevelScheduleCycleOff;
    }


  if (!pauseWaterCycle){
    // Water valve 1
    readSensors();
    soilsSensStartingValue = soilSens1;
    if (soilSens1 > thresholdMoist && !errorValve1) {
      while(soilSens1 > endMoist){

        terminal.println("Schemalagd bevattning på ventil 1" );
        digitalWrite(VALVE_PIN_1,HIGH);
        valveVLED1.on(); //Turn on virtual LED
        pausePressed();
        if(pauseWaterCycle){return;}
        delay(1000);          // wait for sensors to stabilize
        readSensors();

        forceEnd++;
        if (forceEnd == (maxCycle*60)) {
            if (soilsSensStartingValue - soilSens2 < 10){
              digitalWrite(ERROR_LED1_PIN, HIGH);
              errorLED1.on();
              errorValve1 = true;
              }
            break;
          }
          sendStatsToServer();
      }
    }
    forceStop1:

    //terminal.println("Ventil 1 stängd pga pause");
    digitalWrite(VALVE_PIN_1,LOW);
    valveVLED1.off(); //Turn on virtual LED
    if(errorValve1){
      return;
    }

    // Water valve 2
    readSensors();
    soilsSensStartingValue = soilSens2;
    if (soilSens2 > thresholdMoist && !errorValve2) {
      while(soilSens2 > endMoist){

        // Serial.println("Watering on Valve 2" );
        terminal.println(soilSens2);
        digitalWrite(VALVE_PIN_2,HIGH);
        valveVLED1.on(); //Turn on virtual LED
        pausePressed();
        if(pauseWaterCycle){return;}
        delay(1000);          // wait for sensors to stabilize
        readSensors();
        forceEnd++;
        if (forceEnd == (maxCycle*60)) {
            if (soilsSensStartingValue - soilSens2 < 10){
              digitalWrite(ERROR_LED1_PIN, HIGH);
              errorLED2.on();
              errorValve2 = true;
              }
            break;
          }
          sendStatsToServer();
      }
    }
    forceStop2:
    digitalWrite(VALVE_PIN_2,LOW);
    valveVLED1.off(); //Turn on virtual LED
    if(errorValve2){
      return;
    }

 }
 else {
  // Water-cycle is paused. Check time to see wether it's time to un-pause.
    //checkTime();
     if (now.hour() == pausendH && now.minute() == pausendM){
        pauseWaterCycle=false; // End pause-mode when the timing is right.
        return;
      }
    }
    return;
  }

void readSensors() {
    // READ SENSORS
    digitalWrite(SENSORS_VCC, HIGH);
    delay(100); //make sure the sensor is powered

    // soil sensors

    soilSens1 = map((4095-analogRead(SOIL_SENS_PIN_1)), 0, 4095, 0, 100);
    soilSens2 = map((4095-analogRead(SOIL_SENS_PIN_2)), 0, 4095, 0, 100);


    //rain sensor (Digital LOW means it's raining)
    rainSens = digitalRead(RAIN_SENS_PIN);


    digitalWrite(SENSORS_VCC, LOW);
  }

  void printTimeToTerminal(){
    DateTime now = rtc.now();
    terminal.print(now.year(), DEC);
    terminal.print('-');
    terminal.print(now.month(), DEC);
    terminal.print('-');
    terminal.print(now.day(), DEC);
    terminal.print(" ");
    if (now.hour()<10){terminal.print('0');}
    terminal.print(now.hour(), DEC);
    terminal.print(':');
    if (now.minute()<10){terminal.print('0');}
    terminal.print(now.minute(), DEC);
    terminal.print(':');
    if (now.second()<10){terminal.print('0');}
    terminal.print(now.second(), DEC);
    terminal.print(' ');
  }

  void pausePressed() {

    DateTime now = rtc.now();
    int pauseMinTemp;
    int pauseHours;
    if (digitalRead(PAUSE_PIN)==HIGH){
      pauseWaterCycle=true;
      digitalWrite(VALVE_PIN_1,LOW);
      digitalWrite(VALVE_PIN_2,LOW);

      //checkTime();
      pauseMinTemp = now.minute() + pauseTime;
      pauseHours = 0;
      // Calculate pause end time
      while(pauseMinTemp>60){
        pauseHours++;
        pauseMinTemp = pauseMinTemp-60;
      }
      pausendH = now.hour()+pauseHours;
      pausendM = now.minute()+(pauseTime-(pauseHours*60));

    }
  }
