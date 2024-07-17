#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2022, 2024 Collabora Ltd
#
# This test validates the power supply uAPI: namely, the files in sysfs and
# lines in uevent that expose the power supply properties.
#
# By default all power supplies available are tested. Optionally the name of a
# power supply can be passed as a parameter to test only that one instead.
DIR="$(dirname "$(readlink -f "$0")")"

. "${DIR}"/../kselftest/ktap_helpers.sh

. "${DIR}"/helpers.sh

count_tests() {
	SUPPLIES=$1

	# This needs to be updated every time a new test is added.
	NUM_TESTS=33

	total_tests=0

	for i in $SUPPLIES; do
		total_tests=$((total_tests + NUM_TESTS))
	done

	echo "$total_tests"
}

ktap_print_header

SYSFS_SUPPLIES=/sys/class/power_supply/

if [ $# -eq 0 ]; then
	supplies=$(ls "$SYSFS_SUPPLIES")
else
	supplies=$1
fi

ktap_set_plan "$(count_tests "$supplies")"

for DEVNAME in $supplies; do
	ktap_print_msg Testing device "$DEVNAME"

	if [ ! -d "$SYSFS_SUPPLIES"/"$DEVNAME" ]; then
		ktap_test_fail "$DEVNAME".exists
		ktap_exit_fail_msg Device does not exist
	fi

	ktap_test_pass "$DEVNAME".exists

	test_uevent_prop NAME "$DEVNAME"

	test_sysfs_prop type
	SUPPLY_TYPE=$(cat "$SYSFS_SUPPLIES"/"$DEVNAME"/type)
	# This fails on kernels < 5.8 (needs 2ad3d74e3c69f)
	test_uevent_prop TYPE "$SUPPLY_TYPE"

	test_sysfs_prop_optional usb_type

	test_sysfs_prop_optional_range online 0 2
	test_sysfs_prop_optional_range present 0 1

	test_sysfs_prop_optional_list status "Unknown","Charging","Discharging","Not charging","Full"

	# Capacity is reported as percentage, thus any value less than 0 and
	# greater than 100 are not allowed.
	test_sysfs_prop_optional_range capacity 0 100 "%"

	test_sysfs_prop_optional_list capacity_level "Unknown","Critical","Low","Normal","High","Full"

	test_sysfs_prop_optional model_name
	test_sysfs_prop_optional manufacturer
	test_sysfs_prop_optional serial_number
	test_sysfs_prop_optional_list technology "Unknown","NiMH","Li-ion","Li-poly","LiFe","NiCd","LiMn"

	test_sysfs_prop_optional cycle_count

	test_sysfs_prop_optional_list scope "Unknown","System","Device"

	test_sysfs_prop_optional input_current_limit "uA"
	test_sysfs_prop_optional input_voltage_limit "uV"

	# Technically the power-supply class does not limit reported values.
	# E.g. one could expose an RTC backup-battery, which goes below 1.5V or
	# an electric vehicle battery with over 300V. But most devices do not
	# have a step-up capable regulator behind the battery and operate with
	# voltages considered safe to touch, so we limit the allowed range to
	# 1.8V-60V to catch drivers reporting incorrectly scaled values. E.g. a
	# common mistake is reporting data in mV instead of µV.
	test_sysfs_prop_optional_range voltage_now 1800000 60000000 "uV"
	test_sysfs_prop_optional_range voltage_min 1800000 60000000 "uV"
	test_sysfs_prop_optional_range voltage_max 1800000 60000000 "uV"
	test_sysfs_prop_optional_range voltage_min_design 1800000 60000000 "uV"
	test_sysfs_prop_optional_range voltage_max_design 1800000 60000000 "uV"

	# current based systems
	test_sysfs_prop_optional current_now "uA"
	test_sysfs_prop_optional current_max "uA"
	test_sysfs_prop_optional charge_now "uAh"
	test_sysfs_prop_optional charge_full "uAh"
	test_sysfs_prop_optional charge_full_design "uAh"

	# power based systems
	test_sysfs_prop_optional power_now "uW"
	test_sysfs_prop_optional energy_now "uWh"
	test_sysfs_prop_optional energy_full "uWh"
	test_sysfs_prop_optional energy_full_design "uWh"
	test_sysfs_prop_optional energy_full_design "uWh"
done

ktap_finished
