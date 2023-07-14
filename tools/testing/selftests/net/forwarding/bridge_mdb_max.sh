#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-----------------------+                          +------------------------+
# | H1 (vrf)              |                          | H2 (vrf)               |
# | + $h1.10              |                          | + $h2.10               |
# | | 192.0.2.1/28        |                          | | 192.0.2.2/28         |
# | | 2001:db8:1::1/64    |                          | | 2001:db8:1::2/64     |
# | |                     |                          | |                      |
# | |  + $h1.20           |                          | |  + $h2.20            |
# | \  | 198.51.100.1/24  |                          | \  | 198.51.100.2/24   |
# |  \ | 2001:db8:2::1/64 |                          |  \ | 2001:db8:2::2/64  |
# |   \|                  |                          |   \|                   |
# |    + $h1              |                          |    + $h2               |
# +----|------------------+                          +----|-------------------+
#      |                                                  |
# +----|--------------------------------------------------|-------------------+
# | SW |                                                  |                   |
# | +--|--------------------------------------------------|-----------------+ |
# | |  + $swp1                   BR0 (802.1q)             + $swp2           | |
# | |     vid 10                                             vid 10         | |
# | |     vid 20                                             vid 20         | |
# | |                                                                       | |
# | +-----------------------------------------------------------------------+ |
# +---------------------------------------------------------------------------+

ALL_TESTS="
	test_8021d
	test_8021q
	test_8021qvs
"

NUM_NETIFS=4
source lib.sh
source tc_common.sh

h1_create()
{
	simple_if_init $h1
	vlan_create $h1 10 v$h1 192.0.2.1/28 2001:db8:1::1/64
	vlan_create $h1 20 v$h1 198.51.100.1/24 2001:db8:2::1/64
}

h1_destroy()
{
	vlan_destroy $h1 20
	vlan_destroy $h1 10
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
	vlan_create $h2 10 v$h2 192.0.2.2/28
	vlan_create $h2 20 v$h2 198.51.100.2/24
}

h2_destroy()
{
	vlan_destroy $h2 20
	vlan_destroy $h2 10
	simple_if_fini $h2
}

switch_create_8021d()
{
	log_info "802.1d tests"

	ip link add name br0 type bridge vlan_filtering 0 \
		mcast_snooping 1 \
		mcast_igmp_version 3 mcast_mld_version 2
	ip link set dev br0 up

	ip link set dev $swp1 master br0
	ip link set dev $swp1 up
	bridge link set dev $swp1 fastleave on

	ip link set dev $swp2 master br0
	ip link set dev $swp2 up
}

switch_create_8021q()
{
	local br_flags=$1; shift

	log_info "802.1q $br_flags${br_flags:+ }tests"

	ip link add name br0 type bridge vlan_filtering 1 vlan_default_pvid 0 \
		mcast_snooping 1 $br_flags \
		mcast_igmp_version 3 mcast_mld_version 2
	bridge vlan add vid 10 dev br0 self
	bridge vlan add vid 20 dev br0 self
	ip link set dev br0 up

	ip link set dev $swp1 master br0
	ip link set dev $swp1 up
	bridge link set dev $swp1 fastleave on
	bridge vlan add vid 10 dev $swp1
	bridge vlan add vid 20 dev $swp1

	ip link set dev $swp2 master br0
	ip link set dev $swp2 up
	bridge vlan add vid 10 dev $swp2
	bridge vlan add vid 20 dev $swp2
}

switch_create_8021qvs()
{
	switch_create_8021q "mcast_vlan_snooping 1"
	bridge vlan global set dev br0 vid 10 mcast_igmp_version 3
	bridge vlan global set dev br0 vid 10 mcast_mld_version 2
	bridge vlan global set dev br0 vid 20 mcast_igmp_version 3
	bridge vlan global set dev br0 vid 20 mcast_mld_version 2
}

switch_destroy()
{
	ip link set dev $swp2 down
	ip link set dev $swp2 nomaster

	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster

	ip link set dev br0 down
	ip link del dev br0
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare
	forwarding_enable

	h1_create
	h2_create
}

cleanup()
{
	pre_cleanup

	switch_destroy 2>/dev/null
	h2_destroy
	h1_destroy

	forwarding_restore
	vrf_cleanup
}

cfg_src_list()
{
	local IPs=("$@")
	local IPstr=$(echo ${IPs[@]} | tr '[:space:]' , | sed 's/,$//')

	echo ${IPstr:+source_list }${IPstr}
}

cfg_group_op()
{
	local op=$1; shift
	local locus=$1; shift
	local GRP=$1; shift
	local state=$1; shift
	local IPs=("$@")

	local source_list=$(cfg_src_list ${IPs[@]})

	# Everything besides `bridge mdb' uses the "dev X vid Y" syntax,
	# so we use it here as well and convert.
	local br_locus=$(echo "$locus" | sed 's/^dev /port /')

	bridge mdb $op dev br0 $br_locus grp $GRP $state \
	       filter_mode include $source_list
}

