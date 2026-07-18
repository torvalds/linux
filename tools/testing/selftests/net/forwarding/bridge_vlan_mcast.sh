#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="vlmc_control_test vlmc_querier_test vlmc_igmp_mld_version_test \
	   vlmc_last_member_test vlmc_startup_query_test vlmc_membership_test \
	   vlmc_querier_intvl_test vlmc_query_intvl_test vlmc_query_response_intvl_test \
	   vlmc_router_port_test vlmc_filtering_test vlmc_mcast_toggle_test"
NUM_NETIFS=4
CHECK_TC="yes"
TEST_GROUP="239.10.10.10"

source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 2001:db8:1::1/64
	ip link add l $h1 $h1.10 up type vlan id 10
}

h1_destroy()
{
	ip link del $h1.10
	simple_if_fini $h1 192.0.2.1/24 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/24 2001:db8:1::2/64
	ip link add l $h2 $h2.10 up type vlan id 10
}

h2_destroy()
{
	ip link del $h2.10
	simple_if_fini $h2 192.0.2.2/24 2001:db8:1::2/64
}

switch_create()
{
	ip link add dev br0 type bridge mcast_snooping 1 mcast_querier 1 vlan_filtering 1

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br0

	ip link set dev br0 up
	ip link set dev $swp1 up
	ip link set dev $swp2 up

	tc qdisc add dev $swp1 clsact
	tc qdisc add dev $swp2 clsact

	bridge vlan add vid 10-11 dev $swp1 master
	bridge vlan add vid 10-11 dev $swp2 master

	ip link set dev br0 type bridge mcast_vlan_snooping 1
	check_err $? "Could not enable global vlan multicast snooping"
	log_test "Vlan multicast snooping enable"
}

switch_destroy()
{
	tc qdisc del dev $swp2 clsact
	tc qdisc del dev $swp1 clsact

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

vlmc_v2join_test()
{
	local expect=$1

	RET=0
	ip address add dev $h2.10 $TEST_GROUP/32 autojoin
	check_err $? "Could not join $TEST_GROUP"

	sleep 5
	bridge -j mdb show dev br0 |
		jq -e ".[].mdb[] | select(.grp == \"$TEST_GROUP\" and .vid == 10)" &>/dev/null
	if [ $expect -eq 0 ]; then
		check_err $? "IGMPv2 report didn't create mdb entry for $TEST_GROUP"
	else
		check_fail $? "IGMPv2 report shouldn't have created mdb entry for $TEST_GROUP"
	fi

	# check if we need to cleanup
	if [ $RET -eq 0 ]; then
		ip address del dev $h2.10 $TEST_GROUP/32 2>&1 1>/dev/null
		sleep 5
		bridge -j mdb show dev br0 |
			jq -e ".[].mdb[] | select(.grp == \"$TEST_GROUP\" and \
						  .vid == 10)" &>/dev/null
		check_fail $? "IGMPv2 leave didn't remove mdb entry for $TEST_GROUP"
	fi
}

vlmc_control_test()
{
	RET=0
	local goutput=`bridge -j vlan global show`
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10)" &>/dev/null
	check_err $? "Could not find vlan 10's global options"
	log_test "Vlan global options existence"

	RET=0
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and .mcast_snooping == 1) " &>/dev/null
	check_err $? "Wrong default mcast_snooping global option value"
	log_test "Vlan mcast_snooping global option default value"

	RET=0
	vlmc_v2join_test 0
	bridge vlan global set vid 10 dev br0 mcast_snooping 0
	check_err $? "Could not disable multicast snooping in vlan 10"
	vlmc_v2join_test 1
	log_test "Vlan 10 multicast snooping control"
}

# setup for general query counting
vlmc_query_cnt_xstats()
{
	local type=$1
	local version=$2
	local dev=$3

	ip -j link xstats type bridge_slave dev $dev | \
	jq -e ".[].multicast.${type}_queries.tx_v${version}"
}

