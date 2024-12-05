#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	port_pool_test
	port_tc_ip_test
	port_tc_arp_test
"

NUM_NETIFS=2
source ../../../net/forwarding/lib.sh
source ../../../net/forwarding/devlink_lib.sh
source mlxsw_lib.sh

SB_POOL_ING=0
SB_POOL_EGR_CPU=10

SB_ITC_CPU_IP=2
SB_ITC_CPU_ARP=2
SB_ITC=0

h1_create()
{
	simple_if_init $h1 192.0.1.1/24
}

h1_destroy()
{
	simple_if_fini $h1 192.0.1.1/24
}

h2_create()
{
	simple_if_init $h2 192.0.1.2/24
}

h2_destroy()
{
	simple_if_fini $h2 192.0.1.2/24
}

sb_occ_pool_check()
{
	local dl_port=$1; shift
	local pool=$1; shift
	local exp_max_occ=$1
	local max_occ
	local err=0

	max_occ=$(devlink sb -j occupancy show $dl_port \
		  | jq -e ".[][][\"pool\"][\"$pool\"][\"max\"]")

	if [[ "$max_occ" -ne "$exp_max_occ" ]]; then
		err=1
	fi

	echo $max_occ
	return $err
}

sb_occ_itc_check()
{
	local dl_port=$1; shift
	local itc=$1; shift
	local exp_max_occ=$1
	local max_occ
	local err=0

	max_occ=$(devlink sb -j occupancy show $dl_port \
		  | jq -e ".[][][\"itc\"][\"$itc\"][\"max\"]")

	if [[ "$max_occ" -ne "$exp_max_occ" ]]; then
		err=1
	fi

	echo $max_occ
	return $err
}

sb_occ_etc_check()
{
	local dl_port=$1; shift
	local etc=$1; shift
	local exp_max_occ=$1; shift
	local max_occ
	local err=0

	max_occ=$(devlink sb -j occupancy show $dl_port \
		  | jq -e ".[][][\"etc\"][\"$etc\"][\"max\"]")

	if [[ "$max_occ" -ne "$exp_max_occ" ]]; then
		err=1
	fi

	echo $max_occ
	return $err
}

port_pool_test()
{
	local exp_max_occ=$(devlink_cell_size_get)
	local max_occ

	devlink sb occupancy clearmax $DEVLINK_DEV

	$MZ $h1 -c 1 -p 10 -a $h1mac -b $h2mac -A 192.0.1.1 -B 192.0.1.2 \
		-t ip -q

	devlink sb occupancy snapshot $DEVLINK_DEV

	RET=0
	max_occ=$(sb_occ_pool_check $dl_port2 $SB_POOL_ING $exp_max_occ)
	check_err $? "Expected iPool($SB_POOL_ING) max occupancy to be $exp_max_occ, but got $max_occ"
	log_test "physical port's($h2) ingress pool"

	RET=0
	max_occ=$(sb_occ_pool_check $cpu_dl_port $SB_POOL_EGR_CPU $exp_max_occ)
	check_err $? "Expected ePool($SB_POOL_EGR_CPU) max occupancy to be $exp_max_occ, but got $max_occ"
	log_test "CPU port's egress pool"
}

port_tc_ip_test()
{
	local exp_max_occ=$(devlink_cell_size_get)
	local max_occ

	devlink sb occupancy clearmax $DEVLINK_DEV

	$MZ $h1 -c 1 -p 10 -a $h1mac -b $h2mac -A 192.0.1.1 -B 192.0.1.2 \
		-t ip -q

	devlink sb occupancy snapshot $DEVLINK_DEV

	RET=0
	max_occ=$(sb_occ_itc_check $dl_port2 $SB_ITC $exp_max_occ)
	check_err $? "Expected ingress TC($SB_ITC) max occupancy to be $exp_max_occ, but got $max_occ"
	log_test "physical port's($h2) ingress TC - IP packet"

	RET=0
	max_occ=$(sb_occ_etc_check $cpu_dl_port $SB_ITC_CPU_IP $exp_max_occ)
	check_err $? "Expected egress TC($SB_ITC_CPU_IP) max occupancy to be $exp_max_occ, but got $max_occ"
	log_test "CPU port's egress TC - IP packet"
}

port_tc_arp_test()
{
	local exp_max_occ=$(devlink_cell_size_get)
	local max_occ

	devlink sb occupancy clearmax $DEVLINK_DEV

	$MZ $h1 -c 1 -p 10 -a $h1mac -A 192.0.1.1 -t arp -q

	devlink sb occupancy snapshot $DEVLINK_DEV

	RET=0
	max_occ=$(sb_occ_itc_check $dl_port2 $SB_ITC $exp_max_occ)
	check_err $? "Expected ingress TC($SB_ITC) max occupancy to be $exp_max_occ, but got $max_occ"
	log_test "physical port's($h2) ingress TC - ARP packet"

	RET=0
	max_occ=$(sb_occ_etc_check $cpu_dl_port $SB_ITC_CPU_ARP $exp_max_occ)
	check_err $? "Expected egress TC($SB_ITC_IP2ME) max occupancy to be $exp_max_occ, but got $max_occ"
	log_test "CPU port's egress TC - ARP packet"
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}

	h1mac=$(mac_get $h1)
	h2mac=$(mac_get $h2)

	dl_port1=$(devlink_port_by_netdev $h1)
	dl_port2=$(devlink_port_by_netdev $h2)

	cpu_dl_port=$(devlink_cpu_port_get)

	vrf_prepare

	h1_create
	h2_create
}

cleanup()
{
	pre_cleanup

	h2_destroy
	h1_destroy

	vrf_cleanup
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
