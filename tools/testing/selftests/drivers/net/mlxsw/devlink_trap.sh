#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test generic devlink-trap functionality over mlxsw. These tests are not
# specific to a single trap, but do not check the devlink-trap common
# infrastructure either.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	dev_del_test
"
NUM_NETIFS=4
source $lib_dir/tc_common.sh
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

h1_create()
{
	simple_if_init $h1
}

h1_destroy()
{
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
}

h2_destroy()
{
	simple_if_fini $h2
}

switch_create()
{
	ip link add dev br0 type bridge vlan_filtering 1 mcast_snooping 0

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br0

	ip link set dev br0 up
	ip link set dev $swp1 up
	ip link set dev $swp2 up
}

switch_destroy()
{
	ip link set dev $swp2 down
	ip link set dev $swp1 down

	ip link del dev br0
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare

	h1_create
	h2_create

	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy

	h2_destroy
	h1_destroy

	vrf_cleanup
}

dev_del_test()
{
	local trap_name="source_mac_is_multicast"
	local smac=01:02:03:04:05:06
	local num_iter=5
	local mz_pid
	local i

	$MZ $h1 -c 0 -p 100 -a $smac -b bcast -t ip -q &
	mz_pid=$!

	# The purpose of this test is to make sure we correctly dismantle a
	# port while packets are trapped from it. This is done by reloading the
	# the driver while the 'ingress_smac_mc_drop' trap is triggered.
	RET=0

	for i in $(seq 1 $num_iter); do
		log_info "Iteration $i / $num_iter"

		devlink_trap_action_set $trap_name "trap"
		sleep 1

		devlink_reload
		# Allow netdevices to be re-created following the reload
		sleep 20

		cleanup
		setup_prepare
		setup_wait
	done

	log_test "Device delete"

	kill_process $mz_pid
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