vlmc_query_cnt_setup()
{
	local type=$1
	local dev=$2
	local match=($3)

	if [[ $type == "igmp" ]]; then
		# This matches: IP Protocol 2 (IGMP)
		tc filter add dev "$dev" egress pref 10 prot 802.1Q \
			flower vlan_id 10 vlan_ethtype ipv4 dst_ip 224.0.0.1 ip_proto 2 \
			action goto chain 1
		# AND Type 0x11 (Query) at offset 0 of IGMP header
		# 20 bytes IPv4 header + 4 bytes Router Alert option + IGMP[offset 0]
		tc filter add dev "$dev" egress pref 20 chain 1 prot 802.1Q u32 \
			match u8 0x11 0xff at 24 "${match[@]}" \
			action pass
	else
		# This matches: ICMPv6
		tc filter add dev "$dev" egress pref 10 prot 802.1Q \
			flower vlan_id 10 vlan_ethtype ipv6 dst_ip ff02::1 ip_proto icmpv6 \
			action goto chain 1
		# AND Type 0x82 (Query) at offset 0 of MLD header
		# 40 bytes IPv6 header + 8 bytes Hop-by-hop option + MLD[offset 0]
		tc filter add dev "$dev" egress pref 20 chain 1 prot 802.1Q u32 \
			match u8 0x82 0xff at 48 "${match[@]}" \
			action pass
	fi

	ip link set dev br0 type bridge mcast_stats_enabled 1
}

vlmc_query_cnt_cleanup()
{
	local dev=$1

	ip link set dev br0 type bridge mcast_stats_enabled 0
	tc filter del dev "$dev" egress pref 20 chain 1
	tc filter del dev "$dev" egress pref 10
}

vlmc_query_get_intvl_match()
{
	local type=$1
	local version=$2
	local test=$3
	local enc_val=$4

	if [ "$test" = "qqic" ]; then
		# QQIC is 8-bit floating point encoding for IGMPv3 and MLDv2
		if [ "${type}v${version}" = "igmpv3" ]; then
			# QQIC is at offset 9 of IGMP header
			# 20 bytes IPv4 header + 4 bytes Router Alert option + IGMP[offset 9]
			echo "match u8 $enc_val 0xff at 33"
		elif [ "${type}v${version}" = "mldv2" ]; then
			# QQIC is at offset 25 of MLD header
			# 40 bytes IPv6 header + 8 bytes Hop-by-hop option + MLD[offset 25]
			echo "match u8 $enc_val 0xff at 73"
		fi
	elif [ "$test" = "mrc" ]; then
		if [ "${type}v${version}" = "igmpv3" ]; then
			# MRC is 8-bit floating point encoding at offset 1 of IGMP header
			# 20 bytes IPv4 header + 4 bytes Router Alert option + IGMP[offset 1]
			echo "match u8 $enc_val 0xff at 25"
		elif [ "${type}v${version}" = "mldv2" ]; then
			# MRC is 16-bit floating point encoding at offset 4 of MLD header
			# 40 bytes IPv6 header + 8 bytes Hop-by-hop option + MLD[offset 4]
			echo "match u16 $enc_val 0xffff at 52"
		fi
	fi
}

vlmc_check_query()
{
	local type=$1
	local version=$2
	local dev=$3
	local expect=$4
	local time=$5
	local test=$6
	local enc_val=$7
	local intvl_match=""
	local ret=0

	intvl_match="$(vlmc_query_get_intvl_match "$type" "$version" "$test" "$enc_val")"
	vlmc_query_cnt_setup "$type" "$dev" "$intvl_match"

	local pre_tx_xstats=$(vlmc_query_cnt_xstats $type $version $dev)
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_querier 1
	ret=$?
	if [[ $ret -eq 0 ]]; then
		sleep $time

		local tcstats=$(tc_rule_stats_get "$dev" 20 egress)
		local post_tx_xstats=$(vlmc_query_cnt_xstats $type $version $dev)

		if [[ $tcstats != $expect || \
		      $(($post_tx_xstats-$pre_tx_xstats)) != $expect || \
		      $tcstats != $(($post_tx_xstats-$pre_tx_xstats)) ]]; then
			ret=1
		fi
	fi

	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_querier 0
	vlmc_query_cnt_cleanup $dev

	return $ret
}

