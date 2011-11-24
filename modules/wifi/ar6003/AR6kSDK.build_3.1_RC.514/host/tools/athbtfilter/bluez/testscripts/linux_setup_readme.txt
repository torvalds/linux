This document provides setup instructions for setting up and testing bluetooth on
a Fedora Core 9 system.

Setting up Bluetooth Stack
==========================

1). The following configurations can be made to /etc/bluetooth/audio.conf

   For HCI devices that use a separate PCM audio path, you must uncomment

      #SCORouting=PCM

   the default is to route audio through the HCI interface (used for USB adapters).

   If you wish to disable role switching (local radio is always master uncomment:

      #Master=true

   To enable A2DP uncomment all lines:
 
      #[A2DP]
      #..
      #..   

** NOTE ****

   Some linux documents indicate the use of sdpd and bluetoothd-service-audio.  These are now deprecated.  The hcid application
   now functions as an SDP daemon (launched as "hcid -s").  The bluetoothd-service-audio is also deprecated as the hcid 
   application will load shared libraries (libaudio.so or audio.so) from the /usr/lib/bluetooth/plugin directory.


2). Setting up alsa audio devices:

   Create/edit .asoundrc at your home directory ~

   Add the following:

   pcm.bluetooth {
         type bluetooth
   } 


Testing Bluetooth
==================

*** FC9 ships with BlueZ 3.30 pre-installed. A USB-HCI transport is available in the kernel.  
Installing a USB adapter supporting the BT-USB class will automatically be recognized by the BT stack.


A). Preparing to add a bluetooth headset

  1. Make sure HCI adapter is up and running

     For USB, plug in USB-BT adapter.

     For Serial HCI devices (example: csr serial on ttyS0 ): 

          hciattach /dev/ttyS0 bcsp 115200
	
          hciconfig -a
     
  2. Log into FC9 desktop session.  Select System->Preference->Internet and Network->Bluetooth
 
     Set radio button to "Other devices can connect".  

     Leave desktop session active.  You will need this to enter a pin code because the default PIN code agent is
     the GNOME desktop Bluetooth applet. 


  3. Scan for your headset device:

   > hcitool -i hci0 scan

     make note of the BT address of your headset e.g.: 00:1C:A4:2D:8D:5A


  *** DEBUGGING BLUEZ stack ***
  
    You can capture BT stack debug prints (including BT-related dbus prints).
    
    ** the following can be done as superuser
  
    1. Stop Bluetooth service             :    > service stop bluetooth
    2. launch hcid with debugging enabled :    > /usr/sbin/hcid -x -s -n -d
    

B). Pairing Mono headset

   export the following environment variable:

   export BTMONO_HEADSET=00:1C:A4:2D:8D:5A

   run the following python script in $WORKAREA\host\tools\athbtfilter\bluez\testscripts

        > bthmonoheadset.py create
   
   The desktop applet will ask for a PIN code.  Once the pairing has completed you should be able to see "Bonded Devices"
   in the Bluetooth applet.  You can make this device "TRUSTED" which will store the PIN code and will no longer 
   require you to input the PIN code.

   To do a quick test (only for BT adapters that route SC(see above).O over HCI):

        > bthmonoheadset.py start
        > arecord -D bluetooth -f S16_LE | aplay -D bluetooth -f S16_LE

        This command records the headset microphone input and echos it back to the headset speakers.

   If you have a BT adapter that routes SCO over a PCM interface you can simply turn on the SCO path
   using the following command:
   
        > bthmonoheadset.py start pcm
        
        To stop:
        
        > bthmonoheadset.py stop 

   **** NOTE: you must configure /etc/bluetooth/audio.conf and set "SCORouting=PCM" (see above)

   You can disconnect the mono headset :
   
        > bthmonoheadset.py stop

C). Pairing Stereo headset 

   export the following environment variable:

        > export BTSTEREO_HEADSET=00:1C:A4:2D:8D:5A

   run the following python script in $WORKAREA\host\tools\athbtfilter\bluez\testscripts

        > bthstereoheadset.py create
   
   The desktop applet will ask for a PIN code.  Once the pairing has completed you should be able to see "Bonded Devices"
   in the Bluetooth applet.  You can make this device "TRUSTED" which will store the PIN code and will no longer 
   require you to input the PIN code.
  
   To do a quick test:
       > bthstereoheadset.py start
       > aplay -D bluetooth <wav file>

   You can disconnect the mono headset :
   
        > bthstereoheadset.py stop

Testing Atheros BT Filter
=========================

   The filter resides in $WORKAREA\host\.output\$ATH_PLATFORM\image as 'abtfilt'.
   
   To operate as a silent daemon execute :      > ./abtfilt
   To operate the daemon with sysloging:        > ./abtfilt -d
   To operate the daemon with console logging:  > ./abtfilt -c -d
   To operate as an ordinary application just add -n to the command line.
   
   The option "-a" enables the filter to issue the AFH classification HCI command when WLAN connects.
   The channel mask is derived from the 2.4 Ghz operating channel (1-14).  Upon WLAN disconnect or when
   WLAN is switches to 11a, the default AFH classification mask (all channels okay) is issued.
   
   The following table shows the AFH channel mapping for channels 1-14, channel 0 is a special case 
   (WLAN disconnected or not operating in 2.4 Ghz band).  The channel map is read LSB to the far left
   to MSB to the far right and covers the 10 octets defined in the BT specification for the AFH
   map.
   
      LSB ------------------------------------------> MSB
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F}}, /* 0 -- no WLAN */
    { {0x00,0x00,0xC0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F}}, /* 1 */
    { {0x0F,0x00,0x00,0xF8,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F}}, /* 2 */
    { {0xFF,0x01,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F}}, /* 3 */
    { {0xFF,0x3F,0x00,0x00,0xE0,0xFF,0xFF,0xFF,0xFF,0x7F}}, /* 4 */
    { {0xFF,0xFF,0x07,0x00,0x00,0xFC,0xFF,0xFF,0xFF,0x7F}}, /* 5 */
    { {0xFF,0xFF,0xFF,0x00,0x00,0x80,0xFF,0xFF,0xFF,0x7F}}, /* 6 */
    { {0xFF,0xFF,0xFF,0x1F,0x00,0x00,0xF0,0xFF,0xFF,0x7F}}, /* 7 */
    { {0xFF,0xFF,0xFF,0xFF,0x03,0x00,0x00,0xFE,0xFF,0x7F}}, /* 8 */
    { {0xFF,0xFF,0xFF,0xFF,0x7F,0x00,0x00,0xC0,0xFF,0x7F}}, /* 9 */
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0x0F,0x00,0x00,0xF8,0x7F}}, /* 10 */
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x00,0x7F}}, /* 11 */
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x3F,0x00,0x00,0x60}}, /* 12 */
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x07,0x00,0x00}}, /* 13 */
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x00}}, /* 14 */
   
   







