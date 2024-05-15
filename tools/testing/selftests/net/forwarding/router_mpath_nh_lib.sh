# SPDX-License-Identifier: GPL-2.0

nh_stats_do_test()
{
	local what=$1; shift
	local nh1_id=$1; shift
	local nh2_id=$1; shift
	local group_id=$1; shift
	local stats_get=$1; shift
	local mz="$@"

	local dp

	RET=0

	sleep 2
	for ((dp=0; dp < 60000; dp += 10000)); do
		local dd
		local t0_rp12=$(link_stats_tx_packets_get $rp12)
		local t0_rp13=$(link_stats_tx_packets_get $rp13)
		local t0_nh1=$($stats_get $group_id $nh1_id)
		local t0_nh2=$($stats_get $group_id $nh2_id)

		ip vrf exec vrf-h1 \
			$mz -q -p 64 -d 0 -t udp \
				"sp=1024,dp=$((dp))-$((dp + 10000))"
		sleep 2

		local t1_rp12=$(link_stats_tx_packets_get $rp12)
		local t1_rp13=$(link_stats_tx_packets_get $rp13)
		local t1_nh1=$($stats_get $group_id $nh1_id)
		local t1_nh2=$($stats_get $group_id $nh2_id)

		local d_rp12=$((t1_rp12 - t0_rp12))
		local d_rp13=$((t1_rp13 - t0_rp13))
		local d_nh1=$((t1_nh1 - t0_nh1))
		local d_nh2=$((t1_nh2 - t0_nh2))

		dd=$(absval $((d_rp12 - d_nh1)))
		((dd < 10))
		check_err $? "Discrepancy between link and $stats_get: d_rp12=$d_rp12 d_nh1=$d_nh1"

		dd=$(absval $((d_rp13 - d_nh2)))
		((dd < 10))
		check_err $? "Discrepancy between link and $stats_get: d_rp13=$d_rp13 d_nh2=$d_nh2"
	done

	log_test "NH stats test $what"
}

nh_stats_test_dispatch_swhw()
{
	local what=$1; shift
	local nh1_id=$1; shift
	local nh2_id=$1; shift
	local group_id=$1; shift
	local mz="$@"

	nh_stats_do_test "$what" "$nh1_id" "$nh2_id" "$group_id" \
			 nh_stats_get "${mz[@]}"

	xfail_on_veth $rp11 \
		nh_stats_do_test "HW $what" "$nh1_id" "$nh2_id" "$group_id" \
				 nh_stats_get_hw "${mz[@]}"
}

nh_stats_test_dispatch()
{
	local nhgtype=$1; shift
	local what=$1; shift
	local nh1_id=$1; shift
	local nh2_id=$1; shift
	local group_id=$1; shift
	local mz="$@"

	local enabled

	if ! ip nexthop help 2>&1 | grep -q hw_stats; then
		log_test_skip "NH stats test: ip doesn't support HW stats"
		return
	fi

	ip nexthop replace id $group_id group $nh1_id/$nh2_id \
			   hw_stats on type $nhgtype
	enabled=$(ip -s -j -d nexthop show id $group_id |
		      jq '.[].hw_stats.enabled')
	if [[ $enabled == true ]]; then
		nh_stats_test_dispatch_swhw "$what" "$nh1_id" "$nh2_id" \
					    "$group_id" "${mz[@]}"
	elif [[ $enabled == false ]]; then
		check_err 1 "HW stats still disabled after enabling"
		log_test "NH stats test"
	else
		log_test_skip "NH stats test: ip doesn't report hw_stats info"
	fi

	ip nexthop replace id $group_id group $nh1_id/$nh2_id \
			   hw_stats off type $nhgtype
}

__nh_stats_test_v4()
{
	local nhgtype=$1; shift

	sysctl_set net.ipv4.fib_multipath_hash_policy 1
	nh_stats_test_dispatch $nhgtype "IPv4" 101 102 103 \
			       $MZ $h1 -A 192.0.2.2 -B 198.51.100.2
	sysctl_restore net.ipv4.fib_multipath_hash_policy
}

__nh_stats_test_v6()
{
	local nhgtype=$1; shift

	sysctl_set net.ipv6.fib_multipath_hash_policy 1
	nh_stats_test_dispatch $nhgtype "IPv6" 104 105 106 \
			       $MZ -6 $h1 -A 2001:db8:1::2 -B 2001:db8:2::2
	sysctl_restore net.ipv6.fib_multipath_hash_policy
}
