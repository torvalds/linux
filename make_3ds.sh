#!/bin/sh

CROSS_PATH="/opt/x-tools/arm-unknown-linux-gnueabi/bin/"

export PATH=$PATH:$CROSS_PATH
cp arch/arm/configs/nintendo3ds_defconfig .config
make ARCH=arm CROSS_COMPILE=arm-unknown-linux-gnueabi- -j3
make ARCH=arm CROSS_COMPILE=arm-unknown-linux-gnueabi- nintendo3ds_ctr.dtb
echo "Output file: ./arch/arm/boot/zImage"
echo "Output DTB: ./arch/arm/boot/dts/nintendo3ds_ctr.dtb"
