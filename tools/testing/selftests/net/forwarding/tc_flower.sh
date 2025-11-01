#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="match_dst_mac_test match_src_mac_test match_dst_ip_test \
	match_src_ip_test match_ip_flags_test match_pcp_test match_vlan_test \
	match_ip_tos_test match_indev_test match_ip_ttl_test
	match_mpls_label_test \
	match_mpls_tc_test match_mpls_bos_test match_mpls_ttl_test \
	match_mpls_lse_test match_erspan_opts_test"
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

	tc_check_packets "dev $h2 ingress" 102 0
	check_fail $? "Did not match on correct filter"

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

	tc_check_packets "dev $h2 ingress" 102 0
	check_fail $? "Did not match on correct filter"

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

match_ip_tos_test()
{
	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 192.0.2.2 ip_tos 0x20 action drop
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.0.2.2 ip_tos 0x18 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip tos=18 -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched on a wrong filter (0x18)"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter (0x18)"

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip tos=20 -q

	tc_check_packets "dev $h2 ingress" 102 2
	check_fail $? "Matched on a wrong filter (0x20)"

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match on correct filter (0x20)"

	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	log_test "ip_tos match ($tcflags)"
}

match_ip_ttl_test()
{
	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 192.0.2.2 ip_ttl 63 action drop
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.0.2.2 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip "ttl=63" -q

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip "ttl=63,mf,frag=256" -q

	tc_check_packets "dev $h2 ingress" 102 1
	check_fail $? "Matched on the wrong filter (no check on ttl)"

	tc_check_packets "dev $h2 ingress" 101 2
	check_err $? "Did not match on correct filter (ttl=63)"

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip "ttl=255" -q

	tc_check_packets "dev $h2 ingress" 101 3
	check_fail $? "Matched on a wrong filter (ttl=63)"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter (no check on ttl)"

	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	log_test "ip_ttl match ($tcflags)"
}

match_indev_test()
{
	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags indev $h1 dst_mac $h2mac action drop
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags indev $h2 dst_mac $h2mac action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched on a wrong filter"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter"

	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	log_test "indev match ($tcflags)"
}

# Unfortunately, mausezahn can't build MPLS headers when used in L2
# mode, so we have this function to build Label Stack Entries.
mpls_lse()
{
	local label=$1
	local tc=$2
	local bos=$3
	local ttl=$4

	printf "%02x %02x %02x %02x"                        \
		$((label >> 12))                            \
		$((label >> 4 & 0xff))                      \
		$((((label & 0xf) << 4) + (tc << 1) + bos)) \
		$ttl
}

match_mpls_label_test()
{
	local ethtype="88 47"; readonly ethtype
	local pkt

	RET=0

	check_tc_mpls_support $h2 || return 0

	tc filter add dev $h2 ingress protocol mpls_uc pref 1 handle 101 \
		flower $tcflags mpls_label 0 action drop
	tc filter add dev $h2 ingress protocol mpls_uc pref 2 handle 102 \
		flower $tcflags mpls_label 1048575 action drop

	pkt="$ethtype $(mpls_lse 1048575 0 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched on a wrong filter (1048575)"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter (1048575)"

	pkt="$ethtype $(mpls_lse 0 0 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 102 2
	check_fail $? "Matched on a wrong filter (0)"

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match on correct filter (0)"

	tc filter del dev $h2 ingress protocol mpls_uc pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 1 handle 101 flower

	log_test "mpls_label match ($tcflags)"
}

match_mpls_tc_test()
{
	local ethtype="88 47"; readonly ethtype
	local pkt

	RET=0

	check_tc_mpls_support $h2 || return 0

	tc filter add dev $h2 ingress protocol mpls_uc pref 1 handle 101 \
		flower $tcflags mpls_tc 0 action drop
	tc filter add dev $h2 ingress protocol mpls_uc pref 2 handle 102 \
		flower $tcflags mpls_tc 7 action drop

	pkt="$ethtype $(mpls_lse 0 7 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched on a wrong filter (7)"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter (7)"

	pkt="$ethtype $(mpls_lse 0 0 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 102 2
	check_fail $? "Matched on a wrong filter (0)"

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match on correct filter (0)"

	tc filter del dev $h2 ingress protocol mpls_uc pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 1 handle 101 flower

	log_test "mpls_tc match ($tcflags)"
}

