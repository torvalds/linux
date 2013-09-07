exfat-nofuse
============

Linux non-fuse read/write kernel driver for the exFAT file system.<br />
Originally ported from android kernel v3.0.


Kudos to ksv1986 for the mutex patch!<br />
Thanks to JackNorris for being awesome and providing the clear_inode() patch.<br />
<br />
Big thanks to lqs for completing the driver!
Big thanks to benpicco for fixing 3.11.y compatibility!


Special thanks to github user AndreiLux for spreading the word about the leak!<br />


Installation as stand alone module:
====================================

    make -f Makefile.module KDIR="path to kernel source" CROSS_COMPILE="path to android chain tools (as linaro)/bin/SOMETHING- (see your folder for clues)"

Example how it's works for me!

    make -f Makefile.module CROSS_COMPILE=../dorimanx-SG2-I9100-Kernel/android-toolchain/bin/arm-eabi- KDIR=../dorimanx-SG2-I9100-Kernel/

exfat.ko module file will be created in exfat source folder. and will work with kernel source you have used.

    make -f Makefile.module install

To load the driver manually, run this as root:

    modprobe exfat

To add to kernel you need to do this:
======================================

cd your kernel source dir

mkdir fs/exfat

copy all files (exept .git) from exfat-nofuse to your kernel source fs/exfat/

see
https://github.com/dorimanx/Dorimanx-SG2-I9100-Kernel/commit/e8fc728a68096db9ffcebff40244ebfb60a3de18

edit fs/Kconfig
edit fs/Makefile

cd your kernel source
make menuconfig

Go to:
> File systems > DOS/FAT/NT > check the exfat as MODULE (M)
> (437) Default codepage for exFAT
> (utf8) Default iocharset for exFAT

> ESC to main menu
> Save an Alternate Configuration File
> ESC ESC

build your kernel.

and you will have new module!

exfat.ko

have fun.

Free Software for the Free Minds!
=====================================
