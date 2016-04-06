/*

  FILE: gro-matic
  AUTHOR: zrox, Galaktika, Driverone
  VERSION: 0.9.9.8

  2004 I2C LCD
  DS3231 I2C RTC
  BME-280 I2C Thermometer, Hydrometer
  GY-30 I2C Lux Meter
  Chirp I2C I2CSoilMoistureSensor

  Bei allen I2C Sensoren gilt, in Reihe schalten nicht sternförmig anschließen, und zwischen SDA +5V und SCL +5V
  je einen 4,7K Ohm Widerstand einlöten.
  Reihenfolge: Arduino > Wirderstände > DS3231 > 2004 LCD > BME-280 > GY-30 > Chirp
  (bei UNO, SDA = Pin A4, SCL = Pin A5)

  Taster mit INTERNAL_PULLUP an Pin10 zum ein/ausschalten des Displays.
  Taster mit INTERNAL_PULLUP Pin4 zum wechseln der screens

  Encoder KY-040
  Die Anschlüsse des KY-040 sind wie folgt belegt:

  CLK, an Pin3
  DT, an Pin2
  SW, mit INTERNAL_PULLUP an PinA1 zum wechseln des Lichtmodus, reset des Eproms, usw...
  +, an 5V
  GND, an GND des Arduino

  8er Relais Modul
  Relais 1, an Pin 9 als Wechsler für Stufentrafo
  Relais 2, an Pin 7 zur NDL (P braunes Kabel) Steuerung
  Relais 3, an Pin 6 zur LSR (P braunes Kabel) Steuerung
  Relais 4, an Pin 5 zur Umluftventi steuerung
  Relais 5, an Pin 11 zur Wasserpumpe


  Relais 1 Anschluss

  Pin rechts z.B. an 80V anschluss vom Stufentrafo, Pin in der Mitte P (braunes Kabel) an LTI und Pin links an z.B. 190V vom Stufentrafo.
  Anschüsse zum Stufentrafo  190V  P  80V
  |  |  |
  ---------
  |o  o  o|
  | Relais|
  ---------

  Anschluss Relais 2 -5
  Stromzufuhr (von der Steckdose) immer in der Mitte und anschluss zur NDL oder LSR rechts.

  LCD-Display LCD20x4
  Sonderzeichen:  Eingabe z.B. lcd.print(char(0xE1)); für ä
              ä = 0xE1
              ö = 0xEF
              ü = 0xF5
              ß = 0xE2
              ° = 0xDF
              µ = 0xE4
*/

#include "Wire.h"                   // https://www.arduino.cc/en/Reference/Wire (included with Arduino IDE)
#include "LiquidCrystal_I2C.h"      // https://bitbucket.org/fmalpartida/new-liquidcrystal
#include "DS3232RTC.h"              // https://github.com/JChristensen/DS3232RTC.git
#include "EEPROM.h"                 // https://www.arduino.cc/en/Reference/EEPROM
#include "I2CSoilMoistureSensor.h"  // https://github.com/Miceuz/i2c-moisture-sensor
#include "Adafruit_Sensor.h"        // https://github.com/adafruit/Adafruit_Sensor
#include "Adafruit_BME280.h"        // https://github.com/adafruit/Adafruit_BME280_Library
#include "Time.h"                   // https://github.com/PaulStoffregen/Time
#include "TimeAlarms.h"             // https://www.pjrc.com/teensy/td_libs_TimeAlarms.html

/* Festlegen der verschiedenen Lichtprogramme */
enum { LSR, GROW, BLOOM };

/* Wenn MAGIC_NUMBER im EEPROM nicht übereinstimmt wird das EEPROM mit den default einstellungen neu geschreiben */
const uint32_t MAGIC_NUMBER = 0xAAEBCCDF;

/* Structure hält default einstellungen! diese werden von EEPROM überschrieben oder werden geschrieben falls noch nicht gesetzt */
struct setings_t {

  uint32_t MAGIC_NUMBER   = MAGIC_NUMBER;

  byte lichtmodus     = LSR;   // Speichern des gewählten Lichtmodus einstellen (enumeration)
  bool autowasse      = false; // Autobewasserung, on false = disabled

  byte bloom_counter  = 0;    // Speichern des bloom Tage counters.
  byte starttag       = 0;    // Speichern des Start Tages
  byte startmonat     = 0;    // Speichern des Start Monats

/* Ab hier Zeit für die Belichtungsmodis einstellen */
  byte lsr_an           = 5;     // Startzeit des LSR Modis
  byte lsr_aus          = 22;    // Endzeit des LSR Modis
  byte grow_licht_an    = 5;     // Startzeit des Grow Modis
  byte grow_licht_aus   = 22;    // Endzeit des Grow Modis
  byte bloom_licht_an   = 5;     // Startzeit des Bloom Modis
  byte bloom_licht_aus  = 16;    // Endzeit des Grow Modis
 
/* Temperaturwerte für LTI, ab erreichen der Temperaturen in den verschiedenen Licht Modis soll LTI in die hoechste Stufe geschaltet werden. */
  double lsr_temp   = 24.00; // Temp im LSR Modi
  double grow_temp  = 23.00; // Temp im Grow Modi
  double bloom_temp = 22.00; // Temp im Bloom Modi

/* RLF Werte für LTI, z.B. bei 40.00% RLF soll LTI in die hoechste Stufe geschaltet werden. */
  double lsr_rlf    = 60.00;  // RLF im LSR Modi
  double grow_rlf   = 55.00;  // RLF im Grow Modi
  double bloom_rlf  = 40.00;  // RLF im Bloom Modi

/* Autobewaesserung */
  byte autowasser     = 0;
  byte startwasser    = 7;
  byte auswasser      = 5;
  byte startwassermin = 0;
  byte sekauswasser   = 0;

} setings_a, setings_b; // wenn sich structure a von b unterscheidet && write_EEPROM == true dann schreibe EEPROM neu...

bool write_EEPROM = false;

#define BACKLIGHT_PIN (3)
#define LED_ADDR (0x27)  // might need to be 0x3F, if 0x27 doesn't work

LiquidCrystal_I2C lcd(LED_ADDR, 2, 1, 0, 4, 5, 6, 7, BACKLIGHT_PIN, POSITIVE);

// GY-30 Lux Meter
#define BH1750_address 0x23  // I2C Addresse des GY-30

//Backlight button
#define buttonPin 10

//Displayfunktionen
byte hintergrund = 1;    // schalte dispaly an menue

//Programm modus und reset Taster
#define wechslertPin A1  // Pinnummer des Tasters für zum Lichtmodus wechseln und Eprom Reset

bool wechslertGedrueckt = 0;  // abfragen ob Taster gedrückt wurde
#define entprellZeit 200  // Zeit für Entprellung, anpassen!
unsigned long wechslertZeit = 0;  // Zeit beim drücken des Tasters

// Verschiedene Variablen
bool relay_bloom_switching  = false;
bool relay_grow_switching   = false;
bool relay_lsr_switching    = false;

// Ab hier LCD menue fuehrung und taster
byte screen = 1;
#define screenPin 4  // Pin für Taster zum umschalten der LCD seite
bool screenStatus = LOW;  // aktuelles Signal vom Eingangspin
bool screenGedrueckt = false;  // abfragen ob Taster gedrückt wurde
unsigned long screenZeit = 0;  // Zeit beim drücken des Tasters