vlmc_querier_test()
{
	RET=0
	local goutput=`bridge -j vlan global show`
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10)" &>/dev/null
	check_err $? "Could not find vlan 10's global options"

	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and .mcast_querier == 0) " &>/dev/null
	check_err $? "Wrong default mcast_querier global vlan option value"
	log_test "Vlan mcast_querier global option default value"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_querier 1
	check_err $? "Could not enable querier in vlan 10"
	log_test "Vlan 10 multicast querier enable"
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_querier 0

	RET=0
	vlmc_check_query igmp 2 $swp1 1 1
	check_err $? "No vlan tagged IGMPv2 general query packets sent"
	log_test "Vlan 10 tagged IGMPv2 general query sent"

	RET=0
	vlmc_check_query mld 1 $swp1 1 1
	check_err $? "No vlan tagged MLD general query packets sent"
	log_test "Vlan 10 tagged MLD general query sent"
}

vlmc_igmp_mld_version_test()
{
	RET=0
	local goutput=`bridge -j vlan global show`
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10)" &>/dev/null
	check_err $? "Could not find vlan 10's global options"

	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and .mcast_igmp_version == 2) " &>/dev/null
	check_err $? "Wrong default mcast_igmp_version global vlan option value"
	log_test "Vlan mcast_igmp_version global option default value"

	RET=0
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and .mcast_mld_version == 1) " &>/dev/null
	check_err $? "Wrong default mcast_mld_version global vlan option value"
	log_test "Vlan mcast_mld_version global option default value"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_igmp_version 3
	check_err $? "Could not set mcast_igmp_version in vlan 10"
	log_test "Vlan 10 mcast_igmp_version option changed to 3"

	RET=0
	vlmc_check_query igmp 3 $swp1 1 1
	check_err $? "No vlan tagged IGMPv3 general query packets sent"
	log_test "Vlan 10 tagged IGMPv3 general query sent"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_mld_version 2
	check_err $? "Could not set mcast_mld_version in vlan 10"
	log_test "Vlan 10 mcast_mld_version option changed to 2"

	RET=0
	vlmc_check_query mld 2 $swp1 1 1
	check_err $? "No vlan tagged MLDv2 general query packets sent"
	log_test "Vlan 10 tagged MLDv2 general query sent"

	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_igmp_version 2
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_mld_version 1
}

vlmc_last_member_test()
{
	RET=0
	local goutput=`bridge -j vlan global show`
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10)" &>/dev/null
	check_err $? "Could not find vlan 10's global options"

	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and \
					    .mcast_last_member_count == 2) " &>/dev/null
	check_err $? "Wrong default mcast_last_member_count global vlan option value"
	log_test "Vlan mcast_last_member_count global option default value"

	RET=0
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and \
					    .mcast_last_member_interval == 100) " &>/dev/null
	check_err $? "Wrong default mcast_last_member_interval global vlan option value"
	log_test "Vlan mcast_last_member_interval global option default value"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_last_member_count 3
	check_err $? "Could not set mcast_last_member_count in vlan 10"
	log_test "Vlan 10 mcast_last_member_count option changed to 3"
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_last_member_count 2

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_last_member_interval 200
	check_err $? "Could not set mcast_last_member_interval in vlan 10"
	log_test "Vlan 10 mcast_last_member_interval option changed to 200"
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_last_member_interval 100
}

vlmc_startup_query_test()
{
	RET=0
	local goutput=`bridge -j vlan global show`
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10)" &>/dev/null
	check_err $? "Could not find vlan 10's global options"

	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and \
					    .mcast_startup_query_interval == 3125) " &>/dev/null
	check_err $? "Wrong default mcast_startup_query_interval global vlan option value"
	log_test "Vlan mcast_startup_query_interval global option default value"

	RET=0
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and \
					    .mcast_startup_query_count == 2) " &>/dev/null
	check_err $? "Wrong default mcast_startup_query_count global vlan option value"
	log_test "Vlan mcast_startup_query_count global option default value"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_startup_query_interval 100
	check_err $? "Could not set mcast_startup_query_interval in vlan 10"
	vlmc_check_query igmp 2 $swp1 2 3
	check_err $? "Wrong number of tagged IGMPv2 general queries sent"
	log_test "Vlan 10 mcast_startup_query_interval option changed to 100"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_startup_query_count 3
	check_err $? "Could not set mcast_startup_query_count in vlan 10"
	vlmc_check_query igmp 2 $swp1 3 4
	check_err $? "Wrong number of tagged IGMPv2 general queries sent"
	log_test "Vlan 10 mcast_startup_query_count option changed to 3"

	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_startup_query_interval 3125
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_startup_query_count 2
}