cfg4_entries_op()
{
	local op=$1; shift
	local locus=$1; shift
	local state=$1; shift
	local n=$1; shift
	local grp=${1:-1}; shift

	local GRP=239.1.1.${grp}
	local IPs=$(seq -f 192.0.2.%g 1 $((n - 1)))
	cfg_group_op "$op" "$locus" "$GRP" "$state" ${IPs[@]}
}

cfg4_entries_add()
{
	cfg4_entries_op add "$@"
}

cfg4_entries_del()
{
	cfg4_entries_op del "$@"
}

cfg6_entries_op()
{
	local op=$1; shift
	local locus=$1; shift
	local state=$1; shift
	local n=$1; shift
	local grp=${1:-1}; shift

	local GRP=ff0e::${grp}
	local IPs=$(printf "2001:db8:1::%x\n" $(seq 1 $((n - 1))))
	cfg_group_op "$op" "$locus" "$GRP" "$state" ${IPs[@]}
}

cfg6_entries_add()
{
	cfg6_entries_op add "$@"
}

cfg6_entries_del()
{
	cfg6_entries_op del "$@"
}

locus_dev_peer()
{
	local dev_kw=$1; shift
	local dev=$1; shift
	local vid_kw=$1; shift
	local vid=$1; shift

	echo "$h1.${vid:-10}"
}

locus_dev()
{
	local dev_kw=$1; shift
	local dev=$1; shift

	echo $dev
}

ctl4_entries_add()
{
	local locus=$1; shift
	local state=$1; shift
	local n=$1; shift
	local grp=${1:-1}; shift

	local IPs=$(seq -f 192.0.2.%g 1 $((n - 1)))
	local peer=$(locus_dev_peer $locus)
	local GRP=239.1.1.${grp}
	$MZ $peer -c 1 -A 192.0.2.1 -B $GRP \
		-t ip proto=2,p=$(igmpv3_is_in_get $GRP $IPs) -q
	sleep 1

	local nn=$(bridge mdb show dev br0 | grep $GRP | wc -l)
	if ((nn != n)); then
		echo mcast_max_groups > /dev/stderr
		false
	fi
}

ctl4_entries_del()
{
	local locus=$1; shift
	local state=$1; shift
	local n=$1; shift
	local grp=${1:-1}; shift

	local peer=$(locus_dev_peer $locus)
	local GRP=239.1.1.${grp}
	$MZ $peer -c 1 -A 192.0.2.1 -B 224.0.0.2 \
		-t ip proto=2,p=$(igmpv2_leave_get $GRP) -q
	sleep 1
	! bridge mdb show dev br0 | grep -q $GRP
}

ctl6_entries_add()
{
	local locus=$1; shift
	local state=$1; shift
	local n=$1; shift
	local grp=${1:-1}; shift

	local IPs=$(printf "2001:db8:1::%x\n" $(seq 1 $((n - 1))))
	local peer=$(locus_dev_peer $locus)
	local SIP=fe80::1
	local GRP=ff0e::${grp}
	local p=$(mldv2_is_in_get $SIP $GRP $IPs)
	$MZ -6 $peer -c 1 -A $SIP -B $GRP -t ip hop=1,next=0,p="$p" -q
	sleep 1

	local nn=$(bridge mdb show dev br0 | grep $GRP | wc -l)
	if ((nn != n)); then
		echo mcast_max_groups > /dev/stderr
		false
	fi
}

ctl6_entries_del()
{
	local locus=$1; shift
	local state=$1; shift
	local n=$1; shift
	local grp=${1:-1}; shift

	local peer=$(locus_dev_peer $locus)
	local SIP=fe80::1
	local GRP=ff0e::${grp}
	local p=$(mldv1_done_get $SIP $GRP)
	$MZ -6 $peer -c 1 -A $SIP -B $GRP -t ip hop=1,next=0,p="$p" -q
	sleep 1
	! bridge mdb show dev br0 | grep -q $GRP
}

bridge_maxgroups_errmsg_check_cfg()
{
	local msg=$1; shift
	local needle=$1; shift

	echo "$msg" | grep -q mcast_max_groups
	check_err $? "Adding MDB entries failed for the wrong reason: $msg"
}

bridge_maxgroups_errmsg_check_cfg4()
{
	bridge_maxgroups_errmsg_check_cfg "$@"
}

bridge_maxgroups_errmsg_check_cfg6()
{
	bridge_maxgroups_errmsg_check_cfg "$@"
}

bridge_maxgroups_errmsg_check_ctl4()
{
	:
}

bridge_maxgroups_errmsg_check_ctl6()
{
	:
}

bridge_port_ngroups_get()
{
	local locus=$1; shift

	bridge -j -d link show $locus |
	    jq '.[].mcast_n_groups'
}

bridge_port_maxgroups_get()
{
	local locus=$1; shift

	bridge -j -d link show $locus |
	    jq '.[].mcast_max_groups'
}

bridge_port_maxgroups_set()
{
	local locus=$1; shift
	local max=$1; shift

	bridge link set dev $(locus_dev $locus) mcast_max_groups $max
}

