# $FreeBSD$

			README File
		QLogic 8300 series Dual Port
10 Gigabit Ethernet & CNA Adapter Driver for FreeBSD 9.x/10.x

		QLogic Corporation.
		All rights reserved. 


Table of Contents
1. Package Contents 
2. OS Support
3. Supported Features
4. Using the Driver
   4.1 Installing the driver
   4.2 Removing the driver
5. Driver Parameters
6. Additional Notes
7. Contacting Support

1. Package Contents 
 * Documentation 
   - README (this document) version:1.0
   - Release Notes Version:1.0
 * Driver (if_qlxgbe.ko)
	- FreeBSD 9.x/10.x
 * Firmware: pre-flashed on QLogic adapter;

2. OS Support

The Qlogic 83xx 10Gigabit Ethernet/CNA driver is compatible with the 
following OS platforms:
 * FreeBSD 9.x/10.x (64-bit) [Intel EM64T, AMD64]

3. Supported Features
10Gigabit Ethernet NIC/CNA driver supports following features

* Large Segment Offload over TCP IPV4
* Large Segment Offload over TCP IPV6
* Receive Side scaling
* TCP over IPv4 checksum offload
* UDP over IPv4 checksum offload
* IPV4 checksum offload
* TCP over IPv6 checksum offload
* UDP over IPv6 checksum offload
* Jumbo frames
* VLAN Tag


4. Using the driver

 4.1 Installing the driver

   - copy the driver file (if_qlxgbe.ko) into some directory (say qla_driver)
   - cd <to qla_driver>
   - kldload -v ./if_qlxgbe.ko

 4.2 Removing the driver
 
  - kldunload if_qlxgbe

5. Parameters to set prior to installing the driver
     Please run  "sysctl kern.ipc" and "sysctl net.inet.tcp" and see if these
     values are already greater than shown below. Change only those which
     are less than shown below.

   - Add the following lines to /etc/sysctl.conf and reboot the machine prior
     to installing the driver
   
	kern.ipc.nmbjumbo9=2000000
	kern.ipc.nmbclusters=1000000
	net.inet.tcp.recvbuf_max=262144
	net.inet.tcp.recvbuf_inc=16384
	kern.ipc.maxsockbuf=2097152
	net.inet.tcp.recvspace=131072
	net.inet.tcp.sendbuf_max=262144
	net.inet.tcp.sendspace=65536
 
   - If you do not want to reboot the system please run the following commands

	login or su to root

	sysctl kern.ipc.nmbjumbo9=2000000
	sysctl kern.ipc.nmbclusters=1000000
	sysctl net.inet.tcp.recvbuf_max=262144
	sysctl net.inet.tcp.recvbuf_inc=16384
	sysctl kern.ipc.maxsockbuf=2097152
	sysctl net.inet.tcp.recvspace=131072
	sysctl net.inet.tcp.sendbuf_max=262144
	sysctl net.inet.tcp.sendspace=65536

6. Compile options Makefile if building driver from sources
	None

7. Contacting Support 
Please feel free to contact your QLogic approved reseller or QLogic 
Technical Support at any phase of integration for assistance. QLogic
Technical Support can be reached by the following methods: 
Web:    http://support.qlogic.com
E-mail: support@qlogic.com
(c) Copyright 2013-14. All rights reserved worldwide. QLogic, the QLogic 
logo, and the Powered by QLogic logo are registered trademarks of
QLogic Corporation. All other brand and product names are trademarks 
or registered trademarks of their respective owners. 
