/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial
// #define ESP_INTR_FLAG_DEFAULT 0

//#include <SPI.h>
#include "RTClib.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <TimeLib.h>
#include <WidgetRTC.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>


// You should get Auth Token in the Blynk App.
// Go to the Project Settings (nut icon).
char auth[] = "721dbb4ddb714f71835d52ec2626cb56";

// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = "Sambokojan";
char pass[] = "abc123def";
char server[] = "blynk-cloud.com";

BlynkTimer timer;
RTC_DS3231 rtc;
WidgetRTC vrtc;
WidgetLED valveVLED1(V7);
WidgetLED valveVLED2(V8);
WidgetLED errorLED1(V6);
WidgetLED errorLED2(V5);
WidgetTerminal terminal(V20);
WidgetLCD lcd(V22);

// PINS
#define SOIL_SENS_PIN_1 A7
#define SOIL_SENS_PIN_2 A6
#define SENSORS_VCC 23 // powers soilsensors and rainsensor
#define RAIN_SENS_PIN 5
#define VALVE_PIN_1 12
#define VALVE_PIN_2 14
#define VALVE_LEDPIN_1 32
#define VALVE_LEDPIN_2 33
#define ERROR_LED1_PIN 25 //temporary
#define ERROR_LED2_PIN 26
#define MANUAL_VALVE_PIN_1 18
#define MANUAL_VALVE_PIN_2 4
#define PAUSE_PIN 27
#define LED_BUILTIN 2
#define WIFI_LED 13
#define FLOWPIN 15
#define TEMP_SENS 39


// SETTINGS
int hourLaunch = 05; // Hour when automated watering should start
int minuteLaunch = 30;
int autoLaunch = (3600 * 5 + 60 * 30); //Time to launch automatic watering, in seconds. (3600 * hour + 60 * minutes)
int launchValve_1 = 0; // 0 = off, 1 = scheduled, 2=extreme
int launchValve_2 = 0;
bool limitWater = false; // Set to 1 if there is a water restriction in Gnesta.
int nightModeHourStart = 21*3600; //No extra watering during night hours
int nightModeHourEnd = 7*3600;

int moistlevelExtreme = 10;
int moistLevelLow = 30;
int moistLevelScheduleCycleOff = 50;
int moistLevelExtraCycleOff = 40;

int maxCycle = 10;  //Maximum time of watercycle
int pauseTime = 60; // duration to pause on force pause, in minutes.

//VARIABLES

bool disableExtreme = false;
bool disableSchedule = false;
bool disableManual = false;
double flowRate;    //This is the value we intend to calculate.
volatile double pulseCount; //This integer needs to be set as volatile to ensure it updates correctly during the interrupt process.
double waterTotal, waterForever;
double waterToday, waterSinceLastUpdate;
float calibrationFactor = 6.6;
unsigned int flowMilliLitres;

unsigned long oldTime;
int todayDate;
int x, lastX, y = 0;
double z;
String output;
int requestInput;

bool blinkState = false;
int stepperValue, stepperValueCurrent;
int sensorCycle;
bool rainSens; // HIGH means it's not raining
bool errorValve1;
bool errorValve2;
bool pauseWaterCycle = false;
int pausendH = 0;
int pausendM = 0;
int appSwitch1;
int appSwitch2;
int switchState1 = false;
int switchState2 = false;
int pauseSwitch  = 0, pauseSwitchState = false;
int endMoist;
int valveOpen1, valveOpen2;
bool wateringOnValve_1, wateringOnValve_2;
unsigned long valveOpened_1, valveOpened_2;
int soilSens1StartingValue, soilSens2StartingValue;
int lastSwitchState1;
int lastSwitchState2;
bool valveState1;
bool valveState2;

int tempSens = 0;
int soilSens1 = 0;
int soilSens2 = 0; // variables to store the value coming from the soil humitidy sensor
int connectionCount = 0; //Counter to system if wifi has not connected after X tries.

unsigned int myServerTimeout  =  3500;  //  3.5s server connection timeout (SCT)
unsigned int myWiFiTimeout    =  3200;  //  3.2s WiFi connection timeout   (WCT)
unsigned int blynkInterval    = 20000;  // 25.0s check server frequency    (CSF)

