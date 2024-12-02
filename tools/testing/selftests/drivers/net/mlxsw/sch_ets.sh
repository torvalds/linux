#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# A driver for the ETS selftest that implements testing in offloaded datapath.
lib_dir=$(dirname $0)/../../../net/forwarding
source $lib_dir/sch_ets_core.sh
source $lib_dir/devlink_lib.sh
source qos_lib.sh

ALL_TESTS="
	ping_ipv4
	priomap_mode
	ets_test_strict
	ets_test_mixed
	ets_test_dwrr
"

PARENT="parent 3:3"

switch_create()
{
	# Create a bottleneck so that the DWRR process can kick in.
	tc qdisc replace dev $swp2 root handle 3: tbf rate 1gbit \
		burst 128K limit 1G

	ets_switch_create

	# Set the ingress quota high and use the three egress TCs to limit the
	# amount of traffic that is admitted to the shared buffers. This makes
	# sure that there is always enough traffic of all types to select from
	# for the DWRR process.
	devlink_port_pool_th_save $swp1 0
	devlink_port_pool_th_set $swp1 0 12
	devlink_tc_bind_pool_th_save $swp1 0 ingress
	devlink_tc_bind_pool_th_set $swp1 0 ingress 0 12
	devlink_port_pool_th_save $swp2 4
	devlink_port_pool_th_set $swp2 4 12
	devlink_tc_bind_pool_th_save $swp2 7 egress
	devlink_tc_bind_pool_th_set $swp2 7 egress 4 5
	devlink_tc_bind_pool_th_save $swp2 6 egress
	devlink_tc_bind_pool_th_set $swp2 6 egress 4 5
	devlink_tc_bind_pool_th_save $swp2 5 egress
	devlink_tc_bind_pool_th_set $swp2 5 egress 4 5

	# Note: sch_ets_core.sh uses VLAN ingress-qos-map to assign packet
	# priorities at $swp1 based on their 802.1p headers. ingress-qos-map is
	# not offloaded by mlxsw as of this writing, but the mapping used is
	# 1:1, which is the mapping currently hard-coded by the driver.
}

switch_destroy()
{
	devlink_tc_bind_pool_th_restore $swp2 5 egress
	devlink_tc_bind_pool_th_restore $swp2 6 egress
	devlink_tc_bind_pool_th_restore $swp2 7 egress
	devlink_port_pool_th_restore $swp2 4
	devlink_tc_bind_pool_th_restore $swp1 0 ingress
	devlink_port_pool_th_restore $swp1 0

	ets_switch_destroy

	tc qdisc del dev $swp2 root handle 3:
}

# Callback from sch_ets_tests.sh
collect_stats()
{
	local -a streams=("$@")
	local stream

	# Wait for qdisc counter update so that we don't get it mid-way through.
	busywait_for_counter 1000 +1 \
		qdisc_parent_stats_get $swp2 10:$((${streams[0]} + 1)) .bytes \
		> /dev/null

	for stream in ${streams[@]}; do
		qdisc_parent_stats_get $swp2 10:$((stream + 1)) .bytes
	done
}

bail_on_lldpad
ets_run
