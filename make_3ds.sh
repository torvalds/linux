#!/bin/sh

cp arch/arm/configs/nintendo3ds_defconfig .config
make ARCH=arm -j3
make ARCH=arm -j3 nintendo3ds_ctr.dtb
echo "Output file: ./arch/arm/boot/zImage"
