#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright 2026 Google LLC

BASE_DIR=`dirname $0`
MODULE="gpio-cdev-uaf"

fail() {
	echo "$*" >&2
	echo "GPIO $MODULE test FAIL"
	exit 1
}

skip() {
	echo "$*" >&2
	echo "GPIO $MODULE test SKIP"
	exit 4
}

# Load the gpio-sim module. This will pull in configfs if needed too.
modprobe gpio-sim || skip "unable to load the gpio-sim module"
# Make sure configfs is mounted at /sys/kernel/config. Wait a bit if needed.
for _ in `seq 5`; do
	mountpoint -q /sys/kernel/config && break
	mount -t configfs none /sys/kernel/config
	sleep 0.1
done
mountpoint -q /sys/kernel/config || \
	skip "configfs not mounted at /sys/kernel/config"

echo "1. GPIO"

echo "1.1. poll"
$BASE_DIR/gpio-cdev-uaf chip poll || fail "failed to test chip poll"
echo "1.2. read"
$BASE_DIR/gpio-cdev-uaf chip read || fail "failed to test chip read"
echo "1.3. ioctl"
$BASE_DIR/gpio-cdev-uaf chip ioctl || fail "failed to test chip ioctl"

echo "2. linehandle"

echo "2.1. ioctl"
$BASE_DIR/gpio-cdev-uaf handle ioctl || fail "failed to test handle ioctl"

echo "3. lineevent"

echo "3.1. read"
$BASE_DIR/gpio-cdev-uaf event read || fail "failed to test event read"
echo "3.2. poll"
$BASE_DIR/gpio-cdev-uaf event poll || fail "failed to test event poll"
echo "3.3. ioctl"
$BASE_DIR/gpio-cdev-uaf event ioctl || fail "failed to test event ioctl"

echo "4. linereq"

echo "4.1. read"
$BASE_DIR/gpio-cdev-uaf req read || fail "failed to test req read"
echo "4.2. poll"
$BASE_DIR/gpio-cdev-uaf req poll || fail "failed to test req poll"
echo "4.3. ioctl"
$BASE_DIR/gpio-cdev-uaf req ioctl || fail "failed to test req ioctl"

echo "GPIO $MODULE test PASS"
