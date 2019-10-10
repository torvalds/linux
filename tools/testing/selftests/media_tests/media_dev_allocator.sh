#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Media Device Allocator API test script
# Copyright (c) 2019 Shuah Khan <shuah@kernel.org>

echo "Media Device Allocator testing: unbind and bind"
echo "media driver $1 audio driver $2"

MDRIVER=/sys/bus/usb/drivers/$1
cd $MDRIVER
MDEV=$(ls -d *\-*)

ADRIVER=/sys/bus/usb/drivers/$2
cd $ADRIVER
ADEV=$(ls -d *\-*.1)

echo "=================================="
echo "Test unbind both devices - start"
echo "Running unbind of $MDEV from $MDRIVER"
echo $MDEV > $MDRIVER/unbind;

echo "Media device should still be present!"
ls -l /dev/media*

echo "sound driver is at: $ADRIVER"
echo "Device is: $ADEV"

echo "Running unbind of $ADEV from $ADRIVER"
echo $ADEV > $ADRIVER/unbind;

echo "Media device should have been deleted!"
ls -l /dev/media*
echo "Test unbind both devices - end"

echo "=================================="

echo "Test bind both devices - start"
echo "Running bind of $MDEV from $MDRIVER"
echo $MDEV > $MDRIVER/bind;

echo "Media device should be present!"
ls -l /dev/media*

echo "Running bind of $ADEV from $ADRIVER"
echo $ADEV > $ADRIVER/bind;

echo "Media device should be there!"
ls -l /dev/media*

echo "Test bind both devices - end"

echo "=================================="

echo "Test unbind $MDEV - bind $MDEV - unbind $ADEV - bind $ADEV start"

echo "Running unbind of $MDEV from $MDRIVER"
echo $MDEV > $MDRIVER/unbind;

echo "Media device should be there!"
ls -l /dev/media*

sleep 1

echo "Running bind of $MDEV from $MDRIVER"
echo $MDEV > $MDRIVER/bind;

echo "Media device should be there!"
ls -l /dev/media*

echo "Running unbind of $ADEV from $ADRIVER"
echo $ADEV > $ADRIVER/unbind;

echo "Media device should be there!"
ls -l /dev/media*

sleep 1

echo "Running bind of $ADEV from $ADRIVER"
echo $ADEV > $ADRIVER/bind;

echo "Media device should be there!"
ls -l /dev/media*

echo "Test unbind $MDEV - bind $MDEV - unbind $ADEV - bind $ADEV end"
echo "=================================="
