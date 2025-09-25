# SPDX-License-Identifier: GPL-2.0

# This test sends a stream of traffic from H1 through a switch, to H2. On the
# egress port from the switch ($swp2), a shaper is installed. The test verifies
# that the rates on the port match the configured shaper.
#
# In order to test per-class shaping, $swp2 actually contains TBF under PRIO or
# ETS, with two different configurations. Traffic is prioritized using 802.1p.
#
# +-------------------------------------------+
# | H1                                        |
# |     + $h1.10                  $h1.11 +    |
# |     | 192.0.2.1/28     192.0.2.17/28 |    |
# |     |                                |    |
# |     \______________    _____________/     |
# |                    \ /                    |
# |                     + $h1                 |
# +---------------------|---------------------+
#                       |
# +---------------------|---------------------+
# | SW                  + $swp1               |
# |     _______________/ \_______________     |
# |    /                                 \    |
# |  +-|--------------+   +--------------|-+  |
# |  | + $swp1.10     |   |     $swp1.11 + |  |
# |  |                |   |                |  |
# |  |     BR10       |   |       BR11     |  |
# |  |                |   |                |  |
# |  | + $swp2.10     |   |     $swp2.11 + |  |
# |  +-|--------------+   +--------------|-+  |
# |    \_______________   ______________/     |
# |                    \ /                    |
# |                     + $swp2               |
# +---------------------|---------------------+
#                       |
# +---------------------|---------------------+
# | H2                  + $h2                 |
# |      ______________/ \______________      |
# |     /                               \     |
# |     |                               |     |
# |     + $h2.10                 $h2.11 +     |
# |       192.0.2.2/28    192.0.2.18/28       |
# +-------------------------------------------+

NUM_NETIFS=4
CHECK_TC="yes"
source $lib_dir/lib.sh

ipaddr()
{
	local host=$1; shift
	local vlan=$1; shift

	echo 192.0.2.$((16 * (vlan - 10) + host))
}

host_create()
{
	local dev=$1; shift
	local host=$1; shift

	adf_simple_if_init $dev

	mtu_set $dev 10000
	defer mtu_restore $dev

	vlan_create $dev 10 v$dev $(ipaddr $host 10)/28
	defer vlan_destroy $dev 10
	ip link set dev $dev.10 type vlan egress 0:0

	vlan_create $dev 11 v$dev $(ipaddr $host 11)/28
	defer vlan_destroy $dev 11
	ip link set dev $dev.11 type vlan egress 0:1
}

h1_create()
{
	host_create $h1 1
}

h2_create()
{
	host_create $h2 2

	tc qdisc add dev $h2 clsact
	defer tc qdisc del dev $h2 clsact

	tc filter add dev $h2 ingress pref 1010 prot 802.1q \
	   flower $TCFLAGS vlan_id 10 action pass
	tc filter add dev $h2 ingress pref 1011 prot 802.1q \
	   flower $TCFLAGS vlan_id 11 action pass
}

switch_create()
{
	local intf
	local vlan

	ip link add dev br10 type bridge
	defer ip link del dev br10

	ip link add dev br11 type bridge
	defer ip link del dev br11

	for intf in $swp1 $swp2; do
		ip link set dev $intf up
		defer ip link set dev $intf down

		mtu_set $intf 10000
		defer mtu_restore $intf

		for vlan in 10 11; do
			vlan_create $intf $vlan
			defer vlan_destroy $intf $vlan

			ip link set dev $intf.$vlan master br$vlan
			defer ip link set dev $intf.$vlan nomaster

			ip link set dev $intf.$vlan up
			defer ip link set dev $intf.$vlan down
		done
	done

	for vlan in 10 11; do
		ip link set dev $swp1.$vlan type vlan ingress 0:0 1:1
	done

	ip link set dev br10 up
	defer ip link set dev br10 down

	ip link set dev br11 up
	defer ip link set dev br11 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	swp4=${NETIFS[p7]}
	swp5=${NETIFS[p8]}

	h2_mac=$(mac_get $h2)

	adf_vrf_prepare

	h1_create
	h2_create
	switch_create
}

ping_ipv4()
{
	ping_test $h1.10 $(ipaddr 2 10) " vlan 10"
	ping_test $h1.11 $(ipaddr 2 11) " vlan 11"
}

tbf_get_counter()
{
	local vlan=$1; shift

	tc_rule_stats_get $h2 10$vlan ingress .bytes
}

__tbf_test()
{
	local vlan=$1; shift
	local mbit=$1; shift

	start_traffic $h1.$vlan $(ipaddr 1 $vlan) $(ipaddr 2 $vlan) $h2_mac
	defer stop_traffic $!
	sleep 5 # Wait for the burst to dwindle

	local t2=$(busywait_for_counter 1000 +1 tbf_get_counter $vlan)
	sleep 10
	local t3=$(tbf_get_counter $vlan)

	RET=0

	# Note: TBF uses 10^6 Mbits, not 2^20 ones.
	local er=$((mbit * 1000 * 1000))
	local nr=$(rate $t2 $t3 10)
	local nr_pct=$((100 * (nr - er) / er))
	((-5 <= nr_pct && nr_pct <= 5))
	xfail_on_slow check_err $? "Expected rate $(humanize $er), got $(humanize $nr), which is $nr_pct% off. Required accuracy is +-5%."

	log_test "TC $((vlan - 10)): TBF rate ${mbit}Mbit"
}

do_tbf_test()
{
	in_defer_scope \
		__tbf_test "$@"
}
