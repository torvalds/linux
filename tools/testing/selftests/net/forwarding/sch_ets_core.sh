# SPDX-License-Identifier: GPL-2.0

# This is a template for ETS Qdisc test.
#
# This test sends from H1 several traffic streams with 802.1p-tagged packets.
# The tags are used at $swp1 to prioritize the traffic. Each stream is then
# queued at a different ETS band according to the assigned priority. After
# runnig for a while, counters at H2 are consulted to determine whether the
# traffic scheduling was according to the ETS configuration.
#
# This template is supposed to be embedded by a test driver, which implements
# statistics collection, any HW-specific stuff, and prominently configures the
# system to assure that there is overcommitment at $swp2. That is necessary so
# that the ETS traffic selection algorithm kicks in and has to schedule some
# traffic at the expense of other.
#
# A driver for veth-based testing is in sch_ets.sh, an example of a driver for
# an offloaded data path is in selftests/drivers/net/mlxsw/sch_ets.sh.
#
# +---------------------------------------------------------------------+
# | H1                                                                  |
# |     + $h1.10              + $h1.11              + $h1.12            |
# |     | 192.0.2.1/28        | 192.0.2.17/28       | 192.0.2.33/28     |
# |     | egress-qos-map      | egress-qos-map      | egress-qos-map    |
# |     |  0:0                |  0:1                |  0:2              |
# |     \____________________ | ____________________/                   |
# |                          \|/                                        |
# |                           + $h1                                     |
# +---------------------------|-----------------------------------------+
#                             |
# +---------------------------|-----------------------------------------+
# | SW                        + $swp1                                   |
# |                           | >1Gbps                                  |
# |      ____________________/|\____________________                    |
# |     /                     |                     \                   |
# |  +--|----------------+ +--|----------------+ +--|----------------+  |
# |  |  + $swp1.10       | |  + $swp1.11       | |  + $swp1.12       |  |
# |  |    ingress-qos-map| |    ingress-qos-map| |    ingress-qos-map|  |
# |  |     0:0 1:1 2:2   | |     0:0 1:1 2:2   | |     0:0 1:1 2:2   |  |
# |  |                   | |                   | |                   |  |
# |  |    BR10           | |    BR11           | |    BR12           |  |
# |  |                   | |                   | |                   |  |
# |  |  + $swp2.10       | |  + $swp2.11       | |  + $swp2.12       |  |
# |  +--|----------------+ +--|----------------+ +--|----------------+  |
# |     \____________________ | ____________________/                   |
# |                          \|/                                        |
# |                           + $swp2                                   |
# |                           | 1Gbps (ethtool or HTB qdisc)            |
# |                           | qdisc ets quanta $W0 $W1 $W2            |
# |                           |           priomap 0 1 2                 |
# +---------------------------|-----------------------------------------+
#                             |
# +---------------------------|-----------------------------------------+
# | H2                        + $h2                                     |
# |      ____________________/|\____________________                    |
# |     /                     |                     \                   |
# |     + $h2.10              + $h2.11              + $h2.12            |
# |       192.0.2.2/28          192.0.2.18/28         192.0.2.34/28     |
# +---------------------------------------------------------------------+

NUM_NETIFS=4
CHECK_TC=yes
source $lib_dir/lib.sh
source $lib_dir/sch_ets_tests.sh

PARENT=root
QDISC_DEV=

sip()
{
	echo 192.0.2.$((16 * $1 + 1))
}

dip()
{
	echo 192.0.2.$((16 * $1 + 2))
}

# Callback from sch_ets_tests.sh
ets_start_traffic()
{
	local dst_mac=$(mac_get $h2)
	local i=$1; shift

	start_traffic $h1.1$i $(sip $i) $(dip $i) $dst_mac
}

ETS_CHANGE_QDISC=

priomap_mode()
{
	echo "Running in priomap mode"
	ets_delete_qdisc
	ETS_CHANGE_QDISC=ets_change_qdisc_priomap
}

classifier_mode()
{
	echo "Running in classifier mode"
	ets_delete_qdisc
	ETS_CHANGE_QDISC=ets_change_qdisc_classifier
}