// Variablen für Starttag und bloomcounter
byte letztertag = 0;
byte letztermonat = 0;

// Variable (struct) zum einstellen der RTC
tmElements_t tm;

// BME 280
Adafruit_BME280 bme; // I2C

// Encoder
#define encoderPinA 2
#define encoderPinB 3
volatile unsigned int encoderPos = 0;  // Encoder counter
volatile bool rotating  = false;
volatile bool A_set     = false;
volatile bool B_set     = false;
unsigned int lastReportedPos = 1;

byte anaus = 0;
byte temp_bereich = 0;
byte rlf_bereich = 0;
byte zeitstellen = 0;

I2CSoilMoistureSensor bodensensor; // setze Var fuer Bodenfeuchtesensor (chirp)

// Bekanntmachung der Relais
#define luft_relay    9   // luft_relay = LTI
#define licht_relay_p 7   // licht_relay = zur Steuerung des Hauptleuchtmittels
#define lsr_relay_p   6   // lsr_relay = zur Steuerung der LSR der Jungpflanzen
#define ventilator    5   // vetilator = zur steuerung des Relais Umluftventilators
#define irrigation    11  // wasser_relay = autobewaesserung

// Custom Caracter
enum { MOON, SUN, THERMO, RLF, WATER_ON, WATER_OFF, VENTI_I, VENTI_II };
byte Moon[8]      = { 0b00000, 0b01110, 0b10101, 0b11111, 0b10001, 0b01110, 0b00000, 0b00000 };
byte Sun[8]       = { 0b00000, 0b00100, 0b10101, 0b01110, 0b01110, 0b10101, 0b00100, 0b00000 };
byte Thermo[8]    = { 0b00100, 0b01010, 0b01010, 0b01110, 0b01110, 0b11111, 0b11111, 0b01110 };
byte Rlf[8]       = { 0b00100, 0b00100, 0b01110, 0b01110, 0b11111, 0b11001, 0b11111, 0b01110 };
byte Water_on[8]  = { 0b11100, 0b01000, 0b11110, 0b11111, 0b00011, 0b00011, 0b00000, 0b00011 };
byte Water_off[8] = { 0b11100, 0b01000, 0b11100, 0b11110, 0b00011, 0b00011, 0b00000, 0b00000 };
byte Venti_I[8]   = { 0b00100, 0b01010, 0b00000, 0b00100, 0b10001, 0b11011, 0b00000, 0b00000 };
byte Venti_II[8]  = { 0b00000, 0b11011, 0b10001, 0b00100, 0b00000, 0b01010, 0b00100, 0b00000 };

// GY-30 Luxmeter
void BH1750_Init(int address){

  Wire.beginTransmission(address);
  Wire.write(0x10);  // 1 [lux] aufloesung
  Wire.endTransmission();

}

byte BH1750_Read(int address, byte *buff){

  byte i = 0;

  Wire.beginTransmission(address);
  Wire.requestFrom(address, 2);
  
  while(Wire.available()){
    
    buff[i] = Wire.read();
    i++;

  }
  
  Wire.endTransmission();

  return i;

}

//****************************hier gehen die einzelnen Funktionen los

