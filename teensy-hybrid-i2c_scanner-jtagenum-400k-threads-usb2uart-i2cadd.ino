  
/*
    Name:       Teensy_I2C_Sniffer_V11.ino
    Created:  1/18/2020 10:55:55 AM
    Author:     FRANKNEWXPS15\Frank
*/
/* 'Notes:

    A typical I2C sentence when communicating with a MPU6050 IMU module goes something like:
        "I2C(68) wrote 1 byte to 75 - C0 Done."
        "I2C(68) wrote 3 bytes to 72 - C0 0C 10 Done."
        "I2C(68) read 5 bytes from 6A - C0 0C 10 14 03 Done."

    To form a sentence, we need:
        Device addr: 68 in the above examples
        Read/Write direction
        To/From register address:  75, 72 and 6A in the above examples
        Data:  C0, C0 0C 10, and C0 0C 10 14 03 in the above examples
        number of bytes written/read:  1,3 & 5 in the above examples

     Each I2C communication proceeds as follows (assuming a START from an IDLE condition):
         A START or RESTART condition, denoted by SDA & SCL HIGH, followed by SDA LOW, SCL HIGH
         A 7-bit device address, MSB first (0x8/0xC = 1, 0x0/0x4 = 0)
         A R/W bit (0x8/0xC = read, 0x0/0x4 = write)
         An ACK bit (0x8/0xC = NAK, 0x0/0x4 = ACK)
         If the bus direction is WRITE, then
             A register address for read/write
             zero or more additional data bytes
         Else (the bus direction is READ)
            One or more additional data bytes
         Endif

    This version uses a fixed-size (2048 bytes) array instead of tonton81's circular buffer library.

    To generalize for any I2C slave device rather than just the MPU6050 IMU, comment out the
    "#define MPU6050_SPECIFIC line below. This will remove all MPU6050 specific code
*/
#include <Arduino.h> // for ir receive
#include <RCSwitch.h>
#include <FreqCount.h>
#include <SPI.h>
#include "16ton-nRF24L01.h"
#include "16ton-RF24.h"
#include "PinDefinitionsAndMore.h"
#include <Thermistor.h>
#include <NTC_Thermistor.h>
#include "InternalTemperature.h"
#include <Adafruit_NeoPixel.h>

extern "C" uint32_t set_arm_clock(uint32_t frequency); // required prototype

#define LED_PIN 13

const uint32_t OverclockSpeed = 960000000;
const uint32_t OverclockTimeoutUsec = 7 * 1000000;
const uint32_t OverclockMaxTemp = 70;
const uint32_t OverclockMaxTempRise = 10;

float maxMeasuredTemperature;
bool temperatureExceeded = false;
bool overclockTimerExpired = false;
uint32_t originalClockSpeed;
IntervalTimer overclockTimer;

void startOverclocking_720MHz ()
{
  // this speed does not require cooling

  originalClockSpeed = F_CPU;

// convert clock cycles between samples from original speed to new speed

  // round by adding half of original clock speed
//  cpuClockCycles = cpuClockCycles * 720 + ((originalClockSpeed / 2) / 1000000);
//  cpuClockCycles /= (originalClockSpeed / 1000000);

  set_arm_clock (720000000);
}

void startOverclocking_816MHz()
{
  // this speed does not require cooling

  originalClockSpeed = F_CPU;

  // convert clock cycles between samples from original speed to new speed

  // round by adding half of original clock speed
//  cpuClockCycles = cpuClockCycles * 816 + ((originalClockSpeed / 2) / 1000000);
//  cpuClockCycles /= (originalClockSpeed / 1000000);

  set_arm_clock (816000000);
}

void startOverclocking_960MHz ()
{
  float temperature = InternalTemperature.readTemperatureC ();
  float maxTemp = temperature + OverclockMaxTempRise;

  maxMeasuredTemperature = 0.0;

  if (maxTemp > OverclockMaxTemp)
  {
    maxTemp = OverclockMaxTemp;
  }

  originalClockSpeed = F_CPU;

  overclockTimerExpired = false;
  temperatureExceeded = false;

  SerialUSB2.print("   Pre-temperature: ");
  SerialUSB2.println(temperature);
  delay (200);

  // if within 2 degrees of the max temperature, don't go into overclocking
  // (caused problems with interrupt going off while still transitioning the clock)
  if (temperature > (OverclockMaxTemp - 2))
  {
    
    SerialUSB2.println("Temperature too high to overclock, stopping!");      
  }
  else
  {
    overclockTimer.begin (overclockTimerInterrupt, OverclockTimeoutUsec);
    InternalTemperature.attachHighTempInterruptCelsius (maxTemp, temperatureInterrupt);

    set_arm_clock (OverclockSpeed);
  }
//SerialUSB2.print("F_CPU oc: "));
//SerialUSB2.println(F_CPU));
//SerialUSB2.print("F_BUS oc: "));
//SerialUSB2.println(F_BUS_ACTUAL));
}

void stopOverclocking (bool fromISR)
{
  maxMeasuredTemperature = InternalTemperature.readTemperatureC ();

  set_arm_clock (originalClockSpeed);

  overclockTimer.end ();
  InternalTemperature.detachHighTempInterrupt ();

digitalWriteFast(LED_PIN, LOW);
  // don't print from within an ISR
  if (!fromISR)
  {
    Serial.print("   Max temperature: ");
    Serial.println(maxMeasuredTemperature);

    if (temperatureExceeded)
    {
      Serial.println("Temperature interrupt triggered");      
    }
    if (overclockTimerExpired)
    {
      Serial.println("Overclock timer interrupt triggered");      
    }
  }
}

void temperatureInterrupt ()
{
  stopOverclocking (true);
  temperatureExceeded = true;
}

void overclockTimerInterrupt ()
{
  stopOverclocking (true);
  overclockTimerExpired = true;
}

  enum strategyType {
  STRATEGY_NORMAL,
  STRATEGY_NORMAL_OVERCLOCK_720,
  STRATEGY_NORMAL_RLE,
  STRATEGY_NORMAL_RLE_OVERCLOCK_816,
  STRATEGY_HIGH_SPEED,
  STRATEGY_HIGH_SPEED_RLE,
  STRATEGY_ASM_3_CLOCKS,
  STRATEGY_ASM_5_6_CLOCKS,
  STRATEGY_ASM_8_CLOCKS,
} sniff = STRATEGY_NORMAL;

#if Teensy_4_0

int F_BUS = F_BUS_ACTUAL;

#endif
#define SENSOR_PIN             A4
#define REFERENCE_RESISTANCE   10000
#define NOMINAL_RESISTANCE     10000
#define NOMINAL_TEMPERATURE    25
#define B_VALUE                3455

Thermistor* thermistor;

#define MARK_EXCESS_MICROS    20 // recommended for the cheap VS1838 modules

//#define RECORD_GAP_MICROS 12000 // Activate it for some LG air conditioner protocols
//#define DEBUG // Activate this for lots of lovely debug output from the decoders.
#define INFO // To see valuable informations from universal decoder for pulse width or pulse distance protocols

#include <16tonIRremote.hpp>



RCSwitch mySwitch = RCSwitch();
RF24 radio(37,36);

#define CPU_RESTART_ADDR (uint32_t *)0xE000ED0C
#define CPU_RESTART_VAL 0x5FA0004
#define CPU_RESTART (*CPU_RESTART_ADDR = CPU_RESTART_VAL);
#include <Wire.h>

#include <avr/pgmspace.h>

#define outputStream Serial3

#define Serial3begin(...)    outputStream.begin(__VA_ARGS__)
#define Serial3print(...)    outputStream.print(__VA_ARGS__)
#define Serial3write(...)    outputStream.print(__VA_ARGS__)
#define Serial3println(...)  outputStream.println(__VA_ARGS__)

#include <TimerOne.h> //needed for ISR
#include <TeensyThreads.h>

#if   defined(TEENSY_40)    // Teensy v4 usable digital are: A0-A9; A0-A9 are always digital 14-23, for Arduino compatibility
 byte       pins[] = {  A0 ,  A1 ,  A2 ,  A3 ,  A4 ,  A5 ,  A6 ,  A7, A8, A9  };
 String pinnames[] = { "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9" };
#elif   defined(TEENSY_41)    // Teensy v4 usable digital are: A0-A9; A0-A9 are always digital 14-23, for Arduino compatibility
 byte       pins[] = {  A0 ,  A1 ,  A2 ,  A3 ,  A4 ,  A5 ,  A6 ,  A7, A8, A9  };
 String pinnames[] = { "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9" };
#elif   defined(KINETISK)   // Teensy v3 usable digital are: A0-A7. 13=LED
 byte       pins[] = {  A0 ,  A1 ,  A2 ,  A3 ,  A4 ,  A5 ,  A6 ,  A7  };
 String pinnames[] = { "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7" };
#elif defined(CORE_TEENSY)  // Teensy v2
 byte       pins[] = {  A0 ,  A1 ,  A2 ,  A3 ,  A4 ,  A5 ,  A6 ,  A7, A8, A9  };
 String pinnames[] = { "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9" };
#elif defined(ENERGIA)     // TI Launchpad Tiva C
 byte       pins[] = {  PA_5,   PB_4,   PE_5,   PE_4,   PB_1  };
 String pinnames[] = { "PA_5", "PB_4", "PE_5", "PE_4", "PB_1" };
