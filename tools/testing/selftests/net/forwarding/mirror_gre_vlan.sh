#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test uses standard topology for testing gretap. See
# mirror_gre_topo_lib.sh for more details.
#
# Test for "tc action mirred egress mirror" that mirrors to a gretap netdevice
# whose underlay route points at a vlan device.

ALL_TESTS="
	test_gretap
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

	ip link add name $swp3.555 link $swp3 type vlan id 555
	ip address add dev $swp3.555 192.0.2.129/32
	ip address add dev $swp3.555 2001:db8:2::1/128
	ip link set dev $swp3.555 up

	ip route add 192.0.2.130/32 dev $swp3.555
	ip -6 route add 2001:db8:2::2/128 dev $swp3.555

	ip link add name $h3.555 link $h3 type vlan id 555
	ip link set dev $h3.555 master v$h3
	ip address add dev $h3.555 192.0.2.130/28
	ip address add dev $h3.555 2001:db8:2::2/64
	ip link set dev $h3.555 up
}

cleanup()
{
	pre_cleanup

	ip link del dev $h3.555
	ip link del dev $swp3.555

	mirror_gre_topo_destroy
	vrf_cleanup
}

test_gretap()
{
	full_test_span_gre_dir gt4 ingress 8 0 "mirror to gretap"
	full_test_span_gre_dir gt4 egress 0 8 "mirror to gretap"
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
