#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test for "tc action mirred egress mirror" when the underlay route points at a
# vlan device on top of a bridge device with vlan filtering (802.1q).
#
#   +---------------------+                             +---------------------+
#   | H1                  |                             |                  H2 |
#   |     + $h1           |                             |           $h2 +     |
#   |     | 192.0.2.1/28  |                             |  192.0.2.2/28 |     |
#   +-----|---------------+                             +---------------|-----+
#         |                                                             |
#   +-----|-------------------------------------------------------------|-----+
#   | SW  o--> mirred egress mirror dev {gt4,gt6}                       |     |
#   |     |                                                             |     |
#   | +---|-------------------------------------------------------------|---+ |
#   | |   + $swp1                    br1                          $swp2 +   | |
#   | |                                                                     | |
#   | |   + $swp3                                                           | |
#   | +---|-----------------------------------------------------------------+ |
#   |     |                        |                                          |
#   |     |                        + br1.555                                  |
#   |     |                          192.0.2.130/28                           |
#   |     |                          2001:db8:2::2/64                         |
#   |     |                                                                   |
#   |     |                     + gt6 (ip6gretap)      + gt4 (gretap)         |
#   |     |                     : loc=2001:db8:2::1    : loc=192.0.2.129      |
#   |     |                     : rem=2001:db8:2::2    : rem=192.0.2.130      |
#   |     |                     : ttl=100              : ttl=100              |
#   |     |                     : tos=inherit          : tos=inherit          |
#   |     |                     :                      :                      |
#   +-----|---------------------:----------------------:----------------------+
#         |                     :                      :
#   +-----|---------------------:----------------------:----------------------+
#   | H3  + $h3                 + h3-gt6 (ip6gretap)   + h3-gt4 (gretap)      |
#   |     |                       loc=2001:db8:2::2      loc=192.0.2.130      |
#   |     + $h3.555               rem=2001:db8:2::1      rem=192.0.2.129      |
#   |       192.0.2.130/28        ttl=100                ttl=100              |
#   |       2001:db8:2::2/64      tos=inherit            tos=inherit          |
#   |                                                                         |
#   +-------------------------------------------------------------------------+

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

h3_addr_add_del()
{
	local add_del=$1; shift
	local dev=$1; shift

	ip addr $add_del dev $dev 192.0.2.130/28
	ip addr $add_del dev $dev 2001:db8:2::2/64
}

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

	vlan_create $h3 555 v$h3
	h3_addr_add_del add $h3.555

	ip link set dev $swp3 master br1
	bridge vlan add dev $swp3 vid 555
	bridge vlan add dev $swp2 vid 555
}

cleanup()
{
	pre_cleanup

	ip link set dev $swp2 nomaster
	ip link set dev $swp3 nomaster

	h3_addr_add_del del $h3.555
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
	test_vlan_match gt6 'skip_hw vlan_id 555 vlan_ethtype ipv6' \
			"mirror to ip6gretap"
}

test_span_gre_forbidden_cpu()
{
	local tundev=$1; shift
	local what=$1; shift

	RET=0

	# Run the pass-test first, to prime neighbor table.
	mirror_install $swp1 ingress $tundev "matchall"
	quick_test_span_gre_dir $tundev

	# Now forbid the VLAN at the bridge and see it fail.
	bridge vlan del dev br1 vid 555 self
	sleep 1
	fail_test_span_gre_dir $tundev

	bridge vlan add dev br1 vid 555 self
	sleep 1
	quick_test_span_gre_dir $tundev

	mirror_uninstall $swp1 ingress

	log_test "$what: vlan forbidden at a bridge"
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

	mirror_install $swp1 ingress $tundev "matchall"
	quick_test_span_gre_dir $tundev

	bridge vlan del dev $swp3 vid 555
	sleep 1
	fail_test_span_gre_dir $tundev

	bridge vlan add dev $swp3 vid 555
	# Re-prime FDB
	$ARPING -I br1.555 192.0.2.130 -fqc 1
	sleep 1
	quick_test_span_gre_dir $tundev

	mirror_uninstall $swp1 ingress

	log_test "$what: vlan forbidden at a bridge egress"
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
	local ul_proto=$1; shift
	local what=$1; shift

	RET=0

	mirror_install $swp1 ingress $tundev "matchall"

	quick_test_span_gre_dir $tundev
	quick_test_span_vlan_dir $h3 555 "$ul_proto"

	h3_addr_add_del del $h3.555
	bridge vlan add dev $swp3 vid 555 pvid untagged
	h3_addr_add_del add $h3
	sleep 5

	quick_test_span_gre_dir $tundev
	fail_test_span_vlan_dir $h3 555 "$ul_proto"

	h3_addr_add_del del $h3
	bridge vlan add dev $swp3 vid 555
	h3_addr_add_del add $h3.555
	sleep 5

	quick_test_span_gre_dir $tundev
	quick_test_span_vlan_dir $h3 555 "$ul_proto"

	mirror_uninstall $swp1 ingress

	log_test "$what: vlan untagged at a bridge egress"
}

test_gretap_untagged_egress()
{
	test_span_gre_untagged_egress gt4 ip "mirror to gretap"
}

test_ip6gretap_untagged_egress()
{
	test_span_gre_untagged_egress gt6 ipv6 "mirror to ip6gretap"
}

test_span_gre_fdb_roaming()
{
	local tundev=$1; shift
	local what=$1; shift
	local h3mac=$(mac_get $h3)

	RET=0

	mirror_install $swp1 ingress $tundev "matchall"
	quick_test_span_gre_dir $tundev

	while ((RET == 0)); do
		bridge fdb del dev $swp3 $h3mac vlan 555 master 2>/dev/null
		bridge fdb add dev $swp2 $h3mac vlan 555 master static
		sleep 1
		fail_test_span_gre_dir $tundev

		if ! bridge fdb sh dev $swp2 vlan 555 master \
		    | grep -q $h3mac; then
			printf "TEST: %-60s  [RETRY]\n" \
				"$what: MAC roaming"
			# ARP or ND probably reprimed the FDB while the test
			# was running. We would get a spurious failure.
			RET=0
			continue
		fi
		break
	done

	bridge fdb del dev $swp2 $h3mac vlan 555 master 2>/dev/null
	# Re-prime FDB
	$ARPING -I br1.555 192.0.2.130 -fqc 1
	sleep 1
	quick_test_span_gre_dir $tundev

	mirror_uninstall $swp1 ingress

	log_test "$what: MAC roaming"
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

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
