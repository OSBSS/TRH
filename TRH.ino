//****************************************************************

// OSBSS T/RH datalogger code - v0.04
// Last edited on February 13, 2015

// Added BBSQW, enabling RTC to generate interrupts even on battery
// BBSQW only works when the pullup on the RTC breakout is desoldered, forcing SQW pin to be pulled up to Pro Mini's VCC at all times
// Added code to sync compile time with RTC (has a 15-20 second lag with actual time)
// Idle current consumption now at 0.1mA
// save initial state and direction of port D pins and restore before sleeping each time to avoid excessive power draw

//****************************************************************

#include <EEPROM.h>
#include <DS3234lib3.h>
#include <PowerSaver.h>
#include <SHT15libmod2.h>
#include <SdFat.h>
#include <SPI.h>
#include <Wire.h>
#include <RTClib.h>
#include <RTC_DS3234.h>

PowerSaver chip;  // declare object for PowerSaver class

// Avoid spurious warnings
#undef PROGMEM
#define PROGMEM __attribute__(( section(".progmem.data") ))
#undef PSTR
#define PSTR(s) (__extension__({static prog_char __c[] PROGMEM = (s); &__c[0];}))

// RTC stuff   ******************************
RTC_DS3234 RTC_Sync(10);  // pin 10 is SS pin for RTC
DS3234 RTC;    // declare object for DS3234 class
long interval = 60;  // set logging interval in SECONDS, eg: set 300 seconds for an interval of 5 mins
int dayStart = 15, hourStart = 22, minStart = 10;    // define logger start time: day of the month, hour, minute

// Main code stuff   ******************************
#define POWA 4    // pin 4 supplies power to microSD card breakout and SHT15 sensor
#define RTCPOWA 6    // pin 6 supplies power to DS3234 RTC
#define LED 7  // pin 7 controls LED
byte d, p, d1, p1, bytes=0;

// SD card stuff   ******************************
int SDcsPin = 9; // pin 9 is CS pin for MicroSD breakout
SdFat sd;
SdFile file;
char filename[15] = "log.csv";    // file name is automatically assigned by GUI. Format: "12345678.123". Cannot be more than 8 characters in length

// SHT stuff   ******************************
int SHT_clockPin = 3;  // pin used for SCK on SHT15 breakout
int SHT_dataPin = 5;  // pin used for DATA on SHT15 breakout
SHT15 sensor(SHT_clockPin, SHT_dataPin);  // declare object for SHT15 class

// Interrupt stuff ****************************************************************
ISR(PCINT0_vect)  // Interrupt Vector Routine to be executed when pin 8 receives an interrupt. PCINT0 defines ISRs for PC
{
  PORTB ^= (1<<PORTB1);
}

// setup ****************************************************************
void setup()
{
  Serial.begin(19200); // open serial at 19200 bps
  pinMode(POWA, OUTPUT);
  pinMode(RTCPOWA, OUTPUT);
  pinMode(LED, OUTPUT);
  
  digitalWrite(POWA, HIGH);    // turn on SD card
  digitalWrite(RTCPOWA, HIGH);    // turn on RTC
  
  // Sync compile time with RTC
  SPI.begin();
  RTC_Sync.begin();
  RTC_Sync.adjust(DateTime(__DATE__, __TIME__));
  
  delay(1);    // give some delay to ensure RTC and SD are initialized properly
  
  if(!sd.init(SPI_FULL_SPEED, SDcsPin))  // initialize SD card on the SPI bus - very important
  {
    delay(10);
    SDcardError();
  }
  else
  {
    delay(10);
    file.open(filename, O_CREAT | O_APPEND | O_WRITE);  // open file in write mode and append data to the end of file
    delay(1);
    String time = RTC.timeStamp();    // get date and time from RTC
    file.println();
    file.print("Date/Time,Temp(C),RH(%),Dew Point(C)");    // Print header to file
    file.println();
    PrintFileTimeStamp();
    file.close();    // close file - very important
                     // give some delay by blinking status LED to wait for the file to properly close
    digitalWrite(LED, HIGH);
    delay(10);
    digitalWrite(LED, LOW);
    delay(10);
  }
  RTC.checkInterval(hourStart, minStart, interval); // Check if the logging interval is in secs, mins or hours
  RTC.alarm2set(dayStart, hourStart, minStart);  // Configure begin time
  RTC.alarmFlagClear();  // clear alarm flag
  //RTC.setTempConvRate();  // set temperature conversion  rate to 512 seconds.
                          // This significantly improves battery life of coin cell
                          // at the cost of reduced time accuracy in environments with rapid temperature fluctuations
                          
  chip.sleepInterruptSetup();    // setup sleep function & pin change interrupts on the ATmega328p. Power-down mode is used here
}

