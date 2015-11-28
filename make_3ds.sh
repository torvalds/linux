#!/bin/sh

cp arch/arm/configs/nintendo3ds_defconfig .config
make ARCH=arm CROSS_COMPILE=arm-unknown-linux-gnueabihf- -j3
make ARCH=arm CROSS_COMPILE=arm-unknown-linux-gnueabihf- nintendo3ds_ctr.dtb
echo "Output file: ./arch/arm/boot/zImage"
echo "Output DTB: ./arch/arm/boot/dts/nintendo3ds_ctr.dtb"
