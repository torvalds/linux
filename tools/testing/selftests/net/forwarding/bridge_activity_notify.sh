#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-----------------------+                          +------------------------+
# | H1 (vrf)              |                          | H2 (vrf)               |
# | 192.0.2.1/28          |                          | 192.0.2.2/28           |
# |    + $h1              |                          |    + $h2               |
# +----|------------------+                          +----|-------------------+
#      |                                                  |
# +----|--------------------------------------------------|-------------------+
# | SW |                                                  |                   |
# | +--|--------------------------------------------------|-----------------+ |
# | |  + $swp1                   BR1 (802.1d)             + $swp2           | |
# | |                                                                       | |
# | +-----------------------------------------------------------------------+ |
# +---------------------------------------------------------------------------+

ALL_TESTS="
	new_inactive_test
	existing_active_test
	norefresh_test
"

NUM_NETIFS=4
source lib.sh

h1_create()
{
	adf_simple_if_init "$h1" 192.0.2.1/28
}

h2_create()
{
	adf_simple_if_init "$h2" 192.0.2.2/28
}

switch_create()
{
	adf_ip_link_add br1 type bridge vlan_filtering 0 mcast_snooping 0 \
		ageing_time "$LOW_AGEING_TIME"
	adf_ip_link_set_up br1

	adf_ip_link_set_master "$swp1" br1
	adf_ip_link_set_up "$swp1"

	adf_ip_link_set_master "$swp2" br1
	adf_ip_link_set_up "$swp2"
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	adf_vrf_prepare

	h1_create
	h2_create
	switch_create
}

fdb_active_wait()
{
	local mac=$1; shift

	bridge -d fdb get "$mac" br br1 | grep -q -v "inactive"
}

fdb_inactive_wait()
{
	local mac=$1; shift

	bridge -d fdb get "$mac" br br1 | grep -q "inactive"
}

new_inactive_test()
{
	local mac="00:11:22:33:44:55"

	# Add a new FDB entry as static and inactive and check that it
	# becomes active upon traffic.
	RET=0

	bridge fdb add "$mac" dev "$swp1" master static activity_notify inactive
	bridge -d fdb get "$mac" br br1 | grep -q "inactive"
	check_err $? "FDB entry not present as \"inactive\" when should"

	$MZ "$h1" -c 1 -p 64 -a "$mac" -b bcast -t ip -q

	busywait "$BUSYWAIT_TIMEOUT" fdb_active_wait "$mac"
	check_err $? "FDB entry present as \"inactive\" when should not"

	log_test "Transition from inactive to active"

	bridge fdb del "$mac" dev "$swp1" master
}

existing_active_test()
{
	local mac="00:11:22:33:44:55"
	local ageing_time

	# Enable activity notifications on an existing dynamic FDB entry and
	# check that it becomes inactive after the ageing time passed.
	RET=0

	bridge fdb add "$mac" dev "$swp1" master dynamic
	bridge fdb replace "$mac" dev "$swp1" master static activity_notify norefresh

	bridge -d fdb get "$mac" br br1 | grep -q "activity_notify"
	check_err $? "FDB entry not present as \"activity_notify\" when should"

	bridge -d fdb get "$mac" br br1 | grep -q "inactive"
	check_fail $? "FDB entry present as \"inactive\" when should not"

	ageing_time=$(bridge_ageing_time_get br1)
	slowwait $((ageing_time * 2)) fdb_inactive_wait "$mac"
	check_err $? "FDB entry not present as \"inactive\" when should"

	log_test "Transition from active to inactive"

	bridge fdb del "$mac" dev "$swp1" master
}

norefresh_test()
{
	local mac="00:11:22:33:44:55"
	local updated_time

	# Check that the "updated" time is reset when replacing an FDB entry
	# without the "norefresh" keyword and that it is not reset when
	# replacing with the "norefresh" keyword.
	RET=0

	bridge fdb add "$mac" dev "$swp1" master static
	sleep 1

	bridge fdb replace "$mac" dev "$swp1" master static activity_notify
	updated_time=$(bridge -d -s -j fdb get "$mac" br br1 | jq '.[]["updated"]')
	if [[ $updated_time -ne 0 ]]; then
		check_err 1 "\"updated\" time was not reset when should"
	fi

	sleep 1
	bridge fdb replace "$mac" dev "$swp1" master static norefresh
	updated_time=$(bridge -d -s -j fdb get "$mac" br br1 | jq '.[]["updated"]')
	if [[ $updated_time -eq 0 ]]; then
		check_err 1 "\"updated\" time was reset when should not"
	fi

	log_test "Resetting of \"updated\" time"

	bridge fdb del "$mac" dev "$swp1" master
}

if ! bridge fdb help 2>&1 | grep -q "activity_notify"; then
	echo "SKIP: iproute2 too old, missing bridge FDB activity notification control"
	exit "$ksft_skip"
fi

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit "$EXIT_STATUS"