void displayTime(){ // anzeige der Zeit und Datum auf dem Display
  
  if(hintergrund == 1){

    lcd.setCursor(0, 0);
    
    if(hour() < 10)
      lcd.print("0");
 
    lcd.print(hour(), DEC);
    
    lcd.print(":");
    
    if(minute() < 10)
      lcd.print("0");
      
    lcd.print(minute(), DEC);
    
    lcd.print(":");
    
    if(second() < 10)
      lcd.print("0");

    lcd.print(second(), DEC);
    lcd.print(" ");

    const char c_dayOfWeek[7][3]={"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
    lcd.print(c_dayOfWeek[weekday()]);
    
    lcd.print(" ");
    
    if(day() < 10)
      lcd.print("0");

    lcd.print(day(), DEC);
    lcd.print(" ");
 
    const char c_Month[12][4]={"Jan", "Feb", "Mar", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Mov", "Dec"};
    lcd.print(c_Month[month()]);

  }

}

void bme280(){ // Anzeige der Temp und RLF auf dem Display

  if(hintergrund == 1){

    static unsigned long m;
  
    if(millis() - m > 3000){
      
      m = millis();

      // DISPLAY DATA
      lcd.setCursor(0, 1);           // setze curserposition
      lcd.write(THERMO);    // zeichne thermometer auf dem Display, siehe auch abschnitt Custom Caracter bzw. void setup
      lcd.print(F(" "));
      lcd.print(int(bme.readTemperature()));
      lcd.print((char)223);
      lcd.print(F("C "));
      lcd.print(F(" "));
      lcd.write(RLF);    // zeichne Wassertropfen auf dem Display, siehe auch abschnitt Custom Caracter bzw. void setup
      lcd.print(F(" "));
      lcd.print(int(bme.readHumidity()));
      lcd.print(F("%"));
      lcd.print(F("   "));
      
    }
    
  }
  
}

void DS3231temp(){  // hole und zeige auf dem Display die Case Temperatur der RTC

  if(hintergrund == 1){

    lcd.setCursor(0, 3);
    lcd.print(F("Case:"));
    lcd.print(RTC.temperature());
    lcd.print((char)223);
    lcd.print(F("C"));
    
  }

}

void LTI(){ // die Funtion des Rohrventilators 

  static unsigned long m;

  static double ltitemp;
  static double ltirlf;

  if(millis() - m > 10000){
    
    m = millis();
    ltitemp = bme.readTemperature();
    ltirlf = bme.readHumidity();
    
  }
  
  // Pruefe im LSR oder Grow Modus Temperatur und RLF ist die Temp unter 24 Grad C oder unter RLF unter 55%
  // bleibt Stufentrafo gedimmt (z.B. 80V)
  // ist Temp oder gleich oder höher wird auf hoechste stufe (z.B. 190V) geschaltet.
  
  if(setings_a.lichtmodus == LSR){ 
    
    if(ltitemp < setings_a.lsr_temp)
      digitalWrite(luft_relay, LOW);

    if(ltirlf < setings_a.lsr_rlf){
      
      digitalWrite(luft_relay, LOW);
    
    } else {
    
      digitalWrite(luft_relay, HIGH);
    
    }
  
  }
  
  // Pruefe im LSR oder Grow Modus Temperatur und RLF ist die Temp unter 24 Grad C oder unter RLF unter 55%
  // bleibt Stufentrafo gedimmt (z.B. 80V)
  // ist Temp oder gleich oder höher wird auf hoechste stufe (z.B. 190V) geschaltet.
  if(setings_a.lichtmodus == GROW){ 
    
    if(ltitemp < setings_a.grow_temp)
      digitalWrite(luft_relay, LOW);
    
    if(ltirlf < setings_a.grow_rlf){
      
      digitalWrite(luft_relay, LOW);
      
    } else {
      
      digitalWrite(luft_relay, HIGH);
    
    }
  
  }

  // Pruefe im Uebergangsmodus Grow>Bloom Temperatur und RLF
  if(setings_a.lichtmodus == BLOOM){
    
    if(ltitemp < setings_a.grow_temp)
      digitalWrite(luft_relay, LOW);
    
    if(ltirlf < setings_a.grow_rlf){
      
      digitalWrite(luft_relay, LOW);
      
    } else {
      
      digitalWrite(luft_relay, HIGH);
    
    }
  
  }

  // Pruefe im Bloom Modus Temperatur und RLF ist die Temp unter 22 Grad C oder unter RLF unter 40%
  // bleibt Stufentrafo gedimmt (z.B. 80V)
  // ist Temp oder RLF gleich oder höher wird auf hoechste stufe (z.B. 190V) geschaltet.
  if(setings_a.lichtmodus == BLOOM){
    
    if(ltitemp < setings_a.bloom_temp)
      digitalWrite(luft_relay, LOW);
 
    if(ltirlf < setings_a.bloom_rlf){
      
      digitalWrite(luft_relay, LOW);
      
    } else {
      
      digitalWrite(luft_relay, HIGH);
      
    }
    
  }
  
}

void gy30(){ // Luxmeter
  
  if(hintergrund == 1){

    byte buff[2];
    float valf = 0;
    
    if(BH1750_Read(BH1750_address, buff) == 2){
      
      valf = ((buff[0] << 8) | buff[1]);

      lcd.setCursor(0, 2);
      lcd.print(F("LUX: "));
      
      if(valf < 1000)
        lcd.print(" ");
      
      if(valf < 100)
        lcd.print(" ");
      
      if(valf < 10)
        lcd.print(" ");

      lcd.print(valf, 2);
     
    }
    
  }
  
}

void displaybeleuchtung(){ // hier wird das Display ein und ausgeschaltet

  // Lese status des display buttons und schalte wenn betätigt fuer 30 sek. an
  bool                 buttonState = digitalRead(buttonPin);
  
  static bool          buttonGedrueckt;
  static unsigned long buttonZeit;

  // Wenn der Wechseltaster gedrückt ist...
  if(buttonState == HIGH){

    buttonZeit = millis();  // aktualisiere tasterZeit
    buttonGedrueckt = true; // speichert, dass Taster gedrückt wurde
    
  }

  // Wenn Taster gedrückt wurde die gewählte entprellZeit vergangen ist soll Lichtmodi und gespeichert werden ...
  if((millis() - buttonZeit > entprellZeit) && buttonGedrueckt == true){
    
    hintergrund++;  // LCD Seite wird um +1 erhöht
    buttonGedrueckt = false;  // setzt gedrückten Taster zurück
    
  }

  if(hintergrund == 1){ // display ist an
    
    lcd.display();
    lcd.setBacklight(255);

  } else if(hintergrund == 2){ // display ist ganz aus
    
    lcd.setBacklight(0);
    lcd.noDisplay();
    
  } else if(hintergrund == 3){ // setzt die Funtion wieder auf anfang
    
    hintergrund = 1;

  }
  
}

void tagec(){ // bluete Tagecounter

  bool relay_switching = digitalRead(licht_relay_p);
  static bool last_relay_state;
  
  if(relay_switching != last_relay_state){
    
    if(relay_switching == LOW)
      setings_a.bloom_counter++;
      
    write_EEPROM = true;
    
  }
  
  last_relay_state = relay_switching;
  
}

void doEncoderA(){

  if(rotating)
    delay(1);  // debounce für Encoder Pin A
    
  if(digitalRead(encoderPinA) != A_set){ // debounce erneut
    
    A_set = !A_set;
    
    // stelle counter + 1 im Uhrzeigersinn
    if( A_set && !B_set)
      encoderPos += 1;
      
    rotating = false;
    
  }
  
}

void doEncoderB(){
  
  if(rotating)
    delay(1);
    
  if(digitalRead(encoderPinB) != B_set){
    
    B_set = !B_set;
    
    //  stelle counter - 1 gegen Uhrzeigersinn
    if( B_set && !A_set )
      encoderPos -= 1;
      
    rotating = false;
    
  }
  
}

/* Lese EEPROM in setings oder schreibe defaults in EEPROM */
void readEEPROM(){

  /* Lese EEPROM structure in speicher*/
  EEPROM.get(0, setings_a);

  if(setings_a.MAGIC_NUMBER != MAGIC_NUMBER ){ // Vergleiche Magic number wenn ungleich schreibe EEPROM mit defaults neu.

    setings_a = setings_b; // settings_a ist b (defaults)
    EEPROM.put(0, setings_b); // Schreibe settings_b, enthält default einstellungen.

  } else {

    setings_b = setings_a; // Überschreibe default setings_b mit custom a

  }
  
}

void setup(){

  Serial.begin(9600);

  /* Lese EEPROM in setings oder schreibe defaults in EEPROM */
  readEEPROM();

  Wire.begin();
  lcd.begin(20, 4); // stelle LCD groesse ein
  bme.begin();

  setSyncProvider(RTC.get); // Function to get the time from the RTC
  setSyncInterval(5000);    // Set the number of seconds between re-sync (5 Minuten)
  RTC.read(tm);

  // Splashscreen
  lcd.setCursor(0, 0);
  lcd.print(F("..:: Gro-Matic ::.."));
  lcd.setCursor(0, 2);
  lcd.print(F("  BME-280 Edition"));
  lcd.setCursor(0, 3);
  lcd.print(F(" V. 0.9.9.9 by zrox"));
  Alarm.delay(3000);
  lcd.clear();

  BH1750_Init(BH1750_address);
  Alarm.delay(500);

  digitalWrite(licht_relay_p, HIGH);  // alle Relais Pins beim Start auf HIGH setzen und damit ausschalten.
  digitalWrite(lsr_relay_p, HIGH);
  digitalWrite(luft_relay, HIGH);
  digitalWrite(ventilator, HIGH);
  digitalWrite(irrigation, HIGH);

  pinMode(luft_relay,  OUTPUT);  // alle Relais Pins als ausgang setzen
  pinMode(licht_relay_p,  OUTPUT);
  pinMode(lsr_relay_p,  OUTPUT);
  pinMode(ventilator, OUTPUT);
  pinMode(irrigation, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);  // den backlight Taster als Input setzen
  pinMode(wechslertPin, INPUT_PULLUP);  // Modus-Taster Pin wird als Eingang gesetzt
  pinMode(screenPin, INPUT_PULLUP);  // Modus-Taster Pin wird als Eingang gesetzt
  
  pinMode(encoderPinA, INPUT);
  pinMode(encoderPinB, INPUT);
  
  digitalWrite(encoderPinA, HIGH);
  digitalWrite(encoderPinB, HIGH);
  
  attachInterrupt(0, doEncoderA, CHANGE); // Encoder pin an interrupt 0 (pin 2)
  attachInterrupt(1, doEncoderB, CHANGE); // Encoder pin an interrupt 1 (pin 3)

  // erstelle die Custom character
  lcd.createChar(MOON,      Moon);
  lcd.createChar(SUN,       Sun);
  lcd.createChar(THERMO,    Thermo);
  lcd.createChar(RLF,       Rlf);
  lcd.createChar(WATER_ON,  Water_on);
  lcd.createChar(WATER_OFF, Water_off);
  lcd.createChar(VENTI_I,   Venti_I);
  lcd.createChar(VENTI_II,  Venti_II);

}

void loop(){
  
  //********************************************************************
  LTI();  // ruft die einfache LTI steuerung auf und prueft Temp und RLF und schaltet den Stufentrafo zwischen zwei Stufen.
  displaybeleuchtung();
  Alarm.delay(0);
  //********************************************************************
  rotating = true;  // reset the debouncer

  if(lastReportedPos != encoderPos)
    lastReportedPos = encoderPos;

  // ab hier Taster des Encoders
  bool wechslertStatus = digitalRead(wechslertPin); // aktuelles Signal vom Eingangspin des Wechslertasters

  // Wenn der Wechseltaster gedrückt ist...
  if(wechslertStatus == HIGH){
    
    wechslertZeit = millis();  // aktualisiere tasterZeit
    wechslertGedrueckt = 1;  // speichert, dass Taster gedrückt wurde
    
  }

  //***********************************************

  if(setings_a.lichtmodus == LSR){

    if((hour() >= setings_a.lsr_an) && (hour() < setings_a.lsr_aus)){
      
      digitalWrite(lsr_relay_p, LOW); //schaltet lsr um 5 Uhr an und um 22:59:59 Uhr aus
      digitalWrite(licht_relay_p, HIGH); //schaltet ndl Relais aus sollten sie noch an sein

      // Umluftventilator alle 15 minuten einschalten wenn licht an
      if((minute() >= 15 && minute() <= 29 ) || (minute() >= 45 && minute() <= 59)){
        
        digitalWrite(ventilator, LOW);
        
      } else {
        
        digitalWrite(ventilator, HIGH);
        
      }
      
    } else { 
      
      digitalWrite(lsr_relay_p, HIGH);
      
      if((hour() >= setings_a.grow_licht_aus) & (hour() < setings_a.grow_licht_an) || (hour() >= setings_a.bloom_licht_aus) & (hour() <= setings_a.bloom_licht_an))
        digitalWrite(licht_relay_p, HIGH);  //schaltet lsr Relais aus sollten sie noch an sein
      
      if((minute() >= 15) && (minute() <= 19)){ // schaltet Ventilator im Nachtmodus 1 x jede Stunde fuer 5 Min. an

        digitalWrite(ventilator, LOW);
        
      } else {
        
        digitalWrite(ventilator, HIGH);
        
      }
      
    }

  } else if(setings_a.lichtmodus == GROW){

    if((hour() >= setings_a.grow_licht_an) && (hour() < setings_a.grow_licht_aus)){ 
      
      digitalWrite(licht_relay_p, LOW); //schaltet ndl im Grow modus 18h licht um 5 Uhr an und um 22:59:59 Uhr aus
      digitalWrite(lsr_relay_p, HIGH);  //schaltet lsr Relais aus sollten sie noch an sein

      // Umluftventilator alle 15 minuten einschalten wenn licht an
      if((minute() >= 15 && minute() <= 29 ) || (minute() >= 45 && minute() <= 59) ){
        
        digitalWrite(ventilator, LOW);
        
      } else {
        
        digitalWrite(ventilator, HIGH);
        
      }

    } else {
      
      digitalWrite(licht_relay_p, HIGH);
      
      if((hour() >= setings_a.lsr_aus) & (hour() < setings_a.lsr_an))
        digitalWrite(lsr_relay_p, HIGH);  //schaltet lsr Relais aus sollten sie noch an sein
      
      if((minute() >= 15) && (minute() <= 19)){ // schaltet Ventilator im Nachtmodus 1 x jede Stunde fuer 5 Min. an

        digitalWrite(ventilator, LOW);
        
      } else {
        
        digitalWrite(ventilator, HIGH);
        
      }
      
    }

  } else if(setings_a.lichtmodus == BLOOM){

    if((hour() >= setings_a.grow_licht_an) && (hour() < setings_a.grow_licht_aus)){
      
      digitalWrite(licht_relay_p, LOW); //schaltet ndl im Grow modus 18h licht um 5 Uhr an und um 22:59:59 Uhr aus
      digitalWrite(lsr_relay_p, HIGH);  //schaltet lsr Relais aus sollten sie noch an sein
      
      // Umluftventilator alle 15 minuten einschalten wenn licht an
      if((minute() >= 15 &&  minute() <= 29) || (minute() >= 45 && minute() <= 59)){
        
        digitalWrite(ventilator, LOW);
        
      } else {
        
        digitalWrite(ventilator, HIGH);
        
      }
      
    } else { 
      
      setings_a.lichtmodus = BLOOM;
      write_EEPROM = true;
      
    }
    
  } else if(setings_a.lichtmodus == BLOOM){
    
    tagec();

    if((hour() >= setings_a.bloom_licht_an) && (hour() < setings_a.bloom_licht_aus)){
      
      digitalWrite(licht_relay_p, LOW); //schaltet ndl im Bloom modus 12h licht um 5 Uhr an und um 16:59:59 Uhr aus
      digitalWrite(lsr_relay_p, HIGH);  //schaltet lsr Relais aus sollten sie noch an sein

      // Umluftventilator alle 15 minuten einschalten wenn licht an
      if((minute() >= 15 && minute() <= 29 ) || (minute() >= 45 && minute() <= 59)){
        
        digitalWrite(ventilator, LOW);
        
      } else {
        
        digitalWrite(ventilator, HIGH);
        
      }
      
    } else { 
      
      digitalWrite(licht_relay_p, HIGH);
      
      if((hour() >= setings_a.grow_licht_aus) & (hour() < setings_a.grow_licht_an) || (hour() >= setings_a.lsr_aus) & (hour() < setings_a.lsr_an) )
        digitalWrite(lsr_relay_p, HIGH);  //schaltet lsr Relais aus sollten sie noch an sein
      
      if((minute() >= 15) && (minute() <= 19)){ // schaltet Ventilator im Nachtmodus 1 x jede Stunde fuer 5 Min. an
 
        digitalWrite(ventilator, LOW);
        
      } else {
        
        digitalWrite(ventilator, HIGH);
        
      }
      
    }

  } // Lichtmodus Ende

  // Autobewaesserung

  if(setings_a.autowasser == 1){
  } // Autobewaesserung Ende

  // ab hier Taster abfrage fuer LCD menue
  screenStatus = digitalRead(screenPin);

  // Wenn der Wechseltaster gedrückt ist...
  if(screenStatus == HIGH){
    
    screenZeit = millis();  // aktualisiere tasterZeit
    screenGedrueckt = 1;  // speichert, dass Taster gedrückt wurde
    
  }

  // Wenn Taster gedrückt wurde die gewählte entprellZeit vergangen ist soll Lichtmodi und gespeichert werden ...
  if((millis() - screenZeit > entprellZeit) && screenGedrueckt == 1){
    
    screen++;  // LCD Seite wird um +1 erhöht
    screenGedrueckt = 0;  // setzt gedrückten Taster zurück
    lcd.clear();
    
  }

  //******************************************************************

  if(screen == 1){
    
    // Rufe funktionen für Seite 1 auf
    displayTime();        // zeige die RTC Daten auf dem LCD display,
    bme280();          // zeige temp und rlf auf dem LCD display,
    DS3231temp();  // prüfe gehaeuse temp und gib sie auf dem display aus
    gy30();  // Luxmeter

    //GY-30
    if(second() >= 0){
      
      void BH1750_Init(int address);
      
    }

    // Wenn Taster gedrückt wurde die gewählte entprellZeit vergangen ist soll Lichtmodi und gespeichert werden ...
    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      setings_a.lichtmodus++;  // lichtmodus wird um +1 erhöht
      write_EEPROM = true;
      wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
    
    }

    //*********Ventilator Icon

    static bool           ventiicon;
    static unsigned long  previousMillis;
    unsigned long         currentMillis = millis();
    const uint8_t         OnTime = 300;
    
    if(digitalRead(ventilator) == LOW){
      
      if((ventiicon == HIGH) && (currentMillis - previousMillis >= OnTime)){
        
        ventiicon = LOW;
        previousMillis = currentMillis;
        lcd.setCursor(17, 2);
        lcd.write(VENTI_I);
        
      }

      if((ventiicon == LOW) && (currentMillis - previousMillis >= OnTime)){
        
        ventiicon = HIGH;
        previousMillis = currentMillis;
        lcd.setCursor(17, 2);
        lcd.write(VENTI_II);
        
      }
    }
    
    if(digitalRead(ventilator) == HIGH){
      
      lcd.setCursor(17, 2);
      lcd.print(F(" "));
      
    }
    
    //*************************************Programm-Modis**************************************

    // Wenn Lichtmodus 0 ist, starte im LSR modus
    if(setings_a.lichtmodus == LSR){
      
      lcd.setCursor(10, 3);
      lcd.print(F("  LSR Mode"));
      relay_lsr_switching = digitalRead(lsr_relay_p);
      
      if(relay_lsr_switching == LOW){
        
        lcd.setCursor(19, 2);
        lcd.write(SUN);
        
      }
      
      if(relay_lsr_switching == HIGH){
        
        lcd.setCursor(19, 2);
        lcd.write(MOON);
      
      }
      
    } else if(setings_a.lichtmodus == GROW){
      
      lcd.setCursor(10, 3);
      lcd.print(F(" Grow Mode"));
      relay_grow_switching = digitalRead(licht_relay_p);
      
      if(relay_grow_switching == LOW){
        
        lcd.setCursor(19, 2);
        lcd.write(SUN);
        
      }
      
      if(relay_grow_switching == HIGH){
        
        lcd.setCursor(19, 2);
        lcd.write(MOON);
        
      }
      
    } else if(setings_a.lichtmodus == BLOOM){
      
      lcd.setCursor(10, 3);
      lcd.print(F("Bloom Mode"));
      relay_bloom_switching = digitalRead(licht_relay_p);
      
      if(relay_bloom_switching == LOW){
        
        lcd.setCursor(19, 2);
        lcd.write(SUN);
        
      }
      
      if(relay_bloom_switching == HIGH){
        
        lcd.setCursor(19, 2);
        lcd.write(MOON);
        
      }
      
    } else { // Wenn der Lichtmodus auf 3 springt, setzte ihn wieder zurück auf 0 um von vorne zu beginnen

      setings_a.lichtmodus = LSR;
      
    } // Lichtmodus Ende

  } else if(screen == 2){

    // Wenn Taster gedrückt wurde die gewählte entprellZeit vergangen ist soll Tagecounter gelöscht werden ...
    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      wechslertGedrueckt = 0;      // setzt gedrückten Taster zurück
      
      for(int i = 0; i < 512; i++)
        EEPROM.write(i, 0);
        
      asm volatile ("jmp 0");
      
    }
    
    lcd.setCursor(0, 0);
    lcd.print(F("Starttag am: "));
    
    if(letztertag < 10)  
      lcd.print(F("0"));
    
    lcd.print(letztertag);
    lcd.print(F("."));
    
    if(letztermonat < 10)
      lcd.print(F("0"));
    
    lcd.print(letztermonat);
    lcd.setCursor(0, 1);
    lcd.print(F("Bl"));
    lcd.print((char)0xF5);
    lcd.print(F("tetag:"));
    lcd.print(setings_a.bloom_counter);
    lcd.setCursor(0, 2);
    lcd.print(F("dr"));
    lcd.print((char)0xF5);
    lcd.print(F("cke Enc.taste zum"));
    lcd.setCursor(0, 3);
    lcd.print(F("Speicher l"));
    lcd.print((char)0xEF);
    lcd.print(F("schen."));

  } else if(screen == 3){
    
    // Wenn Taster gedrückt wurde die gewählte entprellZeit vergangen ist soll Lichtmodi und gespeichert werden ...
    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      setings_a.autowasser++;
      write_EEPROM = true;
      wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
      
    }

    //*************************************Programm-Modis**************************************

    // Wenn Lichtmodus 0 ist, starte im LSR modus
    if(setings_a.autowasser == true){
      
      lcd.setCursor(0, 2);
      lcd.print(F("Autobew"));
      lcd.print((char)0xE1);
      lcd.print(F("sserung:"));
      lcd.setCursor(0, 3);
      lcd.write(WATER_OFF);
      lcd.print(F(" aus"));
      
    } else if(setings_a.autowasser == false){
      
      lcd.setCursor(0, 2);
      lcd.print(F("Autobew"));
      lcd.print((char)0xE1);
      lcd.print(F("sserung:"));
      lcd.setCursor(0, 3);
      lcd.write(WATER_ON);
      lcd.print(F(" an "));
      
    } else {
      
      setings_a.autowasser = true;
      
    } // Autobewaesserung Ende

    lcd.setCursor(0, 0);
    lcd.print(F("Boden "));
    lcd.write(RLF);
    lcd.print(F(" "));
    lcd.print(bodensensor.getCapacitance()); //lese bodensensor
    lcd.setCursor(0, 1);
    lcd.print(F("Boden "));
    lcd.write(THERMO);
    lcd.print(F(" "));
    lcd.print(bodensensor.getTemperature() / (float)10); //lese temperatur register des bodensensors
    lcd.print((char)223);
    lcd.print(F("C "));

  } else if(screen == 4){

    lcd.setCursor(0, 0);
    lcd.print(F("Schaltzeiten Licht"));
    lcd.setCursor(0, 1);
    lcd.print(F("LSR:  "));
    lcd.print(setings_a.lsr_an);
    lcd.print(F(":00-"));
    lcd.print(setings_a.lsr_aus);
    lcd.print(F(":00 Uhr"));
    lcd.setCursor(0, 2);
    lcd.print(F("Grow: "));
    lcd.print(setings_a.grow_licht_an);
    lcd.print(F(":00-"));
    lcd.print(setings_a.grow_licht_aus);
    lcd.print(F(":00 Uhr"));
    lcd.setCursor(0, 3);
    lcd.print(F("Bloom:"));
    lcd.print(setings_a.bloom_licht_an);
    lcd.print(F(":00-"));
    lcd.print(setings_a.bloom_licht_aus);
    lcd.print(F(":00 Uhr"));
    
    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      lcd.clear();
      Alarm.delay(50);
      screen = 7;
      wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
      
    }
    
  } else if(screen == 5){

    lcd.setCursor(0, 0);
    lcd.print(F("eingest. LTI Werte"));
    lcd.setCursor(0, 1);
    lcd.print(F("LSR:  "));
    lcd.print(setings_a.lsr_temp);
    lcd.print((char)223);
    lcd.print(F("C, :"));
    lcd.print(setings_a.lsr_rlf);
    lcd.print(F(" %"));
    lcd.setCursor(0, 2);
    lcd.print(F("Grow: "));
    lcd.print(setings_a.grow_temp);
    lcd.print((char)223);
    lcd.print(F("C, :"));
    lcd.print(setings_a.grow_rlf);
    lcd.print(F(" %"));
    lcd.setCursor(0, 3);
    lcd.print(F("Bloom:"));
    lcd.print(setings_a.bloom_temp);
    lcd.print((char)223);
    lcd.print(F("C, :"));
    lcd.print(setings_a.bloom_rlf);
    lcd.print(F(" %"));
    
    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      lcd.clear();
      Alarm.delay(200);
      temp_bereich = 0;
      rlf_bereich = 0;
      screen = 8;
      wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück

    }
    
  } else if(screen == 6){

    screen = 10; // geht wieder auf seite 1 zurück
  
  } else if(screen == 7){

    if(encoderPos == 24)
      encoderPos = 0;

    if(encoderPos >= 24){
      
      lcd.clear();
      encoderPos = 23;
    
    }

    if(anaus == 0){

      lcd.setCursor(0, 0);
      lcd.print(F("LSR an:    "));
      
      if(encoderPos < 10){
        lcd.print("0");
      }
      
      lcd.print(encoderPos);
      lcd.print(F(" Uhr"));
      lcd.setCursor(0, 1);
      lcd.print(F("Startzeit f"));
      lcd.print((char)0xF5);
      lcd.print(F("r LSR"));
      lcd.setCursor(0, 2);
      lcd.print(F("w"));
      lcd.print((char)0xE1);
      lcd.print(F("hlen. optimale"));
      lcd.setCursor(0, 3);
      lcd.print(F("Dauer 18 Stunden"));

      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        setings_a.lsr_an = encoderPos;
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        write_EEPROM = true;
        lcd.clear();
        anaus++;
        
      }
      
    }

    if(anaus == 1){
      
      lcd.setCursor(0, 0);
      lcd.print(F("LSR aus:   "));
      
      if(encoderPos < 10)
        lcd.print("0");
 
      lcd.print(encoderPos);
      lcd.print(F(" Uhr"));
      lcd.setCursor(0, 1);
      lcd.print(F("Endzeit f"));
      lcd.print((char)0xF5);
      lcd.print(F("r LSR"));
      lcd.setCursor(0, 2);
      lcd.print(F("w"));
      lcd.print((char)0xE1);
      lcd.print(F("hlen. optimale"));
      lcd.setCursor(0, 3);
      lcd.print(F("Dauer 18 Stunden"));
 
      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        if(encoderPos == 0){
          
          encoderPos = 23;
          setings_a.lsr_aus = encoderPos;
          
        } else {
          
          setings_a.lsr_aus = encoderPos;
        
        }
        
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        
        write_EEPROM = true;
        lcd.clear();
        anaus++;
      
      }
    
    }

    if(anaus == 2){
      
      lcd.setCursor(0, 0);
      lcd.print(F("Grow an:   "));
      
      if(encoderPos < 10)
        lcd.print("0");

      lcd.print(encoderPos);
      lcd.print(F(" Uhr"));
      lcd.setCursor(0, 1);
      lcd.print(F("Startzeit f"));
      lcd.print((char)0xF5);
      lcd.print(F("r Grow"));
      lcd.setCursor(0, 2);
      lcd.print(F("w"));
      lcd.print((char)0xE1);
      lcd.print(F("hlen. optimale"));
      lcd.setCursor(0, 3);
      lcd.print(F("Dauer 18 Stunden"));
      
      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        setings_a.grow_licht_an = encoderPos;
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        write_EEPROM = true;
        lcd.clear();
        anaus++;
        
      }
      
    }
    
    if(anaus == 3){
      
      lcd.setCursor(0, 0);
      lcd.print(F("Grow aus:  "));
      
      if(encoderPos < 10)
        lcd.print("0");
 
      lcd.print(encoderPos);
      lcd.print(F(" Uhr"));
      lcd.setCursor(0, 1);
      lcd.print(F("Endzeit f"));
      lcd.print((char)0xF5);
      lcd.print(F("r Grow"));
      lcd.setCursor(0, 2);
      lcd.print(F("w"));
      lcd.print((char)0xE1);
      lcd.print(F("hlen. optimale"));
      lcd.setCursor(0, 3);
      lcd.print(F("Dauer 18 Stunden"));
      
      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        if(encoderPos == 0){
          
          encoderPos = 23;
          setings_a.grow_licht_aus = encoderPos;
        
        } else {
          
          setings_a.grow_licht_aus = encoderPos;
          
        }
        
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        write_EEPROM = true;
        lcd.clear();
        anaus++;
        
      }
      
    }
    
    if(anaus == 4){
      
      lcd.setCursor(0, 0);
      lcd.print(F("Bloom an:  "));
      
      if(encoderPos < 10)
        lcd.print("0");

      lcd.print(encoderPos);
      lcd.print(F(" Uhr"));
      lcd.setCursor(0, 1);
      lcd.print(F("Startzeit f"));
      lcd.print((char)0xF5);
      lcd.print(F("r Bloom"));
      lcd.setCursor(0, 2);
      lcd.print(F("w"));
      lcd.print((char)0xE1);
      lcd.print(F("hlen. optimale"));
      lcd.setCursor(0, 3);
      lcd.print(F("Dauer 12 Stunden"));

      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        setings_a.bloom_licht_an = encoderPos;
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        write_EEPROM = true;
        lcd.clear();
        anaus++;
        
      }

    }
 
    if(anaus == 5){
      
      lcd.setCursor(0, 0);
      lcd.print(F("Bloom aus: "));
      
      if(encoderPos < 10)        
        lcd.print("0");

      lcd.print(encoderPos);
      lcd.print(F(" Uhr"));
      lcd.setCursor(0, 1);
      lcd.print(F("Startzeit f"));
      lcd.print((char)0xF5);
      lcd.print(F("r Bloom"));
      lcd.setCursor(0, 2);
      lcd.print(F("w"));
      lcd.print((char)0xE1);
      lcd.print(F("hlen. optimale"));
      lcd.setCursor(0, 3);
      lcd.print(F("Dauer 12 Stunden"));
      
      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        if(encoderPos == 0){
          
          encoderPos = 23;
          setings_a.bloom_licht_aus = encoderPos;
          
        } else {
          
          setings_a.bloom_licht_aus = encoderPos;
          
        }
        
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        write_EEPROM = true;
        lcd.clear();
        Alarm.delay(100);
        anaus = 0;
        screen = 4;
        
      }
      
    }

  }  else if(screen == 8){

    if(encoderPos >= 29){
      
      encoderPos = 17;
      
    }
    
    else if(encoderPos <= 16){
      
      lcd.clear();
      encoderPos = 28;
      
    }

    if(temp_bereich == 0){
      
      lcd.setCursor(0, 0);
      lcd.print(F("LSR max. "));
      lcd.write(THERMO);
      lcd.print(F(" :  "));
      lcd.print(encoderPos);
      lcd.print((char)223);
      lcd.print(F("C"));
      lcd.setCursor(0, 1);
      lcd.print(F("optimaler bereich"));
      lcd.setCursor(0, 2);
      lcd.print(F("zwischen 22"));
      lcd.print((char)223);
      lcd.print(F("C"));
      lcd.print(F(" - 25"));
      lcd.print((char)223);
      lcd.print(F("C"));
      
      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        setings_a.lsr_temp = encoderPos;
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        write_EEPROM = true;
        lcd.clear();
        temp_bereich++;
        
      }
      
    }

    if(temp_bereich == 1){
      
      lcd.setCursor(0, 0);
      lcd.print(F("Grow max. "));
      lcd.write(THERMO);
      lcd.print(F(" : "));
      lcd.print(encoderPos);
      lcd.print((char)223);
      lcd.print(F("C"));
      lcd.setCursor(0, 1);
      lcd.print(F("optimaler bereich"));
      lcd.setCursor(0, 2);
      lcd.print(F("zwischen 21"));
      lcd.print((char)223);
      lcd.print(F("C"));
      lcd.print(F(" - 23"));
      lcd.print((char)223);
      lcd.print(F("C"));
      
      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        setings_a.grow_temp = encoderPos;
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        write_EEPROM = true;
        lcd.clear();
        temp_bereich++;
        
      }
      
    }

    if(temp_bereich == 2){
      
      lcd.setCursor(0, 0);
      lcd.print(F("Bloom max. "));
      lcd.write(THERMO);
      lcd.print(F(" :"));
      lcd.print(encoderPos);
      lcd.print((char)223);
      lcd.print(F("C"));
      lcd.setCursor(0, 1);
      lcd.print(F("optimaler bereich"));
      lcd.setCursor(0, 2);
      lcd.print(F("zwischen 20"));
      lcd.print((char)223);
      lcd.print(F("C"));
      lcd.print(F(" - 22"));
      lcd.print((char)223);
      lcd.print(F("C"));
      
      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        setings_a.bloom_temp = encoderPos;
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        write_EEPROM = true;
        lcd.clear();
        screen = 9;
        
      }
      
    }

  } else if(screen == 9){

    if(encoderPos >= 71){
      
      encoderPos = 15;
      
    } else if(encoderPos <= 14){
      
      lcd.clear();
      encoderPos = 70;
      
    }

    if(rlf_bereich == 0){
      
      lcd.setCursor(0, 0);
      lcd.print(F("LSR max. "));
      lcd.write(RLF);
      lcd.print(F(" : "));
      lcd.print(encoderPos);
      lcd.print(F(" %"));
      lcd.setCursor(0, 1);
      lcd.print(F("optimaler bereich"));
      lcd.setCursor(0, 2);
      lcd.print(F("zwischen 55 % - 60 %"));
      
      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        setings_a.lsr_rlf = (double) encoderPos;
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        write_EEPROM = true;
        lcd.clear();
        rlf_bereich++;
        
      }
      
    }
    
    if(rlf_bereich == 1){
      
      lcd.setCursor(0, 0);
      lcd.print(F("Grow max. "));
      lcd.write(RLF);
      lcd.print(F(" : "));
      lcd.print(encoderPos);
      lcd.print(F(" %"));
      lcd.setCursor(0, 1);
      lcd.print(F("optimaler bereich"));
      lcd.setCursor(0, 2);
      lcd.print(F("zwischen 50 % - 55 %"));
      
      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        setings_a.grow_rlf = (double) encoderPos;
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        write_EEPROM = true;
        lcd.clear();
        rlf_bereich++;
        
      }
      
    }
    
    if(rlf_bereich == 2){
      
      lcd.setCursor(0, 0);
      lcd.print(F("Bloom max. "));
      lcd.write(RLF);
      lcd.print(F(" : "));
      lcd.print(encoderPos);
      lcd.print(F(" %"));
      lcd.setCursor(0, 1);
      lcd.print(F("optimal nicht "));
      lcd.print((char)0xF5);
      lcd.print(F("ber"));
      lcd.setCursor(0, 2);
      lcd.print(F("40 % RLF"));
      
      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        setings_a.bloom_rlf = (double) encoderPos;
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        write_EEPROM = true;
        lcd.clear();
        rlf_bereich++;
        
      }
      
    }
    
    if(rlf_bereich == 3){
      
      temp_bereich = 0;
      rlf_bereich = 0;
      screen = 5;
      
    }

  } else if(screen == 10){

    if(encoderPos > 1){
      
      encoderPos = 1;
      
    } else if(encoderPos < 0){
      
      lcd.clear();
      encoderPos = 0;
      
    }
    
    lcd.setCursor(0, 0);
    lcd.print(F("RTC einstellen?"));
    lcd.setCursor(0, 1);
    
    switch(encoderPos){
      
      case 0:
        lcd.print("nein");
        break;
      case 1:
        lcd.print("ja  ");
        break;
        
    }
    
    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      wechslertGedrueckt = 0;
      
      if(encoderPos == 0)
        screen = 13;
 
      if(encoderPos == 1)
        screen = 12;

    }
    
  } else if(screen == 11){
    
    screen = 1;
    
  } else if(screen == 12){

    if(zeitstellen == 0){
      
      if(encoderPos >= 32){
        
        encoderPos = 1;
        
      } else if(encoderPos <= 0){
        
        lcd.clear();
        encoderPos = 31;
        
      }

      lcd.setCursor(0, 0);
      lcd.print(F("Tag einstellen:"));
      lcd.setCursor(0, 1);
      
      if(encoderPos < 10)
        lcd.print("0");
      
      lcd.print(encoderPos);
      lcd.print(F("."));
      
      if(tm.Month < 10)
        lcd.print("0");
 
      lcd.print(tm.Month);
      lcd.print(F("."));
      lcd.print(tm.Year);

      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        tm.Day = encoderPos;
        lcd.clear();
        zeitstellen++;
        
      }

    }

    if(zeitstellen == 1){
      
      if(encoderPos >= 13){
        
        encoderPos = 1;
        
      } else if(encoderPos <= 0){
        
        lcd.clear();
        encoderPos = 12;
        
      }

      lcd.setCursor(0, 0);
      lcd.print(F("Monat einstellen:"));
      lcd.setCursor(0, 1);
      
      if(tm.Day < 10)
        lcd.print("0");
        
      lcd.print(tm.Day);
      lcd.print(F("."));
      
      if(encoderPos < 10)
        lcd.print("0");

      lcd.print(encoderPos);
      lcd.print(F("."));
      lcd.print(tm.Year);


      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        tm.Month = encoderPos;
        lcd.clear();
        zeitstellen++;
        
      }

    }

    if(zeitstellen == 2){
      
      byte encoderPos = 16;
      
      if(encoderPos <= 15){
        
        lcd.clear();
        encoderPos = 50;
        
      } else if(encoderPos >= 51){
        
        encoderPos = 16;
        
      }

      lcd.setCursor(0, 0);
      lcd.print(F("Jahr einstellen:"));
      lcd.setCursor(0, 1);
      
      if(tm.Day < 10)
        lcd.print("0");

      lcd.print(tm.Day);
      lcd.print(F("."));

      if(tm.Month < 10)
        lcd.print("0");

      lcd.print(tm.Month);
      lcd.print(F("."));
      lcd.print(2000 + encoderPos);

      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        tm.Year = encoderPos;
        lcd.clear();
        zeitstellen++;
        
      }

    }

    if(zeitstellen == 3){
      
      if(encoderPos == 24)
        encoderPos = 0;
 
      if(encoderPos >= 24){
        
        lcd.clear();
        encoderPos = 23;
        
      }

      lcd.setCursor(0, 0);
      lcd.print(F("Stunde einstellen:"));
      lcd.setCursor(0, 1);
 
      if(encoderPos < 10)
        lcd.print("0");
        
      lcd.print(encoderPos);
      lcd.print(F(":"));
      
      if(tm.Minute < 10)
        lcd.print("0");

      lcd.print(tm.Minute);
      lcd.print(F(":"));

      if(tm.Second < 10)
        lcd.print("0");

      lcd.print(tm.Second);

      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        tm.Hour = encoderPos;
        lcd.clear();
        zeitstellen++;
        
      }

    }

    if(zeitstellen == 4){
      
      if(encoderPos == 60){
       
        encoderPos = 0;
        
      } else if(encoderPos == 65535){
        
        lcd.clear();
        encoderPos = 59;
        
      }

      lcd.setCursor(0, 0);
      lcd.print(F("Minute einstellen:"));
      lcd.setCursor(0, 1);
      
      if(tm.Hour < 10)
        lcd.print("0");

      lcd.print(tm.Hour);
      lcd.print(F(":"));
      
      if(encoderPos < 10)
        lcd.print("0");

      lcd.print(encoderPos);
      lcd.print(F(":"));
      if(tm.Second < 10)
        lcd.print("0");

      lcd.print(tm.Second);

      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        tm.Minute = encoderPos;
        lcd.clear();
        zeitstellen++;
        
      }

    }

    if(zeitstellen == 5){
      
      lcd.setCursor(0, 0);

      const char c_dayOfWeek[7][11]={ "Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
      lcd.print(c_dayOfWeek[weekday(encoderPos)]);
 
      lcd.setCursor(0, 1);
      
      if(tm.Hour < 10)
        lcd.print("0");

      lcd.print(tm.Hour);
      lcd.print(F(":"));
      
      if(tm.Minute < 10)
        lcd.print("0");

      lcd.print(tm.Minute);
      
      lcd.print(F(":"));
      if(tm.Second < 10)
        lcd.print("0");

      lcd.print(tm.Second);
      lcd.print(F(" "));
      
      if(tm.Day < 10)
        lcd.print("0");

      lcd.print(tm.Day);

      lcd.print(F("."));
      
      if(tm.Month < 10)
        lcd.print("0");
        
      lcd.print(tm.Month);
      lcd.print(F("."));
      lcd.print(tm.Year);

      lcd.setCursor(0, 2);
      lcd.print("best");
      lcd.print((char)0xE1);
      lcd.print("tigen um Datum,");
      lcd.setCursor(0, 3);
      lcd.print("Zeit zu setzen.");

      if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
        
        wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
        
        // Set RTC time.
        RTC.write(tm);

        // Set system time from RTC
        setTime(RTC.get());

        lcd.clear();
        screen = 1;
      }

    }
    
  } else if(screen == 13){

    if(encoderPos > 1){
      
      encoderPos = 1;
      
    } else if(encoderPos < 0){
      
      lcd.clear();
      encoderPos = 0;
      
    }
    
    lcd.setCursor(0, 0);
    lcd.print(F("Bew"));
    lcd.print((char)0xE1);
    lcd.print(F("sserungszeiten"));
    lcd.setCursor(0, 1);
    lcd.print(F("einstellen?"));
    lcd.setCursor(0, 2);
    
    switch(encoderPos){
      
      case 0:
        lcd.print("nein");
        break;
      case 1:
        lcd.print("ja  ");
        break;
        
    }
    
    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      wechslertGedrueckt = 0;
      
      if(encoderPos == 0)
        screen = 1;
  
      if(encoderPos == 1){
        
        lcd.clear();
        screen = 14;
        
      }
      
    }
    
  } else if(screen == 14){
    
    if(encoderPos == 24)
      encoderPos = 0;

    if(encoderPos >= 24){
      
      lcd.clear();
      encoderPos = 23;
      
    }

    lcd.setCursor(0, 0);
    lcd.print(F("Startzeit setzen:"));
    lcd.setCursor(0, 1);
    
    if(encoderPos < 10)
      lcd.print("0");
      
    lcd.print(encoderPos);
    lcd.print(F(" Uhr"));
    
    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      setings_a.startwasser = encoderPos;
      wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
      write_EEPROM = true;
      lcd.clear();
      screen = 15;
      
    }
    
  } else if(screen == 15){
    
    if(encoderPos == 60){
      
      encoderPos = 0;
      
    } else if(encoderPos == 65535){
      
      lcd.clear();
      encoderPos = 59;
      
    }

    lcd.setCursor(0, 0);
    lcd.print(F("Start Minute:"));
    lcd.setCursor(0, 1);
    
    if(encoderPos < 10)
      lcd.print("0");

    lcd.print(encoderPos);
    lcd.print(F(" Min."));
    
    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      setings_a.startwassermin = encoderPos;
      wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
      write_EEPROM = true;
      lcd.clear();
      screen = 16;
      
    }
    
  } else if(screen == 16){
    
    if(encoderPos == 60){
      
      encoderPos = 0;
      
    } else if(encoderPos == 65535){
      
      lcd.clear();
      encoderPos = 59;
      
    }

    lcd.setCursor(0, 0);
    lcd.print(F("Ende Minute:"));
    lcd.setCursor(0, 1);
    
    if(encoderPos < 10)
      lcd.print("0");

    lcd.print(encoderPos);
    lcd.print(F(" Min."));

    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      setings_a.auswasser = encoderPos;
      wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
      write_EEPROM = true;
      lcd.clear();
      screen = 17;
      
    }
    
  } else if(screen == 17){
    
    if(encoderPos == 60){
      
      encoderPos = 0;
      
    } else if(encoderPos == 65535){
      
      lcd.clear();
      encoderPos = 59;
      
    }

    lcd.setCursor(0, 0);
    lcd.print(F("Dauer in Sekunden:"));
    lcd.setCursor(0, 1);
    
    if(encoderPos < 10)
      lcd.print("0");
 
    lcd.print(encoderPos);
    lcd.print(F(" Sek."));
 
    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      setings_a.sekauswasser = encoderPos;
      wechslertGedrueckt = 0;  // setzt gedrückten Taster zurück
      write_EEPROM = true;
      lcd.clear();
      screen = 18;
      
    }
    
  } else if(screen == 18){

    lcd.setCursor(0, 0);
    lcd.print(F("Startzeit:"));
    lcd.setCursor(0, 1);
    
    if(setings_a.startwasser < 10)
      lcd.print("0");

    lcd.print(setings_a.startwasser);
    lcd.print(":");

    if(setings_a.startwassermin < 10)
      lcd.print("0");

    lcd.print(setings_a.startwassermin);
    lcd.print(":00 Uhr");
    lcd.setCursor(0, 2);
    lcd.print(F("Ende:"));
    lcd.setCursor(0, 3);

    if(setings_a.startwasser < 10)
      lcd.print("0");

    lcd.print(setings_a.startwasser);
    lcd.print(":");

    if(setings_a.auswasser < 10)
      lcd.print("0");

    lcd.print(setings_a.auswasser);
    lcd.print(":");

    if(setings_a.sekauswasser < 10)
      lcd.print("0");

    lcd.print(setings_a.sekauswasser);

    if((millis() - wechslertZeit > entprellZeit) && wechslertGedrueckt == 1){
      
      wechslertGedrueckt = 0;
      asm volatile ("jmp 0");
      
    }
    
  }

  updateEEPROM();

}

void updateEEPROM(){

  if(write_EEPROM){

    if(memcmp(&setings_a, &setings_b, sizeof setings_a) != 0){ // Do noting if noting to do

      EEPROM.put(0, setings_a);
      setings_b = setings_a;

    }

    write_EEPROM = false;

  }

}