// loop ****************************************************************
void loop()
{
  
  digitalWrite(POWA, LOW);  // turn off microSD card to save power
  //pinMode(RTCPOWA, INPUT); // setting RTC power to input to force the pin to high impedance state
  digitalWrite(RTCPOWA, LOW);  // turn off RTC to save power
  delay(1);  // give some delay for SD card and RTC to be low before processor sleeps to avoid it being stuck
  
  if(bytes==0) // save the initial state
  {
    p=PORTD; // save initial states of pins on port D
    d=DDRD; // save initial directions of pins on port D
    bytes=1;
  }
  
  p1=PORTD; // save states of pins on port D
  d1=DDRD; // save directions of pins on port D
  
  PORTD=p; // restore initial states of pins on port D before sleeping
  DDRD=d;  // restore initial directions of pins on port D before sleeping
  
  chip.turnOffADC();    // turn off ADC to save power
  chip.turnOffSPI();  // turn off SPI bus to save power
  //chip.turnOffWDT();  // turn off WatchDog Timer to save power (does not work for Pro Mini - only works for Uno)
  chip.turnOffBOD();    // turn off Brown-out detection to save power
    
  chip.goodNight();    // put processor in extreme power down mode - GOODNIGHT!
                       // this function saves previous states of analog pins and sets them to LOW INPUTS
                       // average current draw on Mini Pro should now be around 0.195 mA (with both onboard LEDs taken out)
                       // Processor will only wake up with an interrupt generated from the RTC, which occurs every logging interval
  
  PORTD=p1; // restore states of pins on port D before sleeping
  DDRD=d1;  // restore directions of pins on port D before sleeping   
  
  chip.turnOnADC();    // enable ADC after processor wakes up
  chip.turnOnSPI();   // turn on SPI bus once the processor wakes up
  delay(1);    // important delay to ensure SPI bus is properly activated
  digitalWrite(RTCPOWA, HIGH);    // turn on RTC
  //pinMode(RTCPOWA, OUTPUT);  // this step is important here as it ensures the SQW pin is now pulled up to VCC
  RTC.alarmFlagClear();    // clear alarm flag
  //RTC.setTempConvRate();
  pinMode(POWA, OUTPUT);
  digitalWrite(POWA, HIGH);  // turn on SD card power
  delay(1);    // give delay to let the SD card and SHT15 get full powa
  
  String time = RTC.timeStamp();    // get date and time from RTC
  SPCR = 0;  // reset SPI control register
    
  for(int i=0; i<5; i++)
    analogRead(A0);  // first few readings from ADC may not be accurate, so they're cleared out here
  delay(1);
  
  // get sensor values
  float adc = averageADC(A0);
  float R = resistance(adc, 10000); // Replace 10,000 ohm with the actual resistance of the resistor measured using a multimeter (e.g. 9880 ohm)
  float temperature = steinhart(R);  // get temperature from thermistor using the custom Steinhart-hart equation by US sensors
  //float temperature_SHT15 = sensor.getTemperature();  // get temperature from SHT15 
  float humidity = sensor.getHumidity(temperature);  // get humidity from SHT15
  float dewPoint = sensor.getDewPoint(temperature, humidity); // calculate dew point using T and RH
  
  pinMode(SDcsPin, OUTPUT);
  if(!sd.init(SPI_FULL_SPEED, SDcsPin))    // very important - reinitialize SD card on the SPI bus
  {
    delay(10);
    SDcardError();
  }
  else
  {
    delay(10);
    file.open(filename, O_WRITE | O_AT_END);  // open file in write mode
    delay(1);
        
    file.print(time);
    file.print(",");
    file.print(temperature, 3);  // print temperature upto 3 decimal places
    file.print(",");
    file.print(humidity, 3);  // print humidity upto 3 decimal places
    file.print(",");
    file.print(dewPoint);
    file.println();
    PrintFileTimeStamp();
    file.close();    // close file - very important
                     // give some delay by blinking status LED to wait for the file to properly close
    digitalWrite(LED, HIGH);
    delay(10);
    digitalWrite(LED, LOW);
    delay(10);  
  }
  RTC.setNextAlarm();      //set next alarm before sleeping
  delay(1);
}

// Averaging ADC values to counter noise in readings  *********************************************
float averageADC(int pin)
{
  float sum=0.0;
  for(int i=0;i<5;i++)
  {
     sum = sum + analogRead(pin);
  }
  float average = sum/5.0;
  return average;
}

// Get resistance ****************************************************************
float resistance(float adc, int true_R)
{
  float R = true_R/(1023.0/adc-1.0);
  return R;
}

// Get temperature from Steinhart equation (US sensors thermistor, 10K, B = 3892) *****************************************
float steinhart(float R)
{
  float A = 0.00113929600457259;
  float B = 0.000231949467390149;
  float C = 0.000000105992476218967;
  float D = -0.0000000000667898975192618;
  float E = log(R);
  
  float T = 1/(A + (B*E) + (C*(E*E*E)) + (D*(E*E*E*E*E)));
  delay(50);
  return T-273.15;
}

// file timestamps
void PrintFileTimeStamp() // Print timestamps to data file. Format: year, month, day, hour, min, sec
{ 
  file.timestamp(T_WRITE, RTC.year, RTC.month, RTC.day, RTC.hour, RTC.minute, RTC.second);    // edit date modified
  file.timestamp(T_ACCESS, RTC.year, RTC.month, RTC.day, RTC.hour, RTC.minute, RTC.second);    // edit date accessed
}

// Read file name ****************************************************************
void readFileName()  // get the file name stored in EEPROM (set by GUI)
{
  for(int i = 0; i < 12; i++)
  {
    filename[i] = EEPROM.read(0x06 + i);
  }
}

// SD card Error response ****************************************************************
void SDcardError()
{
    for(int i=0;i<3;i++)   // blink LED 3 times to indicate SD card write error
    {
      digitalWrite(LED, HIGH);
      delay(50);
      digitalWrite(LED, LOW);
      delay(150);
    }
}

//****************************************************************