bridge_port_vlan_ngroups_get()
{
	local locus=$1; shift

	bridge -j -d vlan show $locus |
	    jq '.[].vlans[].mcast_n_groups'
}

bridge_port_vlan_maxgroups_get()
{
	local locus=$1; shift

	bridge -j -d vlan show $locus |
	    jq '.[].vlans[].mcast_max_groups'
}

bridge_port_vlan_maxgroups_set()
{
	local locus=$1; shift
	local max=$1; shift

	bridge vlan set $locus mcast_max_groups $max
}

test_ngroups_reporting()
{
	local CFG=$1; shift
	local context=$1; shift
	local locus=$1; shift

	RET=0

	local n0=$(bridge_${context}_ngroups_get "$locus")
	${CFG}_entries_add "$locus" temp 5
	check_err $? "Couldn't add MDB entries"
	local n1=$(bridge_${context}_ngroups_get "$locus")

	((n1 == n0 + 5))
	check_err $? "Number of groups was $n0, now is $n1, but $((n0 + 5)) expected"

	${CFG}_entries_del "$locus" temp 5
	check_err $? "Couldn't delete MDB entries"
	local n2=$(bridge_${context}_ngroups_get "$locus")

	((n2 == n0))
	check_err $? "Number of groups was $n0, now is $n2, but should be back to $n0"

	log_test "$CFG: $context: ngroups reporting"
}

test_8021d_ngroups_reporting_cfg4()
{
	test_ngroups_reporting cfg4 port "dev $swp1"
}

test_8021d_ngroups_reporting_ctl4()
{
	test_ngroups_reporting ctl4 port "dev $swp1"
}

test_8021d_ngroups_reporting_cfg6()
{
	test_ngroups_reporting cfg6 port "dev $swp1"
}

test_8021d_ngroups_reporting_ctl6()
{
	test_ngroups_reporting ctl6 port "dev $swp1"
}

test_8021q_ngroups_reporting_cfg4()
{
	test_ngroups_reporting cfg4 port "dev $swp1 vid 10"
}

test_8021q_ngroups_reporting_ctl4()
{
	test_ngroups_reporting ctl4 port "dev $swp1 vid 10"
}

test_8021q_ngroups_reporting_cfg6()
{
	test_ngroups_reporting cfg6 port "dev $swp1 vid 10"
}

test_8021q_ngroups_reporting_ctl6()
{
	test_ngroups_reporting ctl6 port "dev $swp1 vid 10"
}

