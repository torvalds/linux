#!/bin/sh
export TARGET_ARCH=armv7-a
export CFLAGS="-Os -mfloat-abi=softfp -mfpu=vfpv3-d16 -mthumb -marm -march=${TARGET_ARCH}"
ARCH=arm HOST_COMPILER=arm-linux-androideabi "$(dirname "$0")/android-build.sh"
