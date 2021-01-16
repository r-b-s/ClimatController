
#include "DHT.h"
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
 
#define ONE_WIRE_BUS D5
#define EC_PIN1 A0
#define EC_PIN2 D0

#define KEY1_PIN D3
#define KEY2_PIN D4
 
#define DHTTYPE DHT22
#define DHTPIN D6
#define PUMP_PIN D8
#define HUM_MIN_INTERVAL 1 //min
#define PUMP_HUM 55
#define HUM_TIME_ON 60*30 //sec

#define WATER_TIME_ON 35//sec
#define WATER_LEVEL 0
#define WATER_MIN_INTERVAL 60*12 //min

#define LOG_INTERVAL 5 //min
#define URI_SET "http://yourdomain.ru/set.php?p=data"
#define WIFI "yourwifi"
#define PASSW "****"

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
 
DHT dht(DHTPIN, DHTTYPE);
ESP8266WiFiMulti WiFiMulti;

unsigned long ticker=0;

class Logger {
 private:
    int lastState = 0; 
    unsigned long lastLog=0;
    String uri_set;

    float last_hum;
    float last_t1;
    float last_t2;
    float last_h;
    float last_ppm;
 
  public:
    Logger(String  u) {
      this->uri_set = u;     
    }

  void handle(float hum,float t1,float t2,float h,float ppm) {
    if (lastState!=HTTP_CODE_OK && abs(millis()-lastLog)>3*LOG_INTERVAL*1000*60)  {
      WiFiMulti.run();
    }
    
    last_hum=(isnan(hum)||hum<10)?last_hum:hum;
    last_t1=(isnan(t1)||t1<10)?last_t1:t1;
    last_t2=(isnan(t2)||t2<10)?last_t2:t2;
    last_ppm=(isnan(ppm)||ppm<10)?last_ppm:ppm;
    
    if (lastState!=HTTP_CODE_OK || abs(millis()-lastLog)>=LOG_INTERVAL*1000*60-500) {
      lastLog=millis();
      HTTPClient http;
      String url=uri_set
        +"&hum="+String(last_hum,1)
        +"&t1="+String(last_t1,1)
        +"&t2="+String(last_t2,1)
        +"&ppm="+String(last_ppm,1)
        ;
      http.begin(url);

      http.addHeader("Host", "benefication.ru");
      http.addHeader("Connection","close");
      http.addHeader("Accept","*/*");
      http.addHeader("User-Agent","Mozilla/4.0 (compatible; esp8266 Lua; Windows NT 5.1)"); //http.addHeader("Content-Type","application/json;charset=utf-8");
  
      lastState = http.GET();   
      String s= http.getString();
      http.end(); 
      Serial.println(url);
      Serial.println("Code "+String(lastState));
      Serial.println( s);
    }
  }    
};

class Pump {
  private:
    byte pin; 
    boolean State = LOW; 
    unsigned long lastPump=0;
    float pump_trigger;
    int timeOn;
    int min_interval;
 
  public:
    Pump(byte pin,float pump_trigger,int timeOn,int min_interval) {
      this->pin = pin;     
      this->pump_trigger = pump_trigger;
      this->timeOn=timeOn;
      this->min_interval=min_interval;
      pinMode(pin, OUTPUT);
    }

   void handle(float hum) {      
     if (abs(millis()-lastPump)>timeOn*1000) {
        State=LOW;
        Serial.println("Pump off");
     }
     if (abs(millis()-lastPump)>1000*60*min_interval && hum<=pump_trigger && !isnan(hum))  {
        State=HIGH;    
        Serial.println("Pump on");
        lastPump=millis();    
      }
      digitalWrite(pin, State);
  }

  void test() {
    digitalWrite(pin, HIGH);
    delay(1000);
    digitalWrite(pin, LOW);
  }
};


class ECmeter {
 private:
    float raws[10];
    int nextpos=0;
    byte pin_A;
    byte pin_P;    
    float PPMconversion=0.5; 
    float TemperatureCoef = 0.019; //this changes depending on what chemical we are measuring
    float K=0.48;
    int R1= 995;
    int Ra=0; //Resistance of powering Pins
    float  Vin=3.3; //power volts     
 
