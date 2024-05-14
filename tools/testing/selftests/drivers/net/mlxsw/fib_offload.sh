#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test unicast FIB offload indication.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	ipv6_route_add
	ipv6_route_replace
	ipv6_route_nexthop_group_share
	ipv6_route_rate
"
NUM_NETIFS=4
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

tor1_create()
{
	simple_if_init $tor1_p1 2001:db8:1::2/128 2001:db8:1::3/128
}

tor1_destroy()
{
	simple_if_fini $tor1_p1 2001:db8:1::2/128 2001:db8:1::3/128
}

tor2_create()
{
	simple_if_init $tor2_p1 2001:db8:2::2/128 2001:db8:2::3/128
}

tor2_destroy()
{
	simple_if_fini $tor2_p1 2001:db8:2::2/128 2001:db8:2::3/128
}

spine_create()
{
	ip link set dev $spine_p1 up
	ip link set dev $spine_p2 up

	__addr_add_del $spine_p1 add 2001:db8:1::1/64
	__addr_add_del $spine_p2 add 2001:db8:2::1/64
}

spine_destroy()
{
	__addr_add_del $spine_p2 del 2001:db8:2::1/64
	__addr_add_del $spine_p1 del 2001:db8:1::1/64

	ip link set dev $spine_p2 down
	ip link set dev $spine_p1 down
}

ipv6_offload_check()
{
	local pfx="$1"; shift
	local expected_num=$1; shift
	local num

	# Try to avoid races with route offload
	sleep .1

	num=$(ip -6 route show match ${pfx} | grep "offload" | wc -l)

	if [ $num -eq $expected_num ]; then
		return 0
	fi

	return 1
}

ipv6_route_add_prefix()
{
	RET=0

	# Add a prefix route and check that it is offloaded.
	ip -6 route add 2001:db8:3::/64 dev $spine_p1 metric 100
	ipv6_offload_check "2001:db8:3::/64 dev $spine_p1 metric 100" 1
	check_err $? "prefix route not offloaded"

	# Append an identical prefix route with an higher metric and check that
	# offload indication did not change.
	ip -6 route append 2001:db8:3::/64 dev $spine_p1 metric 200
	ipv6_offload_check "2001:db8:3::/64 dev $spine_p1 metric 100" 1
	check_err $? "lowest metric not offloaded after append"
	ipv6_offload_check "2001:db8:3::/64 dev $spine_p1 metric 200" 0
	check_err $? "highest metric offloaded when should not"

	# Prepend an identical prefix route with lower metric and check that
	# it is offloaded and the others are not.
	ip -6 route append 2001:db8:3::/64 dev $spine_p1 metric 10
	ipv6_offload_check "2001:db8:3::/64 dev $spine_p1 metric 10" 1
	check_err $? "lowest metric not offloaded after prepend"
	ipv6_offload_check "2001:db8:3::/64 dev $spine_p1 metric 100" 0
	check_err $? "mid metric offloaded when should not"
	ipv6_offload_check "2001:db8:3::/64 dev $spine_p1 metric 200" 0
	check_err $? "highest metric offloaded when should not"

	# Delete the routes and add the same route with a different nexthop
	# device. Check that it is offloaded.
	ip -6 route flush 2001:db8:3::/64 dev $spine_p1
	ip -6 route add 2001:db8:3::/64 dev $spine_p2
	ipv6_offload_check "2001:db8:3::/64 dev $spine_p2" 1

	log_test "IPv6 prefix route add"

	ip -6 route flush 2001:db8:3::/64
}

