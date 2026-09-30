## Measure IR Carrier frequency with the Irdroid USB Infrared Transceiver

The Irdroid USB Infrared Transceiver can measure IR carrier frequency by using the optional ir detector - National Semi QSE159. The standard version of the adapter does not come equipped with QSE159 and the passive components.

### Usage:

- Compile the program in Linux with make (requires installation of build essentials )
- Use the shell script to enumerate the device as a ttyACM0 serial device
    -    The Linux kernel will enumerate the device as /dev/lirc0 due to thre built in support for the adapter. Therefore to get access to the device from userspace, use the script in this repo to enumerate the adapter as a serial device.   
- The adapter communicates at a fixed serial baud rate of 115200, and the program is configured for that speed.
- Insert the USB Infrared Transceiver (make sure it is enumerated as a serial device e.g /dev/ttyACM*)
- Start the program `./irdroid-freq`
- For each sample, press a remote button from a distance of up to 5cm (preferably 2cm), then press Enter when prompted
- The carrier frequency in HZ will be printed out in the console
- Use `./irdroid-freq -n 10` to capture 10 consecutive samples and print their mean frequency (That way you will estimate the correct carrier frequency)

### Other Software that can be used to measure and analyze ir carrier frequency:

- IR Scrutinizer (Can be used to capture , decode and measure IR Carrier frequency)

### You can purchase a unit from [HERE](https://irdroid.eu/product/usb-infrared-transceiver/) (Make sure to choose the IR Carrier measurement capability)