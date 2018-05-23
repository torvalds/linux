# SPDX-License-Identifier: GPL-2.0

mirror_install()
{
	local from_dev=$1; shift
	local direction=$1; shift
	local to_dev=$1; shift
	local filter=$1; shift

	tc filter add dev $from_dev $direction \
	   pref 1000 $filter \
	   action mirred egress mirror dev $to_dev
}

mirror_uninstall()
{
	local from_dev=$1; shift
	local direction=$1; shift

	tc filter del dev $swp1 $direction pref 1000
}

mirror_test()
{
	local vrf_name=$1; shift
	local sip=$1; shift
	local dip=$1; shift
	local dev=$1; shift
	local pref=$1; shift
	local expect=$1; shift

	local t0=$(tc_rule_stats_get $dev $pref)
	ip vrf exec $vrf_name \
	   ${PING} ${sip:+-I $sip} $dip -c 10 -i 0.1 -w 2 &> /dev/null
	local t1=$(tc_rule_stats_get $dev $pref)
	local delta=$((t1 - t0))
	# Tolerate a couple stray extra packets.
	((expect <= delta && delta <= expect + 2))
	check_err $? "Expected to capture $expect packets, got $delta."
}
