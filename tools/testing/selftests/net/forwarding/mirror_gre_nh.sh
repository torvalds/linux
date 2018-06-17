#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test uses standard topology for testing gretap. See
# mirror_gre_topo_lib.sh for more details.
#
# Test that gretap and ip6gretap mirroring works when the other tunnel endpoint
# is reachable through a next-hop route (as opposed to directly-attached route).

ALL_TESTS="
	test_gretap
	test_ip6gretap
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

	sysctl_set net.ipv4.conf.all.rp_filter 0
	sysctl_set net.ipv4.conf.$h3.rp_filter 0

	vrf_prepare
	mirror_gre_topo_create

	ip address add dev $swp3 192.0.2.161/28
	ip address add dev $h3 192.0.2.162/28
	ip address add dev gt4 192.0.2.129/32
	ip address add dev h3-gt4 192.0.2.130/32

	# IPv6 route can't be added after address. Such routes are rejected due
	# to the gateway address having been configured on the local system. It
	# works the other way around though.
	ip address add dev $swp3 2001:db8:4::1/64
	ip -6 route add 2001:db8:2::2/128 via 2001:db8:4::2
	ip address add dev $h3 2001:db8:4::2/64
	ip address add dev gt6 2001:db8:2::1
	ip address add dev h3-gt6 2001:db8:2::2
}

cleanup()
{
	pre_cleanup

	ip -6 route del 2001:db8:2::2/128 via 2001:db8:4::2
	ip address del dev $h3 2001:db8:4::2/64
	ip address del dev $swp3 2001:db8:4::1/64

	ip address del dev $h3 192.0.2.162/28
	ip address del dev $swp3 192.0.2.161/28

	mirror_gre_topo_destroy
	vrf_cleanup

	sysctl_restore net.ipv4.conf.$h3.rp_filter
	sysctl_restore net.ipv4.conf.all.rp_filter
}

test_gretap()
{
	RET=0
	mirror_install $swp1 ingress gt4 "matchall $tcflags"

	# For IPv4, test that there's no mirroring without the route directing
	# the traffic to tunnel remote address. Then add it and test that
	# mirroring starts. For IPv6 we can't test this due to the limitation
	# that routes for locally-specified IPv6 addresses can't be added.
	fail_test_span_gre_dir gt4 ingress

	ip route add 192.0.2.130/32 via 192.0.2.162
	quick_test_span_gre_dir gt4 ingress
	ip route del 192.0.2.130/32 via 192.0.2.162

	mirror_uninstall $swp1 ingress
	log_test "mirror to gre with next-hop remote ($tcflags)"
}

test_ip6gretap()
{
	RET=0

	mirror_install $swp1 ingress gt6 "matchall $tcflags"
	quick_test_span_gre_dir gt6 ingress
	mirror_uninstall $swp1 ingress

	log_test "mirror to ip6gre with next-hop remote ($tcflags)"
}

test_all()
{
	slow_path_trap_install $swp1 ingress
	slow_path_trap_install $swp1 egress

	tests_run

	slow_path_trap_uninstall $swp1 egress
	slow_path_trap_uninstall $swp1 ingress
}

trap cleanup EXIT

setup_prepare
setup_wait

tcflags="skip_hw"
test_all

if ! tc_offload_check; then
	echo "WARN: Could not test offloaded functionality"
else
	tcflags="skip_sw"
	test_all
fi

exit $EXIT_STATUS
