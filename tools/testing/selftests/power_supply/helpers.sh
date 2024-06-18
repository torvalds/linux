#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2022, 2024 Collabora Ltd
SYSFS_SUPPLIES=/sys/class/power_supply

calc() {
	awk "BEGIN { print $* }";
}

test_sysfs_prop() {
	PROP="$1"
	VALUE="$2" # optional

	PROP_PATH="$SYSFS_SUPPLIES"/"$DEVNAME"/"$PROP"
	TEST_NAME="$DEVNAME".sysfs."$PROP"

	if [ -z "$VALUE" ]; then
		ktap_test_result "$TEST_NAME" [ -f "$PROP_PATH" ]
	else
		ktap_test_result "$TEST_NAME" grep -q "$VALUE" "$PROP_PATH"
	fi
}

to_human_readable_unit() {
	VALUE="$1"
	UNIT="$2"

	case "$VALUE" in
		*[!0-9]* ) return ;; # Not a number
	esac

	if [ "$UNIT" = "uA" ]; then
		new_unit="mA"
		div=1000
	elif [ "$UNIT" = "uV" ]; then
		new_unit="V"
		div=1000000
	elif [ "$UNIT" = "uAh" ]; then
		new_unit="Ah"
		div=1000000
	elif [ "$UNIT" = "uW" ]; then
		new_unit="mW"
		div=1000
	elif [ "$UNIT" = "uWh" ]; then
		new_unit="Wh"
		div=1000000
	else
		return
	fi

	value_converted=$(calc "$VALUE"/"$div")
	echo "$value_converted" "$new_unit"
}

_check_sysfs_prop_available() {
	PROP=$1

	PROP_PATH="$SYSFS_SUPPLIES"/"$DEVNAME"/"$PROP"
	TEST_NAME="$DEVNAME".sysfs."$PROP"

	if [ ! -e "$PROP_PATH" ] ; then
		ktap_test_skip "$TEST_NAME"
		return 1
	fi

	if ! cat "$PROP_PATH" >/dev/null; then
		ktap_print_msg "Failed to read"
		ktap_test_fail "$TEST_NAME"
		return 1
	fi

	return 0
}

test_sysfs_prop_optional() {
	PROP=$1
	UNIT=$2 # optional

	TEST_NAME="$DEVNAME".sysfs."$PROP"

	_check_sysfs_prop_available "$PROP" || return
	DATA=$(cat "$SYSFS_SUPPLIES"/"$DEVNAME"/"$PROP")

	ktap_print_msg "Reported: '$DATA' $UNIT ($(to_human_readable_unit "$DATA" "$UNIT"))"
	ktap_test_pass "$TEST_NAME"
}

test_sysfs_prop_optional_range() {
	PROP=$1
	MIN=$2
	MAX=$3
	UNIT=$4 # optional

	TEST_NAME="$DEVNAME".sysfs."$PROP"

	_check_sysfs_prop_available "$PROP" || return
	DATA=$(cat "$SYSFS_SUPPLIES"/"$DEVNAME"/"$PROP")

	if [ "$DATA" -lt "$MIN" ] || [ "$DATA" -gt "$MAX" ]; then
		ktap_print_msg "'$DATA' is out of range (min=$MIN, max=$MAX)"
		ktap_test_fail "$TEST_NAME"
	else
		ktap_print_msg "Reported: '$DATA' $UNIT ($(to_human_readable_unit "$DATA" "$UNIT"))"
		ktap_test_pass "$TEST_NAME"
	fi
}

test_sysfs_prop_optional_list() {
	PROP=$1
	LIST=$2

	TEST_NAME="$DEVNAME".sysfs."$PROP"

	_check_sysfs_prop_available "$PROP" || return
	DATA=$(cat "$SYSFS_SUPPLIES"/"$DEVNAME"/"$PROP")

	valid=0

	OLDIFS=$IFS
	IFS=","
	for item in $LIST; do
		if [ "$DATA" = "$item" ]; then
			valid=1
			break
		fi
	done
	if [ "$valid" -eq 1 ]; then
		ktap_print_msg "Reported: '$DATA'"
		ktap_test_pass "$TEST_NAME"
	else
		ktap_print_msg "'$DATA' is not a valid value for this property"
		ktap_test_fail "$TEST_NAME"
	fi
	IFS=$OLDIFS
}

dump_file() {
	FILE="$1"
	while read -r line; do
		ktap_print_msg "$line"
	done < "$FILE"
}

__test_uevent_prop() {
	PROP="$1"
	OPTIONAL="$2"
	VALUE="$3" # optional

	UEVENT_PATH="$SYSFS_SUPPLIES"/"$DEVNAME"/uevent
	TEST_NAME="$DEVNAME".uevent."$PROP"

	if ! grep -q "POWER_SUPPLY_$PROP=" "$UEVENT_PATH"; then
		if [ "$OPTIONAL" -eq 1 ]; then
			ktap_test_skip "$TEST_NAME"
		else
			ktap_print_msg "Missing property"
			ktap_test_fail "$TEST_NAME"
		fi
		return
	fi

	if ! grep -q "POWER_SUPPLY_$PROP=$VALUE" "$UEVENT_PATH"; then
		ktap_print_msg "Invalid value for uevent property, dumping..."
		dump_file "$UEVENT_PATH"
		ktap_test_fail "$TEST_NAME"
	else
		ktap_test_pass "$TEST_NAME"
	fi
}

test_uevent_prop() {
	__test_uevent_prop "$1" 0 "$2"
}

test_uevent_prop_optional() {
	__test_uevent_prop "$1" 1 "$2"
}
