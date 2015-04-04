#!/bin/sh

cp arch/arm/configs/nintendo3ds_defconfig .config
make ARCH=arm
echo "Output file: ./arch/arm/boot/zImage"