#elif defined(STM32)       // STM32 bluepill, pinout is here: https://wiki.stm32duino.com/index.php?title=File:Bluepillpinout.gif. See also instructions to get it running with the Arduino IDE here: http://www.zoobab.com/bluepill-arduinoide
 byte       pins[] = {  10 ,  11 ,  12 ,  13 ,  14 ,  15 ,  16 ,  17, 18 , 19 , 21 , 22  };
 String pinnames[] = { "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "21", "22" };
#elif defined(ESP_H)       // ESP8266 Wemos D1 Mini. if properly not set may trigger watchdog
 byte       pins[] = {  D1 ,  D2 ,  D3 ,  D4 ,  D5 ,  D6 ,  D7 ,  D8  };
 String pinnames[] = { "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8" };
#else                      // DEFAULT
                           // Arduino Pro. usable digital 2-12,14-10. 13=LED 0,1=serial
 byte       pins[] = { 2, 3, 4, 5, 6, 7, 8, 9, 11, 12};
 String pinnames[] = { "DIG_2", "DIG_3", "DIG_4", "DIG_5" , "DIG_6",
                       "DIG_7", "DIG_8", "DIG_9", "DIG_10", "DIG_11"};
  #include <EEPROM.h>
  #define EEPROMSTORE
#endif

#define PIN_NOT_SET 0xff
String jtagpinnames[] = { "TCK", "TMS", "TDO", "TDI", "TRST" };

byte TCK  = PIN_NOT_SET;
byte TMS  = PIN_NOT_SET;
byte TDO  = PIN_NOT_SET;
byte TDI  = PIN_NOT_SET;
byte TRST = PIN_NOT_SET;

// Pattern used for scan() and loopback() tests
#define PATTERN_LEN              64
// Use something random when trying find JTAG lines:
static char pattern[PATTERN_LEN] = "0110011101001101101000010111001001";
// Use something more determinate when trying to find
// length of the DR register:
//static char pattern[PATTERN_LEN] = "1000000000000000000000000000000000";

// Max. number of JTAG enabled chips (MAX_DEV_NR) and length
// of the DR register together define the number of
// iterations to run for scan_idcode():
#define MAX_DEV_NR               8
#define IDCODE_LEN               32

// Target specific, check your documentation or guess
#define SCAN_LEN                 1890 // used for IR enum. bigger the better
#define IR_LEN                   5
// IR registers must be IR_LEN wide:
#define IR_IDCODE                "01100" // always 011
#define IR_SAMPLE                "10100" // always 101
#define IR_PRELOAD               IR_SAMPLE

/*
 * END USER DEFINITIONS
 */



// TAP TMS states we care to use. NOTE: MSB sent first
// Meaning ALL TAP and IR codes have their leftmost
// bit sent first. This might be the reverse of what
// documentation for your target(s) show.
#define TAP_RESET                "11111"       // looping 1 will return
                                               // IDCODE if reg available
#define TAP_SHIFTDR              "111110100"
#define TAP_SHIFTIR              "1111101100" // -11111> Reset -0> Idle -1> SelectDR
                                              // -1> SelectIR -0> CaptureIR -0> ShiftIR

// Ignore TCK, TMS use in loopback check:
#define IGNOREPIN                0xFFFF
// Flags configured by UI:
#define TRUE                     255
#define FALSE                    0
boolean VERBOSE                  = FALSE;
boolean DELAY                    = FALSE;
long    DELAYUS                  = 50;
boolean PULLUP                   = TRUE;

const byte pinslen               = sizeof(pins)/sizeof(pins[0]);

// For 3.3v AVR boards. Cuts clock in half. Also see cmd in setup()
#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))


/*
* Return a pointer to the jtag pins given an index.
*
*/
byte * jtag_ptr_from_idx(const int idx){
  byte * curr;
  switch (idx) {
      case 0:
        curr = &TCK;
        break;
      case 1:
        curr = &TMS;
        break;
      case 2:
        curr = &TDO;
        break;
      case 3:
        curr = &TDI;
        break;
      case 4:
        curr = &TRST;
        break;
  }
  return curr;
}

//#define PARSE_LOOP_DEBUG

const uint16_t CAPTURE_ARRAY_SIZE = 2048;
const uint16_t VALID_DATA_ARRAY_SIZE = 2048;
const int WAITING_PRINT_INTERVAL_MSEC = 200;//interval timer for 'Waiting for data...' printout

#define MONITOR_OUT1 2 //so can monitor ISR activity with O'scope
#define MONITOR_OUT2 3 //so can monitor ISR activity with O'scope
#define MONITOR_OUT3 4 //so can monitor ISR activity with O'scope
#define SDA_PIN 18
#define SCL_PIN 19

#pragma region PROCESSING_VARIABLES
uint8_t devAddr;
uint8_t regAddr;
uint8_t databytes[2048]; //holds multiple databytes for later output sentence construction
uint16_t numbytes = 0; //number of data bytes extracted from data stream
int ACKNAKFlag; //can be negative
uint16_t databyte_idx = 0; //index into databyte_array
uint8_t killbuff[2]; //used to consume start/stop bytes
elapsedMillis mSecSinceLastWaitingPrint;
uint8_t valid_data[2048];
uint16_t numvalidbytes = 0; //number of valid bytes in this burst
uint16_t read_idx = 0; //pointer to next byte pair to be processed

//added for bus direction labels
enum BUSDIR
{
  WRITE,
  READ,
  UNKNOWN = -1
} RWDir;
BUSDIR BusDir = BUSDIR::UNKNOWN;
#pragma endregion ProcVars


#pragma region ISR_SUPPORT
uint8_t raw_data[CAPTURE_ARRAY_SIZE]; //holds data captured from I2C bus
volatile uint16_t  write_idx = 0;
volatile uint8_t   current_portb = 0xFF;
volatile uint8_t   last_portb = 0xFF;
volatile uint16_t mult0xCCount = 0;
const uint16_t MAX_IDLE_COUNT = 2500;

volatile bool bDone = false;
volatile bool bWaitingForStart = true;
volatile bool bIsData = true;
volatile bool bIsStart = false;
volatile bool bIsStop = false;
volatile uint8_t last_current;
#pragma endregion ISR Support

const uint64_t pipes[2] = { 0xe1f0f0f0f0LL, 0xe1f0f0f0f0LL };

boolean stringComplete = false;  // whether the string is complete
static int dataBufferIndex = 0;
boolean stringOverflow = false;
char charOverflow = 0;

char SendPayload[31] = "";
char RecvPayload[31] = "";
char serialBuffer[31] = "";

#define PIN 6
#define NUMPIXELS 240

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

void setup()
{
//  pinMode(14, INPUT);
//  buttonState = digitalRead(14);
  pinMode(13, OUTPUT);
//  pinMode(23, INPUT);
  digitalWrite(13, HIGH);
  pinMode(41, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(41), resetint, CHANGE);
  Serial1.begin(115200);
  SerialUSB2.begin(115200);
//  SerialUSB1.begin(9600);
  Serial.begin(460800);
    while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

//threads.addThread(serpass, 1);
delay(100);
    Serial.println("Hack The Dumpster");
    Serial.println("Type a for i2c sniffer");
    Serial.println("Type b for jtagenum"); 
    Serial.println("Type c to scan i2c device addresses");
    Serial.println("Type d to scan/sniff 433mhz pin0");
    Serial.println("Type e to send 433mhz data pin10");
    Serial.println("Type f freq counter pin9");
    Serial.println("Type g nrf uart tx/rx");
    Serial.println("Type h decode IR/RF OOK pin2");
    Serial.println("Type i to read ntc on pinA4");
    Serial.println("Type j to control GRB KHZ800 neopixels pin6");
}



void loop()
{
    while (Serial.available() > 0) {
    char inByte = Serial.read();
    switch (inByte) {
      case 'a':
      i2cscansetup();
      delay(100);
      startOverclocking_816MHz();
      i2cscan(); 
      break;
      
      case 'b':
      Serial.println("Welcome to jtagenum. press h for help");
      setupjtagenum();
      delay(200);
      while(1) {
       jtagenum(); 
      }
      break;
      
      case 'c':
       scani2c(); 
      break;
      
      case 'd':
       Serial.println("Starting 433mhz decode, on pin 0");
       mySwitch.enableReceive(0);
       scan433(); 
      break;
      
      case 'e':
//       Serial.println("Starting 433mhz transmit, on pin 10");
       Serial.println("Please type remote code in DEC");
      pinMode(10, OUTPUT);
       delay(150);
       mySwitch.enableTransmit(10);
       mySwitch.setProtocol(1);
       mySwitch.setPulseLength(420);
       mySwitch.setRepeatTransmit(2);
       send433();
       
       
       case 'f':
       Serial.println("freq count pin9 ");
       FreqCount.begin(1000000);  //Time in microseconds
       freqcount();
       break;
       
       case 'g':
       Serial.println("Starting NRF24 ");
       delay(100);
       radio.begin();
       radio.setDataRate(RF24_2MBPS);
       radio.setPALevel(RF24_PA_MIN);
       radio.setChannel(70);
       radio.enableDynamicPayloads();
       radio.setRetries(15,15);
       radio.setCRCLength(RF24_CRC_16);
       radio.openWritingPipe(pipes[0]);
       radio.openReadingPipe(1,pipes[1]);  
       radio.startListening();
       radio.printDetails();
       radio.printPrettyDetails();
       nrf24();
       break;
       
      case 'h':
      Serial.println(F("START " __FILE__ " from " __DATE__ "\r\nUsing library version " VERSION_IRREMOTE));
      IrReceiver.begin(2, ENABLE_LED_FEEDBACK); // Start the receiver, enable feedback LED, take LED feedback pin from the internal boards definition
      Serial.print(F("Ready to receive IR signals of protocols: "));
      printActiveIRProtocols(&Serial);
      Serial.print(F("at pin "));
      Serial.println(2);
      irdecode();
      break;

      case 'i':
      thermistor = new NTC_Thermistor(
      SENSOR_PIN,
      REFERENCE_RESISTANCE,
      NOMINAL_RESISTANCE,
      NOMINAL_TEMPERATURE,
      B_VALUE
      );
      tempntc();
      break;

      case 'j':
      Serial.println("Starting NEO setup pin6...");
      strip.begin();
      strip.show();
      strip.setBrightness(50);
      Serial.println("Enter Neopixel num, red, green, blue comma seperated IE");
      Serial.println("for neopixel num 16, red 15, green 25, blue 35, enter;");
      Serial.println("16,15,25,35");
      neoser();
      break;
      
    }
  }
}

void tempntc() {
  while (1) {
  // Reads temperature
  const double celsius = thermistor->readCelsius();
  const double kelvin = thermistor->readKelvin();
  const double fahrenheit = thermistor->readFahrenheit();

  // Output of information

  Serial.print("Temperature: ");
  Serial.print(celsius);
  Serial.print(" C, ");
  Serial.print(kelvin);
  Serial.print(" K, ");
  Serial.print(fahrenheit);
  Serial.println(" F");

  delay(500); // optionally, only to delay the output of information in the example.
  }
}

void irdecode()  {
  while (1) {
      if (IrReceiver.decode()) {  // Grab an IR code
        // Check if the buffer overflowed
        if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_WAS_OVERFLOW) {
            Serial.println(F("Overflow detected"));
            Serial.println(F("Try to increase the \"RAW_BUFFER_LENGTH\" value of " STR(RAW_BUFFER_LENGTH) " in " __FILE__));
            // see also https://github.com/Arduino-IRremote/Arduino-IRremote#modifying-compile-options-with-sloeber-ide
        } else {
            Serial.println();                               // 2 blank lines between entries
            Serial.println();
            IrReceiver.printIRResultShort(&Serial);
            Serial.println();
            Serial.println(F("Raw result in internal ticks (50 us) - with leading gap"));
            IrReceiver.printIRResultRawFormatted(&Serial, false); // Output the results in RAW format
            Serial.println(F("Raw result in microseconds - with leading gap"));
            IrReceiver.printIRResultRawFormatted(&Serial, true);  // Output the results in RAW format
            Serial.println();                               // blank line between entries
            Serial.print(F("Result as internal ticks (50 us) array - compensated with MARK_EXCESS_MICROS="));
            Serial.println(MARK_EXCESS_MICROS);
            IrReceiver.compensateAndPrintIRResultAsCArray(&Serial, false); // Output the results as uint8_t source code array of ticks
            Serial.print(F("Result as microseconds array - compensated with MARK_EXCESS_MICROS="));
            Serial.println(MARK_EXCESS_MICROS);
            IrReceiver.compensateAndPrintIRResultAsCArray(&Serial, true); // Output the results as uint16_t source code array of micros
            IrReceiver.printIRResultAsCVariables(&Serial);  // Output address and data as source code variables

            IrReceiver.compensateAndPrintIRResultAsPronto(&Serial);

            /*
             * Example for using the compensateAndStorePronto() function.
             * Creating this String requires 2210 bytes program memory and 10 bytes RAM for the String class.
             * The String object itself requires additional 440 Bytes RAM from the heap.
             * This values are for an Arduino UNO.
             */
//        Serial.println();                                     // blank line between entries
//        String ProntoHEX = F("Pronto HEX contains: ");        // Assign string to ProtoHex string object
//        if (int size = IrReceiver.compensateAndStorePronto(&ProntoHEX)) {   // Dump the content of the IReceiver Pronto HEX to the String object
//            // Append compensateAndStorePronto() size information to the String object (requires 50 bytes heap)
//            ProntoHEX += F("\r\nProntoHEX is ");              // Add codes size information to the String object
//            ProntoHEX += size;
//            ProntoHEX += F(" characters long and contains "); // Add codes count information to the String object
//            ProntoHEX += size / 5;
//            ProntoHEX += F(" codes");
//            Serial.println(ProntoHEX.c_str());                // Print to the serial console the whole String object
//            Serial.println();                                 // blank line between entries
//        }
        }
        IrReceiver.resume();                            // Prepare for the next value
    }  
  }
}