// char daysOfTheWeek[7][12] = {"Sö", "Må", "Ti", "On", "To", "Fr", "Lö"};
// This is called when Smartphone App is opened
BLYNK_APP_CONNECTED() {
  stepperValue = 0;
  Blynk.virtualWrite(V17, 0);
}

// This function will run every time Blynk connection is established
BLYNK_CONNECTED() {
  // Request Blynk server to re-send latest values for all pins
  Blynk.syncAll();
  vrtc.begin();
}


BLYNK_WRITE(V0){moistLevelLow = param.asInt();}
BLYNK_WRITE(V1){moistLevelScheduleCycleOff = param.asInt();}
BLYNK_WRITE(V2){moistlevelExtreme = param.asInt();}
BLYNK_WRITE(V3){moistLevelExtraCycleOff = param.asInt();}
BLYNK_WRITE(V4){autoLaunch = param.asInt();}
BLYNK_WRITE(V12){pauseSwitch = param.asInt();}
BLYNK_WRITE(V13){appSwitch1 = param.asInt();}
BLYNK_WRITE(V14){appSwitch2 = param.asInt();}
BLYNK_WRITE(V18){
  if (param.asInt() == 1){software_Reset(1);}}
BLYNK_WRITE(V17){
  stepperValue = param.asInt();
}
BLYNK_WRITE(V20){

  if (String("RWT") == param.asStr()) {
    Blynk.virtualWrite(V100, 0);
    waterTotal = 0;
    terminal.println("\nTotal water this year is reset.") ;
  }

  else if (String("R4EVER") == param.asStr()) {
    Blynk.virtualWrite(V98, 0);
    terminal.println("\nForever watercounter is reset.");
  }

  else if (String("SHOW4EVER") == param.asStr()) {
    terminal.println("\nValue is updated at midnigt.");
    terminal.print(waterForever/1000);
    terminal.println("m³");
  }
  else if (String("help") == param.asStr()) {
    terminal.println("\nThis is what you can do");
    terminal.println("RWT: Reset this years watercounter");
    terminal.println("R4EVER: Reset total watercounter");
    terminal.println("SHOW4EVER: Display total watercounter");
    terminal.println("LIMIT: Switch water limitation on and off");
    terminal.println("MAXCYCLE: How long should irrigation run?");
    terminal.println("PAUSETIME: How long is the pause time?");
  }
  else if (String("LIMIT") == param.asStr()) {
    if (limitWater) {
      terminal.println("\nWater limitation is turned off");
      limitWater=false;}
    else {
      terminal.println("\nWater limitation is turned on");
      limitWater = true;}
  }
  else if (String("MAXCYCLE") == param.asStr()) {
    terminal.println("\nSet max cycle time in minutes:");
     requestInput = 1;
  }
  else if (String("PAUSETIME") == param.asStr()) {
    terminal.println("\nSet pause time in minutes:");
     requestInput = 2;
  }
  else {
    if (requestInput == 1) {
      maxCycle = param.asInt();
      Blynk.virtualWrite(V96, maxCycle);
      terminal.println("\nMax cycle is changed");
    }
    else if (requestInput == 2) {
      pauseTime = param.asInt();
      Blynk.virtualWrite(V95, pauseTime);
      terminal.println("\Pause time is changed");
    }
    else {
    terminal.println("\nUnrecognized command. Type help to see available commands.");
    }
  }
  terminal.flush();
}
BLYNK_WRITE(V9){
  TimeInputParam t(param);
  if (t.hasStartTime()){
    nightModeHourStart = (t.getStartHour()*3600)+t.getStartMinute() * 60;
  }
  if (t.hasStopTime()){
    nightModeHourEnd = (t.getStopHour()*3600)+t.getStopMinute() * 60;
  }
}
BLYNK_WRITE(V100) {waterTotal = param.asDouble();}
BLYNK_WRITE(V99) {waterToday = param.asDouble();}
BLYNK_WRITE(V98) {waterForever = param.asDouble();}
BLYNK_WRITE(V97) {todayDate = param.asInt();}
BLYNK_WRITE(V96) {maxCycle = param.asInt();}
BLYNK_WRITE(V95) {pauseTime = param.asInt();}


