#!/bin/sh
export TARGET_ARCH=westmere
export CFLAGS="-Os -march=${TARGET_ARCH}"
NDK_PLATFORM=android-21 ARCH=x86_64 HOST_COMPILER=x86_64-linux-android "$(dirname "$0")/android-build.sh"
