Exar USB Serial Driver
======================
Version 1A, 1/9/2015

This driver will work with any USB UART function in these Exar devices:
	XR21V1410/1412/1414
	XR21B1411
	XR21B1420/1422/1424
	XR22801/802/804

The source code has been tested on various Linux kernels from 3.6.x to 3.17.x.  
This may also work with newer kernels as well.  


Installation
------------

* Compile and install the common usb serial driver module

	# make
	# insmod ./xr_usb_serial_common.ko


* Plug the device into the USB host.  You should see up to four devices created,
  typically /dev/ttyXRUSB[0-3].


Tips for Debugging
------------------

* Check that the USB UART is detected by the system

	# lsusb

* Check that the CDC-ACM driver was not installed for the Exar USB UART

	# ls /dev/tty*

	To remove the CDC-ACM driver and install the driver:

	# rmmod cdc-acm
	# modprobe -r usbserial
	# modprobe usbserial
	# insmod ./xr_usb_serial_common.ko


Technical Support
-----------------
Send any technical questions/issues to uarttechsupport@exar.com. 