ets_change_qdisc_priomap()
{
	local dev=$1; shift
	local nstrict=$1; shift
	local priomap=$1; shift
	local quanta=("${@}")

	local op=$(if [[ -n $QDISC_DEV ]]; then echo change; else echo add; fi)

	tc qdisc $op dev $dev $PARENT handle 10: ets			       \
		$(if ((nstrict)); then echo strict $nstrict; fi)	       \
		$(if ((${#quanta[@]})); then echo quanta ${quanta[@]}; fi)     \
		priomap $priomap
	QDISC_DEV=$dev
}

ets_change_qdisc_classifier()
{
	local dev=$1; shift
	local nstrict=$1; shift
	local priomap=$1; shift
	local quanta=("${@}")

	local op=$(if [[ -n $QDISC_DEV ]]; then echo change; else echo add; fi)

	tc qdisc $op dev $dev $PARENT handle 10: ets			       \
		$(if ((nstrict)); then echo strict $nstrict; fi)	       \
		$(if ((${#quanta[@]})); then echo quanta ${quanta[@]}; fi)

	if [[ $op == add ]]; then
		local prio=0
		local band

		for band in $priomap; do
			tc filter add dev $dev parent 10: basic \
				match "meta(priority eq $prio)" \
				flowid 10:$((band + 1))
			((prio++))
		done
	fi
	QDISC_DEV=$dev
}

# Callback from sch_ets_tests.sh
ets_change_qdisc()
{
	if [[ -z "$ETS_CHANGE_QDISC" ]]; then
		exit 1
	fi
	$ETS_CHANGE_QDISC "$@"
}

ets_delete_qdisc()
{
	if [[ -n $QDISC_DEV ]]; then
		tc qdisc del dev $QDISC_DEV $PARENT
		QDISC_DEV=
	fi
}

h1_create()
{
	local i;

	simple_if_init $h1
	mtu_set $h1 9900
	for i in {0..2}; do
		vlan_create $h1 1$i v$h1 $(sip $i)/28
		ip link set dev $h1.1$i type vlan egress 0:$i
	done
}

h1_destroy()
{
	local i

	for i in {0..2}; do
		vlan_destroy $h1 1$i
	done
	mtu_restore $h1
	simple_if_fini $h1
}

h2_create()
{
	local i

	simple_if_init $h2
	mtu_set $h2 9900
	for i in {0..2}; do
		vlan_create $h2 1$i v$h2 $(dip $i)/28
	done
}

h2_destroy()
{
	local i

	for i in {0..2}; do
		vlan_destroy $h2 1$i
	done
	mtu_restore $h2
	simple_if_fini $h2
}

ets_switch_create()
{
	local i

	ip link set dev $swp1 up
	mtu_set $swp1 9900

	ip link set dev $swp2 up
	mtu_set $swp2 9900

	for i in {0..2}; do
		vlan_create $swp1 1$i
		ip link set dev $swp1.1$i type vlan ingress 0:0 1:1 2:2

		vlan_create $swp2 1$i

		ip link add dev br1$i type bridge
		ip link set dev $swp1.1$i master br1$i
		ip link set dev $swp2.1$i master br1$i

		ip link set dev br1$i up
		ip link set dev $swp1.1$i up
		ip link set dev $swp2.1$i up
	done
}

ets_switch_destroy()
{
	local i

	ets_delete_qdisc

	for i in {0..2}; do
		ip link del dev br1$i
		vlan_destroy $swp2 1$i
		vlan_destroy $swp1 1$i
	done

	mtu_restore $swp2
	ip link set dev $swp2 down

	mtu_restore $swp1
	ip link set dev $swp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	put=$swp2
	hut=$h2

	vrf_prepare

	h1_create
	h2_create
	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup
}

ping_ipv4()
{
	ping_test $h1.10 $(dip 0) " vlan 10"
	ping_test $h1.11 $(dip 1) " vlan 11"
	ping_test $h1.12 $(dip 2) " vlan 12"
}

ets_run()
{
	trap cleanup EXIT

	setup_prepare
	setup_wait

	tests_run

	exit $EXIT_STATUS
}