vlmc_membership_test()
{
	RET=0
	local goutput=`bridge -j vlan global show`
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10)" &>/dev/null
	check_err $? "Could not find vlan 10's global options"

	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and \
					    .mcast_membership_interval == 26000) " &>/dev/null
	check_err $? "Wrong default mcast_membership_interval global vlan option value"
	log_test "Vlan mcast_membership_interval global option default value"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_membership_interval 200
	check_err $? "Could not set mcast_membership_interval in vlan 10"
	log_test "Vlan 10 mcast_membership_interval option changed to 200"

	RET=0
	vlmc_v2join_test 1
	log_test "Vlan 10 mcast_membership_interval mdb entry expire"

	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_membership_interval 26000
}

vlmc_querier_intvl_test()
{
	RET=0
	local goutput=`bridge -j vlan global show`
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10)" &>/dev/null
	check_err $? "Could not find vlan 10's global options"

	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and \
					    .mcast_querier_interval == 25500) " &>/dev/null
	check_err $? "Wrong default mcast_querier_interval global vlan option value"
	log_test "Vlan mcast_querier_interval global option default value"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_querier_interval 100
	check_err $? "Could not set mcast_querier_interval in vlan 10"
	log_test "Vlan 10 mcast_querier_interval option changed to 100"

	RET=0
	ip link add dev br1 type bridge mcast_snooping 1 mcast_querier 1 vlan_filtering 1 \
					mcast_vlan_snooping 1
	bridge vlan add vid 10 dev br1 self pvid untagged
	ip link set dev $h1 master br1
	ip link set dev br1 up
	setup_wait_dev $h1 0
	bridge vlan add vid 10 dev $h1 master
	bridge vlan global set vid 10 dev br1 mcast_snooping 1 mcast_querier 1
	sleep 2
	ip link del dev br1
	ip addr replace 2001:db8:1::1/64 dev $h1
	vlmc_check_query igmp 2 $swp1 1 1
	check_err $? "Wrong number of IGMPv2 general queries after querier interval"
	log_test "Vlan 10 mcast_querier_interval expire after outside query"

	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_querier_interval 25500
}

vlmc_query_intvl_test()
{
	RET=0
	local goutput=`bridge -j vlan global show`
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10)" &>/dev/null
	check_err $? "Could not find vlan 10's global options"

	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and \
					    .mcast_query_interval == 12500) " &>/dev/null
	check_err $? "Wrong default mcast_query_interval global vlan option value"
	log_test "Vlan mcast_query_interval global option default value"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_startup_query_count 0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_query_interval 200
	check_err $? "Could not set mcast_query_interval in vlan 10"
	# 1 is sent immediately, then 2 more in the next 5 seconds
	vlmc_check_query igmp 2 $swp1 3 5
	check_err $? "Wrong number of tagged IGMPv2 general queries sent"
	log_test "Number of tagged IGMPv2 general query"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_igmp_version 3
	check_err $? "Could not set mcast_igmp_version in vlan 10"
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_mld_version 2
	check_err $? "Could not set mcast_mld_version in vlan 10"
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_query_interval 6000
	check_err $? "Could not set mcast_query_interval in vlan 10"
	# 1 is sent immediately, IGMPv3 QQIC should match with linear value 60 (0x3c)
	# which is 8-bit encoded value of 60 [units of seconds]
	vlmc_check_query igmp 3 $swp1 1 1 qqic 0x3c
	check_err $? "Wrong QQIC in generated IGMPv3 general queries"
	log_test "IGMPv3 QQIC linear value 60(s)"

	RET=0
	# 1 is sent immediately, MLDv2 QQIC should match with linear value 60 (0x3c)
	# which is 8-bit encoded value of 60 [units of seconds]
	vlmc_check_query mld 2 $swp1 1 1 qqic 0x3c
	check_err $? "Wrong QQIC in generated MLDv2 general queries"
	log_test "MLDv2 QQIC linear value 60(s)"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_query_interval 16000
	check_err $? "Could not set mcast_query_interval in vlan 10"
	# 1 is sent immediately, IGMPv3 QQIC should match with non linear value 132 (0x84)
	# which is 8-bit encoded value of 160 [units of seconds]
	vlmc_check_query igmp 3 $swp1 1 1 qqic 0x84
	check_err $? "Wrong QQIC in generated IGMPv3 general queries"
	log_test "IGMPv3 QQIC non linear value 160(s)"

	RET=0
	# 1 is sent immediately, MLDv2 QQIC should match with non linear value 132 (0x84)
	# which is 8-bit encoded value of 160 [units of seconds]
	vlmc_check_query mld 2 $swp1 1 1 qqic 0x84
	check_err $? "Wrong QQIC in generated MLDv2 general queries"
	log_test "MLDv2 QQIC non linear value 160(s)"

	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_igmp_version 2
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_mld_version 1
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_startup_query_count 2
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_query_interval 12500
}