void setup()
{

  // Debug console
  Serial.begin(9600);

      pinMode(SENSORS_VCC, OUTPUT);
      pinMode(ERROR_LED1_PIN, OUTPUT);
      pinMode(ERROR_LED2_PIN, OUTPUT);
      pinMode(VALVE_PIN_1, OUTPUT);
      pinMode(VALVE_PIN_2, OUTPUT);
      pinMode(LED_BUILTIN, OUTPUT);
      pinMode(VALVE_LEDPIN_1, OUTPUT);
      pinMode(VALVE_LEDPIN_2, OUTPUT);
      pinMode(WIFI_LED, OUTPUT);

      pinMode(RAIN_SENS_PIN, INPUT);
      pinMode(MANUAL_VALVE_PIN_1, INPUT);
      pinMode(MANUAL_VALVE_PIN_2, INPUT);
      pinMode(PAUSE_PIN, INPUT);
      pinMode(SOIL_SENS_PIN_1, INPUT);
      pinMode(SOIL_SENS_PIN_2, INPUT);
      pinMode(TEMP_SENS, INPUT);
      pinMode(FLOWPIN, INPUT_PULLUP);           //Sets the pin as an input
      attachInterrupt(digitalPinToInterrupt(FLOWPIN), flow, RISING);  //Configures interrupt 0 (pin 2 on the Arduino Uno) to run the function "Flow"


    if(WiFi.status() == 6){
        Serial.println("\tWiFi not connected yet.");
      }
    //timer.setInterval(functionInterval, myfunction);// run some function at intervals per functionInterval
    timer.setInterval(blynkInterval, checkBlynk);   // check connection to server per blynkInterval
    timer.setInterval(3000, sendStatsToServer);
    timer.setInterval(60000L, runAutonomously); //once a minute
    timer.setInterval(10, runManually);
    timer.setInterval(1000, waterCycle);
    // timer.setInterval(10, digitalDisplay);
    timer.setInterval(1000,calculateWaterflow);
    timer.setInterval(500,blinkLED);



    unsigned long startWiFi = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    digitalWrite(WIFI_LED, HIGH);
    while (WiFi.status() != WL_CONNECTED){
      delay(500);
      if(millis() > startWiFi + myWiFiTimeout){
        Serial.println("\tCheck the WiFi router. ");
        break;
      }
    }
    digitalWrite(WIFI_LED, LOW);
    Blynk.config(auth, server);
    checkBlynk();


    lastSwitchState1 = digitalRead(MANUAL_VALVE_PIN_1);
    lastSwitchState2 = digitalRead(MANUAL_VALVE_PIN_2);
    if (Blynk.connected()){
      Blynk.virtualWrite(V13, LOW);
      Blynk.virtualWrite(V14, LOW);
      valveVLED1.off();
      valveVLED2.off();
      errorLED1.off();
      errorLED2.off();
      }
    appSwitch1 = 0;
    appSwitch2 = 0;
    digitalWrite(VALVE_PIN_1, LOW);
    digitalWrite(VALVE_PIN_2, LOW);
    digitalWrite(ERROR_LED1_PIN, LOW);
    digitalWrite(ERROR_LED2_PIN, LOW);

    pulseCount        = 0;
    flowRate          = 0;
    flowMilliLitres   = 0;
    waterSinceLastUpdate = 0;
    oldTime           = millis();

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
    if (Blynk.connected()){adjustTime();}

    // Code for OTA updates

    ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
      })
      .onEnd([]() {
        Serial.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      });

    ArduinoOTA.begin();

    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    stepperValue = 0;
    if (Blynk.connected()){Blynk.virtualWrite(V17, 0);}
    digitalDisplay();

}

