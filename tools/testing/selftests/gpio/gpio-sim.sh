#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 Bartosz Golaszewski <brgl@bgdev.pl>

BASE_DIR=`dirname $0`
CONFIGFS_DIR="/sys/kernel/config/gpio-sim"
MODULE="gpio-sim"

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

remove_chip() {
	local CHIP=$1

	for FILE in $CONFIGFS_DIR/$CHIP/*; do
		BANK=`basename $FILE`
		if [ "$BANK" = "live" -o "$BANK" = "dev_name" ]; then
			continue
		fi

		LINES=`ls $CONFIGFS_DIR/$CHIP/$BANK/ | grep -E ^line`
		if [ "$?" = 0 ]; then
			for LINE in $LINES; do
				if [ -e $CONFIGFS_DIR/$CHIP/$BANK/$LINE/hog ]; then
					rmdir $CONFIGFS_DIR/$CHIP/$BANK/$LINE/hog || \
						fail "Unable to remove the hog"
				fi

				rmdir $CONFIGFS_DIR/$CHIP/$BANK/$LINE || \
					fail "Unable to remove the line"
			done
		fi

		rmdir $CONFIGFS_DIR/$CHIP/$BANK
	done

	rmdir $CONFIGFS_DIR/$CHIP || fail "Unable to remove the chip"
}

configfs_cleanup() {
	for CHIP in `ls $CONFIGFS_DIR/`; do
		remove_chip $CHIP
	done
}

create_chip() {
	local CHIP=$1

	mkdir $CONFIGFS_DIR/$CHIP
}

create_bank() {
	local CHIP=$1
	local BANK=$2

	mkdir $CONFIGFS_DIR/$CHIP/$BANK
}

set_label() {
	local CHIP=$1
	local BANK=$2
	local LABEL=$3

	echo $LABEL > $CONFIGFS_DIR/$CHIP/$BANK/label || fail "Unable to set the chip label"
}

set_num_lines() {
	local CHIP=$1
	local BANK=$2
	local NUM_LINES=$3

	echo $NUM_LINES > $CONFIGFS_DIR/$CHIP/$BANK/num_lines || \
		fail "Unable to set the number of lines"
}

set_line_name() {
	local CHIP=$1
	local BANK=$2
	local OFFSET=$3
	local NAME=$4
	local LINE_DIR=$CONFIGFS_DIR/$CHIP/$BANK/line$OFFSET

	test -d $LINE_DIR || mkdir $LINE_DIR
	echo $NAME > $LINE_DIR/name || fail "Unable to set the line name"
}

enable_chip() {
	local CHIP=$1

	echo 1 > $CONFIGFS_DIR/$CHIP/live || fail "Unable to enable the chip"
}

disable_chip() {
	local CHIP=$1

	echo 0 > $CONFIGFS_DIR/$CHIP/live || fail "Unable to disable the chip"
}

configfs_chip_name() {
	local CHIP=$1
	local BANK=$2

	cat $CONFIGFS_DIR/$CHIP/$BANK/chip_name 2> /dev/null || \
		fail "unable to read the chip name from configfs"
}

configfs_dev_name() {
	local CHIP=$1

	cat $CONFIGFS_DIR/$CHIP/dev_name 2> /dev/null || \
		fail "unable to read the device name from configfs"
}

get_chip_num_lines() {
	local CHIP=$1
	local BANK=$2

	$BASE_DIR/gpio-chip-info /dev/`configfs_chip_name $CHIP $BANK` num-lines || \
		fail "unable to read the number of lines from the character device"
}

get_chip_label() {
	local CHIP=$1
	local BANK=$2

	$BASE_DIR/gpio-chip-info /dev/`configfs_chip_name $CHIP $BANK` label || \
		fail "unable to read the chip label from the character device"
}

get_line_name() {
	local CHIP=$1
	local BANK=$2
	local OFFSET=$3

	$BASE_DIR/gpio-line-name /dev/`configfs_chip_name $CHIP $BANK` $OFFSET || \
		fail "unable to read the line name from the character device"
}

sysfs_set_pull() {
	local DEV=$1
	local BANK=$2
	local OFFSET=$3
	local PULL=$4
	local DEVNAME=`configfs_dev_name $DEV`
	local CHIPNAME=`configfs_chip_name $DEV $BANK`
	local SYSFS_PATH="/sys/devices/platform/$DEVNAME/$CHIPNAME/sim_gpio$OFFSET/pull"

	echo $PULL > $SYSFS_PATH || fail "Unable to set line pull in sysfs"
}

# Load the gpio-sim module. This will pull in configfs if needed too.
modprobe gpio-sim || skip "unable to load the gpio-sim module"
# Make sure configfs is mounted at /sys/kernel/config. Wait a bit if needed.
for IDX in `seq 5`; do
	if [ "$IDX" -eq "5" ]; then
		skip "configfs not mounted at /sys/kernel/config"
	fi

	mountpoint -q /sys/kernel/config && break
	sleep 0.1
done
# If the module was already loaded: remove all previous chips
configfs_cleanup

trap "exit 1" SIGTERM SIGINT
trap configfs_cleanup EXIT

echo "1. chip_name and dev_name attributes"

echo "1.1. Chip name is communicated to user"
create_chip chip
create_bank chip bank
enable_chip chip
test -n `cat $CONFIGFS_DIR/chip/bank/chip_name` || fail "chip_name doesn't work"
remove_chip chip

echo "1.2. chip_name returns 'none' if the chip is still pending"
create_chip chip
create_bank chip bank
test "`cat $CONFIGFS_DIR/chip/bank/chip_name`" = "none" || \
	fail "chip_name doesn't return 'none' for a pending chip"
remove_chip chip

echo "1.3. Device name is communicated to user"
create_chip chip
create_bank chip bank
enable_chip chip
test -n `cat $CONFIGFS_DIR/chip/dev_name` || fail "dev_name doesn't work"
remove_chip chip

echo "2. Creating and configuring simulated chips"

echo "2.1. Default number of lines is 1"
create_chip chip
create_bank chip bank
enable_chip chip
test "`get_chip_num_lines chip bank`" = "1" || fail "default number of lines is not 1"
remove_chip chip

echo "2.2. Number of lines can be specified"
create_chip chip
create_bank chip bank
set_num_lines chip bank 16
enable_chip chip
test "`get_chip_num_lines chip bank`" = "16" || fail "number of lines is not 16"
remove_chip chip

echo "2.3. Label can be set"
create_chip chip
create_bank chip bank
set_label chip bank foobar
enable_chip chip
test "`get_chip_label chip bank`" = "foobar" || fail "label is incorrect"
remove_chip chip

echo "2.4. Label can be left empty"
create_chip chip
create_bank chip bank
enable_chip chip
test -z "`cat $CONFIGFS_DIR/chip/bank/label`" || fail "label is not empty"
remove_chip chip

echo "2.5. Line names can be configured"
create_chip chip
create_bank chip bank
set_num_lines chip bank 16
set_line_name chip bank 0 foo
set_line_name chip bank 2 bar
enable_chip chip
test "`get_line_name chip bank 0`" = "foo" || fail "line name is incorrect"
test "`get_line_name chip bank 2`" = "bar" || fail "line name is incorrect"
remove_chip chip

echo "2.6. Line config can remain unused if offset is greater than number of lines"
create_chip chip
create_bank chip bank
set_num_lines chip bank 2
set_line_name chip bank 5 foobar
enable_chip chip
test "`get_line_name chip bank 0`" = "" || fail "line name is incorrect"
test "`get_line_name chip bank 1`" = "" || fail "line name is incorrect"
remove_chip chip

echo "2.7. Line configfs directory names are sanitized"
create_chip chip
create_bank chip bank
mkdir $CONFIGFS_DIR/chip/bank/line12foobar 2> /dev/null && \
	fail "invalid configfs line name accepted"
mkdir $CONFIGFS_DIR/chip/bank/line_no_offset 2> /dev/null && \
	fail "invalid configfs line name accepted"
remove_chip chip

echo "2.8. Multiple chips can be created"
CHIPS="chip0 chip1 chip2"
for CHIP in $CHIPS; do
	create_chip $CHIP
	create_bank $CHIP bank
	enable_chip $CHIP
done
for CHIP in $CHIPS; do
	remove_chip $CHIP
done

echo "2.9. Can't modify settings when chip is live"
create_chip chip
create_bank chip bank
enable_chip chip
echo foobar > $CONFIGFS_DIR/chip/bank/label 2> /dev/null && \
	fail "Setting label of a live chip should fail"
echo 8 > $CONFIGFS_DIR/chip/bank/num_lines 2> /dev/null && \
	fail "Setting number of lines of a live chip should fail"
remove_chip chip

echo "2.10. Can't create line items when chip is live"
create_chip chip
create_bank chip bank
enable_chip chip
mkdir $CONFIGFS_DIR/chip/bank/line0 2> /dev/null && fail "Creating line item should fail"
remove_chip chip

echo "2.11. Probe errors are propagated to user-space"
create_chip chip
create_bank chip bank
set_num_lines chip bank 99999
echo 1 > $CONFIGFS_DIR/chip/live 2> /dev/null && fail "Probe error was not propagated"
remove_chip chip

echo "2.12. Cannot enable a chip without any GPIO banks"
create_chip chip
echo 1 > $CONFIGFS_DIR/chip/live 2> /dev/null && fail "Chip enabled without any GPIO banks"
remove_chip chip

echo "2.13. Duplicate chip labels are not allowed"
create_chip chip
create_bank chip bank0
set_label chip bank0 foobar
create_bank chip bank1
set_label chip bank1 foobar
echo 1 > $CONFIGFS_DIR/chip/live 2> /dev/null && fail "Duplicate chip labels were not rejected"
remove_chip chip

echo "2.14. Lines can be hogged"
create_chip chip
create_bank chip bank
set_num_lines chip bank 8
mkdir -p $CONFIGFS_DIR/chip/bank/line4/hog
enable_chip chip
$BASE_DIR/gpio-mockup-cdev -s 1 /dev/`configfs_chip_name chip bank` 4 2> /dev/null && \
	fail "Setting the value of a hogged line shouldn't succeed"
remove_chip chip

echo "3. Controlling simulated chips"

echo "3.1. Pull can be set over sysfs"
create_chip chip
create_bank chip bank
set_num_lines chip bank 8
enable_chip chip
sysfs_set_pull chip bank 0 pull-up
$BASE_DIR/gpio-mockup-cdev /dev/`configfs_chip_name chip bank` 0
test "$?" = "1" || fail "pull set incorrectly"
sysfs_set_pull chip bank 0 pull-down
$BASE_DIR/gpio-mockup-cdev /dev/`configfs_chip_name chip bank` 1
test "$?" = "0" || fail "pull set incorrectly"
remove_chip chip

echo "3.2. Pull can be read from sysfs"
create_chip chip
create_bank chip bank
set_num_lines chip bank 8
enable_chip chip
DEVNAME=`configfs_dev_name chip`
CHIPNAME=`configfs_chip_name chip bank`
SYSFS_PATH=/sys/devices/platform/$DEVNAME/$CHIPNAME/sim_gpio0/pull
test `cat $SYSFS_PATH` = "pull-down" || fail "reading the pull failed"
sysfs_set_pull chip bank 0 pull-up
test `cat $SYSFS_PATH` = "pull-up" || fail "reading the pull failed"
remove_chip chip

echo "3.3. Incorrect input in sysfs is rejected"
create_chip chip
create_bank chip bank
set_num_lines chip bank 8
enable_chip chip
DEVNAME=`configfs_dev_name chip`
CHIPNAME=`configfs_chip_name chip bank`
SYSFS_PATH="/sys/devices/platform/$DEVNAME/$CHIPNAME/sim_gpio0/pull"
echo foobar > $SYSFS_PATH 2> /dev/null && fail "invalid input not detected"
remove_chip chip

echo "3.4. Can't write to value"
create_chip chip
create_bank chip bank
enable_chip chip
DEVNAME=`configfs_dev_name chip`
CHIPNAME=`configfs_chip_name chip bank`
SYSFS_PATH="/sys/devices/platform/$DEVNAME/$CHIPNAME/sim_gpio0/value"
echo 1 > $SYSFS_PATH 2> /dev/null && fail "writing to 'value' succeeded unexpectedly"
remove_chip chip

echo "4. Simulated GPIO chips are functional"

echo "4.1. Values can be read from sysfs"
create_chip chip
create_bank chip bank
set_num_lines chip bank 8
enable_chip chip
DEVNAME=`configfs_dev_name chip`
CHIPNAME=`configfs_chip_name chip bank`
SYSFS_PATH="/sys/devices/platform/$DEVNAME/$CHIPNAME/sim_gpio0/value"
test `cat $SYSFS_PATH` = "0" || fail "incorrect value read from sysfs"
$BASE_DIR/gpio-mockup-cdev -s 1 /dev/`configfs_chip_name chip bank` 0 &
sleep 0.1 # FIXME Any better way?
test `cat $SYSFS_PATH` = "1" || fail "incorrect value read from sysfs"
kill $!
remove_chip chip

echo "4.2. Bias settings work correctly"
create_chip chip
create_bank chip bank
set_num_lines chip bank 8
enable_chip chip
DEVNAME=`configfs_dev_name chip`
CHIPNAME=`configfs_chip_name chip bank`
SYSFS_PATH="/sys/devices/platform/$DEVNAME/$CHIPNAME/sim_gpio0/value"
$BASE_DIR/gpio-mockup-cdev -b pull-up /dev/`configfs_chip_name chip bank` 0
test `cat $SYSFS_PATH` = "1" || fail "bias setting does not work"
remove_chip chip

echo "GPIO $MODULE test PASS"
