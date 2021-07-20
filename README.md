Currently
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


