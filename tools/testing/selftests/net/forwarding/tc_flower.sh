#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="match_dst_mac_test match_src_mac_test match_dst_ip_test \
	match_src_ip_test match_ip_flags_test match_pcp_test match_vlan_test"
NUM_NETIFS=2
source tc_common.sh
source lib.sh

tcflags="skip_hw"

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 198.51.100.1/24
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/24 198.51.100.1/24
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/24 198.51.100.2/24
	tc qdisc add dev $h2 clsact
}

h2_destroy()
{
	tc qdisc del dev $h2 clsact
	simple_if_fini $h2 192.0.2.2/24 198.51.100.2/24
}

match_dst_mac_test()
{
	local dummy_mac=de:ad:be:ef:aa:aa

	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_mac $dummy_mac action drop
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_mac $h2mac action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched on a wrong filter"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower

	log_test "dst_mac match ($tcflags)"
}

match_src_mac_test()
{
	local dummy_mac=de:ad:be:ef:aa:aa

	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags src_mac $dummy_mac action drop
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags src_mac $h1mac action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched on a wrong filter"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower

	log_test "src_mac match ($tcflags)"
}

match_dst_ip_test()
{
	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 198.51.100.2 action drop
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.0.2.2 action drop
	tc filter add dev $h2 ingress protocol ip pref 3 handle 103 flower \
		$tcflags dst_ip 192.0.2.0/24 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched on a wrong filter"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter"

	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 103 1
	check_err $? "Did not match on correct filter with mask"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol ip pref 3 handle 103 flower

	log_test "dst_ip match ($tcflags)"
}

match_src_ip_test()
{
	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags src_ip 198.51.100.1 action drop
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags src_ip 192.0.2.1 action drop
	tc filter add dev $h2 ingress protocol ip pref 3 handle 103 flower \
		$tcflags src_ip 192.0.2.0/24 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched on a wrong filter"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter"

	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 103 1
	check_err $? "Did not match on correct filter with mask"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol ip pref 3 handle 103 flower

	log_test "src_ip match ($tcflags)"
}

match_ip_flags_test()
{
	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags ip_flags frag action continue
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags ip_flags firstfrag action continue
	tc filter add dev $h2 ingress protocol ip pref 3 handle 103 flower \
		$tcflags ip_flags nofirstfrag action continue
	tc filter add dev $h2 ingress protocol ip pref 4 handle 104 flower \
		$tcflags ip_flags nofrag action drop

	$MZ $h1 -c 1 -p 1000 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip "frag=0" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched on wrong frag filter (nofrag)"

	tc_check_packets "dev $h2 ingress" 102 1
	check_fail $? "Matched on wrong firstfrag filter (nofrag)"

	tc_check_packets "dev $h2 ingress" 103 1
	check_err $? "Did not match on nofirstfrag filter (nofrag) "

	tc_check_packets "dev $h2 ingress" 104 1
	check_err $? "Did not match on nofrag filter (nofrag)"

	$MZ $h1 -c 1 -p 1000 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip "frag=0,mf" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match on frag filter (1stfrag)"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match fistfrag filter (1stfrag)"

	tc_check_packets "dev $h2 ingress" 103 1
	check_err $? "Matched on wrong nofirstfrag filter (1stfrag)"

	tc_check_packets "dev $h2 ingress" 104 1
	check_err $? "Match on wrong nofrag filter (1stfrag)"

	$MZ $h1 -c 1 -p 1000 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip "frag=256,mf" -q
	$MZ $h1 -c 1 -p 1000 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip "frag=256" -q

	tc_check_packets "dev $h2 ingress" 101 3
	check_err $? "Did not match on frag filter (no1stfrag)"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Matched on wrong firstfrag filter (no1stfrag)"

	tc_check_packets "dev $h2 ingress" 103 3
	check_err $? "Did not match on nofirstfrag filter (no1stfrag)"

	tc_check_packets "dev $h2 ingress" 104 1
	check_err $? "Matched on nofrag filter (no1stfrag)"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol ip pref 3 handle 103 flower
	tc filter del dev $h2 ingress protocol ip pref 4 handle 104 flower

	log_test "ip_flags match ($tcflags)"
}

match_pcp_test()
{
	RET=0

	vlan_create $h2 85 v$h2 192.0.2.11/24

	tc filter add dev $h2 ingress protocol 802.1q pref 1 handle 101 \
		flower vlan_prio 6 $tcflags dst_mac $h2mac action drop
	tc filter add dev $h2 ingress protocol 802.1q pref 2 handle 102 \
		flower vlan_prio 7 $tcflags dst_mac $h2mac action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -B 192.0.2.11 -Q 7:85 -t ip -q
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -B 192.0.2.11 -Q 0:85 -t ip -q

	tc_check_packets "dev $h2 ingress" 101 0
	check_err $? "Matched on specified PCP when should not"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on specified PCP"

	tc filter del dev $h2 ingress protocol 802.1q pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol 802.1q pref 1 handle 101 flower

	vlan_destroy $h2 85

	log_test "PCP match ($tcflags)"
}

match_vlan_test()
{
	RET=0

	vlan_create $h2 85 v$h2 192.0.2.11/24
	vlan_create $h2 75 v$h2 192.0.2.10/24

	tc filter add dev $h2 ingress protocol 802.1q pref 1 handle 101 \
		flower vlan_id 75 $tcflags action drop
	tc filter add dev $h2 ingress protocol 802.1q pref 2 handle 102 \
		flower vlan_id 85 $tcflags action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -B 192.0.2.11 -Q 0:85 -t ip -q

	tc_check_packets "dev $h2 ingress" 101 0
	check_err $? "Matched on specified VLAN when should not"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on specified VLAN"

	tc filter del dev $h2 ingress protocol 802.1q pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol 802.1q pref 1 handle 101 flower

	vlan_destroy $h2 75
	vlan_destroy $h2 85

	log_test "VLAN match ($tcflags)"
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}
	h1mac=$(mac_get $h1)
	h2mac=$(mac_get $h2)

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

tc_offload_check
if [[ $? -ne 0 ]]; then
	log_info "Could not test offloaded functionality"
else
	tcflags="skip_sw"
	tests_run
fi

exit $EXIT_STATUS
