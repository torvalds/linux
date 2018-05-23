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

	mirror_install $swp1 ingress $tundev "matchall $tcflags"
	tc qdisc add dev $h3 clsact
	tc filter add dev $h3 ingress pref 77 prot $prot \
		flower ip_ttl 50 action pass

	mirror_test v$h1 192.0.2.1 192.0.2.2 $h3 77 0

	ip link set dev $tundev type $type ttl 50
	mirror_test v$h1 192.0.2.1 192.0.2.2 $h3 77 10

	ip link set dev $tundev type $type ttl 100
	tc filter del dev $h3 ingress pref 77
	tc qdisc del dev $h3 clsact
	mirror_uninstall $swp1 ingress

	log_test "$what: TTL change ($tcflags)"
}

test_span_gre_tun_up()
{
	local tundev=$1; shift
	local what=$1; shift

	RET=0

	ip link set dev $tundev down
	mirror_install $swp1 ingress $tundev "matchall $tcflags"
	fail_test_span_gre_dir $tundev ingress

	ip link set dev $tundev up

	quick_test_span_gre_dir $tundev ingress
	mirror_uninstall $swp1 ingress

	log_test "$what: tunnel down/up ($tcflags)"
}

test_span_gre_egress_up()
{
	local tundev=$1; shift
	local remote_ip=$1; shift
	local what=$1; shift

	RET=0

	ip link set dev $swp3 down
	mirror_install $swp1 ingress $tundev "matchall $tcflags"
	fail_test_span_gre_dir $tundev ingress

	# After setting the device up, wait for neighbor to get resolved so that
	# we can expect mirroring to work.
	ip link set dev $swp3 up
	while true; do
		ip neigh sh dev $swp3 $remote_ip nud reachable |
		    grep -q ^
		if [[ $? -ne 0 ]]; then
			sleep 1
		else
			break
		fi
	done

	quick_test_span_gre_dir $tundev ingress
	mirror_uninstall $swp1 ingress

	log_test "$what: egress down/up ($tcflags)"
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
	mirror_install $swp1 ingress $tundev "matchall $tcflags"
	fail_test_span_gre_dir $tundev ingress

	ip link set dev $tundev type $type remote $correct_ip
	quick_test_span_gre_dir $tundev ingress
	mirror_uninstall $swp1 ingress

	log_test "$what: remote address change ($tcflags)"
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