test_8021qvs_ngroups_reporting_cfg4()
{
	test_ngroups_reporting cfg4 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_ngroups_reporting_ctl4()
{
	test_ngroups_reporting ctl4 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_ngroups_reporting_cfg6()
{
	test_ngroups_reporting cfg6 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_ngroups_reporting_ctl6()
{
	test_ngroups_reporting ctl6 port_vlan "dev $swp1 vid 10"
}

test_ngroups_cross_vlan()
{
	local CFG=$1; shift

	local locus1="dev $swp1 vid 10"
	local locus2="dev $swp1 vid 20"

	RET=0

	local n10=$(bridge_port_vlan_ngroups_get "$locus1")
	local n20=$(bridge_port_vlan_ngroups_get "$locus2")
	${CFG}_entries_add "$locus1" temp 5 111
	check_err $? "Couldn't add MDB entries to VLAN 10"
	local n11=$(bridge_port_vlan_ngroups_get "$locus1")
	local n21=$(bridge_port_vlan_ngroups_get "$locus2")

	((n11 == n10 + 5))
	check_err $? "Number of groups at VLAN 10 was $n10, now is $n11, but 5 entries added on VLAN 10, $((n10 + 5)) expected"

	((n21 == n20))
	check_err $? "Number of groups at VLAN 20 was $n20, now is $n21, but no change expected on VLAN 20"

	${CFG}_entries_add "$locus2" temp 5 112
	check_err $? "Couldn't add MDB entries to VLAN 20"
	local n12=$(bridge_port_vlan_ngroups_get "$locus1")
	local n22=$(bridge_port_vlan_ngroups_get "$locus2")

	((n12 == n11))
	check_err $? "Number of groups at VLAN 10 was $n11, now is $n12, but no change expected on VLAN 10"

	((n22 == n21 + 5))
	check_err $? "Number of groups at VLAN 20 was $n21, now is $n22, but 5 entries added on VLAN 20, $((n21 + 5)) expected"

	${CFG}_entries_del "$locus1" temp 5 111
	check_err $? "Couldn't delete MDB entries from VLAN 10"
	${CFG}_entries_del "$locus2" temp 5 112
	check_err $? "Couldn't delete MDB entries from VLAN 20"
	local n13=$(bridge_port_vlan_ngroups_get "$locus1")
	local n23=$(bridge_port_vlan_ngroups_get "$locus2")

	((n13 == n10))
	check_err $? "Number of groups at VLAN 10 was $n10, now is $n13, but should be back to $n10"

	((n23 == n20))
	check_err $? "Number of groups at VLAN 20 was $n20, now is $n23, but should be back to $n20"

	log_test "$CFG: port_vlan: isolation of port and per-VLAN ngroups"
}

test_8021qvs_ngroups_cross_vlan_cfg4()
{
	test_ngroups_cross_vlan cfg4
}

test_8021qvs_ngroups_cross_vlan_ctl4()
{
	test_ngroups_cross_vlan ctl4
}

test_8021qvs_ngroups_cross_vlan_cfg6()
{
	test_ngroups_cross_vlan cfg6
}

test_8021qvs_ngroups_cross_vlan_ctl6()
{
	test_ngroups_cross_vlan ctl6
}

test_maxgroups_zero()
{
	local CFG=$1; shift
	local context=$1; shift
	local locus=$1; shift

	RET=0
	local max

	max=$(bridge_${context}_maxgroups_get "$locus")
	((max == 0))
	check_err $? "Max groups on $locus should be 0, but $max reported"

	bridge_${context}_maxgroups_set "$locus" 100
	check_err $? "Failed to set max to 100"
	max=$(bridge_${context}_maxgroups_get "$locus")
	((max == 100))
	check_err $? "Max groups expected to be 100, but $max reported"

	bridge_${context}_maxgroups_set "$locus" 0
	check_err $? "Couldn't set maximum to 0"

	# Test that setting 0 explicitly still serves as infinity.
	${CFG}_entries_add "$locus" temp 5
	check_err $? "Adding 5 MDB entries failed but should have passed"
	${CFG}_entries_del "$locus" temp 5
	check_err $? "Couldn't delete MDB entries"

	log_test "$CFG: $context maxgroups: reporting and treatment of 0"
}

test_8021d_maxgroups_zero_cfg4()
{
	test_maxgroups_zero cfg4 port "dev $swp1"
}

test_8021d_maxgroups_zero_ctl4()
{
	test_maxgroups_zero ctl4 port "dev $swp1"
}

test_8021d_maxgroups_zero_cfg6()
{
	test_maxgroups_zero cfg6 port "dev $swp1"
}

test_8021d_maxgroups_zero_ctl6()
{
	test_maxgroups_zero ctl6 port "dev $swp1"
}

test_8021q_maxgroups_zero_cfg4()
{
	test_maxgroups_zero cfg4 port "dev $swp1 vid 10"
}

test_8021q_maxgroups_zero_ctl4()
{
	test_maxgroups_zero ctl4 port "dev $swp1 vid 10"
}

test_8021q_maxgroups_zero_cfg6()
{
	test_maxgroups_zero cfg6 port "dev $swp1 vid 10"
}

test_8021q_maxgroups_zero_ctl6()
{
	test_maxgroups_zero ctl6 port "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_zero_cfg4()
{
	test_maxgroups_zero cfg4 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_zero_ctl4()
{
	test_maxgroups_zero ctl4 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_zero_cfg6()
{
	test_maxgroups_zero cfg6 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_zero_ctl6()
{
	test_maxgroups_zero ctl6 port_vlan "dev $swp1 vid 10"
}

test_maxgroups_zero_cross_vlan()
{
	local CFG=$1; shift

	local locus0="dev $swp1"
	local locus1="dev $swp1 vid 10"
	local locus2="dev $swp1 vid 20"
	local max

	RET=0

	bridge_port_vlan_maxgroups_set "$locus1" 100
	check_err $? "$locus1: Failed to set max to 100"

	max=$(bridge_port_maxgroups_get "$locus0")
	((max == 0))
	check_err $? "$locus0: Max groups expected to be 0, but $max reported"

	max=$(bridge_port_vlan_maxgroups_get "$locus2")
	((max == 0))
	check_err $? "$locus2: Max groups expected to be 0, but $max reported"

	bridge_port_vlan_maxgroups_set "$locus2" 100
	check_err $? "$locus2: Failed to set max to 100"

	max=$(bridge_port_maxgroups_get "$locus0")
	((max == 0))
	check_err $? "$locus0: Max groups expected to be 0, but $max reported"

	max=$(bridge_port_vlan_maxgroups_get "$locus2")
	((max == 100))
	check_err $? "$locus2: Max groups expected to be 100, but $max reported"

	bridge_port_maxgroups_set "$locus0" 100
	check_err $? "$locus0: Failed to set max to 100"

	max=$(bridge_port_maxgroups_get "$locus0")
	((max == 100))
	check_err $? "$locus0: Max groups expected to be 100, but $max reported"

	max=$(bridge_port_vlan_maxgroups_get "$locus2")
	((max == 100))
	check_err $? "$locus2: Max groups expected to be 100, but $max reported"

	bridge_port_vlan_maxgroups_set "$locus1" 0
	check_err $? "$locus1: Failed to set max to 0"

	max=$(bridge_port_maxgroups_get "$locus0")
	((max == 100))
	check_err $? "$locus0: Max groups expected to be 100, but $max reported"

	max=$(bridge_port_vlan_maxgroups_get "$locus2")
	((max == 100))
	check_err $? "$locus2: Max groups expected to be 100, but $max reported"

	bridge_port_vlan_maxgroups_set "$locus2" 0
	check_err $? "$locus2: Failed to set max to 0"

	max=$(bridge_port_maxgroups_get "$locus0")
	((max == 100))
	check_err $? "$locus0: Max groups expected to be 100, but $max reported"

	max=$(bridge_port_vlan_maxgroups_get "$locus2")
	((max == 0))
	check_err $? "$locus2: Max groups expected to be 0 but $max reported"

	bridge_port_maxgroups_set "$locus0" 0
	check_err $? "$locus0: Failed to set max to 0"

	max=$(bridge_port_maxgroups_get "$locus0")
	((max == 0))
	check_err $? "$locus0: Max groups expected to be 0, but $max reported"

	max=$(bridge_port_vlan_maxgroups_get "$locus2")
	((max == 0))
	check_err $? "$locus2: Max groups expected to be 0, but $max reported"

	log_test "$CFG: port_vlan maxgroups: isolation of port and per-VLAN maximums"
}

test_8021qvs_maxgroups_zero_cross_vlan_cfg4()
{
	test_maxgroups_zero_cross_vlan cfg4
}

test_8021qvs_maxgroups_zero_cross_vlan_ctl4()
{
	test_maxgroups_zero_cross_vlan ctl4
}

test_8021qvs_maxgroups_zero_cross_vlan_cfg6()
{
	test_maxgroups_zero_cross_vlan cfg6
}

test_8021qvs_maxgroups_zero_cross_vlan_ctl6()
{
	test_maxgroups_zero_cross_vlan ctl6
}

test_maxgroups_too_low()
{
	local CFG=$1; shift
	local context=$1; shift
	local locus=$1; shift

	RET=0

	local n=$(bridge_${context}_ngroups_get "$locus")
	local msg

	${CFG}_entries_add "$locus" temp 5 111
	check_err $? "$locus: Couldn't add MDB entries"

	bridge_${context}_maxgroups_set "$locus" $((n+2))
	check_err $? "$locus: Setting maxgroups to $((n+2)) failed"

	msg=$(${CFG}_entries_add "$locus" temp 2 112 2>&1)
	check_fail $? "$locus: Adding more entries passed when max<n"
	bridge_maxgroups_errmsg_check_cfg "$msg"

	${CFG}_entries_del "$locus" temp 5 111
	check_err $? "$locus: Couldn't delete MDB entries"

	${CFG}_entries_add "$locus" temp 2 112
	check_err $? "$locus: Adding more entries failed"

	${CFG}_entries_del "$locus" temp 2 112
	check_err $? "$locus: Deleting more entries failed"

	bridge_${context}_maxgroups_set "$locus" 0
	check_err $? "$locus: Couldn't set maximum to 0"

	log_test "$CFG: $context maxgroups: configure below ngroups"
}

test_8021d_maxgroups_too_low_cfg4()
{
	test_maxgroups_too_low cfg4 port "dev $swp1"
}

test_8021d_maxgroups_too_low_ctl4()
{
	test_maxgroups_too_low ctl4 port "dev $swp1"
}

test_8021d_maxgroups_too_low_cfg6()
{
	test_maxgroups_too_low cfg6 port "dev $swp1"
}

test_8021d_maxgroups_too_low_ctl6()
{
	test_maxgroups_too_low ctl6 port "dev $swp1"
}

test_8021q_maxgroups_too_low_cfg4()
{
	test_maxgroups_too_low cfg4 port "dev $swp1 vid 10"
}

test_8021q_maxgroups_too_low_ctl4()
{
	test_maxgroups_too_low ctl4 port "dev $swp1 vid 10"
}

test_8021q_maxgroups_too_low_cfg6()
{
	test_maxgroups_too_low cfg6 port "dev $swp1 vid 10"
}

test_8021q_maxgroups_too_low_ctl6()
{
	test_maxgroups_too_low ctl6 port "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_too_low_cfg4()
{
	test_maxgroups_too_low cfg4 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_too_low_ctl4()
{
	test_maxgroups_too_low ctl4 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_too_low_cfg6()
{
	test_maxgroups_too_low cfg6 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_too_low_ctl6()
{
	test_maxgroups_too_low ctl6 port_vlan "dev $swp1 vid 10"
}

test_maxgroups_too_many_entries()
{
	local CFG=$1; shift
	local context=$1; shift
	local locus=$1; shift

	RET=0

	local n=$(bridge_${context}_ngroups_get "$locus")
	local msg

	# Configure a low maximum
	bridge_${context}_maxgroups_set "$locus" $((n+1))
	check_err $? "$locus: Couldn't set maximum"

	# Try to add more entries than the configured maximum
	msg=$(${CFG}_entries_add "$locus" temp 5 2>&1)
	check_fail $? "Adding 5 MDB entries passed, but should have failed"
	bridge_maxgroups_errmsg_check_${CFG} "$msg"

	# When adding entries through the control path, as many as possible
	# get created. That's consistent with the mcast_hash_max behavior.
	# So there, drop the entries explicitly.
	if [[ ${CFG%[46]} == ctl ]]; then
		${CFG}_entries_del "$locus" temp 17 2>&1
	fi

	local n2=$(bridge_${context}_ngroups_get "$locus")
	((n2 == n))
	check_err $? "Number of groups was $n, but after a failed attempt to add MDB entries it changed to $n2"

	bridge_${context}_maxgroups_set "$locus" 0
	check_err $? "$locus: Couldn't set maximum to 0"

	log_test "$CFG: $context maxgroups: add too many MDB entries"
}

test_8021d_maxgroups_too_many_entries_cfg4()
{
	test_maxgroups_too_many_entries cfg4 port "dev $swp1"
}

test_8021d_maxgroups_too_many_entries_ctl4()
{
	test_maxgroups_too_many_entries ctl4 port "dev $swp1"
}

test_8021d_maxgroups_too_many_entries_cfg6()
{
	test_maxgroups_too_many_entries cfg6 port "dev $swp1"
}

test_8021d_maxgroups_too_many_entries_ctl6()
{
	test_maxgroups_too_many_entries ctl6 port "dev $swp1"
}

test_8021q_maxgroups_too_many_entries_cfg4()
{
	test_maxgroups_too_many_entries cfg4 port "dev $swp1 vid 10"
}

test_8021q_maxgroups_too_many_entries_ctl4()
{
	test_maxgroups_too_many_entries ctl4 port "dev $swp1 vid 10"
}

test_8021q_maxgroups_too_many_entries_cfg6()
{
	test_maxgroups_too_many_entries cfg6 port "dev $swp1 vid 10"
}

test_8021q_maxgroups_too_many_entries_ctl6()
{
	test_maxgroups_too_many_entries ctl6 port "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_too_many_entries_cfg4()
{
	test_maxgroups_too_many_entries cfg4 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_too_many_entries_ctl4()
{
	test_maxgroups_too_many_entries ctl4 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_too_many_entries_cfg6()
{
	test_maxgroups_too_many_entries cfg6 port_vlan "dev $swp1 vid 10"
}

test_8021qvs_maxgroups_too_many_entries_ctl6()
{
	test_maxgroups_too_many_entries ctl6 port_vlan "dev $swp1 vid 10"
}

test_maxgroups_too_many_cross_vlan()
{
	local CFG=$1; shift

	RET=0

	local locus0="dev $swp1"
	local locus1="dev $swp1 vid 10"
	local locus2="dev $swp1 vid 20"
	local n1=$(bridge_port_vlan_ngroups_get "$locus1")
	local n2=$(bridge_port_vlan_ngroups_get "$locus2")
	local msg

	if ((n1 > n2)); then
		local tmp=$n1
		n1=$n2
		n2=$tmp

		tmp="$locus1"
		locus1="$locus2"
		locus2="$tmp"
	fi

	# Now 0 <= n1 <= n2.
	${CFG}_entries_add "$locus2" temp 5 112
	check_err $? "Couldn't add 5 entries"

	n2=$(bridge_port_vlan_ngroups_get "$locus2")
	# Now 0 <= n1 < n2-1.

	# Setting locus1'maxgroups to n2-1 should pass. The number is
	# smaller than both the absolute number of MDB entries, and in
	# particular than number of locus2's number of entries, but it is
	# large enough to cover locus1's entries. Thus we check that
	# individual VLAN's ngroups are independent.
	bridge_port_vlan_maxgroups_set "$locus1" $((n2-1))
	check_err $? "Setting ${locus1}'s maxgroups to $((n2-1)) failed"

	msg=$(${CFG}_entries_add "$locus1" temp $n2 111 2>&1)
	check_fail $? "$locus1: Adding $n2 MDB entries passed, but should have failed"
	bridge_maxgroups_errmsg_check_${CFG} "$msg"

	bridge_port_maxgroups_set "$locus0" $((n1 + n2 + 2))
	check_err $? "$locus0: Couldn't set maximum"

	msg=$(${CFG}_entries_add "$locus1" temp 5 111 2>&1)
	check_fail $? "$locus1: Adding 5 MDB entries passed, but should have failed"
	bridge_maxgroups_errmsg_check_${CFG} "$msg"

	# IGMP/MLD packets can cause several entries to be added, before
	# the maximum is hit and the rest is then bounced. Remove what was
	# committed, if anything.
	${CFG}_entries_del "$locus1" temp 5 111 2>/dev/null

	${CFG}_entries_add "$locus1" temp 2 111
	check_err $? "$locus1: Adding 2 MDB entries failed, but should have passed"

	${CFG}_entries_del "$locus1" temp 2 111
	check_err $? "Couldn't delete MDB entries"

	${CFG}_entries_del "$locus2" temp 5 112
	check_err $? "Couldn't delete MDB entries"

	bridge_port_vlan_maxgroups_set "$locus1" 0
	check_err $? "$locus1: Couldn't set maximum to 0"

	bridge_port_maxgroups_set "$locus0" 0
	check_err $? "$locus0: Couldn't set maximum to 0"

	log_test "$CFG: port_vlan maxgroups: isolation of port and per-VLAN ngroups"
}

test_8021qvs_maxgroups_too_many_cross_vlan_cfg4()
{
	test_maxgroups_too_many_cross_vlan cfg4
}

test_8021qvs_maxgroups_too_many_cross_vlan_ctl4()
{
	test_maxgroups_too_many_cross_vlan ctl4
}

test_8021qvs_maxgroups_too_many_cross_vlan_cfg6()
{
	test_maxgroups_too_many_cross_vlan cfg6
}

test_8021qvs_maxgroups_too_many_cross_vlan_ctl6()
{
	test_maxgroups_too_many_cross_vlan ctl6
}

test_vlan_attributes()
{
	local locus=$1; shift
	local expect=$1; shift

	RET=0

	local max=$(bridge_port_vlan_maxgroups_get "$locus")
	local n=$(bridge_port_vlan_ngroups_get "$locus")

	eval "[[ $max $expect ]]"
	check_err $? "$locus: maxgroups attribute expected to be $expect, but was $max"

	eval "[[ $n $expect ]]"
	check_err $? "$locus: ngroups attribute expected to be $expect, but was $n"

	log_test "port_vlan: presence of ngroups and maxgroups attributes"
}

test_8021q_vlan_attributes()
{
	test_vlan_attributes "dev $swp1 vid 10" "== null"
}

test_8021qvs_vlan_attributes()
{
	test_vlan_attributes "dev $swp1 vid 10" "-ge 0"
}

test_toggle_vlan_snooping()
{
	local mode=$1; shift

	RET=0

	local CFG=cfg4
	local context=port_vlan
	local locus="dev $swp1 vid 10"

	${CFG}_entries_add "$locus" $mode 5
	check_err $? "Couldn't add MDB entries"

	bridge_${context}_maxgroups_set "$locus" 100
	check_err $? "Failed to set max to 100"

	ip link set dev br0 type bridge mcast_vlan_snooping 0
	sleep 1
	ip link set dev br0 type bridge mcast_vlan_snooping 1

	local n=$(bridge_${context}_ngroups_get "$locus")
	local nn=$(bridge mdb show dev br0 | grep $swp1 | wc -l)
	((nn == n))
	check_err $? "mcast_n_groups expected to be $nn, but $n reported"

	local max=$(bridge_${context}_maxgroups_get "$locus")
	((max == 100))
	check_err $? "Max groups expected to be 100 but $max reported"

	bridge_${context}_maxgroups_set "$locus" 0
	check_err $? "Failed to set max to 0"

	log_test "$CFG: $context: $mode: mcast_vlan_snooping toggle"
}

test_toggle_vlan_snooping_temp()
{
	test_toggle_vlan_snooping temp
}

test_toggle_vlan_snooping_permanent()
{
	test_toggle_vlan_snooping permanent
}

# ngroup test suites

test_8021d_ngroups_cfg4()
{
	test_8021d_ngroups_reporting_cfg4
}

test_8021d_ngroups_ctl4()
{
	test_8021d_ngroups_reporting_ctl4
}

test_8021d_ngroups_cfg6()
{
	test_8021d_ngroups_reporting_cfg6
}

test_8021d_ngroups_ctl6()
{
	test_8021d_ngroups_reporting_ctl6
}

test_8021q_ngroups_cfg4()
{
	test_8021q_ngroups_reporting_cfg4
}

test_8021q_ngroups_ctl4()
{
	test_8021q_ngroups_reporting_ctl4
}

test_8021q_ngroups_cfg6()
{
	test_8021q_ngroups_reporting_cfg6
}

test_8021q_ngroups_ctl6()
{
	test_8021q_ngroups_reporting_ctl6
}

test_8021qvs_ngroups_cfg4()
{
	test_8021qvs_ngroups_reporting_cfg4
	test_8021qvs_ngroups_cross_vlan_cfg4
}

test_8021qvs_ngroups_ctl4()
{
	test_8021qvs_ngroups_reporting_ctl4
	test_8021qvs_ngroups_cross_vlan_ctl4
}

test_8021qvs_ngroups_cfg6()
{
	test_8021qvs_ngroups_reporting_cfg6
	test_8021qvs_ngroups_cross_vlan_cfg6
}

test_8021qvs_ngroups_ctl6()
{
	test_8021qvs_ngroups_reporting_ctl6
	test_8021qvs_ngroups_cross_vlan_ctl6
}

# maxgroups test suites

test_8021d_maxgroups_cfg4()
{
	test_8021d_maxgroups_zero_cfg4
	test_8021d_maxgroups_too_low_cfg4
	test_8021d_maxgroups_too_many_entries_cfg4
}

test_8021d_maxgroups_ctl4()
{
	test_8021d_maxgroups_zero_ctl4
	test_8021d_maxgroups_too_low_ctl4
	test_8021d_maxgroups_too_many_entries_ctl4
}

test_8021d_maxgroups_cfg6()
{
	test_8021d_maxgroups_zero_cfg6
	test_8021d_maxgroups_too_low_cfg6
	test_8021d_maxgroups_too_many_entries_cfg6
}

test_8021d_maxgroups_ctl6()
{
	test_8021d_maxgroups_zero_ctl6
	test_8021d_maxgroups_too_low_ctl6
	test_8021d_maxgroups_too_many_entries_ctl6
}

test_8021q_maxgroups_cfg4()
{
	test_8021q_maxgroups_zero_cfg4
	test_8021q_maxgroups_too_low_cfg4
	test_8021q_maxgroups_too_many_entries_cfg4
}

test_8021q_maxgroups_ctl4()
{
	test_8021q_maxgroups_zero_ctl4
	test_8021q_maxgroups_too_low_ctl4
	test_8021q_maxgroups_too_many_entries_ctl4
}

test_8021q_maxgroups_cfg6()
{
	test_8021q_maxgroups_zero_cfg6
	test_8021q_maxgroups_too_low_cfg6
	test_8021q_maxgroups_too_many_entries_cfg6
}

test_8021q_maxgroups_ctl6()
{
	test_8021q_maxgroups_zero_ctl6
	test_8021q_maxgroups_too_low_ctl6
	test_8021q_maxgroups_too_many_entries_ctl6
}

test_8021qvs_maxgroups_cfg4()
{
	test_8021qvs_maxgroups_zero_cfg4
	test_8021qvs_maxgroups_zero_cross_vlan_cfg4
	test_8021qvs_maxgroups_too_low_cfg4
	test_8021qvs_maxgroups_too_many_entries_cfg4
	test_8021qvs_maxgroups_too_many_cross_vlan_cfg4
}

test_8021qvs_maxgroups_ctl4()
{
	test_8021qvs_maxgroups_zero_ctl4
	test_8021qvs_maxgroups_zero_cross_vlan_ctl4
	test_8021qvs_maxgroups_too_low_ctl4
	test_8021qvs_maxgroups_too_many_entries_ctl4
	test_8021qvs_maxgroups_too_many_cross_vlan_ctl4
}

test_8021qvs_maxgroups_cfg6()
{
	test_8021qvs_maxgroups_zero_cfg6
	test_8021qvs_maxgroups_zero_cross_vlan_cfg6
	test_8021qvs_maxgroups_too_low_cfg6
	test_8021qvs_maxgroups_too_many_entries_cfg6
	test_8021qvs_maxgroups_too_many_cross_vlan_cfg6
}

test_8021qvs_maxgroups_ctl6()
{
	test_8021qvs_maxgroups_zero_ctl6
	test_8021qvs_maxgroups_zero_cross_vlan_ctl6
	test_8021qvs_maxgroups_too_low_ctl6
	test_8021qvs_maxgroups_too_many_entries_ctl6
	test_8021qvs_maxgroups_too_many_cross_vlan_ctl6
}

# other test suites

test_8021qvs_toggle_vlan_snooping()
{
	test_toggle_vlan_snooping_temp
	test_toggle_vlan_snooping_permanent
}

# test groups

test_8021d()
{
	# Tests for vlan_filtering 0 mcast_vlan_snooping 0.

	switch_create_8021d
	setup_wait

	test_8021d_ngroups_cfg4
	test_8021d_ngroups_ctl4
	test_8021d_ngroups_cfg6
	test_8021d_ngroups_ctl6
	test_8021d_maxgroups_cfg4
	test_8021d_maxgroups_ctl4
	test_8021d_maxgroups_cfg6
	test_8021d_maxgroups_ctl6

	switch_destroy
}

test_8021q()
{
	# Tests for vlan_filtering 1 mcast_vlan_snooping 0.

	switch_create_8021q
	setup_wait

	test_8021q_vlan_attributes
	test_8021q_ngroups_cfg4
	test_8021q_ngroups_ctl4
	test_8021q_ngroups_cfg6
	test_8021q_ngroups_ctl6
	test_8021q_maxgroups_cfg4
	test_8021q_maxgroups_ctl4
	test_8021q_maxgroups_cfg6
	test_8021q_maxgroups_ctl6

	switch_destroy
}

test_8021qvs()
{
	# Tests for vlan_filtering 1 mcast_vlan_snooping 1.

	switch_create_8021qvs
	setup_wait

	test_8021qvs_vlan_attributes
	test_8021qvs_ngroups_cfg4
	test_8021qvs_ngroups_ctl4
	test_8021qvs_ngroups_cfg6
	test_8021qvs_ngroups_ctl6
	test_8021qvs_maxgroups_cfg4
	test_8021qvs_maxgroups_ctl4
	test_8021qvs_maxgroups_cfg6
	test_8021qvs_maxgroups_ctl6
	test_8021qvs_toggle_vlan_snooping

	switch_destroy
}

trap cleanup EXIT

setup_prepare
tests_run

exit $EXIT_STATUS
