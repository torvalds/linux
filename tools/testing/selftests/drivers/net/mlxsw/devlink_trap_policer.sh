#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test devlink-trap policer functionality over mlxsw.

# +---------------------------------+
# | H1 (vrf)                        |
# |    + $h1                        |
# |    | 192.0.2.1/24               |
# |    |                            |
# |    |  default via 192.0.2.2     |
# +----|----------------------------+
#      |
# +----|----------------------------------------------------------------------+
# | SW |                                                                      |
# |    + $rp1                                                                 |
# |        192.0.2.2/24                                                       |
# |                                                                           |
# |        198.51.100.2/24                                                    |
# |    + $rp2                                                                 |
# |    |                                                                      |
# +----|----------------------------------------------------------------------+
#      |
# +----|----------------------------+
# |    |  default via 198.51.100.2  |
# |    |                            |
# |    | 198.51.100.1/24            |
# |    + $h2                        |
# | H2 (vrf)                        |
# +---------------------------------+

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	rate_limits_test
	burst_limits_test
	rate_test
	burst_test
"
NUM_NETIFS=4
source $lib_dir/tc_common.sh
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24
	mtu_set $h1 10000

	ip -4 route add default vrf v$h1 nexthop via 192.0.2.2
}

h1_destroy()
{
	ip -4 route del default vrf v$h1 nexthop via 192.0.2.2

	mtu_restore $h1
	simple_if_fini $h1 192.0.2.1/24
}

h2_create()
{
	simple_if_init $h2 198.51.100.1/24
	mtu_set $h2 10000

	ip -4 route add default vrf v$h2 nexthop via 198.51.100.2
}

h2_destroy()
{
	ip -4 route del default vrf v$h2 nexthop via 198.51.100.2

	mtu_restore $h2
	simple_if_fini $h2 198.51.100.1/24
}

router_create()
{
	ip link set dev $rp1 up
	ip link set dev $rp2 up

	__addr_add_del $rp1 add 192.0.2.2/24
	__addr_add_del $rp2 add 198.51.100.2/24
	mtu_set $rp1 10000
	mtu_set $rp2 10000

	ip -4 route add blackhole 198.51.100.100

	devlink trap set $DEVLINK_DEV trap blackhole_route action trap
}

router_destroy()
{
	devlink trap set $DEVLINK_DEV trap blackhole_route action drop

	ip -4 route del blackhole 198.51.100.100

	mtu_restore $rp2
	mtu_restore $rp1
	__addr_add_del $rp2 del 198.51.100.2/24
	__addr_add_del $rp1 del 192.0.2.2/24

	ip link set dev $rp2 down
	ip link set dev $rp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	rp1_mac=$(mac_get $rp1)

	vrf_prepare

	h1_create
	h2_create

	router_create
}

cleanup()
{
	pre_cleanup

	router_destroy

	h2_destroy
	h1_destroy

	vrf_cleanup

	# Reload to ensure devlink-trap settings are back to default.
	devlink_reload
}

rate_limits_test()
{
	RET=0

	devlink trap policer set $DEVLINK_DEV policer 1 rate 0 &> /dev/null
	check_fail $? "Policer rate was changed to rate lower than limit"
	devlink trap policer set $DEVLINK_DEV policer 1 \
		rate 2000000001 &> /dev/null
	check_fail $? "Policer rate was changed to rate higher than limit"

	devlink trap policer set $DEVLINK_DEV policer 1 rate 1
	check_err $? "Failed to set policer rate to minimum"
	devlink trap policer set $DEVLINK_DEV policer 1 rate 2000000000
	check_err $? "Failed to set policer rate to maximum"

	log_test "Trap policer rate limits"
}

burst_limits_test()
{
	RET=0

	devlink trap policer set $DEVLINK_DEV policer 1 burst 0 &> /dev/null
	check_fail $? "Policer burst size was changed to 0"
	devlink trap policer set $DEVLINK_DEV policer 1 burst 17 &> /dev/null
	check_fail $? "Policer burst size was changed to burst size that is not power of 2"
	devlink trap policer set $DEVLINK_DEV policer 1 burst 8 &> /dev/null
	check_fail $? "Policer burst size was changed to burst size lower than limit"
	devlink trap policer set $DEVLINK_DEV policer 1 \
		burst $((2**25)) &> /dev/null
	check_fail $? "Policer burst size was changed to burst size higher than limit"

	devlink trap policer set $DEVLINK_DEV policer 1 burst 16
	check_err $? "Failed to set policer burst size to minimum"
	devlink trap policer set $DEVLINK_DEV policer 1 burst $((2**24))
	check_err $? "Failed to set policer burst size to maximum"

	log_test "Trap policer burst size limits"
}

trap_rate_get()
{
	local t0 t1

	t0=$(devlink_trap_rx_packets_get blackhole_route)
	sleep 10
	t1=$(devlink_trap_rx_packets_get blackhole_route)

	echo $(((t1 - t0) / 10))
}

policer_drop_rate_get()
{
	local id=$1; shift
	local t0 t1

	t0=$(devlink_trap_policer_rx_dropped_get $id)
	sleep 10
	t1=$(devlink_trap_policer_rx_dropped_get $id)

	echo $(((t1 - t0) / 10))
}

