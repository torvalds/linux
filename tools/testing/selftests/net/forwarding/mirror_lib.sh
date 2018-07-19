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

do_test_span_dir_ips()
{
	local expect=$1; shift
	local dev=$1; shift
	local direction=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift

	icmp_capture_install $dev
	mirror_test v$h1 $ip1 $ip2 $dev 100 $expect
	mirror_test v$h2 $ip2 $ip1 $dev 100 $expect
	icmp_capture_uninstall $dev
}

quick_test_span_dir_ips()
{
	do_test_span_dir_ips 10 "$@"
}

fail_test_span_dir_ips()
{
	do_test_span_dir_ips 0 "$@"
}

test_span_dir_ips()
{
	local dev=$1; shift
	local direction=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift

	quick_test_span_dir_ips "$dev" "$direction" "$ip1" "$ip2"

	icmp_capture_install $dev "type $forward_type"
	mirror_test v$h1 $ip1 $ip2 $dev 100 10
	icmp_capture_uninstall $dev

	icmp_capture_install $dev "type $backward_type"
	mirror_test v$h2 $ip2 $ip1 $dev 100 10
	icmp_capture_uninstall $dev
}

fail_test_span_dir()
{
	fail_test_span_dir_ips "$@" 192.0.2.1 192.0.2.2
}

test_span_dir()
{
	test_span_dir_ips "$@" 192.0.2.1 192.0.2.2
}

do_test_span_vlan_dir_ips()
{
	local expect=$1; shift
	local dev=$1; shift
	local vid=$1; shift
	local direction=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift

	# Install the capture as skip_hw to avoid double-counting of packets.
	# The traffic is meant for local box anyway, so will be trapped to
	# kernel.
	vlan_capture_install $dev "skip_hw vlan_id $vid"
	mirror_test v$h1 $ip1 $ip2 $dev 100 $expect
	mirror_test v$h2 $ip2 $ip1 $dev 100 $expect
	vlan_capture_uninstall $dev
}

quick_test_span_vlan_dir_ips()
{
	do_test_span_vlan_dir_ips 10 "$@"
}

fail_test_span_vlan_dir_ips()
{
	do_test_span_vlan_dir_ips 0 "$@"
}

quick_test_span_vlan_dir()
{
	quick_test_span_vlan_dir_ips "$@" 192.0.2.1 192.0.2.2
}

fail_test_span_vlan_dir()
{
	fail_test_span_vlan_dir_ips "$@" 192.0.2.1 192.0.2.2
}