vlmc_query_response_intvl_test()
{
	RET=0
	local goutput=`bridge -j vlan global show`
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10)" &>/dev/null
	check_err $? "Could not find vlan 10's global options"

	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and \
					    .mcast_query_response_interval == 1000) " &>/dev/null
	check_err $? "Wrong default mcast_query_response_interval global vlan option value"
	log_test "Vlan mcast_query_response_interval global option default value"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_startup_query_count 0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_igmp_version 3
	check_err $? "Could not set mcast_igmp_version in vlan 10"
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_mld_version 2
	check_err $? "Could not set mcast_mld_version in vlan 10"
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_query_response_interval 600
	check_err $? "Could not set mcast_query_response_interval in vlan 10"
	# 1 is sent immediately, IGMPv3 MRC should match with linear value 60 (0x3c)
	# which is 8-bit encoded value of 60 [units of 0.1s = 6 seconds]
	vlmc_check_query igmp 3 $swp1 1 1 mrc 0x3c
	check_err $? "Wrong MRC in generated IGMPv3 general queries"
	log_test "IGMPv3 MRC linear value of 60(x0.1s)"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_query_response_interval 2400
	check_err $? "Could not set mcast_query_response_interval in vlan 10"
	# 1 is sent immediately, MLDv2 MRC should match with linear value 0x5dc0 (24000)
	# which is 16-bit encoded value of 24000 [units of ms / 24 seconds]
	vlmc_check_query mld 2 $swp1 1 1 mrc 0x5dc0
	check_err $? "Wrong MRC in generated MLDv2 general queries"
	log_test "MLDv2 MRC linear value of 24000(ms)"

	RET=0
	# 1 is sent immediately, IGMPv3 MRC should match with non linear value 142 (0x8e)
	# which is 8-bit encoded value of 240 [units of 0.1s = 24 seconds]
	vlmc_check_query igmp 3 $swp1 1 1 mrc 0x8e
	check_err $? "Wrong MRC in generated IGMPv3 general queries"
	log_test "IGMPv3 MRC non linear value of 240(x0.1s)"

	RET=0
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_query_response_interval 4800
	check_err $? "Could not set mcast_query_response_interval in vlan 10"
	# 1 is sent immediately, MLDv2 MRC should match with non linear value 0x8770 (34672)
	# which is 16-bit encoded value of 48000 [units of ms / 48 seconds]
	vlmc_check_query mld 2 $swp1 1 1 mrc 0x8770
	check_err $? "Wrong MRC in generated MLDv2 general queries"
	log_test "MLDv2 MRC non linear value of 48000(ms)"

	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_igmp_version 2
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_mld_version 1
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_startup_query_count 2
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_query_response_interval 1000
}

