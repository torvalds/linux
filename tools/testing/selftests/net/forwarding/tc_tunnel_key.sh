#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

ALL_TESTS="tunnel_key_nofrag_test"

NUM_NETIFS=4
source tc_common.sh
source lib.sh

tcflags="skip_hw"

h1_create()
{
	simple_if_init $h1 192.0.2.1/24
	forwarding_enable
	mtu_set $h1 1500
	tunnel_create h1-et vxlan 192.0.2.1 192.0.2.2 dev $h1 dstport 0 external
	tc qdisc add dev h1-et clsact
	mtu_set h1-et 1230
	mtu_restore $h1
	mtu_set $h1 1000
}

h1_destroy()
{
	tc qdisc del dev h1-et clsact
	tunnel_destroy h1-et
	forwarding_restore
	mtu_restore $h1
	simple_if_fini $h1 192.0.2.1/24
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/24
}

h2_destroy()
{
	simple_if_fini $h2 192.0.2.2/24
}

switch_create()
{
	simple_if_init $swp1 192.0.2.2/24
	tc qdisc add dev $swp1 clsact
	simple_if_init $swp2 192.0.2.1/24
}

switch_destroy()
{
	simple_if_fini $swp2 192.0.2.1/24
	tc qdisc del dev $swp1 clsact
	simple_if_fini $swp1 192.0.2.2/24
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	h1mac=$(mac_get $h1)
	h2mac=$(mac_get $h2)

	swp1origmac=$(mac_get $swp1)
	swp2origmac=$(mac_get $swp2)
	ip link set $swp1 address $h2mac
	ip link set $swp2 address $h1mac

	vrf_prepare

	h1_create
	h2_create
	switch_create

	if ! tc action add action tunnel_key help 2>&1 | grep -q nofrag; then
		log_test "SKIP: iproute doesn't support nofrag"
		exit $ksft_skip
	fi
}

cleanup()
{
	pre_cleanup

	switch_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup

	ip link set $swp2 address $swp2origmac
	ip link set $swp1 address $swp1origmac
}

tunnel_key_nofrag_test()
{
	RET=0
	local i

	tc filter add dev $swp1 ingress protocol ip pref 100 handle 100 \
		flower ip_flags nofrag action drop
	tc filter add dev $swp1 ingress protocol ip pref 101 handle 101 \
		flower ip_flags firstfrag action drop
	tc filter add dev $swp1 ingress protocol ip pref 102 handle 102 \
		flower ip_flags nofirstfrag action drop

	# test 'nofrag' set
	tc filter add dev h1-et egress protocol all pref 1 handle 1 matchall $tcflags \
		action tunnel_key set src_ip 192.0.2.1 dst_ip 192.0.2.2 id 42 nofrag index 10
	$MZ h1-et -c 1 -p 930 -a 00:aa:bb:cc:dd:ee -b 00:ee:dd:cc:bb:aa -t ip -q
	tc_check_packets "dev $swp1 ingress" 100 1
	check_err $? "packet smaller than MTU was not tunneled"

	$MZ h1-et -c 1 -p 931 -a 00:aa:bb:cc:dd:ee -b 00:ee:dd:cc:bb:aa -t ip -q
	tc_check_packets "dev $swp1 ingress" 100 1
	check_err $? "packet bigger than MTU matched nofrag (nofrag was set)"
	tc_check_packets "dev $swp1 ingress" 101 0
	check_err $? "packet bigger than MTU matched firstfrag (nofrag was set)"
	tc_check_packets "dev $swp1 ingress" 102 0
	check_err $? "packet bigger than MTU matched nofirstfrag (nofrag was set)"

	# test 'nofrag' cleared
	tc actions change action tunnel_key set src_ip 192.0.2.1 dst_ip 192.0.2.2 id 42 index 10
	$MZ h1-et -c 1 -p 931 -a 00:aa:bb:cc:dd:ee -b 00:ee:dd:cc:bb:aa -t ip -q
	tc_check_packets "dev $swp1  ingress" 100 1
	check_err $? "packet bigger than MTU matched nofrag (nofrag was unset)"
	tc_check_packets "dev $swp1  ingress" 101 1
	check_err $? "packet bigger than MTU didn't match firstfrag (nofrag was unset) "
	tc_check_packets "dev $swp1 ingress" 102 1
	check_err $? "packet bigger than MTU didn't match nofirstfrag (nofrag was unset) "

	for i in 100 101 102; do
		tc filter del dev $swp1 ingress protocol ip pref $i handle $i flower
	done
	tc filter del dev h1-et egress pref 1 handle 1 matchall

	log_test "tunnel_key nofrag ($tcflags)"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

tc_offload_check
if [[ $? -ne 0 ]]; then
	log_info "Could not test offloaded functionality"
else
	tcflags="skip_sw"
	tests_run
fi

exit $EXIT_STATUS
