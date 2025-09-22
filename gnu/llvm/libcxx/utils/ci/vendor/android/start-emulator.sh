#!/usr/bin/env bash
#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

# Starts a new Docker container using a Docker image containing the Android
# Emulator and an OS image. Stops and removes the old container if it exists
# already.

set -e

THIS_DIR="$(cd "$(dirname "$0")" && pwd)"
. "${THIS_DIR}/emulator-functions.sh"

EMU_IMG="${1}"
if ! validate_emu_img "${EMU_IMG}"; then
    echo "error: The first argument must be a valid emulator image." >&2
    exit 1
fi

"${THIS_DIR}/stop-emulator.sh"

# Start the container.
docker run --name libcxx-ci-android-emulator --detach --device /dev/kvm \
    -eEMU_PARTITION_SIZE=8192 \
    --volume android-platform-tools:/mnt/android-platform-tools \
    $(docker_image_of_emu_img ${EMU_IMG})
ERR=0
docker exec libcxx-ci-android-emulator emulator-wait-for-ready.sh || ERR=${?}
echo "Emulator container initial logs:"
docker logs libcxx-ci-android-emulator
if [ ${ERR} != 0 ]; then
    exit ${ERR}
fi

# Make sure the device is accessible from outside the emulator container and
# advertise to the user that this script exists.
. "${THIS_DIR}/setup-env-for-emulator.sh"
adb wait-for-device
