#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test the protodown mechanism. Verify basic protodown toggling, protodown
# reasons, operational state when the lower device carrier changes, and correct
# operational state when the lower device has no carrier.

# shellcheck disable=SC1091,SC2034,SC2154,SC2317
source lib.sh

require_command jq

ALL_TESTS="
	protodown_basic_macvlan
	protodown_basic_vxlan
	protodown_reasons
	protodown_lower_toggle
	protodown_lower_down
"

operstate_get()
{
	local ns=$1; shift
	local dev=$1; shift

	ip -n "$ns" -j link show dev "$dev" | jq -r '.[].operstate'
}

operstate_check()
{
	local ns=$1; shift
	local dev=$1; shift
	local expected=$1; shift

	local current
	current=$(operstate_get "$ns" "$dev")

	[ "$current" = "$expected" ]
}

setup_prepare()
{
	setup_ns NS
	defer cleanup_all_ns

	ip -n "$NS" link add name dummy0 up type dummy

	ip -n "$NS" link add name macvlan0 link dummy0 up type macvlan mode bridge

	ip -n "$NS" link add name vxlan0 up type vxlan id 10010 dstport 4789
}

protodown_basic()
{
	local dev=$1; shift

	ip -n "$NS" link set dev "$dev" protodown on
	check_err $? "Failed to set protodown on"

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" "$dev" DOWN
	check_err $? "Operational state is not DOWN after setting protodown"

	ip -n "$NS" link set dev "$dev" protodown off
	check_err $? "Failed to set protodown off"

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" "$dev" UP
	check_err $? "Operational state is not UP after clearing protodown"
}

protodown_basic_macvlan()
{
	RET=0

	protodown_basic macvlan0

	log_test "Basic protodown on/off with macvlan"
}

protodown_basic_vxlan()
{
	RET=0

	protodown_basic vxlan0

	log_test "Basic protodown on/off with vxlan"
}

protodown_reasons()
{
	RET=0

	ip -n "$NS" link set dev macvlan0 protodown on

	ip -n "$NS" link set dev macvlan0 protodown_reason 0 on
	check_err $? "Failed to set protodown reason bit 0"

	# Cannot clear protodown while reasons are active.
	ip -n "$NS" link set dev macvlan0 protodown off 2>/dev/null
	check_fail $? "Clearing protodown succeeded with active reasons"

	ip -n "$NS" link set dev macvlan0 protodown_reason 0 off
	check_err $? "Failed to clear protodown reason bit 0"

	# Can clear protodown when no reasons are active.
	ip -n "$NS" link set dev macvlan0 protodown off
	check_err $? "Failed to clear protodown with no active reasons"

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" macvlan0 UP
	check_err $? "Operational state is not UP after clearing protodown"

	log_test "Protodown reasons"
}

protodown_lower_toggle()
{
	RET=0

	ip -n "$NS" link set dev macvlan0 protodown on

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" macvlan0 DOWN
	check_err $? "Operational state is not DOWN after setting protodown"

	# Toggle carrier on the lower device. The macvlan should stay DOWN
	# because protodown is on.
	ip -n "$NS" link set dev dummy0 carrier off
	ip -n "$NS" link set dev dummy0 carrier on

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" dummy0 UP
	check_err $? "Lower device is not UP after carrier on"

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" macvlan0 DOWN
	check_err $? "Macvlan operational state is not DOWN despite protodown"

	# Clear protodown and verify the macvlan comes back up.
	ip -n "$NS" link set dev macvlan0 protodown off

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" macvlan0 UP
	check_err $? "Operational state is not UP after clearing protodown"

	log_test "Protodown with lower device toggled"
}

protodown_lower_down()
{
	RET=0

	# Bring the lower device carrier down first.
	ip -n "$NS" link set dev dummy0 carrier off

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" macvlan0 LOWERLAYERDOWN
	check_err $? "Macvlan is not LOWERLAYERDOWN with lower carrier off"

	# Toggle protodown on and off while lower has no carrier. The macvlan
	# should not transition to UP.
	ip -n "$NS" link set dev macvlan0 protodown on

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" macvlan0 LOWERLAYERDOWN
	check_err $? "Macvlan is not LOWERLAYERDOWN after setting protodown"

	ip -n "$NS" link set dev macvlan0 protodown off

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" macvlan0 LOWERLAYERDOWN
	check_err $? "Macvlan is not LOWERLAYERDOWN after clearing protodown"

	# Bring the lower device carrier up. The macvlan should transition to
	# UP.
	ip -n "$NS" link set dev dummy0 carrier on

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" dummy0 UP
	check_err $? "Lower device is not UP after carrier on"

	busywait "$BUSYWAIT_TIMEOUT" operstate_check "$NS" macvlan0 UP
	check_err $? "Macvlan is not UP after lower device is UP"

	log_test "Protodown with lower device down"
}

trap defer_scopes_cleanup EXIT
setup_prepare
tests_run

exit "$EXIT_STATUS"