vlmc_router_port_test()
{
	RET=0
	local goutput=`bridge -j -d vlan show`
	echo -n $goutput |
		jq -e ".[] | select(.ifname == \"$swp1\" and \
				    .vlans[].vlan == 10)" &>/dev/null
	check_err $? "Could not find port vlan 10's options"

	echo -n $goutput |
		jq -e ".[] | select(.ifname == \"$swp1\" and \
				    .vlans[].vlan == 10 and \
				    .vlans[].mcast_router == 1)" &>/dev/null
	check_err $? "Wrong default port mcast_router option value"
	log_test "Port vlan 10 option mcast_router default value"

	RET=0
	bridge vlan set vid 10 dev $swp1 mcast_router 2
	check_err $? "Could not set port vlan 10's mcast_router option"
	log_test "Port vlan 10 mcast_router option changed to 2"

	RET=0
	tc filter add dev $swp1 egress pref 10 prot 802.1Q \
		flower vlan_id 10 vlan_ethtype ipv4 dst_ip 239.1.1.1 ip_proto udp action pass
	tc filter add dev $swp2 egress pref 10 prot 802.1Q \
		flower vlan_id 10 vlan_ethtype ipv4 dst_ip 239.1.1.1 ip_proto udp action pass
	bridge vlan set vid 10 dev $swp2 mcast_router 0
	# we need to enable querier and disable query response interval to
	# make sure packets are flooded only to router ports
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_querier 1 \
					      mcast_query_response_interval 0
	bridge vlan add vid 10 dev br0 self
	sleep 1
	mausezahn br0 -Q 10 -c 10 -p 128 -b 01:00:5e:01:01:01 -B 239.1.1.1 \
			-t udp "dp=1024" &>/dev/null
	local swp1_tcstats=$(tc_rule_stats_get $swp1 10 egress)
	if [[ $swp1_tcstats != 10 ]]; then
		check_err 1 "Wrong number of vlan 10 multicast packets flooded"
	fi
	local swp2_tcstats=$(tc_rule_stats_get $swp2 10 egress)
	check_err $swp2_tcstats "Vlan 10 multicast packets flooded to non-router port"
	log_test "Flood unknown vlan multicast packets to router port only"

	tc filter del dev $swp2 egress pref 10
	tc filter del dev $swp1 egress pref 10
	bridge vlan del vid 10 dev br0 self
	bridge vlan global set vid 10 dev br0 mcast_snooping 1 mcast_query_response_interval 1000
	bridge vlan set vid 10 dev $swp2 mcast_router 1
	bridge vlan set vid 10 dev $swp1 mcast_router 1
}

vlmc_filtering_test()
{
	RET=0
	ip link set dev br0 type bridge vlan_filtering 0
	ip -j -d link show dev br0 | \
	jq -e "select(.[0].linkinfo.info_data.mcast_vlan_snooping == 1)" &>/dev/null
	check_fail $? "Vlan filtering is disabled but multicast vlan snooping is still enabled"
	log_test "Disable multicast vlan snooping when vlan filtering is disabled"
}

vlmc_mcast_toggle_test()
{
	RET=0

	ip link add name br1-mcast up type bridge mcast_snooping 1 mcast_querier 1 vlan_filtering 1
	ip link add name dummy1-mcast up master br1-mcast type dummy

	# Enabling per-VLAN multicast snooping should disable the per-port
	# multicast context on "dummy1-mcast".
	ip link set dev br1-mcast type bridge mcast_vlan_snooping 1

	# Toggling multicast snooping on the bridge should not affect the
	# per-port multicast context on "dummy1-mcast" given that per-VLAN
	# multicast snooping is enabled.
	ip link set dev br1-mcast type bridge mcast_snooping 0
	ip link set dev br1-mcast type bridge mcast_snooping 1

	# If both the per-port and per-{port, VLAN} multicast contexts are
	# enabled on "dummy1-mcast", removing it from the bridge will result
	# in a splat.
	ip link set dev dummy1-mcast nomaster

	log_test "Toggling mcast snooping with per-VLAN mcast snooping enabled"

	ip link del dev dummy1-mcast
	ip link del dev br1-mcast
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
