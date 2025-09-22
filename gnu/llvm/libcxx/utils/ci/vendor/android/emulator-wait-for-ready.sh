#!/usr/bin/env bash
#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

set -ex

# Time to wait in seconds. The emulator ought to start in 5-15 seconds or so,
# so add a safety factor in case something takes longer in CI.
TIMEOUT=${1-300}

# This syntax (using an IP address of 127.0.0.1 rather than localhost) seems to
# prevent the adb client from ever spawning an adb host server.
export ADB_SERVER_SOCKET=tcp:127.0.0.1:5037

# Invoke nc first to ensure that something is listening to port 5037. Otherwise,
# invoking adb might fork an adb server.
#
# TODO: Consider waiting for `adb shell getprop dev.bootcomplete 2>/dev/null
# | grep 1 >/dev/null` as well. It adds ~4 seconds to 21-def-x86 and ~15 seconds
# to 33-goog-x86_64 and doesn't seem to be necessary for running libc++ tests.
timeout ${TIMEOUT} bash -c '
until (nc -z localhost 5037 && adb wait-for-device); do
    sleep 0.5
done
'