void loop()
{

  interrupts();   //Enables interrupts on the Arduino
  if (Blynk.connected()) {Blynk.run();}
  //Blynk.run();
  timer.run(); // Initiates BlynkTimer
  ArduinoOTA.handle();

}
void digitalDisplay() {
  if (stepperValue != stepperValueCurrent){
    lcd.clear();
    switch (stepperValue) {
      case 0:
        lcd.print(0,0,"Välkommen till");
        lcd.print(0,1,"Fjällvatten");
        break;
      case 1:
        lcd.print(0,0, "Fukt 1: ");
        lcd.print(10,0,"%");
        lcd.print(0,1, "Fukt 2: ");
        lcd.print(10,1,"%");
        break;
      case 2:
        lcd.print(0,0,"Vattenflöde");
        break;
      case 3:
        lcd.print(0,0,"Vatten idag");
        break;
      case 4:
        lcd.print(0,0,"Vatten totalt");
        break;
      case 5:
        lcd.print(0,0, "Paus");
        break;
      case 6:
          lcd.print(0,0, "Pause");
          // lcd.print(0,1, hour());
        break;
    }

    stepperValueCurrent = stepperValue;
  }
  switch (stepperValue) {
    case 1:
      lcd.print(8,0, round(soilSens1));
      lcd.print(8,1, round(soilSens2));
      break;
    case 2:
      output = (String)int(flowRate);
      output += "L/min  ";
      lcd.print(0,1,output);
      break;
    case 3:
      z = roundf(waterToday*10000)/10000;
      output = (String)z;
      output += "L   ";
      lcd.print(0,1, output);
      break;
    case 4:
      z = roundf(waterTotal*100)/100;
      output = (String)z;
      output += "m³   ";
      lcd.print(0,1, output);
      break;
    case 5:
      if (pauseWaterCycle) {
        output = "till ";
        if (pausendH <=9) {output += "0";}
        output += pausendH;
        output += ":";
        if (pausendM <=9) {output += "0";}
        output += pausendM;
      } else {
        output = "avslutad";
      }
      lcd.print(0,1, output);
  }

}

void sendStatsToServer() {
  // Serial.println("\tLook, no Blynk  block.");
  // if(WiFi.status()== 3){
  //   // Serial.println("\tWiFi still  connected.");
  //
  // }

  if(Blynk.connected()){
    if (day() != todayDate){
      Blynk.virtualWrite(V99, 0);
      Blynk.virtualWrite(V26, waterToday);
      Blynk.virtualWrite(V98, waterForever + waterToday);
      waterToday = 0;
      todayDate = day();
    }
    waterTotal += (waterSinceLastUpdate/1000000); //Store totals water as cubic meters
    waterToday += (waterSinceLastUpdate/1000); // Store todays water as liters
    waterSinceLastUpdate = 0;


    readSensors("partial");
    digitalWrite(LED_BUILTIN, HIGH);
    digitalWrite(WIFI_LED, HIGH);

    // These values should update
    Blynk.virtualWrite(V10, soilSens1);
    Blynk.virtualWrite(V11, soilSens2);
    if(valveState1){Blynk.virtualWrite(V15,100);}
    if(valveState2){Blynk.virtualWrite(V16,100);}
    Blynk.virtualWrite(V100, waterTotal);
    Blynk.virtualWrite(V99, waterToday);
    Blynk.virtualWrite(V25, waterTotal);

    delay(20);
    digitalWrite(LED_BUILTIN, LOW);
    digitalWrite(WIFI_LED, LOW);

    digitalDisplay(); // Update LCD values every time stats are sent to server
  }
}

