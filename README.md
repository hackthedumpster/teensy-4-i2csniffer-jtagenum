Firmware now contains;

    ("Type a for i2c sniffer");
    ("Type b for jtagenum"); 
    ("Type c to scan i2c device addresses");
    ("Type d to scan/sniff 433mhz pin0");
    ("Type e to send 433mhz data pin10");
    ("Type f freq counter pin9");
    ("Type g nrf uart tx/rx");
    ("Type h decode IR/RF OOK pin2");
    ("Type i to read ntc on pinA4");
    ("Type j to control GRB KHZ800 neopixels pin6");

So option A is an I2c sniffer with protocol decoder built in and seen over the uart (USE HI USB2UART BUS SPEEDS!) Im fairly certain at one point in time I had it sniffing 400k no prob, no drops but I musta changed something cuz Im seeing dropped data now. 
b is arduino port of jtagnum, used it once at start of prorject for 30 seconds, so uhm solidly tested well say. C good ol i2c bus address scanner with many common devices for the addresses builtin. With standard direct to wire dummy rf433 transmitters and receivers you can use options D, E, and H to decode send basic rf remote rf, in fsk/ook (works remarkably well) F for teensies surprisingly fast freq counter. My nrf24 networks dont do master/slave they switch modes depending on if datas being sent or not, allowing for seemless two comms. (IE wireless uart) G impliments some version of that, crap Im pretty sure I hacked up the nrf lib for that willl hafta add to repo. read temp from ntc using option I. Last J for basic serial control of neopixels, everything is hardcoded ATM maybe get back to this someday.




OLD OLD Currently

1st ttyACM is a serial based menu;

jtagenum

i2c-sniffer

i2c-scan

2nd ttyACM

usb to serial converter pins 0 1


A combonation serial menu based firmware. When compiling make sure to set usb to triple serial, when connecting to the teensy first ttyACM is the menu connect with baud set to 2000000 if going to use i2c-sniffer. The i2c sniffer has been tested at 400k wo issue so far, I have not added any buttons etc etc to exit i2c-sniffer you will have to reset the device, with timing so fast and critical I didnt want it to check for the button state each cycle. I think I currently have the time stamp commented out which i should return.


i2c-sniffer from;

https://forum.pjrc.com/threads/59160-Teensy-I2C-Sniffer-for-MPU6050

https://github.com/KurtE/Teensy_I2C_Sniffer_V11/tree/T4


jtagenum;

https://github.com/cyphunk/JTAGenum.git



