//****************************************************************

// 8/22/2014 - filename through GUI now works - saves to EEPROM
// Added error handling for invalid filenames in GUI

// 9/16/2014 - temporarily removed "fail" code. Now Arduino won't sleep permanently after 10 tries to read SD card.

// 9/24/2014 - Using SHT15libmod2 to use thermistor temperature readings in RH compensation
// cleaned up unused variables and code
// Slightly edited DS3234lib3. Now getFileTimeStamp function is called parseTimeStamp and is called automatically by timeStamp function
// Decreased LED light up delay
// Edited filetimestamp function
// Connected thermistor on A0 pin.
// Added thermistor code from water activity
// Note: measure the actual resistance of the resistor before soldering it on the board.
// Replace 10,000 in function with actual resistance value. This gives us more accurate readings.

// 9/29/2014 - changed various power saving features in library
// pre-sleep analog port states are now saved before sleeping and restored after waking up

//****************************************************************

#include <EEPROM.h>
#include <DS3234lib3.h>
#include <PowerSaver.h>
#include <SHT15libmod2.h>
#include <SdFat.h>

PowerSaver chip;  // declare object for PowerSaver class

// Main code stuff   ******************************
#define POWA 4    // pin 4 supplies power to microSD card breakout and SHT15 sensor
int SDcsPin = 9;
long interval = 10;  // interval in seconds (value automatically assigned by the GUI)

// RTC stuff   ******************************
DS3234 RTC;    // declare object for DS3234 class
int dayStart = 0, hourStart = 0, minStart = 0;    // start time: day of the month, hour, minute (values automatically assigned by the GUI)

// SD card stuff   ******************************
#define LED 7  // pin 7 controls LED
SdFat sd;
SdFile file;
char filename[15] = "log.csv";    // file name is automatically assigned by GUI. Format: "12345678.123". Cannot be more than 8 characters in length

// SHT stuff   ******************************
int SHT_clockPin = 3;  // pin used for SCK on SHT15 breakout
int SHT_dataPin = 5;  // pin used for DATA on SHT15 breakout
SHT15 sensor(SHT_clockPin, SHT_dataPin);  // declare object for SHT15 class

// Interrupt stuff ****************************************************************
ISR(PCINT0_vect)  // Setup interrupts on digital pin 8
{
  PORTB ^= (1<<PORTB1);
}

// setup ****************************************************************
void setup()
{
  Serial.begin(19200); // open serial at 19200 bps
  pinMode(POWA, OUTPUT);
  pinMode(LED, OUTPUT);
  
  RTC.fetchAndSetTime();  // syncs date and time with the PC's clock
  RTC.getLaunchParameters(interval, dayStart, hourStart, minStart); // get parameters from GUI
  readFileName();  // read filename from EEPROM (assigned by GUI)
  delay(500);    // give some delay to ensure the RTC gets proper date/time
  
  digitalWrite(POWA, HIGH);    // turn on SD card
  delay(50);    // give some delay to ensure SD card is turned on properly
  if(!sd.init(SPI_FULL_SPEED, SDcsPin))  // initialize SD card on the SPI bus
  {
    delay(100);
    SDcardError();
  }
  else
  {
    delay(50);
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
  chip.sleepInterruptSetup();    // setup sleep function on the ATmega328p. Power-down mode is used here
}

// loop ****************************************************************
void loop()
{
  digitalWrite(POWA, LOW);  // turn off microSD card to save power
  delay(1);  // give some delay for SD card power to be low before processor sleeps to avoid it being stuck
  chip.turnOffADC();    // turn off ADC to save power
  chip.turnOffSPI();  // turn off SPI bus to save power
  chip.turnOffBOD();    // turn off Brown-out detection to save power
  SPCR = 0;
  
  chip.goodNight();    // put processor in extreme power down mode - GOODNIGHT!
                       // average current draw on Mini Pro should now be around 0.195 mA (with both onboard LEDs taken out)
                       // Processor will only wake up with an interrupt generated from the RTC, which occurs every logging interval
                       
  chip.goodMorning();  // Wakey wakey - GOODMORNING! This will restore pre-sleep state of analog pins
  chip.turnOnSPI();   // turn on SPI bus once the processor wakes up
  chip.turnOnADC();    // enable ADC after processor wakes up
  delay(1);    // important delay to ensure SPI bus is properly activated
  RTC.alarmFlagClear();    // clear alarm flag
  pinMode(POWA, OUTPUT);
  digitalWrite(POWA, HIGH);  // turn on SD card power
  delay(50);    // give delay to let the SD card get full powa
  pinMode(SDcsPin, OUTPUT);
  if(!sd.init(SPI_FULL_SPEED, SDcsPin))    // very important - reinitialize SD card on the SPI bus
  {
    delay(100);
    SDcardError();
  }
  else
  {
    delay(50);
    file.open(filename, O_WRITE | O_AT_END);  // open file in write mode
    delay(1);
    String time = RTC.timeStamp();    // get date and time from RTC
    SPCR = 0;
    
    // get sensor values
    float adc0 = averageADC(A0);
    float R = resistance(adc0, 9920); // Replace 10000 ohm with the actual resistance of the resistor measured using a multimeter (e.g. 9880 ohm)
    float temperature = steinhart(R);  // get temperature from thermistor using the custom Steinhart-hart equation by US sensors
    //float temperature = sensor.getTemperature();  // get temperature from SHT15 
    float humidity = sensor.getHumidity(temperature);  // get humidity from SHT15
    float dewPoint = sensor.getDewPoint(temperature, humidity); // calculate dew point using T and RH
    
    file.print(time);
    file.print(",");
    file.print(temperature, 3);  // print temperature upto 4 decimal places
    file.print(",");
    file.print(humidity, 3);  // print humidity upto 4 decimal places
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
  int sum=0;
  for(int i=0;i<30;i++)
  {
     sum = sum + analogRead(pin);
  }
  float average = sum/30.0;
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
      delay(175);
    }
}

//****************************************************************