void runAutonomously() {

  DateTime now = rtc.now();
  int currentTimeInSeconds;
  if(Blynk.connected()){
    currentTimeInSeconds = 3600 * hour() + 60 * minute();
  } else {
    currentTimeInSeconds = 3600 * now.hour() + 60 * now.minute();
    }
  // Launch scheduled watering
  if (currentTimeInSeconds == autoLaunch && !disableSchedule) {
    readSensors("full");
    if (rainSens == HIGH) {

      if (!errorValve1 && launchValve_1 == 0 && moistLevelLow > soilSens1){
        Serial.println("automatisk bevattning på V1.");
        launchValve_1 = 1;
        disableExtreme = true;
        disableManual = true;
      }
      if (!errorValve2 && launchValve_2 == 0 && moistLevelLow > soilSens2){
        Serial.println("automatisk bevattning på V2.");
        launchValve_2 = 1;
        disableExtreme = true;
        disableManual = true;
      }

    }
    else {

        if(Blynk.connected()){
          printTimeToTerminal();
          terminal.println("Schemalagd bevattning inställd på grund av regn.");
          terminal.flush();
        }
    }
  }

  int currentHour, currentMinute;
  if(Blynk.connected()){
    currentHour = hour();
    currentMinute = minute();
  } else {
    currentHour = now.hour();
    currentMinute = now.minute();
  }

  currentTimeInSeconds = currentHour * 3600 + currentMinute * 60;
  if (!disableExtreme && !limitWater && currentTimeInSeconds > nightModeHourEnd && currentTimeInSeconds < nightModeHourStart) { //If there is a limitation on water, only water on schedule
    readSensors("full");
    if (rainSens == HIGH){
      if(!errorValve1 && soilSens1 < moistlevelExtreme){
        launchValve_1 = 2;
        disableSchedule = true;
        disableManual = true;
      }
      if(!errorValve2 && soilSens2 < moistlevelExtreme){
        launchValve_2 = 2;
        disableSchedule = true;
        disableManual = true;
      }
    }
  }
}

void runManually() {

  if (!disableManual){//If we are in a watering cycle, disable manual watering.
    // Manual switches
    if (Blynk.connected()) {
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
    }

    if (digitalRead(MANUAL_VALVE_PIN_1) != lastSwitchState1){
      if (!valveState1) {
        valveControll("open", 1);
        appSwitch1 = 1;
        lastSwitchState1 = digitalRead(MANUAL_VALVE_PIN_1);
        if (Blynk.connected()) {
          Blynk.virtualWrite(V13, HIGH); //Set virtual button to ON
          printTimeToTerminal();
          terminal.println("Ventil 1 öppnad manuellt");
        }

      }
      else {
        valveControll("close", 1);
        appSwitch1 = 0;
        lastSwitchState1 = digitalRead(MANUAL_VALVE_PIN_1);

        if (Blynk.connected()) {
          Blynk.virtualWrite(V13, LOW);
          printTimeToTerminal();
          terminal.println("Ventil 1 stängd manuellt");
        }
      }
    }

    if (Blynk.connected()) {
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
    }

    if (digitalRead(MANUAL_VALVE_PIN_2) != lastSwitchState2){
      if (!valveState2) {
        valveControll("open", 2);
        appSwitch2 = 1;
        lastSwitchState2 = digitalRead(MANUAL_VALVE_PIN_2);
        if (Blynk.connected()){
          Blynk.virtualWrite(V14, HIGH); //Set virtual button to ON
          printTimeToTerminal();
          terminal.println("Ventil 2 öppnad manuellt");

        }
      }
      else {
        valveControll("close", 2);
        appSwitch2 = 0;
        lastSwitchState2 = digitalRead(MANUAL_VALVE_PIN_2);

        if (Blynk.connected()){
          Blynk.virtualWrite(V14, LOW);
          printTimeToTerminal();
          terminal.println("Ventil 2 stängd manuellt");
        }
      }
    }

    pausePressed();

    if (Blynk.connected()) {terminal.flush();}
    if (stepperValue != stepperValueCurrent){digitalDisplay();}

  }
}

