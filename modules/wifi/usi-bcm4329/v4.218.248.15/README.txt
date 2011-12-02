
This distribution includes binary files in the following subdirectories:

firmware/  -- binary images (and their ascii bin-arrays) for download 
              to the dongle device
host/      -- linux kernel modules: host drivers for the dongle
apps/      -- tools for dongle download and runtime WLAN commands

A brief overview of how to use these files can be found in ReleaseNotes.html.

Source code for rebuilding the host-side binaries is provided in the src
directory; an overview of that structure is:

src/include	-- various header files
src/shared	-- source files which may be shared by several binaries
src/dongle	-- dongle-specific header
src/dhd/linux	-- build directory for dhd linux module (dhd and sdstd)
src/dhd/sys	-- source code for dhd driver
src/bcmsdio/sys	-- source code for sdstd driver (for standard SDIO host)

To rebuild the dhd kernel module, which incorporates both the dongle host
driver (dhd) and the SDIO host controller driver (sdstd):
  % cd src/dhd/linux
  % make dhd-cdc-sdstd

The resulting module file (dhd.o or dhd.ko) will be placed in a subdirectory
named dhd-cdc-sdstd-<version>, where <version> is the Linux kernel version of
the compiling system.

The kernel module may also be built with an embedded dongle image which can
then be loaded automatically when the module starts, rather than requiring a
separate user command to download (as described in the Release Notes):
  % cd src/dhd/linux
  % make dhd-cdc-sdstd-dnglimage

The firmware image embedded in the module is generally determined by the
DNGL_IMAGE_NAME variable in src/dhd/linux/Makefile. You may modify that
line in the Makefile or specify DNGL_IMAGE_NAME=<variant> on the make
command line (where <variant> is the name of one of the image directories
found in src/dongle/rte/wl/builds)

To rebuild a module with additional debugging messages enabled, add -debug to
the make target:
  % cd src/dhd/linux
  % make dhd-cdc-sdstd-debug

------------------------------------------------------------------------