void neoser(void) {
    while (Serial.available() > 0) {
    int neo = Serial.parseInt();
    // look for the next valid integer in the incoming serial stream:
    int red = Serial.parseInt();
    // do it again:
    int green = Serial.parseInt();
    // do it again:
    int blue = Serial.parseInt();

        if (Serial.read() == '\n') {
      // constrain the values to 0 - 255 and invert
      // if you're using a common-cathode LED, just use "constrain(color, 0, 255);"
      neo = constrain(neo, 0, 240);
      red = constrain(red, 0, 255);
      green = constrain(green, 0, 255);
      blue = constrain(blue, 0, 255);

      strip.setPixelColor(neo, red,green,blue);
      strip.show();
        }
      }
}

void nrf24() {
//  Serial.println("nrf b4 loop");
  while (1) {
//  Serial.println("in nrf loop");
  nRF_receive();
  serial_receive();
  serialstuff();
  } 
}

void serialstuff() {
  while (Serial.available() > 0 ) {
      char incomingByte = Serial.read();
      if (stringOverflow) {
         serialBuffer[dataBufferIndex++] = charOverflow;  // Place saved overflow byte into buffer
         serialBuffer[dataBufferIndex++] = incomingByte;  // saved next byte into next buffer
         stringOverflow = false;                          // turn overflow flag off
      } else if (dataBufferIndex > 31) {
         stringComplete = true;        // Send this buffer out to radio
         stringOverflow = true;        // trigger the overflow flag
         charOverflow = incomingByte;  // Saved the overflow byte for next loop
         dataBufferIndex = 0;          // reset the bufferindex
         break; 
      } 
      else if(incomingByte=='\n'){
          serialBuffer[dataBufferIndex] = 0; 
          stringComplete = true;
      } else {
          serialBuffer[dataBufferIndex++] = incomingByte;
          serialBuffer[dataBufferIndex] = 0; 
      }          
  } // end while()
} // end serialEvent()

void nRF_receive(void) {
  int len = 0;
  if ( radio.available() ) {
      bool done = false;
      while ( !done ) {
        len = radio.getDynamicPayloadSize();
        done = radio.read(&RecvPayload,len);
        delay(5);
      }
      delay(100);

    RecvPayload[len] = 0; // null terminate string

    Serial.print("R: ");
    Serial.print(RecvPayload);
    Serial.println();
    RecvPayload[0] = 0;  // Clear the buffers
  }  
} // end nRF_receive()

void serial_receive(void){

  if (stringComplete) { 

        strcat(SendPayload,serialBuffer);      
        // swap TX & Rx addr for writing
        radio.openWritingPipe(pipes[1]);
        radio.openReadingPipe(0,pipes[0]);  
        radio.stopListening();
        bool ok = radio.write(&SendPayload,strlen(SendPayload));
        Serial.print("S: ");
        Serial.println(SendPayload);
        stringComplete = false;

        // restore TX & Rx addr for reading       
        radio.openWritingPipe(pipes[0]);
        radio.openReadingPipe(1,pipes[1]); 
        radio.startListening();  
        SendPayload[0] = 0;
        dataBufferIndex = 0;
        delay(100);

  } // endif
} // end serial_receive()    

void freqcount () {
  while (1) {
  if (FreqCount.available()) {
    unsigned long count = FreqCount.read();
    Serial.println(count);
    
  }
 }
}

void send433setup() {
  Serial.println("yup");

  delay(500);
  while (1) {
  mySwitch.send(13909523, 24);
  delay(1000);
  Serial.println("1");
//  mySwitch.send(110101000011111000010011);
  delay(1000);
  mySwitch.send(13909524, 24);
  delay(1000);
  Serial.println("2");
//  mySwitch.send(110101000011111000010100);
  delay(2000);
  mySwitch.send(13909525, 24);
  delay(1000);
  Serial.println("3");
//  mySwitch.send(110101000011111000010101);
  delay(3000);
  }
  
}

void resetint(){
  SCB_AIRCR = 0x5FA0004;
}

int pload433;
void send433() {
  int pload;
  while(1)
  if (Serial.available() > 0) {
      pload433 = Serial.parseInt();
      pload = pload433;
      if (Serial.read() == '\n') {
        mySwitch.send(pload, 24);
        delay(200);
        Serial.print("sent ");
        Serial.println(pload);
        delay(1000);
      }
  }

}

void scan433() {
  while(1)
  if (mySwitch.available()) {
    output(mySwitch.getReceivedValue(), mySwitch.getReceivedBitlength(), mySwitch.getReceivedDelay(), mySwitch.getReceivedRawdata(),mySwitch.getReceivedProtocol());
    mySwitch.resetAvailable();
  }
}
  
void scani2c() {
  Wire.begin();
  delay(5);
  byte error, address;
  int nDevices;

  Serial.println(F("Scanning..."));

  nDevices = 0;
  for (address = 1; address < 127; address++) {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print(F("Device found at address 0x"));
      if (address < 16) {
        Serial.print("0");
      }
      Serial.print(address,HEX);
      Serial.print("  (");
      printKnownChips(address);
      Serial.println(")");

      nDevices++;
    } else if (error==4) {
      Serial.print(F("Unknown error at address 0x"));
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address,HEX);
    }
  }
  if (nDevices == 0) {
    Serial.println(F("No I2C devices found\n"));
    Serial.println("");
    Serial.println("Hack The Dumpster");
    Serial.println("Type a for i2c sniffer");
    Serial.println("Type b for jtagenum"); 
    Serial.println("Type c to scan i2c device addresses");
    Serial.println("Type d to scan/sniff 433mhz pin0");
    Serial.println("Type e to send 433mhz data pin10");
    Serial.println("Type f freq counter pin9");
    Serial.println("Type g nrf uart tx/rx");
    Serial.println("Type h decode IR/RF OOK pin2");
    Serial.println("Type i to read ntc on pinA4");
  } else {
    Serial.println(F("done\n"));
    Serial.println("");
    Serial.println("Hack The Dumpster");
    Serial.println("Type a for i2c sniffer");
    Serial.println("Type b for jtagenum"); 
    Serial.println("Type c to scan i2c device addresses");
    Serial.println("Type d to scan/sniff 433mhz pin0");
    Serial.println("Type e to send 433mhz data pin10");
    Serial.println("Type f freq counter pin9");
    Serial.println("Type g nrf uart tx/rx");
    Serial.println("Type h decode IR/RF OOK pin2");
    Serial.println("Type i to read ntc on pinA4");
  }
//  CPU_RESTART;
}

