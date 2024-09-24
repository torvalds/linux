# SPDX-License-Identifier: GPL-2.0

source "$net_forwarding_dir/mirror_lib.sh"

quick_test_span_gre_dir_ips()
{
	local tundev=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift

	do_test_span_dir_ips 10 h3-$tundev "$ip1" "$ip2" \
			     "$forward_type" "$backward_type"
}

fail_test_span_gre_dir_ips()
{
	local tundev=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift

	do_test_span_dir_ips 0 h3-$tundev "$ip1" "$ip2"
}

test_span_gre_dir_ips()
{
	local tundev=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift

	test_span_dir_ips h3-$tundev "$forward_type" \
			  "$backward_type" "$ip1" "$ip2"
}

full_test_span_gre_dir_ips()
{
	local tundev=$1; shift
	local direction=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift
	local what=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift

	RET=0

	mirror_install $swp1 $direction $tundev "matchall"
	test_span_dir_ips "h3-$tundev" "$forward_type" \
			  "$backward_type" "$ip1" "$ip2"
	mirror_uninstall $swp1 $direction

	log_test "$direction $what"
}

full_test_span_gre_dir_vlan_ips()
{
	local tundev=$1; shift
	local direction=$1; shift
	local vlan_match=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift
	local what=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift

	RET=0

	mirror_install $swp1 $direction $tundev "matchall"

	test_span_dir_ips "h3-$tundev" "$forward_type" \
			  "$backward_type" "$ip1" "$ip2"

	tc filter add dev $h3 ingress pref 77 prot 802.1q \
		flower $vlan_match \
		action pass
	mirror_test v$h1 $ip1 $ip2 $h3 77 '>= 10'
	tc filter del dev $h3 ingress pref 77

	mirror_uninstall $swp1 $direction

	log_test "$direction $what"
}

quick_test_span_gre_dir()
{
	local tundev=$1; shift
	local forward_type=${1-8}; shift
	local backward_type=${1-0}; shift

	quick_test_span_gre_dir_ips "$tundev" 192.0.2.1 192.0.2.2 \
				    "$forward_type" "$backward_type"
}

fail_test_span_gre_dir()
{
	local tundev=$1; shift

	fail_test_span_gre_dir_ips "$tundev" 192.0.2.1 192.0.2.2
}

full_test_span_gre_dir()
{
	local tundev=$1; shift
	local direction=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift
	local what=$1; shift

	full_test_span_gre_dir_ips "$tundev" "$direction" "$forward_type" \
				   "$backward_type" "$what" 192.0.2.1 192.0.2.2
}

full_test_span_gre_dir_vlan()
{
	local tundev=$1; shift
	local direction=$1; shift
	local vlan_match=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift
	local what=$1; shift

	full_test_span_gre_dir_vlan_ips "$tundev" "$direction" "$vlan_match" \
					"$forward_type" "$backward_type" \
					"$what" 192.0.2.1 192.0.2.2
}

full_test_span_gre_stp_ips()
{
	local tundev=$1; shift
	local nbpdev=$1; shift
	local what=$1; shift
	local ip1=$1; shift
	local ip2=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift
	local h3mac=$(mac_get $h3)

	RET=0

	mirror_install $swp1 ingress $tundev "matchall"
	quick_test_span_gre_dir_ips $tundev $ip1 $ip2 \
				    "$forward_type" "$backward_type"

	bridge link set dev $nbpdev state disabled
	sleep 1
	fail_test_span_gre_dir_ips $tundev $ip1 $ip2

	bridge link set dev $nbpdev state forwarding
	sleep 1
	quick_test_span_gre_dir_ips $tundev $ip1 $ip2 \
				    "$forward_type" "$backward_type"

	mirror_uninstall $swp1 ingress

	log_test "$what: STP state"
}

full_test_span_gre_stp()
{
	local tundev=$1; shift
	local nbpdev=$1; shift
	local what=$1; shift
	local forward_type=${1-8}; shift
	local backward_type=${1-0}; shift

	full_test_span_gre_stp_ips "$tundev" "$nbpdev" "$what" \
				   192.0.2.1 192.0.2.2 \
				   "$forward_type" "$backward_type"
}
