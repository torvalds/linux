
"Readme.freebsd.txt"    PMC-Sierra, Inc.   05/01/2013

              PMC-Sierra SPCv/SPCve/SPCv+ TISA FreeBSD Initiator Driver
              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Date:                 06/01/2014
Release Purpose:      PMC-Sierra sTSDK support customer evaluation
SPCv Host Driver ver. 1.2.0 for Rev C chip


1. Driver Source Structure
--------------------------
   - SAS related source tree -
   Tisa/sallsdk/api
   Tisa/sallsdk/spc
   Tisa/sallsdk/hda
   Tisa/tisa/sassata
   Tisa/discovery/api
   Tisa/discovery/src
   Tisa/sat/api
   Tisa/sat/src
   - FreeBSD related source tree -
   Tisa/tisa/api
   freebsd/common
   freebsd/ini
   freebsd/ini/src

2. Process To Build the Driver Module First Time
------------------------------------------------
  From the directory freebsd/ini/src, type "make".

3. Process To Rebuild the Driver Module
---------------------------------------
  1. from the directory freebsd/ini/src, type "make clean".
  2. When the mclean operation is finished, type "make".

4. Loading and Unloading Driver Module
--------------------------------------
  Type "kldload ./pmspcv.ko" to load the driver 

  Type "kldunload pmspcv.ko" to unload the driver 

   Please note:
   Loading may fail to detect attached device because improper
   parameter setting, or because of an extra "^M" character
   at the end of each line in some file.
   

5. Description
--------------
  1. This driver has been built and tested on FreeBSD 9.0 amd64


6. Additional Notes
-------------------------------------
 1. This section covers how phy ID in PhyParms should be used in the
    different types of the controller. In SPCv/SPCve controller (8-phy
    controller),
    PhyParms[0-3] are mapped to Phy0-3 and PhyParms[8-11] are mapped to Phy4-8. 
    In SPCv+/SPCve+ controller(16-phy controller), PhyParms[0-15] are mapped to
    Phy 0-15.