void valveControll(String direction, int valve){
  switch (valve) {
    case 1:
      if (direction == "open"){
        digitalWrite(VALVE_PIN_1, HIGH); //open valve
        digitalWrite(VALVE_LEDPIN_1, HIGH); //turn on physical LED
        valveState1 = true; //Flag to mark valve as open
        if (Blynk.connected()){
          valveVLED1.on(); //Turn on virtual LED
          Blynk.virtualWrite(V15,100);

        }
      }
      else {
        digitalWrite(VALVE_PIN_1, LOW);
        digitalWrite(VALVE_LEDPIN_1, LOW);
        valveState1 = false;
        if (Blynk.connected()){
          valveVLED1.off();
          Blynk.virtualWrite(V15,0);
        }
      }
      break;
    case 2:
      if (direction == "open"){
        digitalWrite(VALVE_PIN_2, HIGH); //open valve
        digitalWrite(VALVE_LEDPIN_2, HIGH); //turn on physical LED
        valveState2 = true; //Flag to mark valve as open
        if (Blynk.connected()){
          valveVLED2.on(); //Turn on virtual LED
          Blynk.virtualWrite(V16,100);
        }
      }
      else {
        digitalWrite(VALVE_PIN_2, LOW);
        valveState2 = false;
        digitalWrite(VALVE_LEDPIN_2, LOW);
        if (Blynk.connected()){
          valveVLED2.off();
          Blynk.virtualWrite(V16,0);
        }
      }
      break;
    default:
      if (Blynk.connected()){terminal.println("DEBUG: Valve action called without specified valve id.");}
  }

}

void waterCycle() {

  if (launchValve_1 != 0 || launchValve_2 != 0 && !pauseWaterCycle) {
    readSensors("partial");

    // watering on valve 1
    if (launchValve_1 != 0 && !errorValve1 && !valveState2){
      if (!valveState1) {
        soilSens1StartingValue = soilSens1;
        valveOpened_1 = millis();
        valveControll("open", 1);
        if (launchValve_1==1){
          if(Blynk.connected()){
            printTimeToTerminal();
            terminal.println("Ventil 1 har öppnats enligt schema.");
          }
        }
        if (launchValve_1==2){
          if(Blynk.connected()){
            printTimeToTerminal();
            terminal.println("Ventil 1 har öppnats på grund av torka.");
          }
        }
      }  else {
        if (launchValve_1==1){endMoist = moistLevelScheduleCycleOff;}
        else {endMoist = moistLevelExtraCycleOff;}

        if (soilSens1 >= endMoist) {
          valveControll("close", 1);
          launchValve_1 = 0;
        }
        else {
          if ((millis() - valveOpened_1) >= (maxCycle*60*1000)){
            if (soilSens1StartingValue >= soilSens1 - 3 && soilSens1StartingValue <= soilSens1 + 3 ) {
              //Error
              digitalWrite(ERROR_LED1_PIN, HIGH);
              errorValve1 = true;
              if(Blynk.connected()){
                printTimeToTerminal();
                terminal.println("Ventil 1 inte registrerat förändrad fuktighet inom maxtid. Troligen har ett fel uppstått.");
                errorLED1.on();
              }
            }
            else {
              if(Blynk.connected()){
                printTimeToTerminal();
                terminal.println("Ventil 1 har varit öppen så länge som systemet tillåter.");
              }
            }
            if(Blynk.connected()){
              printTimeToTerminal();
              terminal.printf("\tVentil 1 stängs efter %i sekunder.\n", (millis() - valveOpened_1)/1000);
            }
            valveControll("close", 1);
            launchValve_1 = 0;
          }
        }
      }
    }

    // watering on valve 2
    if (launchValve_2 != 0 && !errorValve2 && !valveState1){
      if (!valveState2) {
        soilSens2StartingValue = soilSens2;
        valveOpened_2 = millis();
        valveControll("open", 2);
        if (launchValve_2==1){
          if(Blynk.connected()){
            printTimeToTerminal();
            terminal.println("Ventil 2 har öppnats enligt schema.");
          }
        }
        if (launchValve_2==2){
          if(Blynk.connected()){
            printTimeToTerminal();
            terminal.println("Ventil 2 har öppnats på grund av torka.");
          }
        }
      }
      else {
        if (launchValve_2==1){endMoist = moistLevelScheduleCycleOff;}
        else {endMoist = moistLevelExtraCycleOff;}

        if (soilSens1 >= endMoist) {
          valveControll("close", 2);
          launchValve_2 = 0;
        }
        else {
          if ((millis() - valveOpened_2) >= (maxCycle*60*1000)){
            if (soilSens2StartingValue >= soilSens2 - 3 && soilSens2StartingValue <= soilSens2 + 3 ) {
              //Error
              digitalWrite(ERROR_LED2_PIN, HIGH);
              errorValve2 = true;
              if(Blynk.connected()){
                printTimeToTerminal();
                terminal.println("Ventil 2 inte registrerat förändrad fuktighet inom maxtid. Troligen har ett fel uppstått.");
                errorLED2.on();
              }
            }
            else {
              if(Blynk.connected()){
                printTimeToTerminal();
                terminal.println("Ventil 2 har varit öppen så länge som systemet tillåter.");
              }
            }
            if(Blynk.connected()){
              printTimeToTerminal();
              terminal.printf("\tVentil 2 stängs efter %i sekunder.\n", (millis() - valveOpened_2)/1000);
            }
            valveControll("close", 2);
            launchValve_2 = 0;
          }
        }
      }
    }

  if (launchValve_1 == 0 && launchValve_2 == 0 ){
    disableExtreme = false;
    disableSchedule = false;
    disableManual = false;
  }

  if(Blynk.connected()){terminal.flush();}
}
}