void printKnownChips(byte address)
{
  // Is this list missing part numbers for chips you use?
  // Please suggest additions here:
  // https://github.com/PaulStoffregen/Wire/issues/new
  switch (address) {
    case 0x00: Serial.print(F("AS3935")); break;
    case 0x01: Serial.print(F("AS3935")); break;
    case 0x02: Serial.print(F("AS3935")); break;
    case 0x03: Serial.print(F("AS3935")); break;
    case 0x04: Serial.print(F("ADAU1966")); break;
    case 0x0A: Serial.print(F("SGTL5000")); break; // MCLK required
    case 0x0B: Serial.print(F("SMBusBattery?")); break;
    case 0x0C: Serial.print(F("AK8963")); break;
    case 0x10: Serial.print(F("CS4272")); break;
    case 0x11: Serial.print(F("Si4713")); break;
    case 0x13: Serial.print(F("VCNL4000,AK4558")); break;
    case 0x18: Serial.print(F("LIS331DLH")); break;
    case 0x19: Serial.print(F("LSM303,LIS331DLH")); break;
    case 0x1A: Serial.print(F("WM8731")); break;
    case 0x1C: Serial.print(F("LIS3MDL")); break;
    case 0x1D: Serial.print(F("LSM303D,LSM9DS0,ADXL345,MMA7455L,LSM9DS1,LIS3DSH")); break;
    case 0x1E: Serial.print(F("LSM303D,HMC5883L,FXOS8700,LIS3DSH")); break;
    case 0x20: Serial.print(F("MCP23017,MCP23008,PCF8574,FXAS21002,SoilMoisture")); break;
    case 0x21: Serial.print(F("MCP23017,MCP23008,PCF8574")); break;
    case 0x22: Serial.print(F("MCP23017,MCP23008,PCF8574")); break;
    case 0x23: Serial.print(F("MCP23017,MCP23008,PCF8574")); break;
    case 0x24: Serial.print(F("MCP23017,MCP23008,PCF8574,ADAU1966,HM01B0")); break;
    case 0x25: Serial.print(F("MCP23017,MCP23008,PCF8574")); break;
    case 0x26: Serial.print(F("MCP23017,MCP23008,PCF8574")); break;
    case 0x27: Serial.print(F("MCP23017,MCP23008,PCF8574,LCD16x2,DigoleDisplay")); break;
    case 0x28: Serial.print(F("BNO055,EM7180,CAP1188")); break;
    case 0x29: Serial.print(F("TSL2561,VL6180,TSL2561,TSL2591,BNO055,CAP1188")); break;
    case 0x2A: Serial.print(F("SGTL5000,CAP1188")); break;
    case 0x2B: Serial.print(F("CAP1188")); break;
    case 0x2C: Serial.print(F("MCP44XX ePot")); break;
    case 0x2D: Serial.print(F("MCP44XX ePot")); break;
    case 0x2E: Serial.print(F("MCP44XX ePot")); break;
    case 0x2F: Serial.print(F("MCP44XX ePot")); break;
    case 0x30: Serial.print(F("Si7210")); break;
    case 0x31: Serial.print(F("Si7210")); break;
    case 0x32: Serial.print(F("Si7210")); break;
    case 0x33: Serial.print(F("MAX11614,MAX11615,Si7210")); break;
    case 0x34: Serial.print(F("MAX11612,MAX11613")); break;
    case 0x35: Serial.print(F("MAX11616,MAX11617")); break;
    case 0x38: Serial.print(F("RA8875,FT6206,MAX98390")); break;
    case 0x39: Serial.print(F("TSL2561, APDS9960")); break;
    case 0x3C: Serial.print(F("SSD1306,DigisparkOLED")); break;
    case 0x3D: Serial.print(F("SSD1306")); break;
    case 0x40: Serial.print(F("PCA9685,Si7021,MS8607")); break;
    case 0x41: Serial.print(F("STMPE610,PCA9685")); break;
    case 0x42: Serial.print(F("PCA9685")); break;
    case 0x43: Serial.print(F("PCA9685")); break;
    case 0x44: Serial.print(F("PCA9685, SHT3X, ADAU1966")); break;
    case 0x45: Serial.print(F("PCA9685, SHT3X")); break;
    case 0x46: Serial.print(F("PCA9685")); break;
    case 0x47: Serial.print(F("PCA9685")); break;
    case 0x48: Serial.print(F("ADS1115,PN532,TMP102,LM75,PCF8591,CS42448")); break;
    case 0x49: Serial.print(F("ADS1115,TSL2561,PCF8591,CS42448")); break;
    case 0x4A: Serial.print(F("ADS1115,Qwiic Keypad,CS42448")); break;
    case 0x4B: Serial.print(F("ADS1115,TMP102,BNO080,Qwiic Keypad,CS42448")); break;
    case 0x50: Serial.print(F("EEPROM,FRAM")); break;
    case 0x51: Serial.print(F("EEPROM")); break;
    case 0x52: Serial.print(F("Nunchuk,EEPROM")); break;
    case 0x53: Serial.print(F("ADXL345,EEPROM")); break;
    case 0x54: Serial.print(F("EEPROM")); break;
    case 0x55: Serial.print(F("EEPROM")); break;
    case 0x56: Serial.print(F("EEPROM")); break;
    case 0x57: Serial.print(F("EEPROM")); break;
    case 0x58: Serial.print(F("TPA2016,MAX21100")); break;
    case 0x5A: Serial.print(F("MPR121")); break;
    case 0x60: Serial.print(F("MPL3115,MCP4725,MCP4728,TEA5767,Si5351")); break;
    case 0x61: Serial.print(F("MCP4725,AtlasEzoDO")); break;
    case 0x62: Serial.print(F("LidarLite,MCP4725,AtlasEzoORP")); break;
    case 0x63: Serial.print(F("MCP4725,AtlasEzoPH")); break;
    case 0x64: Serial.print(F("AtlasEzoEC, ADAU1966")); break;
    case 0x66: Serial.print(F("AtlasEzoRTD")); break;
    case 0x68: Serial.print(F("DS1307,DS3231,MPU6050,MPU9050,MPU9250,ITG3200,ITG3701,LSM9DS0,L3G4200D")); break;
    case 0x69: Serial.print(F("MPU6050,MPU9050,MPU9250,ITG3701,L3G4200D")); break;
    case 0x6A: Serial.print(F("LSM9DS1")); break;
    case 0x6B: Serial.print(F("LSM9DS0")); break;
    case 0x6F: Serial.print(F("Qwiic Button")); break;
    case 0x70: Serial.print(F("HT16K33,TCA9548A")); break;
    case 0x71: Serial.print(F("SFE7SEG,HT16K33")); break;
    case 0x72: Serial.print(F("HT16K33")); break;
    case 0x73: Serial.print(F("HT16K33")); break;
    case 0x76: Serial.print(F("MS5607,MS5611,MS5637,BMP280")); break;
    case 0x77: Serial.print(F("BMP085,BMA180,BMP280,MS5611")); break;
    case 0x7C: Serial.print(F("FRAM_ID")); break;
    default: Serial.print(F("unknown chip"));
  }
}

void serpass() {
  while(1)
  if (SerialUSB2.available()) {      // If anything comes in Serial (USB),
    Serial1.write(SerialUSB2.read());   // read it and send it out Serial1 (pins 0 & 1)
  }  else if (Serial1.available()) {     // If anything comes in Serial1 (pins 0 & 1)
    SerialUSB2.write(Serial1.read());   // read it and send it out Serial (USB)
  }
}
void i2cscansetup()
{
  unsigned long now = millis();
  pinMode(5, OUTPUT);
  int idx = 0;
  while (!Serial && (millis() - now) < 3000)
  {
    delay(500);
    idx++;
  }
  Serial.printf("Serial available after %lu mSec\n", millis() - now);
  pinMode(MONITOR_OUT1, OUTPUT);
  digitalWrite(MONITOR_OUT1, LOW);
  pinMode(MONITOR_OUT2, OUTPUT);
  digitalWrite(MONITOR_OUT2, LOW);
  pinMode(MONITOR_OUT3, OUTPUT);
  digitalWrite(MONITOR_OUT3, LOW);

  Serial3begin(900000);
  Serial3println("HELLO");
  Serial3println(F("F HELLO"));
  Serial3println(123.456, 2);
  Serial3println(3735928559UL, HEX);
  pinMode(SCL_PIN, INPUT);
  pinMode(SDA_PIN, INPUT);

  //reset port byte vars & start timer
  last_portb = current_portb = 0;
  write_idx = 0;
  memset(raw_data, 255, CAPTURE_ARRAY_SIZE);
  //PrintNextArrayBytes(raw_data, 255, 20);
//  Timer1.initialize(1); // run every mico second
#if 1
  Timer1_initialize(0.5); // run every half micro second
#else
  Timer1.initialize(0.5); // run every half micro second
#endif

  Timer1.attachInterrupt(capture_data);


  mSecSinceLastWaitingPrint = 0;
}
#if 1
void Timer1_initialize(float float_ms) {
  uint32_t period = (float)F_BUS_ACTUAL * float_ms * 0.0000005f;
  uint32_t prescale = 0;
  while (period > 32767) {
    period = period >> 1;
    if (++prescale > 7) {
      prescale = 7; // when F_BUS is 150 MHz, longest
      period = 32767; // period is 55922 us (~17.9 Hz)
      break;
    }
  }
  //Serial.printf("setPeriod, period=%u, prescale=%u\n", period, prescale);
  FLEXPWM1_FCTRL0 |= FLEXPWM_FCTRL0_FLVL(8); // logic high = fault
  FLEXPWM1_FSTS0 = 0x0008; // clear fault status
  FLEXPWM1_MCTRL |= FLEXPWM_MCTRL_CLDOK(8);
  FLEXPWM1_SM3CTRL2 = FLEXPWM_SMCTRL2_INDEP;
  FLEXPWM1_SM3CTRL = FLEXPWM_SMCTRL_HALF | FLEXPWM_SMCTRL_PRSC(prescale);
  FLEXPWM1_SM3INIT = -period;
  FLEXPWM1_SM3VAL0 = 0;
  FLEXPWM1_SM3VAL1 = period;
  FLEXPWM1_SM3VAL2 = 0;
  FLEXPWM1_SM3VAL3 = 0;
  FLEXPWM1_SM3VAL4 = 0;
  FLEXPWM1_SM3VAL5 = 0;
  FLEXPWM1_MCTRL |= FLEXPWM_MCTRL_LDOK(8) | FLEXPWM_MCTRL_RUN(8);
 // pwmPeriod = period;
}

#endif
//............................... jtagnum setup .............................
//............................................................................
void setupjtagenum()
{
#ifdef HALFCLOCK
        // for 3.3v boards. Cuts clock in half
        // normally only on avr based arduino & teensy hardware
        CPU_PRESCALE(0x01);
#endif
 //       Serial.begin(115200);
  Serial.println("jtag setup");
        byte *curr;
        for (int i = 0; i < 5; ++i) {
          curr = jtag_ptr_from_idx(i);
          if (*curr == PIN_NOT_SET) {
#ifdef EEPROMSTORE
            // if we are on arduino we can save/restore jtag pins from
            // the eeprom
            *curr = EEPROM.read(i);
#endif
            if( *curr < 0 || *curr >= pinslen){
              *curr = i;
            }
//           break;
          }
        }
    Serial.println("jtag setup end");
}

//-------------------------------------------------------------------------------
//--------------------------------    ISR    ------------------------------------
//-------------------------------------------------------------------------------
FASTRUN void capture_data()
//void capture_data()
{
  digitalWriteFast(5, HIGH);
  last_portb = current_portb;
#if defined(__IMXRT1062__)
   current_portb = (digitalReadFast(SCL_PIN) << 2) | (digitalReadFast(SDA_PIN) << 3);
   current_portb = (digitalReadFast(SCL_PIN)? 4 : 0) | (digitalReadFast(SDA_PIN)? 8 : 0);
#else
  current_portb = GPIOB_PDIR & 12; //reads state of SDA (18) & SCL (19) at same time
#endif
  if (!bDone && last_portb != current_portb)
  {
    mult0xCCount = 0; //reset IDLE counter
    digitalWriteFast(MONITOR_OUT1, HIGH);

    //01/17/20: joepasquariello suggestion
    last_current = (last_portb << 4) | (current_portb);
    bIsStart = (last_current == 0xC4);
    bIsStop = (last_current == 0x4C);
    bIsData = (last_current == 0x04) || (last_current == 0x8C);

    if (bIsStart) //START
    {
      digitalWriteFast(MONITOR_OUT2, HIGH);
      if (bWaitingForStart)
      {
        digitalWriteFast(MONITOR_OUT3, HIGH); //start of entire capture
        bWaitingForStart = false;
      }
    }
    else if (bIsStop) //STOP
    {
      digitalWriteFast(MONITOR_OUT2, LOW);
    }

    if (!bWaitingForStart && (bIsData || bIsStart || bIsStop))
    {
      //digitalWriteFast(MONITOR_OUT3, HIGH);
      raw_data[write_idx] = last_portb;
      write_idx++;
      raw_data[write_idx] = current_portb;
      write_idx++;
      if (write_idx >= CAPTURE_ARRAY_SIZE)
      {
        bDone = true;
        digitalWriteFast(MONITOR_OUT3, LOW);
      }
    }
    digitalWriteFast(MONITOR_OUT1, LOW);
  }
  else if (!bDone && mult0xCCount < MAX_IDLE_COUNT && last_portb == 0xc && current_portb == 0xc)
  {
    mult0xCCount++;
    if (mult0xCCount >= MAX_IDLE_COUNT)
    {
      digitalWriteFast(MONITOR_OUT3, LOW);
      bDone = true;
    }
  }
 digitalWriteFast(5, LOW);
 }
//-------------------------------------------------------------------------------
//-------------------------------- END ISR    ---------------------------------
//-------------------------------------------------------------------------------