  public:
    ECmeter(byte pin1,byte pin2) {
      this->pin_A=pin1;  
      this->pin_P=pin2;  
      pinMode(pin1,INPUT);
      pinMode(pin2,OUTPUT);//Setting pin for sourcing current       
    }
    
  void init(){ 
    for (int i=0;i<10;i++)
        SetPPM(25);
  }
    
  void SetPPM(float t2)  {
    if (t2<=15 || isnan(t2)){
      t2=25.0;
    }    
   
    //************Estimates Resistance of Liquid ****************//
    digitalWrite(pin_P,HIGH);
    delay(2000);
    float raw= analogRead(pin_A);
    raw= analogRead(pin_A);// This is not a mistake, First reading will be low beause if charged a capacitor
    delay(500);
    digitalWrite(pin_P,LOW);          
    //Serial.println(String(raw,1));
    //***************** Converts to EC **************************//
    float Vdrop= (Vin*raw)/1024.0;
    //Serial.println(String(Vdrop,3));
    float Rc=(Vdrop*R1)/(Vin-Vdrop);
    //Serial.println(String(Rc,3));
    
    Rc=Rc-Ra; //acounting for Digital Pin Resitance
    float Rz=1/Rc;
    float z=(pow(Rz,1.5665));
    float truePPM=72152353.25*z;
    float EC = 1000/(Rc*K);  
    float EC25  =  EC/ (1+ TemperatureCoef*(t2-25.0));
    float falsePPM=(EC25)*(PPMconversion*1000);
    
    Serial.println("ppm: "+String(truePPM,0)+" Rc: "+String(Rc,0));
    raws[nextpos]=Rc; 
    nextpos+=(nextpos==9)?-9:1;    
  }

  float GetPPM(float t2){
    SetPPM(t2);    
    float sum=0;
    for(int i=0;i<10;i++)
      sum+=raws[i];
    return sum/10;
  }  

  void debug(){
    for(int i=0;i<10;i++)
      Serial.println(String(raws[i],0));
  }
};

ECmeter Ecm1=ECmeter(EC_PIN1,EC_PIN2);
Pump Pump1 = Pump(PUMP_PIN,PUMP_HUM,HUM_TIME_ON, HUM_MIN_INTERVAL);
Pump Pump2 = Pump(PUMP_PIN,WATER_LEVEL,WATER_TIME_ON,WATER_MIN_INTERVAL);
//Pump Pump3 = Pump(PUMP_PIN,SOIL_HUM,WATER_TIME_ON,WATER_MIN_INTERVAL );
Logger Logger1=Logger(URI_SET);

void setup() {
  Serial.begin(115200);
  
  digitalWrite(0, LOW); // sets output to gnd
  pinMode(0, OUTPUT); // switches power to DHT on
  delay(1000); // delay necessary after power up for DHT to stabilize
  
  dht.begin();
  sensors.begin();
  
  Serial.println("start: ");
  
     //WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(WIFI, PASSW);
  while((WiFiMulti.run() != WL_CONNECTED)) {
    delay(1000);
  }
  Serial.println("connected");
  Pump1.test();       
  Pump2.test();

  Ecm1.init();

  pinMode(KEY1_PIN,INPUT_PULLUP);
  pinMode(KEY2_PIN,INPUT_PULLUP);
}

void loop() {
   if (abs(millis()-ticker)>1000*30){
    ticker=millis();  

    float hum = dht.readHumidity();
    float t = dht.readTemperature();
    Serial.println("t1: " + String(t,1));
    Serial.println("hum: " + String(hum,0));
    sensors.requestTemperatures();
    float t2=sensors.getTempCByIndex(0);
    Serial.println("t2: " + String(t2,1));
    

    float Rc=Ecm1.GetPPM(t2);
    Serial.println("Rc: "+String(Rc,0));

    // Ecm1.debug();

    int key=digitalRead(KEY1_PIN);
    if (key==LOW){
      //Pump3.handle(soil);
        Serial.println("soil pump");
    }
    else{
      key=digitalRead(KEY2_PIN);
      if (key==LOW){
        Serial.println(String(1.0/Rc,4));
        Pump2.handle(1.0/Rc);
        Serial.println("ppm pump");
      }
      else{        
        Pump1.handle(hum);
        Serial.println("hum pump");
      }      
    }
    
    Logger1.handle(hum,t,t2,NULL,Rc);    
   }
}   


  