void calculateWaterflow() {

  if (millis()>4000) {
  // Disable the interrupt while calculating flow rate and sending the value to
  // the host
  if (pulseCount >= 1000) {
      pulseCount == 0;
  }
  detachInterrupt(digitalPinToInterrupt(FLOWPIN));


  // Because this loop may not complete in exactly 1 second intervals we calculate
  // the number of milliseconds that have passed since the last execution and use
  // that to scale the output. We also apply the calibrationFactor to scale the output
  // based on the number of pulses per second per units of measure (litres/minute in
  // this case) coming from the sensor.
  flowRate = ((1000.0 / (millis() - oldTime)) * pulseCount) / calibrationFactor;
  // Note the time this processing pass was executed. Note that because we've
  // disabled interrupts the millis() function won't actually be incrementing right
  // at this point, but it will still return the value it was set to just before
  // interrupts went away.
  oldTime = millis();
  // Reset the pulse counter so we can start incrementing again
  pulseCount = 0;
  attachInterrupt(digitalPinToInterrupt(FLOWPIN), flow, RISING);


  // Divide the flow rate in litres/minute by 60 to determine how many litres have
  // passed through the sensor in this 1 second interval, then multiply by 1000 to
  // convert to millilitres.
  flowMilliLitres = (flowRate / 60) * 1000;

  // Add the millilitres passed in this second to a cumulative variable thats added to value stored online
  waterSinceLastUpdate += flowMilliLitres;




  }
}

void readSensors(String type) {
    int d;
    if (valveState1 || valveState2){
      d = 100; //For some reason delay must be higher when valves are open
    } else {
      d = 10;  //make sure the sensor is powered
    }

    // READ SENSORS
    delay(d);

    // soil sensors
    soilSens1 = map((analogRead(SOIL_SENS_PIN_1)), 3980, 1600, 0, 100);
    delay(d); //make sure the sensor is powered
    soilSens2 = map((analogRead(SOIL_SENS_PIN_2)), 3980, 1600, 0, 100);


    // Serial.println(analogRead(SOIL_SENS_PIN_2));
     if (type == "full"){
    digitalWrite(SENSORS_VCC, HIGH); // Rain sensor only powered on reading to prevent corrosion
    delay(d); //make sure the sensor is powered
    rainSens = digitalRead(RAIN_SENS_PIN); //rain sensor (Digital LOW means it's raining)
    digitalWrite(SENSORS_VCC, LOW);
    }
  }

  void printTimeToTerminal(){
    output = "[";
    output += day();
    output +="/";
    output += month();
    output +=" ";
    if (hour()<10){output +="0";}
    output += hour();
    output += ':';
    if (minute()<10){output += "0";}
    output += minute();
    output += "] ";
    terminal.print(output);
  }

  void pausePressed() {
    int pauseInitiate = 0, tempMin, tempHour;
    long endPauseTime;

    // Start checking state of pause buttons
    if ((pauseSwitch == 1 || digitalRead(PAUSE_PIN)== HIGH) && !pauseWaterCycle ){pauseInitiate = 1;}
    // Now check if physical pause button is pressed
    // if (digitalRead(PAUSE_PIN)== HIGH){pauseInitiate = 1;}

    // Initiate pause mode
    if (pauseInitiate == 1) {
      digitalWrite(VALVE_PIN_1,LOW);
      digitalWrite(VALVE_PIN_2,LOW);
      if(Blynk.connected()){
        tempHour = hour();
        tempMin = minute();
      } else{
        DateTime now = rtc.now();
        tempHour = now.hour();
        tempMin = now.minute();
      }

      endPauseTime = tempHour * 3600 + tempMin * 60 + pauseTime * 60; // calculate when pause chould end in millis
      pauseWaterCycle = true;

      int pauseMinTemp = tempMin + pauseTime;
      int pauseHours = 0;
      // Calculate pause end time
      while(pauseMinTemp>60){
        pauseHours++;
        pauseMinTemp = pauseMinTemp-60;
      }
      pausendH = tempHour+pauseHours;
      pausendM = tempMin+(pauseTime-(pauseHours*60));
      stepperValue = 5; // Display pause end time in app
      if (Blynk.connected()){Blynk.virtualWrite(V17,5);}
      blinkState = true;
      pauseInitiate = 0;
    }

    // Watch if pause is finished
    if (pauseWaterCycle) {
      long currentTimeInSeconds = tempHour * 3600 + tempMin * 60; // calculate when pause chould end in seconds
      if (currentTimeInSeconds >= endPauseTime ) {
        pauseWaterCycle = false;
        stepperValue = 1;
        blinkState = false;
        if (Blynk.connected()){
          Blynk.virtualWrite(V17,1);
          Blynk.virtualWrite(V12,0);
        }

      }
    }
  }

