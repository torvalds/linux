#!/usr/bin/env bash
#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

set -e

THIS_DIR="$(cd "$(dirname "$0")" && pwd)"
. "${THIS_DIR}/emulator-functions.sh"

# Cleanup the emulator if it's already running.
if docker container inspect libcxx-ci-android-emulator &>/dev/null; then
    echo "Stopping existing emulator container..."
    docker stop libcxx-ci-android-emulator

    echo "Emulator container final logs:"
    docker logs libcxx-ci-android-emulator

    echo "Removing existing emulator container..."
    docker rm libcxx-ci-android-emulator
fi
