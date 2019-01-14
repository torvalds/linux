#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test uses standard topology for testing gretap. See
# mirror_gre_topo_lib.sh for more details.
#
# This tests flower-triggered mirroring to gretap and ip6gretap netdevices. The
# interfaces on H1 and H2 have two addresses each. Flower match on one of the
# addresses is configured with mirror action. It is expected that when pinging
# this address, mirroring takes place, whereas when pinging the other one,
# there's no mirroring.

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

	vrf_prepare
	mirror_gre_topo_create

	ip address add dev $swp3 192.0.2.129/28
	ip address add dev $h3 192.0.2.130/28

	ip address add dev $swp3 2001:db8:2::1/64
	ip address add dev $h3 2001:db8:2::2/64

	ip address add dev $h1 192.0.2.3/28
	ip address add dev $h2 192.0.2.4/28
}

cleanup()
{
	pre_cleanup

	ip address del dev $h2 192.0.2.4/28
	ip address del dev $h1 192.0.2.3/28

	ip address del dev $h3 2001:db8:2::2/64
	ip address del dev $swp3 2001:db8:2::1/64

	ip address del dev $h3 192.0.2.130/28
	ip address del dev $swp3 192.0.2.129/28

	mirror_gre_topo_destroy
	vrf_cleanup
}

test_span_gre_dir_acl()
{
	test_span_gre_dir_ips "$@" 192.0.2.3 192.0.2.4
}

fail_test_span_gre_dir_acl()
{
	fail_test_span_gre_dir_ips "$@" 192.0.2.3 192.0.2.4
}

full_test_span_gre_dir_acl()
{
	local tundev=$1; shift
	local direction=$1; shift
	local forward_type=$1; shift
	local backward_type=$1; shift
	local match_dip=$1; shift
	local what=$1; shift

	mirror_install $swp1 $direction $tundev \
		       "protocol ip flower $tcflags dst_ip $match_dip"
	fail_test_span_gre_dir $tundev $direction
	test_span_gre_dir_acl "$tundev" "$direction" \
			  "$forward_type" "$backward_type"
	mirror_uninstall $swp1 $direction

	# Test lack of mirroring after ACL mirror is uninstalled.
	fail_test_span_gre_dir_acl "$tundev" "$direction"

	log_test "$direction $what ($tcflags)"
}

test_gretap()
{
	full_test_span_gre_dir_acl gt4 ingress 8 0 192.0.2.4 "ACL mirror to gretap"
	full_test_span_gre_dir_acl gt4 egress 0 8 192.0.2.3 "ACL mirror to gretap"
}

test_ip6gretap()
{
	full_test_span_gre_dir_acl gt6 ingress 8 0 192.0.2.4 "ACL mirror to ip6gretap"
	full_test_span_gre_dir_acl gt6 egress 0 8 192.0.2.3 "ACL mirror to ip6gretap"
}

test_all()
{
	RET=0

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