__rate_test()
{
	local rate pct drop_rate
	local id=$1; shift

	RET=0

	devlink trap policer set $DEVLINK_DEV policer $id rate 1000 burst 512
	devlink trap group set $DEVLINK_DEV group l3_drops policer $id

	# Send packets at highest possible rate and make sure they are dropped
	# by the policer. Make sure measured received rate is about 1000 pps
	log_info "=== Tx rate: Highest, Policer rate: 1000 pps ==="

	start_traffic $h1 192.0.2.1 198.51.100.100 $rp1_mac

	sleep 5 # Take measurements when rate is stable

	rate=$(trap_rate_get)
	pct=$((100 * (rate - 1000) / 1000))
	((-10 <= pct && pct <= 10))
	check_err $? "Expected rate 1000 pps, got $rate pps, which is $pct% off. Required accuracy is +-10%"
	log_info "Expected rate 1000 pps, measured rate $rate pps"

	drop_rate=$(policer_drop_rate_get $id)
	(( drop_rate > 0 ))
	check_err $? "Expected non-zero policer drop rate, got 0"
	log_info "Measured policer drop rate of $drop_rate pps"

	stop_traffic

	# Send packets at a rate of 1000 pps and make sure they are not dropped
	# by the policer
	log_info "=== Tx rate: 1000 pps, Policer rate: 1000 pps ==="

	start_traffic $h1 192.0.2.1 198.51.100.100 $rp1_mac -d 1msec

	sleep 5 # Take measurements when rate is stable

	drop_rate=$(policer_drop_rate_get $id)
	(( drop_rate == 0 ))
	check_err $? "Expected zero policer drop rate, got a drop rate of $drop_rate pps"
	log_info "Measured policer drop rate of $drop_rate pps"

	stop_traffic

	# Unbind the policer and send packets at highest possible rate. Make
	# sure they are not dropped by the policer and that the measured
	# received rate is higher than 1000 pps
	log_info "=== Tx rate: Highest, Policer rate: No policer ==="

	devlink trap group set $DEVLINK_DEV group l3_drops nopolicer

	start_traffic $h1 192.0.2.1 198.51.100.100 $rp1_mac

	rate=$(trap_rate_get)
	(( rate > 1000 ))
	check_err $? "Expected rate higher than 1000 pps, got $rate pps"
	log_info "Measured rate $rate pps"

	drop_rate=$(policer_drop_rate_get $id)
	(( drop_rate == 0 ))
	check_err $? "Expected zero policer drop rate, got a drop rate of $drop_rate pps"
	log_info "Measured policer drop rate of $drop_rate pps"

	stop_traffic

	log_test "Trap policer rate"
}

rate_test()
{
	local id

	for id in $(devlink_trap_policer_ids_get); do
		echo
		log_info "Running rate test for policer $id"
		__rate_test $id
	done
}

__burst_test()
{
	local t0_rx t0_drop t1_rx t1_drop rx drop
	local id=$1; shift

	RET=0

	devlink trap policer set $DEVLINK_DEV policer $id rate 1000 burst 512
	devlink trap group set $DEVLINK_DEV group l3_drops policer $id

	# Send a burst of 16 packets and make sure that 16 are received
	# and that none are dropped by the policer
	log_info "=== Tx burst size: 16, Policer burst size: 512 ==="

	t0_rx=$(devlink_trap_rx_packets_get blackhole_route)
	t0_drop=$(devlink_trap_policer_rx_dropped_get $id)

	start_traffic $h1 192.0.2.1 198.51.100.100 $rp1_mac -c 16

	t1_rx=$(devlink_trap_rx_packets_get blackhole_route)
	t1_drop=$(devlink_trap_policer_rx_dropped_get $id)

	rx=$((t1_rx - t0_rx))
	(( rx == 16 ))
	check_err $? "Expected burst size of 16 packets, got $rx packets"
	log_info "Expected burst size of 16 packets, measured burst size of $rx packets"

	drop=$((t1_drop - t0_drop))
	(( drop == 0 ))
	check_err $? "Expected zero policer drops, got $drop"
	log_info "Measured policer drops of $drop packets"

	# Unbind the policer and send a burst of 64 packets. Make sure that
	# 64 packets are received and that none are dropped by the policer
	log_info "=== Tx burst size: 64, Policer burst size: No policer ==="

	devlink trap group set $DEVLINK_DEV group l3_drops nopolicer

	t0_rx=$(devlink_trap_rx_packets_get blackhole_route)
	t0_drop=$(devlink_trap_policer_rx_dropped_get $id)

	start_traffic $h1 192.0.2.1 198.51.100.100 $rp1_mac -c 64

	t1_rx=$(devlink_trap_rx_packets_get blackhole_route)
	t1_drop=$(devlink_trap_policer_rx_dropped_get $id)

	rx=$((t1_rx - t0_rx))
	(( rx == 64 ))
	check_err $? "Expected burst size of 64 packets, got $rx packets"
	log_info "Expected burst size of 64 packets, measured burst size of $rx packets"

	drop=$((t1_drop - t0_drop))
	(( drop == 0 ))
	check_err $? "Expected zero policer drops, got $drop"
	log_info "Measured policer drops of $drop packets"

	log_test "Trap policer burst size"
}

burst_test()
{
	local id

	for id in $(devlink_trap_policer_ids_get); do
		echo
		log_info "Running burst size test for policer $id"
		__burst_test $id
	done
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
