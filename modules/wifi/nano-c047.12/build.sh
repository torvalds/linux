#!/bin/sh
set -e

make CROSS_COMPILE=~/workspace/exdroid/lichee/buildroot/output/external-toolchain/bin/arm-none-linux-gnueabi- \
	ARCH=arm KERNEL_DIR=~/workspace/exdroid/lichee/linux-2.6.36

 
