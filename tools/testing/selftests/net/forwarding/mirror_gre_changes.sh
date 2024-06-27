#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test uses standard topology for testing gretap. See
# mirror_gre_topo_lib.sh for more details.
#
# Test how mirrors to gretap and ip6gretap react to changes to relevant
# configuration.

ALL_TESTS="
	test_ttl
	test_tun_up
	test_egress_up
	test_remote_ip
	test_tun_del
	test_route_del
"

NUM_NETIFS=6
source lib.sh
source mirror_lib.sh
source mirror_gre_lib.sh
source mirror_gre_topo_lib.sh

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	vrf_prepare
	mirror_gre_topo_create

	# This test downs $swp3, which deletes the configured IPv6 address
	# unless this sysctl is set.
	sysctl_set net.ipv6.conf.$swp3.keep_addr_on_down 1

	ip address add dev $swp3 192.0.2.129/28
	ip address add dev $h3 192.0.2.130/28

	ip address add dev $swp3 2001:db8:2::1/64
	ip address add dev $h3 2001:db8:2::2/64
}

cleanup()
{
	pre_cleanup

	ip address del dev $h3 2001:db8:2::2/64
	ip address del dev $swp3 2001:db8:2::1/64

	ip address del dev $h3 192.0.2.130/28
	ip address del dev $swp3 192.0.2.129/28

	sysctl_restore net.ipv6.conf.$swp3.keep_addr_on_down

	mirror_gre_topo_destroy
	vrf_cleanup
}

test_span_gre_ttl()
{
	local tundev=$1; shift
	local type=$1; shift
	local prot=$1; shift
	local what=$1; shift

	RET=0

	mirror_install $swp1 ingress $tundev \
		"prot ip flower ip_prot icmp"
	tc filter add dev $h3 ingress pref 77 prot $prot \
		flower skip_hw ip_ttl 50 action pass

	mirror_test v$h1 192.0.2.1 192.0.2.2 $h3 77 0

	ip link set dev $tundev type $type ttl 50
	sleep 2
	mirror_test v$h1 192.0.2.1 192.0.2.2 $h3 77 ">= 10"

	ip link set dev $tundev type $type ttl 100
	tc filter del dev $h3 ingress pref 77
	mirror_uninstall $swp1 ingress

	log_test "$what: TTL change"
}

test_span_gre_tun_up()
{
	local tundev=$1; shift
	local what=$1; shift

	RET=0

	ip link set dev $tundev down
	mirror_install $swp1 ingress $tundev "matchall"
	fail_test_span_gre_dir $tundev

	ip link set dev $tundev up

	quick_test_span_gre_dir $tundev
	mirror_uninstall $swp1 ingress

	log_test "$what: tunnel down/up"
}

test_span_gre_egress_up()
{
	local tundev=$1; shift
	local remote_ip=$1; shift
	local what=$1; shift

	RET=0

	ip link set dev $swp3 down
	mirror_install $swp1 ingress $tundev "matchall"
	fail_test_span_gre_dir $tundev

	# After setting the device up, wait for neighbor to get resolved so that
	# we can expect mirroring to work.
	ip link set dev $swp3 up
	setup_wait_dev $swp3
	ping -c 1 -I $swp3 $remote_ip &>/dev/null

	quick_test_span_gre_dir $tundev
	mirror_uninstall $swp1 ingress

	log_test "$what: egress down/up"
}

test_span_gre_remote_ip()
{
	local tundev=$1; shift
	local type=$1; shift
	local correct_ip=$1; shift
	local wrong_ip=$1; shift
	local what=$1; shift

	RET=0

	ip link set dev $tundev type $type remote $wrong_ip
	mirror_install $swp1 ingress $tundev "matchall"
	fail_test_span_gre_dir $tundev

	ip link set dev $tundev type $type remote $correct_ip
	quick_test_span_gre_dir $tundev
	mirror_uninstall $swp1 ingress

	log_test "$what: remote address change"
}

test_span_gre_tun_del()
{
	local tundev=$1; shift
	local type=$1; shift
	local flags=$1; shift
	local local_ip=$1; shift
	local remote_ip=$1; shift
	local what=$1; shift

	RET=0

	mirror_install $swp1 ingress $tundev "matchall"
	quick_test_span_gre_dir $tundev
	ip link del dev $tundev
	fail_test_span_gre_dir $tundev

	tunnel_create $tundev $type $local_ip $remote_ip \
		      ttl 100 tos inherit $flags

	# Recreating the tunnel doesn't reestablish mirroring, so reinstall it
	# and verify it works for the follow-up tests.
	mirror_uninstall $swp1 ingress
	mirror_install $swp1 ingress $tundev "matchall"
	quick_test_span_gre_dir $tundev
	mirror_uninstall $swp1 ingress

	log_test "$what: tunnel deleted"
}

test_span_gre_route_del()
{
	local tundev=$1; shift
	local edev=$1; shift
	local route=$1; shift
	local what=$1; shift

	RET=0

	mirror_install $swp1 ingress $tundev "matchall"
	quick_test_span_gre_dir $tundev

	ip route del $route dev $edev
	fail_test_span_gre_dir $tundev

	ip route add $route dev $edev
	quick_test_span_gre_dir $tundev

	mirror_uninstall $swp1 ingress

	log_test "$what: underlay route removal"
}

test_ttl()
{
	test_span_gre_ttl gt4 gretap ip "mirror to gretap"
	test_span_gre_ttl gt6 ip6gretap ipv6 "mirror to ip6gretap"
}

test_tun_up()
{
	test_span_gre_tun_up gt4 "mirror to gretap"
	test_span_gre_tun_up gt6 "mirror to ip6gretap"
}

test_egress_up()
{
	test_span_gre_egress_up gt4 192.0.2.130 "mirror to gretap"
	test_span_gre_egress_up gt6 2001:db8:2::2 "mirror to ip6gretap"
}

test_remote_ip()
{
	test_span_gre_remote_ip gt4 gretap 192.0.2.130 192.0.2.132 "mirror to gretap"
	test_span_gre_remote_ip gt6 ip6gretap 2001:db8:2::2 2001:db8:2::4 "mirror to ip6gretap"
}

test_tun_del()
{
	test_span_gre_tun_del gt4 gretap "" \
			      192.0.2.129 192.0.2.130 "mirror to gretap"
	test_span_gre_tun_del gt6 ip6gretap allow-localremote \
			      2001:db8:2::1 2001:db8:2::2 "mirror to ip6gretap"
}

test_route_del()
{
	test_span_gre_route_del gt4 $swp3 192.0.2.128/28 "mirror to gretap"
	test_span_gre_route_del gt6 $swp3 2001:db8:2::/64 "mirror to ip6gretap"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