ipv6_route_add_mpath()
{
	RET=0

	# Add a multipath route and check that it is offloaded.
	ip -6 route add 2001:db8:3::/64 metric 100 \
		nexthop via 2001:db8:1::2 dev $spine_p1 \
		nexthop via 2001:db8:2::2 dev $spine_p2
	ipv6_offload_check "2001:db8:3::/64 metric 100" 2
	check_err $? "multipath route not offloaded when should"

	# Append another nexthop and check that it is offloaded as well.
	ip -6 route append 2001:db8:3::/64 metric 100 \
		nexthop via 2001:db8:1::3 dev $spine_p1
	ipv6_offload_check "2001:db8:3::/64 metric 100" 3
	check_err $? "appended nexthop not offloaded when should"

	# Mimic route replace by removing the route and adding it back with
	# only two nexthops.
	ip -6 route del 2001:db8:3::/64
	ip -6 route add 2001:db8:3::/64 metric 100 \
		nexthop via 2001:db8:1::2 dev $spine_p1 \
		nexthop via 2001:db8:2::2 dev $spine_p2
	ipv6_offload_check "2001:db8:3::/64 metric 100" 2
	check_err $? "multipath route not offloaded after delete & add"

	# Append a nexthop with an higher metric and check that the offload
	# indication did not change.
	ip -6 route append 2001:db8:3::/64 metric 200 \
		nexthop via 2001:db8:1::3 dev $spine_p1
	ipv6_offload_check "2001:db8:3::/64 metric 100" 2
	check_err $? "lowest metric not offloaded after append"
	ipv6_offload_check "2001:db8:3::/64 metric 200" 0
	check_err $? "highest metric offloaded when should not"

	# Prepend a nexthop with a lower metric and check that it is offloaded
	# and the others are not.
	ip -6 route append 2001:db8:3::/64 metric 10 \
		nexthop via 2001:db8:1::3 dev $spine_p1
	ipv6_offload_check "2001:db8:3::/64 metric 10" 1
	check_err $? "lowest metric not offloaded after prepend"
	ipv6_offload_check "2001:db8:3::/64 metric 100" 0
	check_err $? "mid metric offloaded when should not"
	ipv6_offload_check "2001:db8:3::/64 metric 200" 0
	check_err $? "highest metric offloaded when should not"

	log_test "IPv6 multipath route add"

	ip -6 route flush 2001:db8:3::/64
}

ipv6_route_add()
{
	ipv6_route_add_prefix
	ipv6_route_add_mpath
}

ipv6_route_replace()
{
	RET=0

	# Replace prefix route with prefix route.
	ip -6 route add 2001:db8:3::/64 metric 100 dev $spine_p1
	ipv6_offload_check "2001:db8:3::/64 metric 100" 1
	check_err $? "prefix route not offloaded when should"
	ip -6 route replace 2001:db8:3::/64 metric 100 dev $spine_p2
	ipv6_offload_check "2001:db8:3::/64 metric 100" 1
	check_err $? "prefix route not offloaded after replace"

	# Replace prefix route with multipath route.
	ip -6 route replace 2001:db8:3::/64 metric 100 \
		nexthop via 2001:db8:1::2 dev $spine_p1 \
		nexthop via 2001:db8:2::2 dev $spine_p2
	ipv6_offload_check "2001:db8:3::/64 metric 100" 2
	check_err $? "multipath route not offloaded after replace"

	# Replace multipath route with prefix route. A prefix route cannot
	# replace a multipath route, so it is appended.
	ip -6 route replace 2001:db8:3::/64 metric 100 dev $spine_p1
	ipv6_offload_check "2001:db8:3::/64 metric 100 dev $spine_p1" 0
	check_err $? "prefix route offloaded after 'replacing' multipath route"
	ipv6_offload_check "2001:db8:3::/64 metric 100" 2
	check_err $? "multipath route not offloaded after being 'replaced' by prefix route"

	# Replace multipath route with multipath route.
	ip -6 route replace 2001:db8:3::/64 metric 100 \
		nexthop via 2001:db8:1::3 dev $spine_p1 \
		nexthop via 2001:db8:2::3 dev $spine_p2
	ipv6_offload_check "2001:db8:3::/64 metric 100" 2
	check_err $? "multipath route not offloaded after replacing multipath route"

	# Replace a non-existing multipath route with a multipath route and
	# check that it is appended and not offloaded.
	ip -6 route replace 2001:db8:3::/64 metric 200 \
		nexthop via 2001:db8:1::3 dev $spine_p1 \
		nexthop via 2001:db8:2::3 dev $spine_p2
	ipv6_offload_check "2001:db8:3::/64 metric 100" 2
	check_err $? "multipath route not offloaded after non-existing route was 'replaced'"
	ipv6_offload_check "2001:db8:3::/64 metric 200" 0
	check_err $? "multipath route offloaded after 'replacing' non-existing route"

	log_test "IPv6 route replace"

	ip -6 route flush 2001:db8:3::/64
}

