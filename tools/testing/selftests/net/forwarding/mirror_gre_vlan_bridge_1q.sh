#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test uses standard topology for testing gretap. See
# mirror_gre_topo_lib.sh for more details.
#
# Test for "tc action mirred egress mirror" when the underlay route points at a
# vlan device on top of a bridge device with vlan filtering (802.1q).

ALL_TESTS="
	test_gretap
	test_ip6gretap
	test_gretap_forbidden_cpu
	test_ip6gretap_forbidden_cpu
	test_gretap_forbidden_egress
	test_ip6gretap_forbidden_egress
	test_gretap_untagged_egress
	test_ip6gretap_untagged_egress
	test_gretap_fdb_roaming
	test_ip6gretap_fdb_roaming
	test_gretap_stp
	test_ip6gretap_stp
"

NUM_NETIFS=6
source lib.sh
source mirror_lib.sh
source mirror_gre_lib.sh
source mirror_gre_topo_lib.sh

require_command $ARPING

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	# gt4's remote address is at $h3.555, not $h3. Thus the packets arriving
	# directly to $h3 for test_gretap_untagged_egress() are rejected by
	# rp_filter and the test spuriously fails.
	sysctl_set net.ipv4.conf.all.rp_filter 0
	sysctl_set net.ipv4.conf.$h3.rp_filter 0

	vrf_prepare
	mirror_gre_topo_create

	vlan_create br1 555 "" 192.0.2.129/32 2001:db8:2::1/128
	bridge vlan add dev br1 vid 555 self
	ip route rep 192.0.2.130/32 dev br1.555
	ip -6 route rep 2001:db8:2::2/128 dev br1.555

	vlan_create $h3 555 v$h3 192.0.2.130/28 2001:db8:2::2/64

	ip link set dev $swp3 master br1
	bridge vlan add dev $swp3 vid 555
	bridge vlan add dev $swp2 vid 555
}

cleanup()
{
	pre_cleanup

	ip link set dev $swp2 nomaster
	ip link set dev $swp3 nomaster
	vlan_destroy $h3 555
	vlan_destroy br1 555

	mirror_gre_topo_destroy
	vrf_cleanup

	sysctl_restore net.ipv4.conf.$h3.rp_filter
	sysctl_restore net.ipv4.conf.all.rp_filter
}

test_vlan_match()
{
	local tundev=$1; shift
	local vlan_match=$1; shift
	local what=$1; shift

	full_test_span_gre_dir_vlan $tundev ingress "$vlan_match" 8 0 "$what"
	full_test_span_gre_dir_vlan $tundev egress "$vlan_match" 0 8 "$what"
}

test_gretap()
{
	test_vlan_match gt4 'skip_hw vlan_id 555 vlan_ethtype ip' \
			"mirror to gretap"
}

test_ip6gretap()
{
	test_vlan_match gt6 'skip_hw vlan_id 555 vlan_ethtype ip' \
			"mirror to ip6gretap"
}

test_span_gre_forbidden_cpu()
{
	local tundev=$1; shift
	local what=$1; shift

	RET=0

	# Run the pass-test first, to prime neighbor table.
	mirror_install $swp1 ingress $tundev "matchall $tcflags"
	quick_test_span_gre_dir $tundev ingress

	# Now forbid the VLAN at the bridge and see it fail.
	bridge vlan del dev br1 vid 555 self
	sleep 1
	fail_test_span_gre_dir $tundev ingress

	bridge vlan add dev br1 vid 555 self
	sleep 1
	quick_test_span_gre_dir $tundev ingress

	mirror_uninstall $swp1 ingress

	log_test "$what: vlan forbidden at a bridge ($tcflags)"
}

test_gretap_forbidden_cpu()
{
	test_span_gre_forbidden_cpu gt4 "mirror to gretap"
}

test_ip6gretap_forbidden_cpu()
{
	test_span_gre_forbidden_cpu gt6 "mirror to ip6gretap"
}

test_span_gre_forbidden_egress()
{
	local tundev=$1; shift
	local what=$1; shift

	RET=0

	mirror_install $swp1 ingress $tundev "matchall $tcflags"
	quick_test_span_gre_dir $tundev ingress

	bridge vlan del dev $swp3 vid 555
	sleep 1
	fail_test_span_gre_dir $tundev ingress

	bridge vlan add dev $swp3 vid 555
	# Re-prime FDB
	$ARPING -I br1.555 192.0.2.130 -fqc 1
	sleep 1
	quick_test_span_gre_dir $tundev ingress

	mirror_uninstall $swp1 ingress

	log_test "$what: vlan forbidden at a bridge egress ($tcflags)"
}

test_gretap_forbidden_egress()
{
	test_span_gre_forbidden_egress gt4 "mirror to gretap"
}

test_ip6gretap_forbidden_egress()
{
	test_span_gre_forbidden_egress gt6 "mirror to ip6gretap"
}

test_span_gre_untagged_egress()
{
	local tundev=$1; shift
	local what=$1; shift

	RET=0

	mirror_install $swp1 ingress $tundev "matchall $tcflags"

	quick_test_span_gre_dir $tundev ingress
	quick_test_span_vlan_dir $h3 555 ingress

	bridge vlan add dev $swp3 vid 555 pvid untagged
	sleep 1
	quick_test_span_gre_dir $tundev ingress
	fail_test_span_vlan_dir $h3 555 ingress

	bridge vlan add dev $swp3 vid 555
	sleep 1
	quick_test_span_gre_dir $tundev ingress
	quick_test_span_vlan_dir $h3 555 ingress

	mirror_uninstall $swp1 ingress

	log_test "$what: vlan untagged at a bridge egress ($tcflags)"
}

test_gretap_untagged_egress()
{
	test_span_gre_untagged_egress gt4 "mirror to gretap"
}

test_ip6gretap_untagged_egress()
{
	test_span_gre_untagged_egress gt6 "mirror to ip6gretap"
}

test_span_gre_fdb_roaming()
{
	local tundev=$1; shift
	local what=$1; shift
	local h3mac=$(mac_get $h3)

	RET=0

	mirror_install $swp1 ingress $tundev "matchall $tcflags"
	quick_test_span_gre_dir $tundev ingress

	bridge fdb del dev $swp3 $h3mac vlan 555 master
	bridge fdb add dev $swp2 $h3mac vlan 555 master
	sleep 1
	fail_test_span_gre_dir $tundev ingress

	bridge fdb del dev $swp2 $h3mac vlan 555 master
	# Re-prime FDB
	$ARPING -I br1.555 192.0.2.130 -fqc 1
	sleep 1
	quick_test_span_gre_dir $tundev ingress

	mirror_uninstall $swp1 ingress

	log_test "$what: MAC roaming ($tcflags)"
}

test_gretap_fdb_roaming()
{
	test_span_gre_fdb_roaming gt4 "mirror to gretap"
}

test_ip6gretap_fdb_roaming()
{
	test_span_gre_fdb_roaming gt6 "mirror to ip6gretap"
}

test_gretap_stp()
{
	full_test_span_gre_stp gt4 $swp3 "mirror to gretap"
}

test_ip6gretap_stp()
{
	full_test_span_gre_stp gt6 $swp3 "mirror to ip6gretap"
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