void i2cscan()
{
//  if (Serial.available())
//  Serial.print("i2cscan serial avail");
//  delay(5);
while(1)
//while (Serial)
 // {
  if (bDone)
  {
    if (write_idx > 14)
    {
      //OK, we have some data to process. IDLE detection must have been EOM
      Timer1.stop();

      unsigned long startMsec = millis();

      //Serial.printf("%lu\t %d\t", millis(), write_idx);
      //PrintNextArrayBytes(raw_data, 0, 50);
      //Serial.printf(" - %lu\n", millis());
      uint16_t numprocessed = DecodeAndPrintValidData(raw_data); //decode and print everything captured so far
      unsigned long endMsec = millis();
      Serial3.printf("%lu: processed = %d elements in %lu mSec\n\n", startMsec, numprocessed, endMsec - startMsec);

      Timer1.start();
    }

    read_idx = 0;
    bDone = false;
    mult0xCCount = 0;
    write_idx = 0;
    bWaitingForStart = true;
  }
  else
  {
    //no data to process, but don't blow prints out every mSec...
    if (mSecSinceLastWaitingPrint > WAITING_PRINT_INTERVAL_MSEC)
    {
      mSecSinceLastWaitingPrint -= WAITING_PRINT_INTERVAL_MSEC;
      Serial3.printf("%lu: Waiting for Data...\n", millis());
    }
  }
// }
}

void PrintNextArrayBytes(uint8_t* data, uint16_t startidx, uint16_t numbytes)
{
  Serial.printf("%d bytes starting at %d: ", numbytes, startidx);
  for (uint16_t i = 0; i < numbytes; i++)
  {
    Serial.printf("%x ", data[i + startidx]);
  }
}


uint16_t DecodeAndPrintValidData(byte* data)
{
  //Purpose:  decode and print I2C conversation held in raw_data array
  //Inputs:
  //  cb = 2048 element FIFO
  //Outputs:
  //  returns number of bytes processed, or -1 for failure
  //  outputs structured I2C sentence to serial monitor
  //Plan:
  //  Step1: Cull out invalid bytes
  //  Step2: Determine if there is anything to do (have to have more than one transition in FIFO)
  //  Step3: Parse transitions into I2C sentence structure
  //  Step4: Output sentence to serial monitor

  memset(valid_data, 0, VALID_DATA_ARRAY_SIZE);
#ifdef PARSE_LOOP_DEBUG
  PrintNextArrayBytes(valid_data, 0, 20); //print out first 20 bytes for verification
#endif
  numvalidbytes = RemoveInvalidBytes(raw_data, valid_data);
#ifdef PARSE_LOOP_DEBUG
  Serial.printf("Removed %d invalid bytes, leaving %d remaining\n", write_idx + 1 - numvalidbytes, numvalidbytes);
  PrintNextArrayBytes(valid_data, 0, numvalidbytes); //print out first 20 bytes of valid_data array
#endif


  if (numvalidbytes < 2)
  {
    return 0;
  }

  while (read_idx < numvalidbytes)
  {
#ifdef PARSE_LOOP_DEBUG
    Serial.printf("At top of while (read_idx < numvalidbytes): read_idx = %d\n", read_idx);
    Serial.printf("Next two bytes in valid_data are %x, %x\n", valid_data[read_idx], valid_data[read_idx + 1]);
#endif
    //Find a START sequence (0xC followed by 0x4)
    while (!IsStart(valid_data, read_idx) && read_idx < numvalidbytes)
    {
      //Serial.printf("looking for start...\n");
      read_idx++;
    }
    //at this point, read_idx should point to next valid byte pair

#ifdef PARSE_LOOP_DEBUG
    Serial.printf("Start sequence found at %d\n", read_idx - 2);
    //PrintNextFIFOBytes(valid_data, 20);
#endif

    if (numvalidbytes - read_idx > 14)//14 entries required for 7-bit address
    {
      //Get 7-bit device address
      devAddr = Get7BitDeviceAddr(valid_data, read_idx);
#ifdef PARSE_LOOP_DEBUG
      Serial.printf("devAddr = %x\n", devAddr);
#endif
    }
    else
    {

#ifdef PARSE_LOOP_DEBUG
      Serial.printf("ran out of data at readidx = %d - exiting!\n", read_idx);
#endif
      break;
    }

    //get read/write flag  1 = Read, 0 = Write, -1 = error
    BusDir = (BUSDIR)GetReadWriteFlag(valid_data, read_idx);

#ifdef PARSE_LOOP_DEBUG
    Serial.printf("BusDir = %s\n", ((BusDir == BUSDIR::WRITE) ? "WRITE" : "READ"));
    //PrintNextFIFOBytes(valid_data, 20);
#endif

    //get ACK/NAK flag
    ACKNAKFlag = GetACKNAKFlag(valid_data, read_idx);
    numbytes = GetDataBytes(valid_data, read_idx, databytes); //terminates on a START, but the start bytes are not consumed
#ifdef PARSE_LOOP_DEBUG
    Serial.printf("Got %d bytes from GetDataBytes() --> ", numbytes);
    for (size_t i = 0; i < numbytes; i++)
    {
      Serial.printf(" %x ", databytes[i]);
    }
    Serial.printf("\n");

    //PrintNextFIFOBytes(cb_trans, 20);
#endif
    //If the bus direction is WRITE, then extract
    //    A register address for read / write
    //    zero or more additional data bytes
    if (BusDir == BUSDIR::WRITE)
    {
      regAddr = databytes[0];
#ifdef PARSE_LOOP_DEBUG
      Serial.printf("regAddr = %x, read_idx = %d\n", regAddr, read_idx);
#endif

      //check for additional data
      if (numbytes > 1)
      {
#ifdef PARSE_LOOP_DEBUG
        Serial.printf("Additional data found!\n");
        for (size_t i = 0; i < numbytes; i++)
        {
          Serial.printf("data[%d] = %x\n", i, databytes[i]);
        }
#endif
        //1st byte is register addr, subsequent bytes are data
        OutputFormattedSentence(BusDir, devAddr, regAddr, numbytes, databytes, 1);
      }
    }
    else  //all bytes are data
    {
#ifdef PARSE_LOOP_DEBUG
      Serial.printf("In data block:  got %d bytes of data\n", numbytes);
      for (size_t i = 0; i < numbytes; i++)
      {
        Serial.printf("data[%d] = %x\n", i, databytes[i]);
      }
#endif
      OutputFormattedSentence(BusDir, devAddr, regAddr, numbytes, databytes, 0);
    }
#ifdef PARSE_LOOP_DEBUG
    Serial.printf("At end of while (read_idx < numvalidbytes): read_idx = %d\n", read_idx);
#endif

  }//while (read_idx < numvalidbytes)
  return numvalidbytes;
}


#pragma region Support Functions
bool IsStart(byte* data, uint16_t& readidx)
{
  bool result = false;

  //Serial.printf("IsStart[%d] = %x, IsStart[%d] = %x\n",
  //    readidx, data[readidx], readidx + 1, data[readidx + 1]);

  if (data[readidx] == 0xC && data[readidx + 1] == 0x4)
  {
    result = true;
    readidx += 2; //bump to next byte pair
  }
  return result;
}

bool IsStop(byte* data, uint16_t& readidx)
{
  bool result = false;

  //Serial.printf("IsStop[%d] = %x, IsStop[%d] = %x\n",
  //readidx, data[readidx], readidx + 1, data[readidx + 1]);

  if (data[readidx] == 0x4 && data[readidx + 1] == 0xC)
  {
    result = true;
    readidx += 2; //bump to next byte pair
  }
  return result;
}

uint8_t Get7BitDeviceAddr(byte* data, uint16_t& readidx)
{
  //Purpose: Construct a 7-bit address starting from dataidx
  //Inputs:
  //  data = pointer to valid data array
  //  readidx = starting index of 7-bit address sequence (MSB first)
  //Outputs:
  //  returns the address as an 8-bit value with the MSB = 0, or 0x0 if unsuccessful
  //  dataidx = pointer to next data entry
  //Plan:
  //  Step1: Convert a pair of data entries into a 0 or 1
  //  Step2: Add the appropriate value to an ongoing sum
  //  Step3: return the total.
  //Notes:
  //  A '0' is coded as a 0x0 followed by a 0x4
  //  A '1' is coded as a 0x8 followed by a 0xC

  uint8_t devAddr = 0x0; //failure return value

  //Serial.printf("Get7BitDeviceAddr: readidx = %d\n",readidx);

  //devAddr is exactly 7 bits long, so 8 bits with MSB = 0
  for (size_t i = 0; i < 7; i++)
  {
    if (data[readidx] == 0x0 && data[readidx + 1] == 0x4)
    {
      readidx += 2; //advance the pointer, but don't add to sum
    }

    else if (data[readidx] == 0x8 && data[readidx + 1] == 0xC)
    {
      //Serial.printf("Get7BitDeviceAddr: '1' found at i = %d, adding %x to devAddr to get %x\n",
      //    i, 1 << (7 - i), devAddr + (1 << (7-i)));

      readidx += 2; //advance the pointer
      devAddr += (1 << (7 - i)); //add 2^(7-i) to sum
    }
  }

  devAddr = devAddr >> 1; //divide result by 2 to get 7-bit addr from 8 bits
  return devAddr;
}

int Get8BitDataByte(byte* data, uint16_t& readidx)
{
  //Purpose: Construct a 8-bit data byte starting from dataidx
  //Inputs:
  //  data = pointer to valid data array
  //  readidx = starting index of 8-bit data byte (MSB first)
  //Outputs:
  //  returns the address as an 8-bit value, or 0x0 if unsuccessful
  //  dataidx = pointer to next data entry
  //Plan:
  //  Step1: Convert a pair of data entries into a 0 or 1
  //  Step2: Add the appropriate value to an ongoing sum
  //  Step3: return the total.
  //Notes:
  //  A '0' is coded as a 0x0 followed by a 0x4
  //  A '1' is coded as a 0x8 followed by a 0xC
  //  12/29/19 - changed return val to int, so can return -1 when a 'short byte' is detected

  int dataval = 0x0; //failure return value

#ifdef GET_8BIT_DATABYTE_DEBUG
  Serial.printf("Get8BitDataByte: data[%d] = %x, data[%d] = %x\n",
                readidx, data[readidx], readidx + 1, data[readidx + 1]);
#endif

  //8 bits with MSB = 0
  int numbytes = 0;
  for (size_t i = 0; i < 8; i++)
  {
    if (data[readidx] == 0x0 && data[readidx + 1] == 0x4)
    {
      readidx += 2; //advance the pointer, but don't add to sum
      numbytes++;
    }

    else if (data[readidx] == 0x8 && data[readidx + 1] == 0xC)
    {
#ifdef GET_8BIT_DATABYTE_DEBUG
      Serial.printf("Get8BitDataByte: '1' found at i = %d, adding %x to devAddr to get %x\n",
                    i, 1 << (7 - i), dataval + (1 << (7 - i)));
#endif
      readidx += 2; //advance the pointer
      dataval += (1 << (7 - i)); //add 2^(8-i) to sum
      numbytes++;
    }
  }

#ifdef GET_8BIT_DATABYTE_DEBUG
  Serial.printf("Get8BitDataByte: numbytes = %d\n", numbytes);
#endif
  if (numbytes != 8)
  {
    dataval = -1; //error return value
  }

  return dataval;
}

