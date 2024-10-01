#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +--------------------+
# | H1                 |
# |                    |
# |           $h1.10 + |
# |     192.0.2.2/24 | |
# | 2001:db8:1::2/64 | |
# |                  | |
# |              $h1 + |
# |                  | |
# +------------------|-+
#                    |
# +------------------|-+
# | SW               | |
# |            $swp1 + |
# |                  | |
# |         $swp1.10 + |
# |     192.0.2.1/24   |
# | 2001:db8:1::1/64   |
# |                    |
# +--------------------+

ALL_TESTS="
	ping_ipv4
	ping_ipv6
	max_mtu_config_test
	max_mtu_traffic_test
	min_mtu_config_test
	min_mtu_traffic_test
"

NUM_NETIFS=2
source lib.sh

h1_create()
{
	simple_if_init $h1
	vlan_create $h1 10 v$h1 192.0.2.2/24 2001:db8:1::2/64
}

h1_destroy()
{
	vlan_destroy $h1 10 192.0.2.2/24 2001:db8:1::2/64
	simple_if_fini $h1
}

switch_create()
{
	ip li set dev $swp1 up
	vlan_create $swp1 10 "" 192.0.2.1/24 2001:db8:1::1/64
}

switch_destroy()
{
	ip li set dev $swp1 down
	vlan_destroy $swp1 10
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	vrf_prepare

	h1_create

	switch_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	switch_destroy

	h1_destroy

	vrf_cleanup
}

ping_ipv4()
{
	ping_test $h1.10 192.0.2.1
}

ping_ipv6()
{
	ping6_test $h1.10 2001:db8:1::1
}

min_max_mtu_get_if()
{
	local dev=$1; shift
	local min_max=$1; shift

	ip -d -j link show $dev | jq ".[].$min_max"
}

ensure_compatible_min_max_mtu()
{
	local min_max=$1; shift

	local mtu=$(min_max_mtu_get_if ${NETIFS[p1]} $min_max)
	local i

	for ((i = 2; i <= NUM_NETIFS; ++i)); do
		local current_mtu=$(min_max_mtu_get_if ${NETIFS[p$i]} $min_max)

		if [ $current_mtu -ne $mtu ]; then
			return 1
		fi
	done
}

mtu_set_if()
{
	local dev=$1; shift
	local mtu=$1; shift
	local should_fail=${1:-0}; shift

	mtu_set $dev $mtu 2>/dev/null
	check_err_fail $should_fail $? "Set MTU $mtu for $dev"
}

mtu_set_all_if()
{
	local mtu=$1; shift
	local i

	for ((i = 1; i <= NUM_NETIFS; ++i)); do
		mtu_set_if ${NETIFS[p$i]} $mtu
		mtu_set_if ${NETIFS[p$i]}.10 $mtu
	done
}

mtu_restore_all_if()
{
	local i

	for ((i = 1; i <= NUM_NETIFS; ++i)); do
		mtu_restore ${NETIFS[p$i]}.10
		mtu_restore ${NETIFS[p$i]}
	done
}

mtu_test_ping4()
{
	local mtu=$1; shift
	local should_fail=$1; shift

	# Ping adds 8 bytes for ICMP header and 20 bytes for IP header
	local ping_headers_len=$((20 + 8))
	local pkt_size=$((mtu - ping_headers_len))

	ping_do $h1.10 192.0.2.1 "-s $pkt_size -M do"
	check_err_fail $should_fail $? "Ping, packet size: $pkt_size"
}

mtu_test_ping6()
{
	local mtu=$1; shift
	local should_fail=$1; shift

	# Ping adds 8 bytes for ICMP header and 40 bytes for IPv6 header
	local ping6_headers_len=$((40 + 8))
	local pkt_size=$((mtu - ping6_headers_len))

	ping6_do $h1.10 2001:db8:1::1 "-s $pkt_size -M do"
	check_err_fail $should_fail $? "Ping6, packet size: $pkt_size"
}

max_mtu_config_test()
{
	local i

	RET=0

	for ((i = 1; i <= NUM_NETIFS; ++i)); do
		local dev=${NETIFS[p$i]}
		local max_mtu=$(min_max_mtu_get_if $dev "max_mtu")
		local should_fail

		should_fail=0
		mtu_set_if $dev $max_mtu $should_fail
		mtu_restore $dev

		should_fail=1
		mtu_set_if $dev $((max_mtu + 1)) $should_fail
		mtu_restore $dev
	done

	log_test "Test maximum MTU configuration"
}

max_mtu_traffic_test()
{
	local should_fail
	local max_mtu

	RET=0

	if ! ensure_compatible_min_max_mtu "max_mtu"; then
		log_test_xfail "Topology has incompatible maximum MTU values"
		return
	fi

	max_mtu=$(min_max_mtu_get_if ${NETIFS[p1]} "max_mtu")

	should_fail=0
	mtu_set_all_if $max_mtu
	mtu_test_ping4 $max_mtu $should_fail
	mtu_test_ping6 $max_mtu $should_fail
	mtu_restore_all_if

	should_fail=1
	mtu_set_all_if $((max_mtu - 1))
	mtu_test_ping4 $max_mtu $should_fail
	mtu_test_ping6 $max_mtu $should_fail
	mtu_restore_all_if

	log_test "Test traffic, packet size is maximum MTU"
}

min_mtu_config_test()
{
	local i

	RET=0

	for ((i = 1; i <= NUM_NETIFS; ++i)); do
		local dev=${NETIFS[p$i]}
		local min_mtu=$(min_max_mtu_get_if $dev "min_mtu")
		local should_fail

		should_fail=0
		mtu_set_if $dev $min_mtu $should_fail
		mtu_restore $dev

		should_fail=1
		mtu_set_if $dev $((min_mtu - 1)) $should_fail
		mtu_restore $dev
	done

	log_test "Test minimum MTU configuration"
}

min_mtu_traffic_test()
{
	local should_fail=0
	local min_mtu

	RET=0

	if ! ensure_compatible_min_max_mtu "min_mtu"; then
		log_test_xfail "Topology has incompatible minimum MTU values"
		return
	fi

	min_mtu=$(min_max_mtu_get_if ${NETIFS[p1]} "min_mtu")
	mtu_set_all_if $min_mtu
	mtu_test_ping4 $min_mtu $should_fail
	# Do not test minimum MTU with IPv6, as IPv6 requires higher MTU.

	mtu_restore_all_if

	log_test "Test traffic, packet size is minimum MTU"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
