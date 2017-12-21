# TinyWDT_Static

sudo avrdude -v -p t85 -c dragon_isp -P usb -u -U efuse:w:0xff:m
sudo avrdude -v -p t85 -c dragon_isp -P usb -u -U hfuse:w:0xcc:m
sudo avrdude -v -p t85 -c dragon_isp -P usb -u -U lfuse:w:0x62:m 
sudo avrdude -v -p t85 -c dragon_isp -e -P usb -U flash:w:TinyWDT.hex