int GetReadWriteFlag(byte* data, uint16_t& readidx)
{
  //Purpose: decode R/W byte pair
  //Inputs:
  //  data = pointer to valid data array
  //  readidx = index into data to start of R/W byte pair
  //Outputs:
  //  readidx = if successful, points to next byte pair in data
  //  returns 1 for Read (0x8/0xC), 0 for Write (0x0/0x4), -1 for failure
  //Notes:
  //

  //Serial.printf("GetReadWriteFlag: readidx = %d, data[readidx] = %x, data[readidx+1]= %x\n",
  //    readidx, data[readidx], data[readidx + 1]);
  int result = 0;
  if (data[readidx] == 0x8 && data[readidx + 1] == 0xC)
  {
    result = 1; //read detected
    readidx += 2; //point to next byte pair
  }

  else if (data[readidx] == 0x0 && data[readidx + 1] == 0x4)
  {
    result = 0; //write detected
    readidx += 2; //point to next byte pair
  }
  else
  {
    result = -1; //failed to detect read or write
  }

  return result;
}

int GetACKNAKFlag(byte* data, uint16_t& readidx)
{
  //Purpose: decode ACK/NAK byte pair
  //Inputs:
  //  data = pointer to valid data array
  //  readidx = index into data to start of ACK/NAK byte pair
  //Outputs:
  //  readidx = if successful, points to next byte pair in data
  //  returns 1 for NAK (0x8/0xC), 0 for ACK (0x0/0x4), -1 for failure
  //Notes:
  //

  //Serial.printf("GetACKNAKFlag: readidx = %d, data[readidx] = %x, data[readidx+1]= %x\n",
  //    readidx, data[readidx], data[readidx + 1]);
  int result = 0;
  if (data[readidx] == 0x8 && data[readidx + 1] == 0xC)
  {
    result = 1; //NAK detected
    readidx += 2; //point to next byte pair
  }

  else if (data[readidx] == 0x0 && data[readidx + 1] == 0x4)
  {
    result = 0; //ACK detected
    readidx += 2; //point to next byte pair
  }
  else
  {
    result = -1; //failed to detect ACK or NAK
  }

  return result;
}

int GetDataBytes(uint8_t* data, uint16_t& readidx, uint8_t* databytes)
{
  //Notes:
  //  01/01/2020: removed databyteidx from sig - always starts at zero

  uint16_t numbytes = 0;
  uint16_t databyte_idx = 0;

  bool StartFlag = false;
  bool StopFlag = false;

  do
  {
    int dataval = Get8BitDataByte(data, readidx);

    //watch out for 'short byte' reads
    if (dataval >= 0)
    {
      uint8_t databyte = (uint8_t)dataval;
      databytes[databyte_idx] = databyte;
      databyte_idx++;
      numbytes++;
    }

    ACKNAKFlag = GetACKNAKFlag(data, readidx);
    StartFlag = IsStart(data, readidx);
    StopFlag = IsStop(data, readidx);

#ifdef PARSE_LOOP_DEBUG
    Serial.printf("IsStart returned %d, IsStop returned %d, dataidx = %d\n",
                  StartFlag, StopFlag, readidx);
#endif

  } while (!StartFlag && !StopFlag && readidx < numvalidbytes);


  readidx -= 2;//back readidx up so loop top is positioned correctly.

  return numbytes;
}

void OutputFormattedSentence(int RW, uint8_t dev, uint8_t reg, uint8_t numbytes, uint8_t* bytearray, uint16_t startidx)
{
  Serial3.print("%lu uhg");
  Serial.printf("I2C(%x) %s %d bytes %s I2C(%x) 0x%x ",
                dev, (RW == 0 ? "writing" : "reading"),  numbytes - startidx, (RW == 0 ? "to" : "from"), dev, reg);
  for (size_t i = startidx; i < numbytes; i++)
  {
    Serial.printf("0x%x ", bytearray[i]);
  }
//  Serial.printf(". Done\n");
    Serial.printf("\n");
}

uint16_t RemoveInvalidBytes(uint8_t* rawdata, uint8_t* validdata)
{
  uint16_t numvalid = 0;
  uint16_t valididx = 0;

  //Serial.printf("raw data array contains %d bytes\n", write_idx + 1);
  //PrintNextArrayBytes(raw_data, 0, 20);

  //OK, now go back through the array, excising invalid sequences
  for (uint16_t rawidx = 0; rawidx < write_idx;/*rawidx incremented internally*/)
  {
    uint8_t firstByte = raw_data[rawidx]; //get the first byte
    uint8_t secondByte = raw_data[rawidx + 1]; //get the next byte
    bool validpair =
      (
        (firstByte == 0xC && secondByte == 0x4) //START or RESTART
        || (firstByte == 0x4 && secondByte == 0xC) //STOP
        || (firstByte == 0x0 && secondByte == 0x4) //0 OR ACK
        || (firstByte == 0x8 && secondByte == 0xC) //1 or NAK
      );

    //Serial.printf("rawidx %d: Considering %x and %x: validity = %d\n",
    //rawidx, firstByte, secondByte, validpair);
    if (validpair)
    {
      //save valid bytes to valid_bytes array
      validdata[valididx] = firstByte;
      validdata[valididx + 1] = secondByte;
      numvalid += 2;
      //Serial.printf("Added %x & %x at idx = %d & %d\n", firstByte, secondByte, valididx, valididx + 1);
      //PrintNextArrayBytes(validdata,0,numvalid);
      rawidx += 2;
      valididx += 2;
    }
    else
    {
      rawidx++; //on invalid, just go to next byte
    }
  }

  return numvalid;
}
#pragma endregion Support Functions


void tap_state(String tap_state, int tck, int tms)
{
#ifdef DEBUGTAP
  Serial.print("tap_state: tms set to: ");
#endif
  int tap_state_length = tap_state.length();
  for (int i=0; i<tap_state_length; i++) {
    if (DELAY) delayMicroseconds(DELAYUS);
    digitalWrite(tck, LOW);
    digitalWrite(tms, tap_state[i] - '0'); // conv from ascii pattern
#ifdef DEBUGTAP
    Serial.print(tap_state[i] - '0',DEC);
#endif
    digitalWrite(tck, HIGH); // rising edge shifts in TMS
  }
#ifdef DEBUGTAP
  Serial.println();
#endif
}

static void pulse_tms(int tck, int tms, int s_tms)
{
  if (tck == IGNOREPIN) return;
  digitalWrite(tck, LOW);
  digitalWrite(tms, s_tms);
  digitalWrite(tck, HIGH);
}
static void pulse_tdi(int tck, int tdi, int s_tdi)
{
  if (DELAY) delayMicroseconds(DELAYUS);
  if (tck != IGNOREPIN) digitalWrite(tck, LOW);
  digitalWrite(tdi, s_tdi);
  if (tck != IGNOREPIN) digitalWrite(tck, HIGH);
}
byte pulse_tdo(int tck, int tdo)
{
  byte tdo_read;
  if (DELAY) delayMicroseconds(DELAYUS);
  digitalWrite(tck, LOW); // read in TDO on falling edge
  tdo_read = digitalRead(tdo);
  digitalWrite(tck, HIGH);
  return tdo_read;
}

void init_pins(int tck = IGNOREPIN, int tms = IGNOREPIN, int tdi = IGNOREPIN, int ntrst = IGNOREPIN)
{
#if defined(ESP8266) || defined(ESP_H)
  ESP.wdtFeed();
#endif
  // default all to INPUT state
  for (int i = 0; i < pinslen; i++) {
    pinMode(pins[i], INPUT);
    // internal pullups default to logic 1:
    if (PULLUP) digitalWrite(pins[i], HIGH);
  }
  // TCK = output
  if (tck != IGNOREPIN) pinMode(tck, OUTPUT);
  // TMS = output
  if (tms != IGNOREPIN) pinMode(tms, OUTPUT);
  // tdi = output
  if (tdi != IGNOREPIN) pinMode(tdi, OUTPUT);
  // ntrst = output, fixed to 1
  if (ntrst != IGNOREPIN) {
    pinMode(ntrst, OUTPUT);
    digitalWrite(ntrst, HIGH);
  }
}

static int check_data(char pattern[], int iterations, int tck, int tdi, int tdo,
                      int *reg_len)
{
  int i;
  int w          = 0;
  int plen       = strlen(pattern);
  char tdo_read;
  char tdo_prev;
  int nr_toggle  = 0; // count how often tdo toggled
  /* we store the last plen (<=PATTERN_LEN) bits,
   *  rcv[0] contains the oldest bit */
  char rcv[PATTERN_LEN];

  tdo_prev = '0' + (digitalRead(tdo) == HIGH);

  for(i = 0; i < iterations; i++) {

    /* output pattern and incr write index */
    pulse_tdi(tck, tdi, pattern[w++] - '0');
    if (!pattern[w])
      w = 0;

    /* read from TDO and put it into rcv[] */
    tdo_read  =  '0' + (digitalRead(tdo) == HIGH);

    nr_toggle += (tdo_read != tdo_prev);
    tdo_prev  =  tdo_read;

    if (i < plen)
      rcv[i] = tdo_read;
    else
    {
      memmove(rcv, rcv + 1, plen - 1);
      rcv[plen-1] = tdo_read;
    }

    /* check if we got the pattern in rcv[] */
    if (i >= (plen - 1) ) {
      if (!memcmp(pattern, rcv, plen)) {
        *reg_len = i + 1 - plen;
        return 1;
      }
    }
  } /* for(i=0; ... ) */

  *reg_len = 0;
  return nr_toggle > 1 ? nr_toggle : 0;
}

static void print_pins(int tck, int tms, int tdo, int tdi, int ntrst)
{
  if (ntrst != IGNOREPIN) {
    Serial.print(" ntrst:");
    Serial.print(pinnames[ntrst]);
  }
  Serial.print(" tck:");
  Serial.print(pinnames[tck]);
  Serial.print(" tms:");
  Serial.print(pinnames[tms]);
  Serial.print(" tdo:");
  Serial.print(pinnames[tdo]);
  if (tdi != IGNOREPIN) {
    Serial.print(" tdi:");
    Serial.print(pinnames[tdi]);
  }
}

