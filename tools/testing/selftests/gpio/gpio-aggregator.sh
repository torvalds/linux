#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2025 Bartosz Golaszewski <brgl@bgdev.pl>
# Copyright (C) 2025 Koichiro Den <koichiro.den@canonical.com>

BASE_DIR=$(dirname "$0")
CONFIGFS_SIM_DIR="/sys/kernel/config/gpio-sim"
CONFIGFS_AGG_DIR="/sys/kernel/config/gpio-aggregator"
SYSFS_AGG_DIR="/sys/bus/platform/drivers/gpio-aggregator"
MODULE="gpio-aggregator"

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

# gpio-sim
sim_enable_chip() {
	local CHIP=$1

	echo 1 > "$CONFIGFS_SIM_DIR/$CHIP/live" || fail "Unable to enable the chip"
}

sim_disable_chip() {
	local CHIP=$1

	echo 0 > "$CONFIGFS_SIM_DIR/$CHIP/live" || fail "Unable to disable the chip"
}

sim_configfs_cleanup() {
	local NOCHECK=${1:-0}

	for CHIP_DIR in "$CONFIGFS_SIM_DIR"/*; do
		[ -d "$CHIP_DIR" ] || continue
		echo 0 > "$CHIP_DIR/live"
		find "$CHIP_DIR" -depth -type d -exec rmdir {} \;
	done
	[ "$NOCHECK" -eq 1 ] && return;
	remaining=$(find "$CONFIGFS_SIM_DIR" -mindepth 1 -type d 2> /dev/null)
	if [ -n "$remaining" ]; then
		fail "Directories remain in $CONFIGFS_SIM_DIR: $remaining"
	fi
}

sim_get_chip_label() {
	local CHIP=$1
	local BANK=$2
	local CHIP_NAME=$(cat "$CONFIGFS_SIM_DIR/$CHIP/$BANK/chip_name" 2> /dev/null) || \
		fail "Unable to read the chip name from configfs"

	$BASE_DIR/gpio-chip-info "/dev/$CHIP_NAME" label || \
		fail "Unable to read the chip label from the character device"
}

# gpio-aggregator
agg_create_chip() {
	local CHIP=$1

	mkdir "$CONFIGFS_AGG_DIR/$CHIP"
}

agg_remove_chip() {
	local CHIP=$1

	find "$CONFIGFS_AGG_DIR/$CHIP/" -depth -type d -exec rmdir {} \; || \
		fail "Unable to remove $CONFIGFS_AGG_DIR/$CHIP"
}

agg_create_line() {
	local CHIP=$1
	local LINE=$2

	mkdir "$CONFIGFS_AGG_DIR/$CHIP/$LINE"
}

agg_remove_line() {
	local CHIP=$1
	local LINE=$2

	rmdir "$CONFIGFS_AGG_DIR/$CHIP/$LINE"
}

agg_set_key() {
	local CHIP=$1
	local LINE=$2
	local KEY=$3

	echo "$KEY" > "$CONFIGFS_AGG_DIR/$CHIP/$LINE/key" || fail "Unable to set the lookup key"
}

agg_set_offset() {
	local CHIP=$1
	local LINE=$2
	local OFFSET=$3

	echo "$OFFSET" > "$CONFIGFS_AGG_DIR/$CHIP/$LINE/offset" || \
		fail "Unable to set the lookup offset"
}

agg_set_line_name() {
	local CHIP=$1
	local LINE=$2
	local NAME=$3

	echo "$NAME" > "$CONFIGFS_AGG_DIR/$CHIP/$LINE/name" || fail "Unable to set the line name"
}

agg_enable_chip() {
	local CHIP=$1

	echo 1 > "$CONFIGFS_AGG_DIR/$CHIP/live" || fail "Unable to enable the chip"
}

agg_disable_chip() {
	local CHIP=$1

	echo 0 > "$CONFIGFS_AGG_DIR/$CHIP/live" || fail "Unable to disable the chip"
}

agg_configfs_cleanup() {
	local NOCHECK=${1:-0}

	for CHIP_DIR in "$CONFIGFS_AGG_DIR"/*; do
		[ -d "$CHIP_DIR" ] || continue
		echo 0 > "$CHIP_DIR/live" 2> /dev/null
		find "$CHIP_DIR" -depth -type d -exec rmdir {} \;
	done
	[ "$NOCHECK" -eq 1 ] && return;
	remaining=$(find "$CONFIGFS_AGG_DIR" -mindepth 1 -type d 2> /dev/null)
	if [ -n "$remaining" ]; then
		fail "Directories remain in $CONFIGFS_AGG_DIR: $remaining"
	fi
}

agg_configfs_dev_name() {
	local CHIP=$1

	cat "$CONFIGFS_AGG_DIR/$CHIP/dev_name" 2> /dev/null || \
		fail "Unable to read the device name from configfs"
}

agg_configfs_chip_name() {
	local CHIP=$1
	local DEV_NAME=$(agg_configfs_dev_name "$CHIP")
	local CHIP_LIST=$(find "/sys/devices/platform/$DEV_NAME" \
		-maxdepth 1 -type d -name "gpiochip[0-9]*" 2> /dev/null)
	local CHIP_COUNT=$(echo "$CHIP_LIST" | wc -l)

	if [ -z "$CHIP_LIST" ]; then
		fail "No gpiochip in /sys/devices/platform/$DEV_NAME/"
	elif [ "$CHIP_COUNT" -ne 1 ]; then
		fail "Multiple gpiochips unexpectedly found: $CHIP_LIST"
	fi
	basename "$CHIP_LIST"
}

agg_get_chip_num_lines() {
	local CHIP=$1
	local N_DIR=$(ls -d $CONFIGFS_AGG_DIR/$CHIP/line[0-9]* 2> /dev/null | wc -l)
	local N_LINES

	if [ "$(cat $CONFIGFS_AGG_DIR/$CHIP/live)" = 0 ]; then
		echo "$N_DIR"
	else
		N_LINES=$(
			$BASE_DIR/gpio-chip-info \
				"/dev/$(agg_configfs_chip_name "$CHIP")" num-lines
		) || fail "Unable to read the number of lines from the character device"
		if [ $N_DIR != $N_LINES ]; then
			fail "Discrepancy between two sources for the number of lines"
		fi
		echo "$N_LINES"
	fi
}

agg_get_chip_label() {
	local CHIP=$1

	$BASE_DIR/gpio-chip-info "/dev/$(agg_configfs_chip_name "$CHIP")" label || \
		fail "Unable to read the chip label from the character device"
}

agg_get_line_name() {
	local CHIP=$1
	local OFFSET=$2
	local NAME_CONFIGFS=$(cat "$CONFIGFS_AGG_DIR/$CHIP/line${OFFSET}/name")
	local NAME_CDEV

	if [ "$(cat "$CONFIGFS_AGG_DIR/$CHIP/live")" = 0 ]; then
		echo "$NAME_CONFIGFS"
	else
		NAME_CDEV=$(
			$BASE_DIR/gpio-line-name \
				"/dev/$(agg_configfs_chip_name "$CHIP")" "$OFFSET"
		) || fail "Unable to read the line name from the character device"
		if [ "$NAME_CONFIGFS" != "$NAME_CDEV" ]; then
			fail "Discrepancy between two sources for the name of line"
		fi
		echo "$NAME_CDEV"
	fi
}


# Load the modules. This will pull in configfs if needed too.
modprobe gpio-sim || skip "unable to load the gpio-sim module"
modprobe gpio-aggregator || skip "unable to load the gpio-aggregator module"

# Make sure configfs is mounted at /sys/kernel/config. Wait a bit if needed.
for IDX in $(seq 5); do
	if [ "$IDX" -eq "5" ]; then
		skip "configfs not mounted at /sys/kernel/config"
	fi

	mountpoint -q /sys/kernel/config && break
	sleep 0.1
done

# If the module was already loaded: remove all previous chips
agg_configfs_cleanup
sim_configfs_cleanup

trap "exit 1" SIGTERM SIGINT
trap "agg_configfs_cleanup 1; sim_configfs_cleanup 1" EXIT

# Use gpio-sim chips as the test backend
for CHIP in $(seq -f "chip%g" 0 1); do
	mkdir $CONFIGFS_SIM_DIR/$CHIP
	for BANK in $(seq -f "bank%g" 0 1); do
		mkdir -p "$CONFIGFS_SIM_DIR/$CHIP/$BANK"
		echo "${CHIP}_${BANK}" > "$CONFIGFS_SIM_DIR/$CHIP/$BANK/label" || \
			fail "unable to set the chip label"
		echo 16 > "$CONFIGFS_SIM_DIR/$CHIP/$BANK/num_lines" || \
			fail "unable to set the number of lines"
		for IDX in $(seq 0 15); do
			LINE_NAME="${CHIP}${BANK}_${IDX}"
			LINE_DIR="$CONFIGFS_SIM_DIR/$CHIP/$BANK/line$IDX"
			mkdir -p $LINE_DIR
			echo "$LINE_NAME" > "$LINE_DIR/name" || fail "unable to set the line name"
		done
	done
	sim_enable_chip "$CHIP"
done

echo "1. GPIO aggregator creation/deletion"

echo "1.1. Creation/deletion via configfs"

echo "1.1.1. Minimum creation/deletion"
agg_create_chip   agg0
agg_create_line   agg0 line0
agg_set_key       agg0 line0 "$(sim_get_chip_label chip0 bank0)"
agg_set_offset    agg0 line0 5
agg_set_line_name agg0 line0 test0
agg_enable_chip   agg0
test "$(cat "$CONFIGFS_AGG_DIR/agg0/live")" = 1 || fail "chip unexpectedly dead"
test "$(agg_get_chip_label agg0)" = "$(agg_configfs_dev_name agg0)" || \
	fail "label is inconsistent"
test "$(agg_get_chip_num_lines agg0)" = "1" || fail "number of lines is not 1"
test "$(agg_get_line_name agg0 0)" = "test0" || fail "line name is unset"
agg_disable_chip  agg0
agg_remove_line   agg0 line0
agg_remove_chip   agg0

echo "1.1.2. Complex creation/deletion"
agg_create_chip   agg0
agg_create_line   agg0 line0
agg_create_line   agg0 line1
agg_create_line   agg0 line2
agg_create_line   agg0 line3
agg_set_key       agg0 line0 "$(sim_get_chip_label chip0 bank0)"
agg_set_key       agg0 line1 "$(sim_get_chip_label chip0 bank1)"
agg_set_key       agg0 line2 "$(sim_get_chip_label chip1 bank0)"
agg_set_key       agg0 line3 "$(sim_get_chip_label chip1 bank1)"
agg_set_offset    agg0 line0 1
agg_set_offset    agg0 line1 3
agg_set_offset    agg0 line2 5
agg_set_offset    agg0 line3 7
agg_set_line_name agg0 line0 test0
agg_set_line_name agg0 line1 test1
agg_set_line_name agg0 line2 test2
agg_set_line_name agg0 line3 test3
agg_enable_chip   agg0
test "$(cat "$CONFIGFS_AGG_DIR/agg0/live")" = 1 || fail "chip unexpectedly dead"
test "$(agg_get_chip_label agg0)" = "$(agg_configfs_dev_name agg0)" || \
	fail "label is inconsistent"
test "$(agg_get_chip_num_lines agg0)" = "4" || fail "number of lines is not 1"
test "$(agg_get_line_name agg0 0)" = "test0" || fail "line name is unset"
test "$(agg_get_line_name agg0 1)" = "test1" || fail "line name is unset"
test "$(agg_get_line_name agg0 2)" = "test2" || fail "line name is unset"
test "$(agg_get_line_name agg0 3)" = "test3" || fail "line name is unset"
agg_disable_chip  agg0
agg_remove_line   agg0 line0
agg_remove_line   agg0 line1
agg_remove_line   agg0 line2
agg_remove_line   agg0 line3
agg_remove_chip   agg0

echo "1.1.3. Can't instantiate a chip without any line"
agg_create_chip   agg0
echo 1 > "$CONFIGFS_AGG_DIR/agg0/live" 2> /dev/null && fail "chip unexpectedly enabled"
test "$(cat "$CONFIGFS_AGG_DIR/agg0/live")" = 0 || fail "chip unexpectedly alive"
agg_remove_chip   agg0

echo "1.1.4. Can't instantiate a chip with invalid configuration"
agg_create_chip   agg0
agg_create_line   agg0 line0
agg_set_key       agg0 line0 "chipX_bankX"
agg_set_offset    agg0 line0 99
agg_set_line_name agg0 line0 test0
echo 1 > "$CONFIGFS_AGG_DIR/agg0/live" 2> /dev/null && fail "chip unexpectedly enabled"
test "$(cat "$CONFIGFS_AGG_DIR/agg0/live")" = 0 || fail "chip unexpectedly alive"
agg_remove_line   agg0 line0
agg_remove_chip   agg0

echo "1.1.5. Can't instantiate a chip asynchronously via deferred probe"
agg_create_chip   agg0
agg_create_line   agg0 line0
agg_set_key       agg0 line0 "chip0_bank0"
agg_set_offset    agg0 line0 5
agg_set_line_name agg0 line0 test0
sim_disable_chip  chip0
echo 1 > "$CONFIGFS_AGG_DIR/agg0/live" 2> /dev/null && fail "chip unexpectedly enabled"
test "$(cat "$CONFIGFS_AGG_DIR/agg0/live")" = 0 || fail "chip unexpectedly alive"
sim_enable_chip   chip0
sleep 1
test "$(cat "$CONFIGFS_AGG_DIR/agg0/live")" = 0 || \
	fail "chip unexpectedly transitioned to 'live' state"
agg_remove_line   agg0 line0
agg_remove_chip   agg0

echo "1.1.6. Can't instantiate a chip with _sysfs prefix"
mkdir "$CONFIGFS_AGG_DIR/_sysfs" 2> /dev/null && fail "chip _sysfs unexpectedly created"
mkdir "$CONFIGFS_AGG_DIR/_sysfs.foo" 2> /dev/null && fail "chip _sysfs.foo unexpectedly created"

echo "1.2. Creation/deletion via sysfs"

echo "1.2.1. Minimum creation/deletion"
echo "chip0_bank0 0" > "$SYSFS_AGG_DIR/new_device"
CHIPNAME=$(agg_configfs_chip_name _sysfs.0)
test "$(cat "$CONFIGFS_AGG_DIR/_sysfs.0/live")" = 1 || fail "chip unexpectedly dead"
test "$(agg_get_chip_label _sysfs.0)" = "$(agg_configfs_dev_name _sysfs.0)" || \
	fail "label is inconsistent"
test "$(agg_get_chip_num_lines _sysfs.0)" = "1" || fail "number of lines is not 1"
test "$(agg_get_line_name _sysfs.0 0)" = "" || fail "line name is unset"
echo "$(agg_configfs_dev_name _sysfs.0)" > "$SYSFS_AGG_DIR/delete_device"
test -d $CONFIGFS_AGG_DIR/_sysfs.0 && fail "_sysfs.0 unexpectedly remains"
test -d /dev/${CHIPNAME} && fail "/dev/${CHIPNAME} unexpectedly remains"

echo "1.2.2. Complex creation/deletion"
echo "chip0bank0_0 chip1_bank1 10-11" > "$SYSFS_AGG_DIR/new_device"
CHIPNAME=$(agg_configfs_chip_name _sysfs.0)
test "$(cat "$CONFIGFS_AGG_DIR/_sysfs.0/live")" = 1 || fail "chip unexpectedly dead"
test "$(agg_get_chip_label _sysfs.0)" = "$(agg_configfs_dev_name _sysfs.0)" || \
	fail "label is inconsistent"
test "$(agg_get_chip_num_lines _sysfs.0)" = "3" || fail "number of lines is not 3"
test "$(agg_get_line_name _sysfs.0 0)" = "" || fail "line name is unset"
test "$(agg_get_line_name _sysfs.0 1)" = "" || fail "line name is unset"
test "$(agg_get_line_name _sysfs.0 2)" = "" || fail "line name is unset"
echo "$(agg_configfs_dev_name _sysfs.0)" > "$SYSFS_AGG_DIR/delete_device"
test -d $CONFIGFS_AGG_DIR/_sysfs.0 && fail "_sysfs.0 unexpectedly remains"
test -d /dev/${CHIPNAME} && fail "/dev/${CHIPNAME} unexpectedly remains"

echo "1.2.3. Asynchronous creation with deferred probe"
sim_disable_chip  chip0
echo 'chip0_bank0 0' > $SYSFS_AGG_DIR/new_device
sleep 1
test "$(cat "$CONFIGFS_AGG_DIR/_sysfs.0/live")" = 0 || fail "chip unexpectedly alive"
sim_enable_chip  chip0
sleep 1
CHIPNAME=$(agg_configfs_chip_name _sysfs.0)
test "$(cat "$CONFIGFS_AGG_DIR/_sysfs.0/live")" = 1 || fail "chip unexpectedly remains dead"
test "$(agg_get_chip_label _sysfs.0)" = "$(agg_configfs_dev_name _sysfs.0)" || \
	fail "label is inconsistent"
test "$(agg_get_chip_num_lines _sysfs.0)" = "1" || fail "number of lines is not 1"
test "$(agg_get_line_name _sysfs.0 0)" = "" || fail "line name unexpectedly set"
echo "$(agg_configfs_dev_name _sysfs.0)" > "$SYSFS_AGG_DIR/delete_device"
test -d $CONFIGFS_AGG_DIR/_sysfs.0 && fail "_sysfs.0 unexpectedly remains"
test -d /dev/${CHIPNAME} && fail "/dev/${CHIPNAME} unexpectedly remains"

echo "1.2.4. Can't instantiate a chip with invalid configuration"
echo "xyz 0" > "$SYSFS_AGG_DIR/new_device"
test "$(cat $CONFIGFS_AGG_DIR/_sysfs.0/live)" = 0 || fail "chip unexpectedly alive"
echo "$(agg_configfs_dev_name _sysfs.0)" > "$SYSFS_AGG_DIR/delete_device"

echo "2. GPIO aggregator configuration"

echo "2.1. Configuring aggregators instantiated via configfs"
setup_2_1() {
	agg_create_chip   agg0
	agg_create_line   agg0 line0
	agg_create_line   agg0 line1
	agg_set_key       agg0 line0 "$(sim_get_chip_label chip0 bank0)"
	agg_set_key       agg0 line1 "$(sim_get_chip_label chip1 bank0)"
	agg_set_offset    agg0 line0 1
	agg_set_offset    agg0 line1 3
	agg_set_line_name agg0 line0 test0
	agg_set_line_name agg0 line1 test1
	agg_enable_chip   agg0
}
teardown_2_1() {
	agg_configfs_cleanup
}

echo "2.1.1. While offline"

echo "2.1.1.1. Line can be added/removed"
setup_2_1
agg_disable_chip  agg0
agg_create_line   agg0 line2
agg_set_key       agg0 line2 "$(sim_get_chip_label chip0 bank1)"
agg_set_offset    agg0 line2 5
agg_enable_chip   agg0
test "$(agg_get_chip_num_lines agg0)" = "3" || fail "number of lines is not 1"
teardown_2_1

echo "2.1.1.2. Line key can be modified"
setup_2_1
agg_disable_chip  agg0
agg_set_key       agg0 line0 "$(sim_get_chip_label chip0 bank1)"
agg_set_key       agg0 line1 "$(sim_get_chip_label chip1 bank1)"
agg_enable_chip   agg0
teardown_2_1

echo "2.1.1.3. Line name can be modified"
setup_2_1
agg_disable_chip  agg0
agg_set_line_name agg0 line0 new0
agg_set_line_name agg0 line1 new1
agg_enable_chip   agg0
test "$(agg_get_line_name agg0 0)" = "new0" || fail "line name is unset"
test "$(agg_get_line_name agg0 1)" = "new1" || fail "line name is unset"
teardown_2_1

echo "2.1.1.4. Line offset can be modified"
setup_2_1
agg_disable_chip  agg0
agg_set_offset    agg0 line0 5
agg_set_offset    agg0 line1 7
agg_enable_chip   agg0
teardown_2_1

echo "2.1.1.5. Can re-enable a chip after valid reconfiguration"
setup_2_1
agg_disable_chip  agg0
agg_set_key       agg0 line0 "$(sim_get_chip_label chip1 bank1)"
agg_set_offset    agg0 line0 15
agg_set_key       agg0 line1 "$(sim_get_chip_label chip0 bank1)"
agg_set_offset    agg0 line0 14
agg_create_line   agg0 line2
agg_set_key       agg0 line2 "$(sim_get_chip_label chip0 bank1)"
agg_set_offset    agg0 line2 13
agg_enable_chip   agg0
test "$(agg_get_chip_num_lines agg0)" = "3" || fail "number of lines is not 1"
teardown_2_1

echo "2.1.1.7. Can't re-enable a chip with invalid reconfiguration"
setup_2_1
agg_disable_chip  agg0
agg_set_key       agg0 line0 invalidkey
echo 1 > "$CONFIGFS_AGG_DIR/agg0/live" 2> /dev/null && fail "chip unexpectedly enabled"
teardown_2_1
setup_2_1
agg_disable_chip  agg0
agg_set_offset    agg0 line0 99
echo 1 > "$CONFIGFS_AGG_DIR/agg0/live" 2> /dev/null && fail "chip unexpectedly enabled"
teardown_2_1

echo "2.1.2. While online"

echo "2.1.2.1. Can't add/remove line"
setup_2_1
mkdir "$CONFIGFS_AGG_DIR/agg0/line2" 2> /dev/null && fail "line unexpectedly added"
rmdir "$CONFIGFS_AGG_DIR/agg0/line1" 2> /dev/null && fail "line unexpectedly removed"
teardown_2_1

echo "2.1.2.2. Can't modify line key"
setup_2_1
echo "chip1_bank1" > "$CONFIGFS_AGG_DIR/agg0/line0/key" 2> /dev/null && \
	fail "lookup key unexpectedly updated"
teardown_2_1

echo "2.1.2.3. Can't modify line name"
setup_2_1
echo "new0" > "$CONFIGFS_AGG_DIR/agg0/line0/name" 2> /dev/null && \
	fail "name unexpectedly updated"
teardown_2_1

echo "2.1.2.4. Can't modify line offset"
setup_2_1
echo "5" > "$CONFIGFS_AGG_DIR/agg0/line0/offset" 2> /dev/null && \
	fail "offset unexpectedly updated"
teardown_2_1

echo "2.2. Configuring aggregators instantiated via sysfs"
setup_2_2() {
	echo "chip0_bank0 1 chip1_bank0 3" > "$SYSFS_AGG_DIR/new_device"
}
teardown_2_2() {
	echo "$(agg_configfs_dev_name _sysfs.0)" > "$SYSFS_AGG_DIR/delete_device"
}

echo "2.2.1. While online"

echo "2.2.1.1. Can toggle live"
setup_2_2
agg_disable_chip  _sysfs.0
agg_enable_chip   _sysfs.0
teardown_2_2

echo "2.2.1.2. Can't add/remove line"
setup_2_2
mkdir "$CONFIGFS_AGG_DIR/_sysfs.0/line2" 2> /dev/null && fail "line unexpectedly added"
rmdir "$CONFIGFS_AGG_DIR/_sysfs.0/line1" 2> /dev/null && fail "line unexpectedly removed"
teardown_2_2

echo "2.2.1.3. Can't modify line key"
setup_2_2
echo "chip1_bank1" > "$CONFIGFS_AGG_DIR/_sysfs.0/line0/key" 2> /dev/null && \
	fail "lookup key unexpectedly updated"
teardown_2_2

echo "2.2.1.4. Can't modify line name"
setup_2_2
echo "new0" > "$CONFIGFS_AGG_DIR/_sysfs.0/line0/name" 2> /dev/null && \
	fail "name unexpectedly updated"
teardown_2_2

echo "2.2.1.5. Can't modify line offset"
setup_2_2
echo "5" > "$CONFIGFS_AGG_DIR/_sysfs.0/line0/offset" 2> /dev/null && \
	fail "offset unexpectedly updated"
teardown_2_2

echo "2.2.2. While waiting for deferred probe"

echo "2.2.2.1. Can't add/remove line despite live = 0"
sim_disable_chip chip0
setup_2_2
mkdir "$CONFIGFS_AGG_DIR/_sysfs.0/line2" 2> /dev/null && fail "line unexpectedly added"
rmdir "$CONFIGFS_AGG_DIR/_sysfs.0/line1" 2> /dev/null && fail "line unexpectedly removed"
teardown_2_2
sim_enable_chip chip0

echo "2.2.2.2. Can't modify line key"
sim_disable_chip chip0
setup_2_2
echo "chip1_bank1" > "$CONFIGFS_AGG_DIR/_sysfs.0/line0/key" 2> /dev/null && \
	fail "lookup key unexpectedly updated"
teardown_2_2
sim_enable_chip chip0

echo "2.2.2.3. Can't modify line name"
sim_disable_chip chip0
setup_2_2
echo "new0" > "$CONFIGFS_AGG_DIR/_sysfs.0/line0/name" 2> /dev/null && \
	fail "name unexpectedly updated"
teardown_2_2
sim_enable_chip chip0

echo "2.2.2.4. Can't modify line offset"
sim_disable_chip chip0
setup_2_2
echo 5 > "$CONFIGFS_AGG_DIR/_sysfs.0/line0/offset" 2> /dev/null && \
	fail "offset unexpectedly updated"
teardown_2_2
sim_enable_chip chip0

echo "2.2.2.5. Can't toggle live"
sim_disable_chip chip0
setup_2_2
test "$(cat "$CONFIGFS_AGG_DIR/_sysfs.0/live")" = 0 || fail "chip unexpectedly alive"
echo 1 > "$CONFIGFS_AGG_DIR/_sysfs.0/live" 2> /dev/null && fail "chip unexpectedly enabled"
teardown_2_2
sim_enable_chip chip0

echo "2.2.3. While offline"

echo "2.2.3.1. Can't add/remove line despite live = 0"
setup_2_2
agg_disable_chip _sysfs.0
mkdir "$CONFIGFS_AGG_DIR/_sysfs.0/line2" 2> /dev/null && fail "line unexpectedly added"
rmdir "$CONFIGFS_AGG_DIR/_sysfs.0/line1" 2> /dev/null && fail "line unexpectedly removed"
teardown_2_2

echo "2.2.3.2. Line key can be modified"
setup_2_2
agg_disable_chip  _sysfs.0
agg_set_key       _sysfs.0 line0 "$(sim_get_chip_label chip0 bank1)"
agg_set_key       _sysfs.0 line1 "$(sim_get_chip_label chip1 bank1)"
agg_enable_chip   _sysfs.0
teardown_2_2

echo "2.2.3.3. Line name can be modified"
setup_2_2
agg_disable_chip  _sysfs.0
agg_set_line_name _sysfs.0 line0 new0
agg_set_line_name _sysfs.0 line1 new1
agg_enable_chip   _sysfs.0
test "$(agg_get_line_name _sysfs.0 0)" = "new0" || fail "line name is unset"
test "$(agg_get_line_name _sysfs.0 1)" = "new1" || fail "line name is unset"
teardown_2_2

echo "2.2.3.4. Line offset can be modified"
setup_2_2
agg_disable_chip  _sysfs.0
agg_set_offset    _sysfs.0 line0 5
agg_set_offset    _sysfs.0 line1 7
agg_enable_chip   _sysfs.0
teardown_2_2

echo "2.2.3.5. Can re-enable a chip with valid reconfiguration"
setup_2_2
agg_disable_chip  _sysfs.0
agg_set_key       _sysfs.0 line0 "$(sim_get_chip_label chip1 bank1)"
agg_set_offset    _sysfs.0 line0 15
agg_set_key       _sysfs.0 line1 "$(sim_get_chip_label chip0 bank1)"
agg_set_offset    _sysfs.0 line0 14
agg_enable_chip   _sysfs.0
teardown_2_2

echo "2.2.3.6. Can't re-enable a chip with invalid reconfiguration"
setup_2_2
agg_disable_chip  _sysfs.0
agg_set_key       _sysfs.0 line0 invalidkey
echo 1 > "$CONFIGFS_AGG_DIR/_sysfs.0/live" 2> /dev/null && fail "chip unexpectedly enabled"
teardown_2_2
setup_2_2
agg_disable_chip  _sysfs.0
agg_set_offset    _sysfs.0 line0 99
echo 1 > "$CONFIGFS_AGG_DIR/_sysfs.0/live" 2> /dev/null && fail "chip unexpectedly enabled"
teardown_2_2

echo "3. Module unload"

echo "3.1. Can't unload module if there is at least one device created via configfs"
agg_create_chip agg0
modprobe -r gpio-aggregator 2> /dev/null
test -d /sys/module/gpio_aggregator || fail "module unexpectedly unloaded"
agg_remove_chip agg0

echo "3.2. Can unload module if there is no device created via configfs"
echo "chip0_bank0 1 chip1_bank0 3" > "$SYSFS_AGG_DIR/new_device"
modprobe -r gpio-aggregator 2> /dev/null
test -d /sys/module/gpio_aggregator && fail "module unexpectedly remains to be loaded"
modprobe gpio-aggregator 2> /dev/null

echo "4. GPIO forwarder functional"
SETTINGS="chip0:bank0:2 chip0:bank1:4 chip1:bank0:6 chip1:bank1:8"
setup_4() {
	local OFFSET=0
	agg_create_chip agg0
	for SETTING in $SETTINGS; do
		CHIP=$(echo "$SETTING" | cut -d: -f1)
		BANK=$(echo "$SETTING" | cut -d: -f2)
		LINE=$(echo "$SETTING" | cut -d: -f3)
		agg_create_line agg0 "line${OFFSET}"
		agg_set_key     agg0 "line${OFFSET}" "$(sim_get_chip_label "$CHIP" "$BANK")"
		agg_set_offset  agg0 "line${OFFSET}" "$LINE"
		OFFSET=$(expr $OFFSET + 1)
	done
	agg_enable_chip agg0
}
teardown_4() {
	agg_configfs_cleanup
}

echo "4.1. Forwarding set values"
setup_4
OFFSET=0
for SETTING in $SETTINGS; do
	CHIP=$(echo "$SETTING" | cut -d: -f1)
	BANK=$(echo "$SETTING" | cut -d: -f2)
	LINE=$(echo "$SETTING" | cut -d: -f3)
	DEVNAME=$(cat "$CONFIGFS_SIM_DIR/$CHIP/dev_name")
	CHIPNAME=$(cat "$CONFIGFS_SIM_DIR/$CHIP/$BANK/chip_name")
	VAL_PATH="/sys/devices/platform/$DEVNAME/$CHIPNAME/sim_gpio${LINE}/value"
	test $(cat $VAL_PATH) = "0" || fail "incorrect value read from sysfs"
	$BASE_DIR/gpio-mockup-cdev -s 1 "/dev/$(agg_configfs_chip_name agg0)" "$OFFSET" &
	mock_pid=$!
	sleep 0.1 # FIXME Any better way?
	test "$(cat $VAL_PATH)" = "1" || fail "incorrect value read from sysfs"
	kill "$mock_pid"
	OFFSET=$(expr $OFFSET + 1)
done
teardown_4

echo "4.2. Forwarding set config"
setup_4
OFFSET=0
for SETTING in $SETTINGS; do
	CHIP=$(echo "$SETTING" | cut -d: -f1)
	BANK=$(echo "$SETTING" | cut -d: -f2)
	LINE=$(echo "$SETTING" | cut -d: -f3)
	DEVNAME=$(cat "$CONFIGFS_SIM_DIR/$CHIP/dev_name")
	CHIPNAME=$(cat "$CONFIGFS_SIM_DIR/$CHIP/$BANK/chip_name")
	VAL_PATH="/sys/devices/platform/$DEVNAME/$CHIPNAME/sim_gpio${LINE}/value"
	$BASE_DIR/gpio-mockup-cdev -b pull-up "/dev/$(agg_configfs_chip_name agg0)" "$OFFSET"
	test $(cat "$VAL_PATH") = "1" || fail "incorrect value read from sysfs"
	OFFSET=$(expr $OFFSET + 1)
done
teardown_4

echo "5. Race condition verification"

echo "5.1. Stress test of new_device/delete_device and module load/unload"
for _ in $(seq 1000); do
	{
		echo "dummy 0" > "$SYSFS_AGG_DIR/new_device"
		cat "$CONFIGFS_AGG_DIR/_sysfs.0/dev_name" > "$SYSFS_AGG_DIR/delete_device"
	} 2> /dev/null
done &
writer_pid=$!
while kill -0 "$writer_pid" 2> /dev/null; do
	{
		modprobe gpio-aggregator
		modprobe -r gpio-aggregator
	} 2> /dev/null
done

echo "GPIO $MODULE test PASS"
