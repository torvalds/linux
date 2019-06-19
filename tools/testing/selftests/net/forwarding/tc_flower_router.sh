#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="match_indev_egress_test"
NUM_NETIFS=6
source tc_common.sh
source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.1.1/24

	ip route add 192.0.2.0/24 vrf v$h1 nexthop via 192.0.1.2
	ip route add 192.0.3.0/24 vrf v$h1 nexthop via 192.0.1.2
}

h1_destroy()
{
	ip route del 192.0.3.0/24 vrf v$h1
	ip route del 192.0.2.0/24 vrf v$h1

	simple_if_fini $h1 192.0.1.1/24
}

h2_create()
{
	simple_if_init $h2 192.0.2.1/24

	ip route add 192.0.1.0/24 vrf v$h2 nexthop via 192.0.2.2
	ip route add 192.0.3.0/24 vrf v$h2 nexthop via 192.0.2.2
}

h2_destroy()
{
	ip route del 192.0.3.0/24 vrf v$h2
	ip route del 192.0.1.0/24 vrf v$h2

	simple_if_fini $h2 192.0.2.1/24
}

h3_create()
{
	simple_if_init $h3 192.0.3.1/24

	ip route add 192.0.1.0/24 vrf v$h3 nexthop via 192.0.3.2
	ip route add 192.0.2.0/24 vrf v$h3 nexthop via 192.0.3.2
}

h3_destroy()
{
	ip route del 192.0.2.0/24 vrf v$h3
	ip route del 192.0.1.0/24 vrf v$h3

	simple_if_fini $h3 192.0.3.1/24
}


router_create()
{
	ip link set dev $rp1 up
	ip link set dev $rp2 up
	ip link set dev $rp3 up

	tc qdisc add dev $rp3 clsact

	ip address add 192.0.1.2/24 dev $rp1
	ip address add 192.0.2.2/24 dev $rp2
	ip address add 192.0.3.2/24 dev $rp3
}

router_destroy()
{
	ip address del 192.0.3.2/24 dev $rp3
	ip address del 192.0.2.2/24 dev $rp2
	ip address del 192.0.1.2/24 dev $rp1

	tc qdisc del dev $rp3 clsact

	ip link set dev $rp3 down
	ip link set dev $rp2 down
	ip link set dev $rp1 down
}

match_indev_egress_test()
{
	RET=0

	tc filter add dev $rp3 egress protocol ip pref 1 handle 101 flower \
		$tcflags indev $rp1 dst_ip 192.0.3.1 action drop
	tc filter add dev $rp3 egress protocol ip pref 2 handle 102 flower \
		$tcflags indev $rp2 dst_ip 192.0.3.1 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $rp1mac -A 192.0.1.1 -B 192.0.3.1 \
		-t ip -q

	tc_check_packets "dev $rp3 egress" 102 1
	check_fail $? "Matched on a wrong filter"

	tc_check_packets "dev $rp3 egress" 101 1
	check_err $? "Did not match on correct filter"

	$MZ $h2 -c 1 -p 64 -a $h2mac -b $rp2mac -A 192.0.2.1 -B 192.0.3.1 \
		-t ip -q

	tc_check_packets "dev $rp3 egress" 101 2
	check_fail $? "Matched on a wrong filter"

	tc_check_packets "dev $rp3 egress" 102 1
	check_err $? "Did not match on correct filter"

	tc filter del dev $rp3 egress protocol ip pref 2 handle 102 flower
	tc filter del dev $rp3 egress protocol ip pref 1 handle 101 flower

	log_test "indev egress match ($tcflags)"
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	h2=${NETIFS[p3]}
	rp2=${NETIFS[p4]}

	h3=${NETIFS[p5]}
	rp3=${NETIFS[p6]}

	h1mac=$(mac_get $h1)
	rp1mac=$(mac_get $rp1)
	h2mac=$(mac_get $h2)
	rp2mac=$(mac_get $rp2)

	vrf_prepare

	h1_create
	h2_create
	h3_create

	router_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	router_destroy

	h3_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup
}

trap cleanup EXIT

setup_prepare
setup_wait

tc_offload_check
if [[ $? -ne 0 ]]; then
	log_info "Could not test offloaded functionality"
else
	tcflags="skip_sw"
	tests_run
fi

exit $EXIT_STATUS