static void scan()
{
  int tck, tms, tdo, tdi, ntrst;
  int checkdataret = 0;
  int len;
  int reg_len;
  printProgStr(PSTR("================================\r\n"
                    "Starting scan for pattern:"));
  Serial.println(pattern);
  for(ntrst=0;ntrst<pinslen;ntrst++) {
    for(tck=0;tck<pinslen;tck++) {
      if(tck == ntrst) continue;
      for(tms=0;tms<pinslen;tms++) {
        if(tms == ntrst) continue;
        if(tms == tck  ) continue;
        for(tdo=0;tdo<pinslen;tdo++) {
          if(tdo == ntrst) continue;
          if(tdo == tck  ) continue;
          if(tdo == tms  ) continue;
          for(tdi=0;tdi<pinslen;tdi++) {
            if(tdi == ntrst) continue;
            if(tdi == tck  ) continue;
            if(tdi == tms  ) continue;
            if(tdi == tdo  ) continue;
            if(VERBOSE) {
              print_pins(tck, tms, tdo, tdi, ntrst);
              Serial.print("    ");
            }
            init_pins(pins[tck], pins[tms], pins[tdi], pins[ntrst]);
            tap_state(TAP_SHIFTIR, pins[tck], pins[tms]);
            checkdataret = check_data(pattern, (2*PATTERN_LEN),
                            pins[tck], pins[tdi], pins[tdo], &reg_len);
            if(checkdataret == 1) {
              Serial.print("FOUND! ");
              print_pins(tck, tms, tdo, tdi, ntrst);
              Serial.print(" IR length: ");
              Serial.println(reg_len, DEC);
            }
            else if(checkdataret > 1) {
              Serial.print("active ");
              print_pins(tck, tms, tdo, tdi, ntrst);
              Serial.print("  bits toggled:");
              Serial.println(checkdataret);
            }
            else if(VERBOSE) Serial.println();
          } /* for(tdi=0; ... ) */
        } /* for(tdo=0; ... ) */
      } /* for(tms=0; ... ) */
    } /* for(tck=0; ... ) */
  } /* for(ntrst=0; ... ) */
  printProgStr(PSTR("================================\r\n"));
}

static void loopback_check()
{
  int tdo, tdi;
  int checkdataret = 0;
  int reg_len;

  printProgStr(PSTR("================================\r\n"
                    "Starting loopback check...\r\n"));
  for(tdo=0;tdo<pinslen;tdo++) {
    for(tdi=0;tdi<pinslen;tdi++) {
      if(tdi == tdo) continue;

      if(VERBOSE) {
        Serial.print(" tdo:");
        Serial.print(pinnames[tdo]);
        Serial.print(" tdi:");
        Serial.print(pinnames[tdi]);
        Serial.print("    ");
      }
      init_pins(IGNOREPIN/*tck*/, IGNOREPIN/*tms*/, pins[tdi], IGNOREPIN /*ntrst*/);
      checkdataret = check_data(pattern, (2*PATTERN_LEN), IGNOREPIN, pins[tdi], pins[tdo], &reg_len);
      if(checkdataret == 1) {
        Serial.print("FOUND! ");
        Serial.print(" tdo:");
        Serial.print(pinnames[tdo]);
        Serial.print(" tdi:");
        Serial.print(pinnames[tdi]);
        Serial.print(" reglen:");
        Serial.println(reg_len);
      }
      else if(checkdataret > 1) {
        Serial.print("active ");
        Serial.print(" tdo:");
        Serial.print(pinnames[tdo]);
        Serial.print(" tdi:");
        Serial.print(pinnames[tdi]);
        Serial.print("  bits toggled:");
        Serial.println(checkdataret);
      }
      else if(VERBOSE) Serial.println();
    }
  }
  printProgStr(PSTR("================================\r\n"));
}

static void list_pin_names()
{
  int pin;
  Serial.print("The configured pins are:\r\n");
  for(pin=0;pin<pinslen;pin++) {
    Serial.print(pinnames[pin]);
    Serial.print(" ");
  }
  Serial.println();
}

/*
 * Scan TDO for IDCODE. Handle MAX_DEV_NR many devices.
 * We feed zeros into TDI and wait for the first 32 of them to come out at TDO (after n * 32 bit).
 * As IEEE 1149.1 requires bit 0 of an IDCODE to be a "1", we check this bit.
 * We record the first bit from the idcodes into bit0.
 * (oppposite to the old code).
 * If we get an IDCODE of all ones, we assume that the pins are wrong.
 * This scan assumes IDCODE is the default DR between TDI and TDO.
 */
static void scan_idcode()
{
  int tck, tms, tdo, tdi, ntrst;
  int i, j;
  int tdo_read;
  uint32_t idcodes[MAX_DEV_NR];
  printProgStr(PSTR("================================\r\n"
                    "Starting scan for IDCODE...\r\n"
                    "(assumes IDCODE default DR)\r\n"));
  uint32_t idcode;
  for(ntrst=0;ntrst<pinslen;ntrst++) {
    for(tck=0;tck<pinslen;tck++) {
      if(tck == ntrst) continue;
      for(tms=0;tms<pinslen;tms++) {
        if(tms == ntrst) continue;
        if(tms == tck  ) continue;
        for(tdo=0;tdo<pinslen;tdo++) {
          if(tdo == ntrst) continue;
          if(tdo == tck  ) continue;
          if(tdo == tms  ) continue;
          for(tdi=0;tdi<pinslen;tdi++) {
            if(tdi == ntrst) continue;
            if(tdi == tck  ) continue;
            if(tdi == tms  ) continue;
            if(tdi == tdo  ) continue;
            if(VERBOSE) {
              print_pins(tck, tms, tdo, tdi, ntrst);
              Serial.print("    ");
            }
            init_pins(pins[tck], pins[tms], pins[tdi], pins[ntrst]);

            /* we hope that IDCODE is the default DR after reset */
            tap_state(TAP_RESET, pins[tck], pins[tms]);
            tap_state(TAP_SHIFTDR, pins[tck], pins[tms]);

            /* j is the number of bits we pulse into TDI and read from TDO */
            for(i = 0; i < MAX_DEV_NR; i++) {
              idcodes[i] = 0;
              for(j = 0; j < IDCODE_LEN;j++) {
                /* we send '0' in */
                pulse_tdi(pins[tck], pins[tdi], 0);
                tdo_read = digitalRead(pins[tdo]);
                if (tdo_read)
                  idcodes[i] |= ( (uint32_t) 1 ) << j;

                if (VERBOSE)
                  Serial.print(tdo_read,DEC);
              } /* for(j=0; ... ) */
              if (VERBOSE) {
                Serial.print(" ");
                Serial.println(idcodes[i],HEX);
              }
              /* save time: break at the first idcode with bit0 != 1 */
              if (!(idcodes[i] & 1) || idcodes[i] == 0xffffffff)
                break;
            } /* for(i=0; ...) */

            if (i > 0) {
              print_pins(tck,tms,tdo,tdi,ntrst);
              Serial.print("  devices: ");
              Serial.println(i,DEC);
              for(j = 0; j < i; j++) {
                Serial.print("  0x");
                Serial.println(idcodes[j],HEX);
              }
            } /* if (i > 0) */
          } /* for(tdo=0; ... ) */
        } /* for(tdi=0; ...) */
      } /* for(tms=0; ...) */
    } /* for(tck=0; ...) */
  } /* for(trst=0; ...) */

  printProgStr(PSTR("================================\r\n"));
}

static void shift_bypass()
{
  int tdi, tdo, tck;
  int checkdataret;
  int reg_len;

  printProgStr(PSTR("================================\r\n"
                    "Starting shift of pattern through bypass...\r\n"
                    "Assumes bypass is the default DR on reset.\r\n"
                    "Hence, no need to check for TMS. Also, currently\r\n"
                    "not checking for nTRST, which might not work\r\n"));
  for(tck=0;tck<pinslen;tck++) {
    for(tdi=0;tdi<pinslen;tdi++) {
      if(tdi == tck) continue;
      for(tdo=0;tdo<pinslen;tdo++) {
        if(tdo == tck) continue;
        if(tdo == tdi) continue;
        if(VERBOSE) {
          Serial.print(" tck:");
          Serial.print(pinnames[tck]);
          Serial.print(" tdi:");
          Serial.print(pinnames[tdi]);
          Serial.print(" tdo:");
          Serial.print(pinnames[tdo]);
          Serial.print("    ");
        }

        init_pins(pins[tck], IGNOREPIN/*tms*/,pins[tdi], IGNOREPIN /*ntrst*/);
        // if bypass is default on start, no need to init TAP state
        checkdataret = check_data(pattern, (2*PATTERN_LEN), pins[tck], pins[tdi], pins[tdo], &reg_len);
        if(checkdataret == 1) {
          Serial.print("FOUND! ");
          Serial.print(" tck:");
          Serial.print(pinnames[tck]);
          Serial.print(" tdo:");
          Serial.print(pinnames[tdo]);
          Serial.print(" tdi:");
          Serial.println(pinnames[tdi]);
        }
        else if(checkdataret > 1) {
          Serial.print("active ");
          Serial.print(" tck:");
          Serial.print(pinnames[tck]);
          Serial.print(" tdo:");
          Serial.print(pinnames[tdo]);
          Serial.print(" tdi:");
          Serial.print(pinnames[tdi]);
          Serial.print("  bits toggled:");
          Serial.println(checkdataret);
        }
        else if(VERBOSE) Serial.println();
      }
    }
  }
  printProgStr(PSTR("================================\r\n"));
}
/* ir_state()
 * Set TAP to Reset then ShiftIR.
 * Shift in state[] as IR value.
 * Switch to ShiftDR state and end.
 */

 void ir_state(String state, int tck, int tms, int tdi)
{
#ifdef DEBUGIR
  Serial.println("ir_state: set TAP to ShiftIR:");
#endif
  tap_state(TAP_SHIFTIR, tck, tms);
#ifdef DEBUGIR
  Serial.print("ir_state: pulse_tdi to: ");
#endif
  for (int i=0; i < IR_LEN; i++) {
    if (DELAY) delayMicroseconds(DELAYUS);
    // TAP/TMS changes to Exit IR state (1) must be executed
    // at same time that the last TDI bit is sent:
    if (i == IR_LEN-1) {
      digitalWrite(tms, HIGH); // ExitIR
#ifdef DEBUGIR
      Serial.print(" (will be in ExitIR after next bit) ");
#endif
    }
    pulse_tdi(tck, tdi, state[i] - '0');
#ifdef DEBUGIR
    Serial.print(state[i] - '0', DEC);
#endif
    // TMS already set to 0 "shiftir" state to shift in bit to IR
  }
#ifdef DEBUGIR
  Serial.println("\r\nir_state: Change TAP from ExitIR to ShiftDR:");
#endif
  // a reset would cause IDCODE instruction to be selected again
  tap_state("1100", tck, tms); // -1> UpdateIR -1> SelectDR -0> CaptureDR -0> ShiftDR
}
static void sample(int iterations, int tck, int tms, int tdi, int tdo, int ntrst=IGNOREPIN)
{
  printProgStr(PSTR("================================\r\n"
                    "Starting sample (boundary scan)...\r\n"));
  init_pins(tck, tms ,tdi, ntrst);

  // send instruction and go to ShiftDR
  ir_state(IR_SAMPLE, tck, tms, tdi);

  // Tell TAP to go to shiftout of selected data register (DR)
  // is determined by the instruction we sent, in our case
  // SAMPLE/boundary scan
  for (int i = 0; i < iterations; i++) {
    // no need to set TMS. It's set to the '0' state to
    // force a Shift DR by the TAP
    Serial.print(pulse_tdo(tck, tdo),DEC);
    if (i % 32  == 31 ) Serial.print(" ");
    if (i % 128 == 127) Serial.println();
  }
}
char ir_buf[IR_LEN+1];
static void brute_ir(int iterations, int tck, int tms, int tdi, int tdo, int ntrst=IGNOREPIN)
{
  printProgStr(PSTR("================================\r\n"
                    "Starting brute force scan of IR instructions...\r\n"
                    "NOTE: If Verbose mode is off output is only printed\r\n"
                    "      after activity (bit changes) are noticed and\r\n"
                    "      you might not see the first bit of output.\r\n"
                    "IR_LEN set to "));
  Serial.println(IR_LEN,DEC);

  init_pins(tck, tms ,tdi, ntrst);
  int iractive;
  byte tdo_read;
  byte prevread;
  for (uint32_t ir = 0; ir < (1UL << IR_LEN); ir++) {
    iractive=0;
    // send instruction and go to ShiftDR (ir_state() does this already)
    // convert ir to string.
    for (int i = 0; i < IR_LEN; i++)
      ir_buf[i]=bitRead(ir, i)+'0';
    ir_buf[IR_LEN]=0;// terminate
    ir_state(ir_buf, tck, tms, tdi);
    // we are now in TAP_SHIFTDR state

    prevread = pulse_tdo(tck, tdo);
    for (int i = 0; i < iterations-1; i++) {
      // no need to set TMS. It's set to the '0' state to force a Shift DR by the TAP
      tdo_read = pulse_tdo(tck, tdo);
      if (tdo_read != prevread) iractive++;

      if (iractive || VERBOSE) {
        Serial.print(prevread,DEC);
        if (i%16 == 15) Serial.print(" ");
        if (i%128 == 127) Serial.println();
      }
      prevread = tdo_read;
    }
    if (iractive || VERBOSE) {
      Serial.print(prevread,DEC);
      Serial.print("  Ir ");
      Serial.print(ir_buf);
      Serial.print("  bits changed ");
      Serial.println(iractive, DEC);
    }
  }
}