void flow(){pulseCount++;}

void checkBlynk() {
  if (WiFi.status() == WL_CONNECTED)
  {
    unsigned long startConnecting = millis();

    digitalWrite(WIFI_LED, HIGH);
    while(!Blynk.connected()){
      Blynk.connect();
      if(millis() > startConnecting + myServerTimeout){
        Serial.print("Unable to connect to server. ");
        digitalWrite(WIFI_LED, LOW);
        break;
      }
    digitalWrite(WIFI_LED, LOW);
    }
  }
  if (WiFi.status() != 3) {
    Serial.print("\tNo WiFi. ");
    connectionCount++;
    if (connectionCount == 30){ // Reset system if wifi has connected after 10 minutes.
      connectionCount = 0;
      software_Reset(0);
    }
  }
  Serial.printf("\tChecking again in %is.\n", blynkInterval / 1000);
  Serial.println();
}

void software_Reset(int source) {
  if(Blynk.connected()){
    printTimeToTerminal();
    if (source==1){
      terminal.println("Systemet startas om på begäran från app.");
    }
    else {
      terminal.println("Systemet startas om på egen begäran.");
    }
    terminal.flush();
    errorLED1.off();
    errorLED2.off();
  }
  ESP.restart();
}

void adjustTime() {
  DateTime now = rtc.now();

  if (Blynk.connected()){
    rtc.adjust(DateTime(year(), month(), day(), hour(), minute(), second()));
  }
  if (now.minute()==165){
      if (Blynk.connected()){
        printTimeToTerminal();
        terminal.println("Fel på systemklocka. Försök starta om systemet.");
        terminal.flush();}
  }
}

void blinkLED() {
  // If system is paused, blink error-LEDs
  if (blinkState) {
    if (y=0) {
      digitalWrite(ERROR_LED1_PIN, HIGH);
      digitalWrite(ERROR_LED2_PIN, LOW);
      y=1;
      }
    else {
      digitalWrite(ERROR_LED1_PIN, LOW);
      digitalWrite(ERROR_LED2_PIN, HIGH);
      y=0;

    }
  } else {
    // if pause is no longer active, return error - leds to their previous state
    if (errorValve1) {digitalWrite(ERROR_LED1_PIN, HIGH);}
      else {digitalWrite(ERROR_LED1_PIN, LOW);}
    if (errorValve2) {digitalWrite(ERROR_LED2_PIN, HIGH);}
      else {digitalWrite(ERROR_LED2_PIN, LOW);}
  }
}
