#!/bin/sh
export TARGET_ARCH=mips64r6
export CFLAGS="-Os -march=${TARGET_ARCH}"
CC="mips64el-linux-android-gcc" NDK_PLATFORM=android-21 ARCH=mips64 HOST_COMPILER=mips64el-linux-android "$(dirname "$0")/android-build.sh"
