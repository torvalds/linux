#!/usr/bin/env bash
#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

set -e

# Different versions of adb can sometimes be incompatible (i.e. "adb server
# version (nn) doesn't match this client (mm); killing..."). Ensure that the adb
# in the main builder image matches that in the emulator by sharing the
# platform-tools from the main image.
if [ -d /mnt/android-platform-tools ]; then
    sudo rm -fr /mnt/android-platform-tools/platform-tools
    sudo cp -r /opt/android/sdk/platform-tools /mnt/android-platform-tools
fi
