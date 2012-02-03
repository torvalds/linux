
This distribution includes Broadcom source and binary files for linux wireless
driver and supporting utilities.

In the package you received you will see following subdirectories

driver/    -- linux bmac wl driver module for chosen OS variant
firmware/  -- firmware dongle images (rtecdc.bin file) for download device
apps/      -- tools for dongle download (bcmdl) and runtime WLAN commands(wl)

Source code for rebuilding the host wl driver or application is also provided.
There are three sub-packages in the form of .tar.gz file. Unzip them with tar 
or gzip utility.

1. srcwldriver-fc6u-nodebug-native-apdef-stadef.tar.gz:
	This zip archive contains all the wireless driver sources
2. srcwlexe.tar.gz
	This zip archive contains all the wl utility sources
3. srcbcmdl.tar.gz
	This zip archive contains all the usb downloader sources

When you extract the sources you may see following directories.

src/include	-- various header files
src/bcmcrypto, src/shared, src/wl/sys:
                -- source files which may be shared by several binaries
src/wl/exe	-- source files for wl.exe utility
src/usbdev	-- broadcom usb downloader files
src/wl/config, src/wl/linux:
                -- other supporting config files and makefiles to build
                -- above sources

1. To rebuild the wl driver module:
  % extract srcwldriver-*.tar.gz package on a linux system
  % cd src/wl/linux
  % make apdef-stadef

   The resulting module file (wl.ko) will be placed in a subdirectory
   named obj-*-<version>, where <version> is the Linux kernel version of
   the compiling system.

   To rebuild a wl driver module with additional debugging messages enabled,
   add -debug to the make target:
     % cd src/wl/linux
     % make debug-apdef-stadef

2. To rebuild wl.exe:
  % extract srcwlexe-*.tar.gz package on a linux system
  % cd src/wl/exe
  % make

   The resulting utility (wl) will be placed in a src/wl/exe itself

3. To rebuild usb downloader
  % extract srcdcmdl-*.tar.gz package on a linux system
  % cd src/usbdev/usbdl
  % make

   The resulting utility (bcmdl) will be placed in a src/usbdev/usbdl itself

----------------------------------------------------------------------------
