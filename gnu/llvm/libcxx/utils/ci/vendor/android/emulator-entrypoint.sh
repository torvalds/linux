#!/usr/bin/env bash
#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

# This script is the entrypoint of an Android Emulator Docker container.

set -e

# The container's /dev/kvm has the same UID+GID as the host device. Changing the
# ownership inside the container doesn't affect the UID+GID on the host.
sudo chown emulator:emulator /dev/kvm

# Always use a copy of platform-tools provided by the host to ensure that the
# versions of adb match between the host and the emulator.
if [ ! -x /mnt/android-platform-tools/platform-tools/adb ]; then
    echo "error: This image requires platform-tools mounted at" \
         "/mnt/android-platform-tools containing platform-tools/adb" >&2
    exit 1
fi
sudo cp -r /mnt/android-platform-tools/platform-tools /opt/android/sdk

# Start an adb host server. `adb start-server` blocks until the port is ready.
# Use ADB_REJECT_KILL_SERVER=1 to ensure that an adb protocol version mismatch
# doesn't kill the adb server.
ADB_REJECT_KILL_SERVER=1 adb -a start-server

# This syntax (using an IP address of 127.0.0.1 rather than localhost) seems to
# prevent the adb client from ever spawning an adb host server.
export ADB_SERVER_SOCKET=tcp:127.0.0.1:5037

# The AVD could already exist if the Docker container were stopped and then
# restarted.
if [ ! -d ~/.android/avd/emulator.avd ]; then
    # N.B. AVD creation takes a few seconds and creates a mostly-empty
    # multi-gigabyte userdata disk image. (It's not useful to create the AVDs in
    # advance.)
    avdmanager --verbose create avd --name emulator \
        --package "${EMU_PACKAGE_NAME}" --device pixel_5
fi

# Use exec so that the emulator is PID 1, so that `docker stop` kills the
# emulator.
exec emulator @emulator -no-audio -no-window -no-metrics \
    -partition-size "${EMU_PARTITION_SIZE}"