match_mpls_bos_test()
{
	local ethtype="88 47"; readonly ethtype
	local pkt

	RET=0

	check_tc_mpls_support $h2 || return 0

	tc filter add dev $h2 ingress protocol mpls_uc pref 1 handle 101 \
		flower $tcflags mpls_bos 0 action drop
	tc filter add dev $h2 ingress protocol mpls_uc pref 2 handle 102 \
		flower $tcflags mpls_bos 1 action drop

	pkt="$ethtype $(mpls_lse 0 0 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched on a wrong filter (1)"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter (1)"

	# Need to add a second label to properly mark the Bottom of Stack
	pkt="$ethtype $(mpls_lse 0 0 0 255) $(mpls_lse 0 0 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 102 2
	check_fail $? "Matched on a wrong filter (0)"

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match on correct filter (0)"

	tc filter del dev $h2 ingress protocol mpls_uc pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 1 handle 101 flower

	log_test "mpls_bos match ($tcflags)"
}

match_mpls_ttl_test()
{
	local ethtype="88 47"; readonly ethtype
	local pkt

	RET=0

	check_tc_mpls_support $h2 || return 0

	tc filter add dev $h2 ingress protocol mpls_uc pref 1 handle 101 \
		flower $tcflags mpls_ttl 0 action drop
	tc filter add dev $h2 ingress protocol mpls_uc pref 2 handle 102 \
		flower $tcflags mpls_ttl 255 action drop

	pkt="$ethtype $(mpls_lse 0 0 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched on a wrong filter (255)"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter (255)"

	pkt="$ethtype $(mpls_lse 0 0 1 0)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 102 2
	check_fail $? "Matched on a wrong filter (0)"

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match on correct filter (0)"

	tc filter del dev $h2 ingress protocol mpls_uc pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 1 handle 101 flower

	log_test "mpls_ttl match ($tcflags)"
}

match_mpls_lse_test()
{
	local ethtype="88 47"; readonly ethtype
	local pkt

	RET=0

	check_tc_mpls_lse_stats $h2 || return 0

	# Match on first LSE (minimal values for each field)
	tc filter add dev $h2 ingress protocol mpls_uc pref 1 handle 101 \
		flower $tcflags mpls lse depth 1 label 0 action continue
	tc filter add dev $h2 ingress protocol mpls_uc pref 2 handle 102 \
		flower $tcflags mpls lse depth 1 tc 0 action continue
	tc filter add dev $h2 ingress protocol mpls_uc pref 3 handle 103 \
		flower $tcflags mpls lse depth 1 bos 0 action continue
	tc filter add dev $h2 ingress protocol mpls_uc pref 4 handle 104 \
		flower $tcflags mpls lse depth 1 ttl 0 action continue

	# Match on second LSE (maximal values for each field)
	tc filter add dev $h2 ingress protocol mpls_uc pref 5 handle 105 \
		flower $tcflags mpls lse depth 2 label 1048575 action continue
	tc filter add dev $h2 ingress protocol mpls_uc pref 6 handle 106 \
		flower $tcflags mpls lse depth 2 tc 7 action continue
	tc filter add dev $h2 ingress protocol mpls_uc pref 7 handle 107 \
		flower $tcflags mpls lse depth 2 bos 1 action continue
	tc filter add dev $h2 ingress protocol mpls_uc pref 8 handle 108 \
		flower $tcflags mpls lse depth 2 ttl 255 action continue

	# Match on LSE depth
	tc filter add dev $h2 ingress protocol mpls_uc pref 9 handle 109 \
		flower $tcflags mpls lse depth 1 action continue
	tc filter add dev $h2 ingress protocol mpls_uc pref 10 handle 110 \
		flower $tcflags mpls lse depth 2 action continue
	tc filter add dev $h2 ingress protocol mpls_uc pref 11 handle 111 \
		flower $tcflags mpls lse depth 3 action continue

	# Base packet, matched by all filters (except for stack depth 3)
	pkt="$ethtype $(mpls_lse 0 0 0 0) $(mpls_lse 1048575 7 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	# Make a variant of the above packet, with a non-matching value
	# for each LSE field

	# Wrong label at depth 1
	pkt="$ethtype $(mpls_lse 1 0 0 0) $(mpls_lse 1048575 7 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	# Wrong TC at depth 1
	pkt="$ethtype $(mpls_lse 0 1 0 0) $(mpls_lse 1048575 7 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	# Wrong BOS at depth 1 (not adding a second LSE here since BOS is set
	# in the first label, so anything that'd follow wouldn't be considered)
	pkt="$ethtype $(mpls_lse 0 0 1 0)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	# Wrong TTL at depth 1
	pkt="$ethtype $(mpls_lse 0 0 0 1) $(mpls_lse 1048575 7 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	# Wrong label at depth 2
	pkt="$ethtype $(mpls_lse 0 0 0 0) $(mpls_lse 1048574 7 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	# Wrong TC at depth 2
	pkt="$ethtype $(mpls_lse 0 0 0 0) $(mpls_lse 1048575 6 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	# Wrong BOS at depth 2 (adding a third LSE here since BOS isn't set in
	# the second label)
	pkt="$ethtype $(mpls_lse 0 0 0 0) $(mpls_lse 1048575 7 0 255)"
	pkt="$pkt $(mpls_lse 0 0 1 255)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	# Wrong TTL at depth 2
	pkt="$ethtype $(mpls_lse 0 0 0 0) $(mpls_lse 1048575 7 1 254)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	# Filters working at depth 1 should match all packets but one

	tc_check_packets "dev $h2 ingress" 101 8
	check_err $? "Did not match on correct filter"

	tc_check_packets "dev $h2 ingress" 102 8
	check_err $? "Did not match on correct filter"

	tc_check_packets "dev $h2 ingress" 103 8
	check_err $? "Did not match on correct filter"

	tc_check_packets "dev $h2 ingress" 104 8
	check_err $? "Did not match on correct filter"

	# Filters working at depth 2 should match all packets but two (because
	# of the test packet where the label stack depth is just one)

	tc_check_packets "dev $h2 ingress" 105 7
	check_err $? "Did not match on correct filter"

	tc_check_packets "dev $h2 ingress" 106 7
	check_err $? "Did not match on correct filter"

	tc_check_packets "dev $h2 ingress" 107 7
	check_err $? "Did not match on correct filter"

	tc_check_packets "dev $h2 ingress" 108 7
	check_err $? "Did not match on correct filter"

	# Finally, verify the filters that only match on LSE depth

	tc_check_packets "dev $h2 ingress" 109 9
	check_err $? "Did not match on correct filter"

	tc_check_packets "dev $h2 ingress" 110 8
	check_err $? "Did not match on correct filter"

	tc_check_packets "dev $h2 ingress" 111 1
	check_err $? "Did not match on correct filter"

	tc filter del dev $h2 ingress protocol mpls_uc pref 11 handle 111 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 10 handle 110 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 9 handle 109 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 8 handle 108 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 7 handle 107 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 6 handle 106 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 5 handle 105 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 4 handle 104 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 3 handle 103 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol mpls_uc pref 1 handle 101 flower

	log_test "mpls lse match ($tcflags)"
}

