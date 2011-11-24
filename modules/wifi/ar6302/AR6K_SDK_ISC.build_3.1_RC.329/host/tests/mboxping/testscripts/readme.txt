Copyright (c) 2009 Atheros Corporation.  All rights reserved.

Mboxping Test Readme

Mboxping is an HTC (Host Target Communications) test program.  Mboxping uses a raw network socket
to communicate with test firmware (endpointping).  
Mboxping provided ping (echo), verification, randomization, delay, performance (RX or TX) options.

To use on an Fedora Core linux host :

Run:

    > $WORKAREA/host/test/mboxping/setup_htcregress.sh
    
To run on an Android build :

    Make sure endpointping.bin firmware binary is placed in the correct firmware path for
    the chip type and version (e.g. /system/wifi/ath6k/AR6003/hw2.0 )

    Run the following:
    
    > insmod ar6000.ko eppingtest=1
    > ifconfig wlan0 up
    
    If your version of insmod does not allow kernel module parameters, you can modify ar6000_drv.c
    and force the eppingtest module parameter to 1.
    
The following are example mboxping test execution command line  parameters:

1). Simple 1500-byte packet ping through stream 1 (4 pings)

   ./mboxping -i <netif> -t 1 -r 1 -s 1500 -c 4
   
2). Simple 1500-byte packet bi-directional performance ping through stream 1 for 10 seconds

   ./mboxping -i <netif> --quiet -t 1 -r 1 -s 1500 -d 10 -p 16

3). Randomized 1500-byte packets, bi-directional ping through stream 1 for 10 seconds

   ./mboxping -i <netif> --quiet --verify --delay -t 1 -r 1 -s 1500 -d 10 -p 16    
    
4). TX-only performance 1500 byte packets, through stream 1 for 10 seconds

   ./mboxping -i <netif> --txperf -t 1 -r 1 -s 1500 -d 10 -p 16  
    
5). RX-only performance 1500 byte packets, through stream 1 for 10 seconds

   ./mboxping -i <netif> --rxperf -t 1 -r 1 -s 1500 -d 10 -p 16 
    

   
   
   
   