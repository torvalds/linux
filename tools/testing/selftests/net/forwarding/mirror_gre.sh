#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test uses standard topology for testing gretap. See
# mirror_gre_topo_lib.sh for more details.
#
# Test for "tc action mirred egress mirror" when the device to mirror to is a
# gretap or ip6gretap netdevice. Expect that the packets come out encapsulated,
# and another gretap / ip6gretap netdevice is then capable of decapsulating the
# traffic. Test that the payload is what is expected (ICMP ping request or
# reply, depending on test).

ALL_TESTS="
	test_gretap
	test_ip6gretap
	test_gretap_mac
	test_ip6gretap_mac
	test_two_spans
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

	mirror_gre_topo_destroy
	vrf_cleanup
}

test_span_gre_mac()
{
	local tundev=$1; shift
	local direction=$1; shift
	local what=$1; shift

	case "$direction" in
	ingress) local src_mac=$(mac_get $h1); local dst_mac=$(mac_get $h2)
		;;
	egress) local src_mac=$(mac_get $h2); local dst_mac=$(mac_get $h1)
		;;
	esac

	RET=0

	mirror_install $swp1 $direction $tundev "matchall $tcflags"
	icmp_capture_install h3-${tundev} "src_mac $src_mac dst_mac $dst_mac"

	mirror_test v$h1 192.0.2.1 192.0.2.2 h3-${tundev} 100 10

	icmp_capture_uninstall h3-${tundev}
	mirror_uninstall $swp1 $direction

	log_test "$direction $what: envelope MAC ($tcflags)"
}

test_two_spans()
{
	RET=0

	mirror_install $swp1 ingress gt4 "matchall $tcflags"
	mirror_install $swp1 egress gt6 "matchall $tcflags"
	quick_test_span_gre_dir gt4 ingress
	quick_test_span_gre_dir gt6 egress

	mirror_uninstall $swp1 ingress
	fail_test_span_gre_dir gt4 ingress
	quick_test_span_gre_dir gt6 egress

	mirror_install $swp1 ingress gt4 "matchall $tcflags"
	mirror_uninstall $swp1 egress
	quick_test_span_gre_dir gt4 ingress
	fail_test_span_gre_dir gt6 egress

	mirror_uninstall $swp1 ingress
	log_test "two simultaneously configured mirrors ($tcflags)"
}

test_gretap()
{
	full_test_span_gre_dir gt4 ingress 8 0 "mirror to gretap"
	full_test_span_gre_dir gt4 egress 0 8 "mirror to gretap"
}

test_ip6gretap()
{
	full_test_span_gre_dir gt6 ingress 8 0 "mirror to ip6gretap"
	full_test_span_gre_dir gt6 egress 0 8 "mirror to ip6gretap"
}

test_gretap_mac()
{
	test_span_gre_mac gt4 ingress "mirror to gretap"
	test_span_gre_mac gt4 egress "mirror to gretap"
}

test_ip6gretap_mac()
{
	test_span_gre_mac gt6 ingress "mirror to ip6gretap"
	test_span_gre_mac gt6 egress "mirror to ip6gretap"
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
