HIF support for native Linux MMC Stack.
paull@atheros.com

11/6/09
Added patches for the MMC stacks Standard Host Controller for 2.6.30 and 2.6.32
Patches enable ENE board support and speed up transfers for any Standard Host Controller
linux2.6.30_mmc_std_host.patch
linuxMMC_std_host_2.6.32-rc5sdio.patch


2/18/2009
Added patch for Freescale MX35 SD host driver. tested with SD25 AR6102 Olca 2.1.2

12/18/2008
Tested on Freescale MX27 and OMAP3530 Beageleboard Linux ver 2.6.28
adds DMA bounce buffer support
hif.c ver 5 and hif.h ver 4 are for the old HTC/HIF interface and shouold be useable with 2.1 drivers
ver 6 and 5 are for the new HTC/HIF interface
For older Linux MMC stack versions, comment out in hif.c hifDeviceInserted() the lines:
    /* give us some time to enable, in ms */
    func->enable_timeout = 100;
it is only required on some platforms, eg Beagleboard.

7/18/2008

a. tested on Fedora Core 9 kernel 2.6.25.6, x86 with ENE standrad host controller,using the Olca 2.1.1RC.15
b. requires applying the linux2.6.25.6mmc.patch to the kernel drivers/mmc directory
c. through put is 20-22mbs up/down link
d. new platform type is:
	ATH_PLATFORM=LOCAL_i686_NATIVEMMC-SDIO
	TARGET_TYPE=AR6002
e. known issues: unloading the driver on Fedora Core 9 after conecting to an AP seems to not be complete.