ipv6_route_nexthop_group_share()
{
	RET=0

	# The driver consolidates identical nexthop groups in order to reduce
	# the resource usage in its adjacency table. Check that the deletion
	# of one multipath route using the group does not affect the other.
	ip -6 route add 2001:db8:3::/64 \
		nexthop via 2001:db8:1::2 dev $spine_p1 \
		nexthop via 2001:db8:2::2 dev $spine_p2
	ip -6 route add 2001:db8:4::/64 \
		nexthop via 2001:db8:1::2 dev $spine_p1 \
		nexthop via 2001:db8:2::2 dev $spine_p2
	ipv6_offload_check "2001:db8:3::/64" 2
	check_err $? "multipath route not offloaded when should"
	ipv6_offload_check "2001:db8:4::/64" 2
	check_err $? "multipath route not offloaded when should"
	ip -6 route del 2001:db8:3::/64
	ipv6_offload_check "2001:db8:4::/64" 2
	check_err $? "multipath route not offloaded after deletion of route sharing the nexthop group"

	# Check that after unsharing a nexthop group the routes are still
	# marked as offloaded.
	ip -6 route add 2001:db8:3::/64 \
		nexthop via 2001:db8:1::2 dev $spine_p1 \
		nexthop via 2001:db8:2::2 dev $spine_p2
	ip -6 route del 2001:db8:4::/64 \
		nexthop via 2001:db8:1::2 dev $spine_p1
	ipv6_offload_check "2001:db8:4::/64" 1
	check_err $? "singlepath route not offloaded after unsharing the nexthop group"
	ipv6_offload_check "2001:db8:3::/64" 2
	check_err $? "multipath route not offloaded after unsharing the nexthop group"

	log_test "IPv6 nexthop group sharing"

	ip -6 route flush 2001:db8:3::/64
	ip -6 route flush 2001:db8:4::/64
}

ipv6_route_rate()
{
	local batch_dir=$(mktemp -d)
	local num_rts=$((40 * 1024))
	local num_nhs=16
	local total
	local start
	local diff
	local end
	local nhs
	local i

	RET=0

	# Prepare 40K /64 multipath routes with 16 nexthops each and check how
	# long it takes to add them. A limit of 60 seconds is set. It is much
	# higher than insertion should take and meant to flag a serious
	# regression.
	total=$((nums_nhs * num_rts))

	for i in $(seq 1 $num_nhs); do
		ip -6 address add 2001:db8:1::10:$i/128 dev $tor1_p1
		nexthops+=" nexthop via 2001:db8:1::10:$i dev $spine_p1"
	done

	for i in $(seq 1 $num_rts); do
		echo "route add 2001:db8:8:$(printf "%x" $i)::/64$nexthops" \
			>> $batch_dir/add.batch
		echo "route del 2001:db8:8:$(printf "%x" $i)::/64$nexthops" \
			>> $batch_dir/del.batch
	done

	start=$(date +%s.%N)

	ip -batch $batch_dir/add.batch
	count=$(ip -6 route show | grep offload | wc -l)
	while [ $count -lt $total ]; do
		sleep .01
		count=$(ip -6 route show | grep offload | wc -l)
	done

	end=$(date +%s.%N)

	diff=$(echo "$end - $start" | bc -l)
	test "$(echo "$diff > 60" | bc -l)" -eq 0
	check_err $? "route insertion took too long"
	log_info "inserted $num_rts routes in $diff seconds"

	log_test "IPv6 routes insertion rate"

	ip -batch $batch_dir/del.batch
	for i in $(seq 1 $num_nhs); do
		ip -6 address del 2001:db8:1::10:$i/128 dev $tor1_p1
	done
	rm -rf $batch_dir
}

setup_prepare()
{
	spine_p1=${NETIFS[p1]}
	tor1_p1=${NETIFS[p2]}

	spine_p2=${NETIFS[p3]}
	tor2_p1=${NETIFS[p4]}

	vrf_prepare
	forwarding_enable

	tor1_create
	tor2_create
	spine_create
}

cleanup()
{
	pre_cleanup

	spine_destroy
	tor2_destroy
	tor1_destroy

	forwarding_restore
	vrf_cleanup
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
