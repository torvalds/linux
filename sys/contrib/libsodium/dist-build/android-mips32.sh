#!/bin/sh
export TARGET_ARCH=mips32
export CFLAGS="-Os"
ARCH=mips HOST_COMPILER=mipsel-linux-android "$(dirname "$0")/android-build.sh"
