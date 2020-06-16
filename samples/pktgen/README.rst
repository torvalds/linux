Sample and benchmark scripts for pktgen (packet generator)
==========================================================
This directory contains some pktgen sample and benchmark scripts, that
can easily be copied and adjusted for your own use-case.

General doc is located in kernel: Documentation/networking/pktgen.rst

Helper include files
====================
This directory contains two helper shell files, that can be "included"
by shell source'ing.  Namely "functions.sh" and "parameters.sh".

Common parameters
-----------------
The parameters.sh file support easy and consistant parameter parsing
across the sample scripts.  Usage example is printed on errors::

 Usage: ./pktgen_sample01_simple.sh [-vx] -i ethX
  -i : ($DEV)       output interface/device (required)
  -s : ($PKT_SIZE)  packet size
  -d : ($DEST_IP)   destination IP. CIDR (e.g. 198.18.0.0/15) is also allowed
  -m : ($DST_MAC)   destination MAC-addr
  -p : ($DST_PORT)  destination PORT range (e.g. 433-444) is also allowed
  -t : ($THREADS)   threads to start
  -f : ($F_THREAD)  index of first thread (zero indexed CPU number)
  -c : ($SKB_CLONE) SKB clones send before alloc new SKB
  -n : ($COUNT)     num messages to send per thread, 0 means indefinitely
  -b : ($BURST)     HW level bursting of SKBs
  -v : ($VERBOSE)   verbose
  -x : ($DEBUG)     debug

The global variable being set is also listed.  E.g. the required
interface/device parameter "-i" sets variable $DEV.

Common functions
----------------
The functions.sh file provides; Three different shell functions for
configuring the different components of pktgen: pg_ctrl(), pg_thread()
and pg_set().

These functions correspond to pktgens different components.
 * pg_ctrl()   control "pgctrl" (/proc/net/pktgen/pgctrl)
 * pg_thread() control the kernel threads and binding to devices
 * pg_set()    control setup of individual devices

See sample scripts for usage examples.