match_erspan_opts_test()
{
	RET=0

	check_tc_erspan_support $h2 || return 0

	# h1 erspan setup
	tunnel_create erspan1 erspan 192.0.2.1 192.0.2.2 dev $h1 seq key 1001 \
		tos C ttl 64 erspan_ver 1 erspan 6789 # ERSPAN Type II
	tunnel_create erspan2 erspan 192.0.2.1 192.0.2.2 dev $h1 seq key 1002 \
		tos C ttl 64 erspan_ver 2 erspan_dir egress erspan_hwid 63 \
		# ERSPAN Type III
	ip link set dev erspan1 master v$h1
	ip link set dev erspan2 master v$h1
	# h2 erspan setup
	ip link add ep-ex type erspan ttl 64 external # To collect tunnel info
	ip link set ep-ex up
	ip link set dev ep-ex master v$h2
	tc qdisc add dev ep-ex clsact

	# ERSPAN Type II [decap direction]
	tc filter add dev ep-ex ingress protocol ip  handle 101 flower \
		$tcflags enc_src_ip 192.0.2.1 enc_dst_ip 192.0.2.2 \
		enc_key_id 1001 erspan_opts 1:6789:0:0 \
		action drop
	# ERSPAN Type III [decap direction]
	tc filter add dev ep-ex ingress protocol ip  handle 102 flower \
		$tcflags enc_src_ip 192.0.2.1 enc_dst_ip 192.0.2.2 \
		enc_key_id 1002 erspan_opts 2:0:1:63 action drop

	ep1mac=$(mac_get erspan1)
	$MZ erspan1 -c 1 -p 64 -a $ep1mac -b $h2mac -t ip -q
	tc_check_packets "dev ep-ex ingress" 101 1
	check_err $? "ERSPAN Type II"

	ep2mac=$(mac_get erspan2)
	$MZ erspan2 -c 1 -p 64 -a $ep1mac -b $h2mac -t ip -q
	tc_check_packets "dev ep-ex ingress" 102 1
	check_err $? "ERSPAN Type III"

	# h2 erspan cleanup
	tc qdisc del dev ep-ex clsact
	tunnel_destroy ep-ex
	# h1 erspan cleanup
	tunnel_destroy erspan2 # ERSPAN Type III
	tunnel_destroy erspan1 # ERSPAN Type II

	log_test "erspan_opts match ($tcflags)"
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