void set_pattern()
{
  int i;
  char c;

  Serial.print("Enter new pattern of 1's or 0's (terminate with new line or '.'):\r\n"
               "> ");
  i = 0;
  while(1) {
    c = Serial.read();
    switch(c) {
    case '0':
    case '1':
      if(i < (PATTERN_LEN - 1) ) {
        pattern[i++] = c;
        Serial.print(c);
      }
      break;
    case '\n':
    case '\r':
    case '.': // bah. for the arduino serial console which does not pass us \n
      pattern[i] = 0;
      Serial.println();
      Serial.print("new pattern set [");
      Serial.print(pattern);
      Serial.println("]");
      return;
    }
  }
}

void configure_pins(){
  Serial.println("Available pins, the index is based on them");
  for(int pin=0;pin<pinslen;pin++) {
    Serial.print(pinnames[pin]);
    Serial.print("[");
    Serial.print(pin);
    Serial.print("]");
    Serial.print(" ");
  }
  Serial.println();
  Serial.println("Current pin configuration");
  print_pins(TCK, TMS, TDO, TDI, TRST);
  byte * curr = NULL;
  for (int i = 0; i < 5; ++i) {
    curr = jtag_ptr_from_idx(i);
    do{
      // Print current value
      Serial.println();
      Serial.print(jtagpinnames[i]);
      Serial.print("(");
      Serial.print(*curr, DEC);
      Serial.print(") = ");
      // Read the new pin configuration
      while(!Serial.available())
            ;
      *curr = Serial.parseInt();
    } while(*curr < 0 || *curr >= pinslen );
    Serial.print(*curr);
#ifdef EEPROMSTORE
    // Save to eeprom
    EEPROM.write(i, *curr);
#endif
  }
  Serial.print("\nConfiguration saved\n");
}

// given a PROGMEM string, use Serial.print() to send it out
void printProgStr(const char *str)
{
  char c;
  if(!str) return;
  while((c = pgm_read_byte(str++)))
    Serial.print(c);
}

void help()
{
  printProgStr(PSTR(
      "Short and long form commands can be used.\r\n"
      "\r\n"
      "SCANS\r\n"
      "-----\r\n"
      "s > pattern scan\r\n"
      "   Scans for all JTAG pins. Attempts to set TAP state to\r\n"
      "   DR_SHIFT and then shift the pattern through the DR.\r\n"
      "p > pattern set\r\n"
      "   currently: ["));
  Serial.print(pattern);
  printProgStr(PSTR("]\r\n"
      "\r\n"
      "i > idcode scan\r\n"
      "   Assumes IDCODE is default DR on reset. Ignores TDI.\r\n"
      "   Sets TAP state to DR_SHIFT and prints TDO to console\r\n"
      "   when TDO appears active. Human examination required to\r\n"
      "   determine if actual IDCODE is present. Run several\r\n"
      "   times to check for consistancy or compare against\r\n"
      "   active tdo lines found with loopback test.\r\n"
      "\r\n"
      "b > bypass scan\r\n"
      "   Assumes BYPASS is default DR on reset. Ignores TMS and\r\n"
      "   shifts pattern[] through TDI/TDO using TCK for clock.\r\n"
      "\r\n"
      "ERATTA\r\n"
      "------\r\n"
      "l > loopback check\r\n"
      "   ignores tck,tms. if patterns passed to tdo pins are\r\n"
      "   connected there is a short or a false-possitive\r\n"
      "   condition exists that should be taken into account\r\n"
      "r > pullups\r\n"
      "   internal pullups on inputs, on/off. might increase\r\n"
              "   stability when using a bad patch cable.\r\n"
      "v > verbose\r\n"
      "   on/off. print tdo bits to console during testing. If on, this will slow\r\n"
      "   down scan.\r\n"
      "d > delay\r\n"
      "   on/off. will slow down scan.\r\n"
      "- > delay -\r\n"
      "   reduce delay microseconds\r\n"
      "c > configure pin\r\n"
      "   configure jtag pins\r\n"
      "- > delay -\r\n"
      "   reduce delay microseconds\r\n"
      "+ > delay +\r\n"
      "h > help\r\n"
      "n > list pin names\r\n"
      "\r\n"
      "OTHER JTAG TESTS\r\n"
      "----------------\r\n"
      "Each of the following will not scan/find JTAG and require\r\n"
      "that you manually set the JTAG pins. See their respective\r\n"
      "call from the loop() function of code to set.\r\n"
      "\r\n"
      "1 > pattern scan single\r\n"
      "   runs a full check on one code-defined tdi<>tdo pair.\r\n"
      "   look at the main()/loop() code to specify pins.\r\n"
      "x > boundary scan\r\n"
      "   checks code defined tdo for 4000+ bits.\r\n"
      "   look at the main()/loop() code to specify pins.\r\n"
      "y > irenum\r\n"
      "   sets every possible Instruction Register and then\r\n"
      "   checks the output of the Data Register.\r\n"
      "   look at the main()/loop() code to specify pins.\r\n"
      ));
}
/*
 * main()
 */
#define CMDLEN 20
char command[CMDLEN];
int dummy;

void jtagenum()
{
//  delay(100);
  if (Serial.available())
//  delay(5);
//while(1)
//while (Serial)
  {
    //Serial.println("jtagenum functoin");
    // READ COMMAND
    delay(5); // hoping read buffer is idle after 5 ms
    int i = 0;
    while (Serial.available() && i < CMDLEN-1)
      command[i++] = Serial.read();

    Serial.flush();
    command[i] = 0; // terminate string
    Serial.println(command); // echo back

    // EXECUTE COMMAND
    if     (strcmp(command, "pattern scan") == 0                     || strcmp(command, "s") == 0)
      scan();
    else if(strcmp(command, "pattern scan single") == 0              || strcmp(command, "1") == 0)
    {
      Serial.print("pins");
      print_pins(TCK, TMS, TDO, TDI, TRST);
      init_pins(pins[TCK], pins[TMS], pins[TDI], pins[TRST] /*ntrst*/);
      tap_state(TAP_SHIFTIR, pins[TCK], pins[TMS]);
      if (check_data(pattern, (2*PATTERN_LEN), pins[TCK], pins[TDI], pins[TDO], &dummy))
        Serial.println("found pattern or other");
      else
        Serial.println("no pattern found");
    }
    else if(strcmp(command, "pattern set") == 0                      || strcmp(command, "p") == 0)
      set_pattern();
    else if(strcmp(command, "loopback check") == 0                   || strcmp(command, "l") == 0)
      loopback_check();
    else if(strcmp(command, "idcode scan") == 0                      || strcmp(command, "i") == 0)
      scan_idcode();
    else if(strcmp(command, "bypass scan") == 0                      || strcmp(command, "b") == 0)
      shift_bypass();
    else if(strcmp(command, "boundary scan") == 0                    || strcmp(command, "x") == 0)
    {
      Serial.print("pins");
      print_pins(TCK, TMS, TDO, TDI, TRST);
      Serial.println();
      sample(SCAN_LEN+100, pins[TCK], pins[TMS], pins[TDI], pins[TDO], pins[TRST]);
    }
    else if(strcmp(command, "irenum") == 0                           || strcmp(command, "y") == 0)
      brute_ir(SCAN_LEN,   pins[TCK], pins[TMS], pins[TDI], pins[TDO], pins[TRST]);
    else if(strcmp(command, "verbose") == 0                          || strcmp(command, "v") == 0)
    {
      if (VERBOSE == FALSE) {VERBOSE = TRUE;} else {VERBOSE = FALSE;}
      Serial.println(VERBOSE ? "Verbose ON" : "Verbose OFF");
    }
    else if(strcmp(command, "delay") == 0                            || strcmp(command, "d") == 0)
    {
      if (DELAY == FALSE) {DELAY = TRUE;} else {DELAY = FALSE;}
      Serial.println(DELAY ? "Delay ON" : "Delay OFF");
    }
    else if(strcmp(command, "delay -") == 0                          || strcmp(command, "-") == 0)
    {
      Serial.print("Delay microseconds: ");
      if (DELAYUS != 0 && DELAYUS > 1000) DELAYUS-=1000;
      else if (DELAYUS != 0 && DELAYUS > 100) DELAYUS-=100;
      else if (DELAYUS != 0) DELAYUS-=10;
      Serial.println(DELAYUS,DEC);
    }
    else if(strcmp(command, "delay +") == 0                          || strcmp(command, "+") == 0)
    {
      Serial.print("Delay microseconds: ");
      if (DELAYUS < 100) DELAYUS+=10;
      else if (DELAYUS <= 1000) DELAYUS+=100;
      else DELAYUS+=1000;
      Serial.println(DELAYUS,DEC);
    }
    else if(strcmp(command, "pullups") == 0                          || strcmp(command, "r") == 0)
    {
      if (PULLUP == FALSE) {PULLUP = TRUE;} else {PULLUP = FALSE;}
      Serial.println(PULLUP ? "Pullups ON" : "Pullups OFF");
    }
    else if(strcmp(command, "help") == 0                             || strcmp(command, "h") == 0)
      help();
    else if(strcmp(command, "list pin names") == 0                   || strcmp(command, "n") == 0)
      list_pin_names();
    else if(strcmp(command, "configure pin") ==  0                   || strcmp(command, "c") == 0)
      configure_pins();
    else
    {
      Serial.println("unknown command");
//      help();
    }
    Serial.println("\n> ");
  }
}
