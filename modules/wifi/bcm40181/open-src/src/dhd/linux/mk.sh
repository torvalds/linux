make dhd-cdc-sdmmc-gpl-debug OEM_ANDROID=1 LINUXDIR=~/workspace/exdroid/lichee/linux-2.6.36 CROSS_COMPILE=/opt/arm-2009q1/bin/arm-none-linux-gnueabi- ARCH=arm 

cp NVRAM.txt ~/workspace/work/BCM40181/bcm_out
cp sdio-wapi.bin ~/workspace/work/BCM40181/bcm_out
cp ./dhd-cdc-sdmmc-gpl-debug-2.6.36-android/dhd.ko ~/workspace/work/BCM40181/bcm_out
