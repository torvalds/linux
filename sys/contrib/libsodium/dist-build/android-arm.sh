#!/bin/sh
export TARGET_ARCH=armv6
export CFLAGS="-Os -mthumb -marm -march=${TARGET_ARCH}"
ARCH=arm HOST_COMPILER=arm-linux-androideabi "$(dirname "$0")/android-build.sh"
