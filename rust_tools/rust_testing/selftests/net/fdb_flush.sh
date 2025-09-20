#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test is for checking functionality of flushing FDB entries.
# Check that flush works as expected with all the supported arguments and verify
# some combinations of arguments.

source lib.sh

FLUSH_BY_STATE_TESTS="
	vxlan_test_flush_by_permanent
	vxlan_test_flush_by_nopermanent
	vxlan_test_flush_by_static
	vxlan_test_flush_by_nostatic
	vxlan_test_flush_by_dynamic
	vxlan_test_flush_by_nodynamic
"

FLUSH_BY_FLAG_TESTS="
	vxlan_test_flush_by_extern_learn
	vxlan_test_flush_by_noextern_learn
	vxlan_test_flush_by_router
	vxlan_test_flush_by_norouter
"

TESTS="
	vxlan_test_flush_by_dev
	vxlan_test_flush_by_vni
	vxlan_test_flush_by_src_vni
	vxlan_test_flush_by_port
	vxlan_test_flush_by_dst_ip
	vxlan_test_flush_by_nhid
	$FLUSH_BY_STATE_TESTS
	$FLUSH_BY_FLAG_TESTS
	vxlan_test_flush_by_several_args
	vxlan_test_flush_by_remote_attributes
	bridge_test_flush_by_dev
	bridge_test_flush_by_vlan
	bridge_vxlan_test_flush
"

: ${VERBOSE:=0}
: ${PAUSE_ON_FAIL:=no}
: ${PAUSE:=no}
: ${VXPORT:=4789}

run_cmd()
{
	local cmd="$1"
	local out
	local rc
	local stderr="2>/dev/null"

	if [ "$VERBOSE" = "1" ]; then
		printf "COMMAND: $cmd\n"
		stderr=
	fi

	out=$(eval $cmd $stderr)
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "    $out"
	fi

	return $rc
}

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"
	local nsuccess
	local nfail
	local ret

	if [ ${rc} -eq ${expected} ]; then
		printf "TEST: %-60s  [ OK ]\n" "${msg}"
		nsuccess=$((nsuccess+1))
	else
		ret=1
		nfail=$((nfail+1))
		printf "TEST: %-60s  [FAIL]\n" "${msg}"
		if [ "$VERBOSE" = "1" ]; then
			echo "    rc=$rc, expected $expected"
		fi

		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
		echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi

	if [ "${PAUSE}" = "yes" ]; then
		echo
		echo "hit enter to continue, 'q' to quit"
		read a
		[ "$a" = "q" ] && exit 1
	fi

	[ "$VERBOSE" = "1" ] && echo
}

MAC_POOL_1="
	de:ad:be:ef:13:10
	de:ad:be:ef:13:11
	de:ad:be:ef:13:12
	de:ad:be:ef:13:13
	de:ad:be:ef:13:14
"
mac_pool_1_len=$(echo "$MAC_POOL_1" | grep -c .)

MAC_POOL_2="
	ca:fe:be:ef:13:10
	ca:fe:be:ef:13:11
	ca:fe:be:ef:13:12
	ca:fe:be:ef:13:13
	ca:fe:be:ef:13:14
"
mac_pool_2_len=$(echo "$MAC_POOL_2" | grep -c .)

fdb_add_mac_pool_1()
{
	local dev=$1; shift
	local args="$@"

	for mac in $MAC_POOL_1
	do
		$BRIDGE fdb add $mac dev $dev $args
	done
}

fdb_add_mac_pool_2()
{
	local dev=$1; shift
	local args="$@"

	for mac in $MAC_POOL_2
	do
		$BRIDGE fdb add $mac dev $dev $args
	done
}

fdb_check_n_entries_by_dev_filter()
{
	local dev=$1; shift
	local exp_entries=$1; shift
	local filter="$@"

	local entries=$($BRIDGE fdb show dev $dev | grep "$filter" | wc -l)

	[[ $entries -eq $exp_entries ]]
	rc=$?

	log_test $rc 0 "$dev: Expected $exp_entries FDB entries, got $entries"
	return $rc
}

vxlan_test_flush_by_dev()
{
	local vni=3000
	local dst_ip=192.0.2.1

	fdb_add_mac_pool_1 vx10 vni $vni dst $dst_ip
	fdb_add_mac_pool_2 vx20 vni $vni dst $dst_ip

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len
	fdb_check_n_entries_by_dev_filter vx20 $mac_pool_2_len

	run_cmd "$BRIDGE fdb flush dev vx10"
	log_test $? 0 "Flush FDB by dev vx10"

	fdb_check_n_entries_by_dev_filter vx10 0
	log_test $? 0 "Flush FDB by dev vx10 - test vx10 entries"

	fdb_check_n_entries_by_dev_filter vx20 $mac_pool_2_len
	log_test $? 0 "Flush FDB by dev vx10 - test vx20 entries"
}

vxlan_test_flush_by_vni()
{
	local vni_1=3000
	local vni_2=4000
	local dst_ip=192.0.2.1

	fdb_add_mac_pool_1 vx10 vni $vni_1 dst $dst_ip
	fdb_add_mac_pool_2 vx10 vni $vni_2 dst $dst_ip

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len vni $vni_1
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_2_len vni $vni_2

	run_cmd "$BRIDGE fdb flush dev vx10 vni $vni_2"
	log_test $? 0 "Flush FDB by dev vx10 and vni $vni_2"

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len vni $vni_1
	log_test $? 0 "Test entries with vni $vni_1"

	fdb_check_n_entries_by_dev_filter vx10 0 vni $vni_2
	log_test $? 0 "Test entries with vni $vni_2"
}

vxlan_test_flush_by_src_vni()
{
	# Set some entries with {vni=x,src_vni=y} and some with the opposite -
	# {vni=y,src_vni=x}, to verify that when we flush by src_vni=x, entries
	# with vni=x are not flused.
	local vni_1=3000
	local vni_2=4000
	local src_vni_1=4000
	local src_vni_2=3000
	local dst_ip=192.0.2.1

	# Reconfigure vx10 with 'external' to get 'src_vni' details in
	# 'bridge fdb' output
	$IP link del dev vx10
	$IP link add name vx10 type vxlan dstport "$VXPORT" external

	fdb_add_mac_pool_1 vx10 vni $vni_1 src_vni $src_vni_1 dst $dst_ip
	fdb_add_mac_pool_2 vx10 vni $vni_2 src_vni $src_vni_2 dst $dst_ip

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len \
		src_vni $src_vni_1
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_2_len \
		src_vni $src_vni_2

	run_cmd "$BRIDGE fdb flush dev vx10 src_vni $src_vni_2"
	log_test $? 0 "Flush FDB by dev vx10 and src_vni $src_vni_2"

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len \
		src_vni $src_vni_1
	log_test $? 0 "Test entries with src_vni $src_vni_1"

	fdb_check_n_entries_by_dev_filter vx10 0 src_vni $src_vni_2
	log_test $? 0 "Test entries with src_vni $src_vni_2"
}

vxlan_test_flush_by_port()
{
	local port_1=1234
	local port_2=4321
	local dst_ip=192.0.2.1

	fdb_add_mac_pool_1 vx10 port $port_1 dst $dst_ip
	fdb_add_mac_pool_2 vx10 port $port_2 dst $dst_ip

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len port $port_1
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_2_len port $port_2

	run_cmd "$BRIDGE fdb flush dev vx10 port $port_2"
	log_test $? 0 "Flush FDB by dev vx10 and port $port_2"

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len port $port_1
	log_test $? 0 "Test entries with port $port_1"

	fdb_check_n_entries_by_dev_filter vx10 0 port $port_2
	log_test $? 0 "Test entries with port $port_2"
}

vxlan_test_flush_by_dst_ip()
{
	local dst_ip_1=192.0.2.1
	local dst_ip_2=192.0.2.2

	fdb_add_mac_pool_1 vx10 dst $dst_ip_1
	fdb_add_mac_pool_2 vx10 dst $dst_ip_2

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len dst $dst_ip_1
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_2_len dst $dst_ip_2

	run_cmd "$BRIDGE fdb flush dev vx10 dst $dst_ip_2"
	log_test $? 0 "Flush FDB by dev vx10 and dst $dst_ip_2"

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len dst $dst_ip_1
	log_test $? 0 "Test entries with dst $dst_ip_1"

	fdb_check_n_entries_by_dev_filter vx10 0 dst $dst_ip_2
	log_test $? 0 "Test entries with dst $dst_ip_2"
}

nexthops_add()
{
	local nhid_1=$1; shift
	local nhid_2=$1; shift

	$IP nexthop add id 10 via 192.0.2.1 fdb
	$IP nexthop add id $nhid_1 group 10 fdb

	$IP nexthop add id 20 via 192.0.2.2 fdb
	$IP nexthop add id $nhid_2 group 20 fdb
}

vxlan_test_flush_by_nhid()
{
	local nhid_1=100
	local nhid_2=200

	nexthops_add $nhid_1 $nhid_2

	fdb_add_mac_pool_1 vx10 nhid $nhid_1
	fdb_add_mac_pool_2 vx10 nhid $nhid_2

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len nhid $nhid_1
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_2_len nhid $nhid_2

	run_cmd "$BRIDGE fdb flush dev vx10 nhid $nhid_2"
	log_test $? 0 "Flush FDB by dev vx10 and nhid $nhid_2"

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len nhid $nhid_1
	log_test $? 0 "Test entries with nhid $nhid_1"

	fdb_check_n_entries_by_dev_filter vx10 0 nhid $nhid_2
	log_test $? 0 "Test entries with nhid $nhid_2"

	# Flush also entries with $nhid_1, and then verify that flushing by
	# 'nhid' does not return an error when there are no entries with
	# nexthops.
	run_cmd "$BRIDGE fdb flush dev vx10 nhid $nhid_1"
	log_test $? 0 "Flush FDB by dev vx10 and nhid $nhid_1"

	fdb_check_n_entries_by_dev_filter vx10 0 nhid
	log_test $? 0 "Test entries with 'nhid' keyword"

	run_cmd "$BRIDGE fdb flush dev vx10 nhid $nhid_1"
	log_test $? 0 "Flush FDB by nhid when there are no entries with nexthop"
}

vxlan_test_flush_by_state()
{
	local flush_by_state=$1; shift
	local state_1=$1; shift
	local exp_state_1=$1; shift
	local state_2=$1; shift
	local exp_state_2=$1; shift

	local dst_ip_1=192.0.2.1
	local dst_ip_2=192.0.2.2

	fdb_add_mac_pool_1 vx10 dst $dst_ip_1 $state_1
	fdb_add_mac_pool_2 vx10 dst $dst_ip_2 $state_2

	# Check the entries by dst_ip as not all states appear in 'bridge fdb'
	# output.
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len dst $dst_ip_1
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_2_len dst $dst_ip_2

	run_cmd "$BRIDGE fdb flush dev vx10 $flush_by_state"
	log_test $? 0 "Flush FDB by dev vx10 and state $flush_by_state"

	fdb_check_n_entries_by_dev_filter vx10 $exp_state_1 dst $dst_ip_1
	log_test $? 0 "Test entries with state $state_1"

	fdb_check_n_entries_by_dev_filter vx10 $exp_state_2 dst $dst_ip_2
	log_test $? 0 "Test entries with state $state_2"
}

vxlan_test_flush_by_permanent()
{
	# Entries that are added without state get 'permanent' state by
	# default, add some entries with flag 'extern_learn' instead of state,
	# so they will be added with 'permanent' and should be flushed also.
	local flush_by_state="permanent"
	local state_1="permanent"
	local exp_state_1=0
	local state_2="extern_learn"
	local exp_state_2=0

	vxlan_test_flush_by_state $flush_by_state $state_1 $exp_state_1 \
		$state_2 $exp_state_2
}

vxlan_test_flush_by_nopermanent()
{
	local flush_by_state="nopermanent"
	local state_1="permanent"
	local exp_state_1=$mac_pool_1_len
	local state_2="static"
	local exp_state_2=0

	vxlan_test_flush_by_state $flush_by_state $state_1 $exp_state_1 \
		$state_2 $exp_state_2
}

vxlan_test_flush_by_static()
{
	local flush_by_state="static"
	local state_1="static"
	local exp_state_1=0
	local state_2="dynamic"
	local exp_state_2=$mac_pool_2_len

	vxlan_test_flush_by_state $flush_by_state $state_1 $exp_state_1 \
		$state_2 $exp_state_2
}

vxlan_test_flush_by_nostatic()
{
	local flush_by_state="nostatic"
	local state_1="permanent"
	local exp_state_1=$mac_pool_1_len
	local state_2="dynamic"
	local exp_state_2=0

	vxlan_test_flush_by_state $flush_by_state $state_1 $exp_state_1 \
		$state_2 $exp_state_2
}

vxlan_test_flush_by_dynamic()
{
	local flush_by_state="dynamic"
	local state_1="dynamic"
	local exp_state_1=0
	local state_2="static"
	local exp_state_2=$mac_pool_2_len

	vxlan_test_flush_by_state $flush_by_state $state_1 $exp_state_1 \
		$state_2 $exp_state_2
}

vxlan_test_flush_by_nodynamic()
{
	local flush_by_state="nodynamic"
	local state_1="permanent"
	local exp_state_1=0
	local state_2="dynamic"
	local exp_state_2=$mac_pool_2_len

	vxlan_test_flush_by_state $flush_by_state $state_1 $exp_state_1 \
		$state_2 $exp_state_2
}

vxlan_test_flush_by_flag()
{
	local flush_by_flag=$1; shift
	local flag_1=$1; shift
	local exp_flag_1=$1; shift
	local flag_2=$1; shift
	local exp_flag_2=$1; shift

	local dst_ip_1=192.0.2.1
	local dst_ip_2=192.0.2.2

	fdb_add_mac_pool_1 vx10 dst $dst_ip_1 $flag_1
	fdb_add_mac_pool_2 vx10 dst $dst_ip_2 $flag_2

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len $flag_1
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_2_len $flag_2

	run_cmd "$BRIDGE fdb flush dev vx10 $flush_by_flag"
	log_test $? 0 "Flush FDB by dev vx10 and flag $flush_by_flag"

	fdb_check_n_entries_by_dev_filter vx10 $exp_flag_1 dst $dst_ip_1
	log_test $? 0 "Test entries with flag $flag_1"

	fdb_check_n_entries_by_dev_filter vx10 $exp_flag_2 dst $dst_ip_2
	log_test $? 0 "Test entries with flag $flag_2"
}

vxlan_test_flush_by_extern_learn()
{
	local flush_by_flag="extern_learn"
	local flag_1="extern_learn"
	local exp_flag_1=0
	local flag_2="router"
	local exp_flag_2=$mac_pool_2_len

	vxlan_test_flush_by_flag $flush_by_flag $flag_1 $exp_flag_1 \
		$flag_2 $exp_flag_2
}

vxlan_test_flush_by_noextern_learn()
{
	local flush_by_flag="noextern_learn"
	local flag_1="extern_learn"
	local exp_flag_1=$mac_pool_1_len
	local flag_2="router"
	local exp_flag_2=0

	vxlan_test_flush_by_flag $flush_by_flag $flag_1 $exp_flag_1 \
		$flag_2 $exp_flag_2
}

vxlan_test_flush_by_router()
{
	local flush_by_flag="router"
	local flag_1="router"
	local exp_flag_1=0
	local flag_2="extern_learn"
	local exp_flag_2=$mac_pool_2_len

	vxlan_test_flush_by_flag $flush_by_flag $flag_1 $exp_flag_1 \
		$flag_2 $exp_flag_2
}

vxlan_test_flush_by_norouter()
{

	local flush_by_flag="norouter"
	local flag_1="router"
	local exp_flag_1=$mac_pool_1_len
	local flag_2="extern_learn"
	local exp_flag_2=0

	vxlan_test_flush_by_flag $flush_by_flag $flag_1 $exp_flag_1 \
		$flag_2 $exp_flag_2
}

vxlan_test_flush_by_several_args()
{
	local dst_ip_1=192.0.2.1
	local dst_ip_2=192.0.2.2
	local state_1=permanent
	local state_2=static
	local vni=3000
	local port=1234
	local nhid=100
	local flag=router
	local flush_args

	################### Flush by 2 args - nhid and flag ####################
	$IP nexthop add id 10 via 192.0.2.1 fdb
	$IP nexthop add id $nhid group 10 fdb

	fdb_add_mac_pool_1 vx10 nhid $nhid $flag $state_1
	fdb_add_mac_pool_2 vx10 nhid $nhid $flag $state_2

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len $state_1
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_2_len $state_2

	run_cmd "$BRIDGE fdb flush dev vx10 nhid $nhid $flag"
	log_test $? 0 "Flush FDB by dev vx10 nhid $nhid $flag"

	# All entries should be flushed as 'state' is not an argument for flush
	# filtering.
	fdb_check_n_entries_by_dev_filter vx10 0 $state_1
	log_test $? 0 "Test entries with state $state_1"

	fdb_check_n_entries_by_dev_filter vx10 0 $state_2
	log_test $? 0 "Test entries with state $state_2"

	################ Flush by 3 args - VNI, port and dst_ip ################
	fdb_add_mac_pool_1 vx10 vni $vni port $port dst $dst_ip_1
	fdb_add_mac_pool_2 vx10 vni $vni port $port dst $dst_ip_2

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len dst $dst_ip_1
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_2_len dst $dst_ip_2

	flush_args="vni $vni port $port dst $dst_ip_2"
	run_cmd "$BRIDGE fdb flush dev vx10 $flush_args"
	log_test $? 0 "Flush FDB by dev vx10 $flush_args"

	# Only entries with $dst_ip_2 should be flushed, even the rest arguments
	# match the filter, the flush should be AND of all the arguments.
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len dst $dst_ip_1
	log_test $? 0 "Test entries with dst $dst_ip_1"

	fdb_check_n_entries_by_dev_filter vx10 0 dst $dst_ip_2
	log_test $? 0 "Test entries with dst $dst_ip_2"
}

multicast_fdb_entries_add()
{
	mac=00:00:00:00:00:00
	vnis=(2000 3000)

	for vni in "${vnis[@]}"; do
		$BRIDGE fdb append $mac dev vx10 dst 192.0.2.1 vni $vni \
			src_vni 5000
		$BRIDGE fdb append $mac dev vx10 dst 192.0.2.1 vni $vni \
			port 1111
		$BRIDGE fdb append $mac dev vx10 dst 192.0.2.2 vni $vni \
			port 2222
	done
}

vxlan_test_flush_by_remote_attributes()
{
	local flush_args

	# Reconfigure vx10 with 'external' to get 'src_vni' details in
	# 'bridge fdb' output
	$IP link del dev vx10
	$IP link add name vx10 type vxlan dstport "$VXPORT" external

	# For multicast FDB entries, the VXLAN driver stores a linked list of
	# remotes for a given key. Verify that only the expected remotes are
	# flushed.
	multicast_fdb_entries_add

	## Flush by 3 remote's attributes - destination IP, port and VNI ##
	flush_args="dst 192.0.2.1 port 1111 vni 2000"
	fdb_check_n_entries_by_dev_filter vx10 1 $flush_args

	t0_n_entries=$($BRIDGE fdb show dev vx10 | wc -l)
	run_cmd "$BRIDGE fdb flush dev vx10 $flush_args"
	log_test $? 0 "Flush FDB by dev vx10 $flush_args"

	fdb_check_n_entries_by_dev_filter vx10 0 $flush_args

	exp_n_entries=$((t0_n_entries - 1))
	t1_n_entries=$($BRIDGE fdb show dev vx10 | wc -l)
	[[ $t1_n_entries -eq $exp_n_entries ]]
	log_test $? 0 "Check how many entries were flushed"

	## Flush by 2 remote's attributes - destination IP and port ##
	flush_args="dst 192.0.2.2 port 2222"

	fdb_check_n_entries_by_dev_filter vx10 2 $flush_args

	t0_n_entries=$($BRIDGE fdb show dev vx10 | wc -l)
	run_cmd "$BRIDGE fdb flush dev vx10 $flush_args"
	log_test $? 0 "Flush FDB by dev vx10 $flush_args"

	fdb_check_n_entries_by_dev_filter vx10 0 $flush_args

	exp_n_entries=$((t0_n_entries - 2))
	t1_n_entries=$($BRIDGE fdb show dev vx10 | wc -l)
	[[ $t1_n_entries -eq $exp_n_entries ]]
	log_test $? 0 "Check how many entries were flushed"

	## Flush by source VNI, which is not remote's attribute and VNI ##
	flush_args="vni 3000 src_vni 5000"

	fdb_check_n_entries_by_dev_filter vx10 1 $flush_args

	t0_n_entries=$($BRIDGE fdb show dev vx10 | wc -l)
	run_cmd "$BRIDGE fdb flush dev vx10 $flush_args"
	log_test $? 0 "Flush FDB by dev vx10 $flush_args"

	fdb_check_n_entries_by_dev_filter vx10 0 $flush_args

	exp_n_entries=$((t0_n_entries -1))
	t1_n_entries=$($BRIDGE fdb show dev vx10 | wc -l)
	[[ $t1_n_entries -eq $exp_n_entries ]]
	log_test $? 0 "Check how many entries were flushed"

	# Flush by 1 remote's attribute - destination IP ##
	flush_args="dst 192.0.2.1"

	fdb_check_n_entries_by_dev_filter vx10 2 $flush_args

	t0_n_entries=$($BRIDGE fdb show dev vx10 | wc -l)
	run_cmd "$BRIDGE fdb flush dev vx10 $flush_args"
	log_test $? 0 "Flush FDB by dev vx10 $flush_args"

	fdb_check_n_entries_by_dev_filter vx10 0 $flush_args

	exp_n_entries=$((t0_n_entries -2))
	t1_n_entries=$($BRIDGE fdb show dev vx10 | wc -l)
	[[ $t1_n_entries -eq $exp_n_entries ]]
	log_test $? 0 "Check how many entries were flushed"
}

bridge_test_flush_by_dev()
{
	local dst_ip=192.0.2.1
	local br0_n_ent_t0=$($BRIDGE fdb show dev br0 | wc -l)
	local br1_n_ent_t0=$($BRIDGE fdb show dev br1 | wc -l)

	fdb_add_mac_pool_1 br0 dst $dst_ip
	fdb_add_mac_pool_2 br1 dst $dst_ip

	# Each 'fdb add' command adds one extra entry in the bridge with the
	# default vlan.
	local exp_br0_n_ent=$(($br0_n_ent_t0 + 2 * $mac_pool_1_len))
	local exp_br1_n_ent=$(($br1_n_ent_t0 + 2 * $mac_pool_2_len))

	fdb_check_n_entries_by_dev_filter br0 $exp_br0_n_ent
	fdb_check_n_entries_by_dev_filter br1 $exp_br1_n_ent

	run_cmd "$BRIDGE fdb flush dev br0"
	log_test $? 0 "Flush FDB by dev br0"

	# The default entry should not be flushed
	fdb_check_n_entries_by_dev_filter br0 1
	log_test $? 0 "Flush FDB by dev br0 - test br0 entries"

	fdb_check_n_entries_by_dev_filter br1 $exp_br1_n_ent
	log_test $? 0 "Flush FDB by dev br0 - test br1 entries"
}

bridge_test_flush_by_vlan()
{
	local vlan_1=10
	local vlan_2=20
	local vlan_1_ent_t0
	local vlan_2_ent_t0

	$BRIDGE vlan add vid $vlan_1 dev br0 self
	$BRIDGE vlan add vid $vlan_2 dev br0 self

	vlan_1_ent_t0=$($BRIDGE fdb show dev br0 | grep "vlan $vlan_1" | wc -l)
	vlan_2_ent_t0=$($BRIDGE fdb show dev br0 | grep "vlan $vlan_2" | wc -l)

	fdb_add_mac_pool_1 br0 vlan $vlan_1
	fdb_add_mac_pool_2 br0 vlan $vlan_2

	local exp_vlan_1_ent=$(($vlan_1_ent_t0 + $mac_pool_1_len))
	local exp_vlan_2_ent=$(($vlan_2_ent_t0 + $mac_pool_2_len))

	fdb_check_n_entries_by_dev_filter br0 $exp_vlan_1_ent vlan $vlan_1
	fdb_check_n_entries_by_dev_filter br0 $exp_vlan_2_ent vlan $vlan_2

	run_cmd "$BRIDGE fdb flush dev br0 vlan $vlan_1"
	log_test $? 0 "Flush FDB by dev br0 and vlan $vlan_1"

	fdb_check_n_entries_by_dev_filter br0 0 vlan $vlan_1
	log_test $? 0 "Test entries with vlan $vlan_1"

	fdb_check_n_entries_by_dev_filter br0 $exp_vlan_2_ent vlan $vlan_2
	log_test $? 0 "Test entries with vlan $vlan_2"
}

bridge_vxlan_test_flush()
{
	local vlan_1=10
	local dst_ip=192.0.2.1

	$IP link set dev vx10 master br0
	$BRIDGE vlan add vid $vlan_1 dev br0 self
	$BRIDGE vlan add vid $vlan_1 dev vx10

	fdb_add_mac_pool_1 vx10 vni 3000 dst $dst_ip self master

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len vlan $vlan_1
	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len vni 3000

	# Such command should fail in VXLAN driver as vlan is not supported,
	# but the command should flush the entries in the bridge
	run_cmd "$BRIDGE fdb flush dev vx10 vlan $vlan_1 master self"
	log_test $? 255 \
		"Flush FDB by dev vx10, vlan $vlan_1, master and self"

	fdb_check_n_entries_by_dev_filter vx10 0 vlan $vlan_1
	log_test $? 0 "Test entries with vlan $vlan_1"

	fdb_check_n_entries_by_dev_filter vx10 $mac_pool_1_len dst $dst_ip
	log_test $? 0 "Test entries with dst $dst_ip"
}

setup()
{
	setup_ns NS
	IP="ip -netns ${NS}"
	BRIDGE="bridge -netns ${NS}"

	$IP link add name vx10 type vxlan id 1000 dstport "$VXPORT"
	$IP link add name vx20 type vxlan id 2000 dstport "$VXPORT"

	$IP link add br0 type bridge vlan_filtering 1
	$IP link add br1 type bridge vlan_filtering 1
}

cleanup()
{
	$IP link del dev br1
	$IP link del dev br0

	$IP link del dev vx20
	$IP link del dev vx10

	cleanup_ns ${NS}
}

################################################################################
# main

while getopts :t:pPhvw: o
do
	case $o in
		t) TESTS=$OPTARG;;
		p) PAUSE_ON_FAIL=yes;;
		P) PAUSE=yes;;
		v) VERBOSE=$(($VERBOSE + 1));;
		w) PING_TIMEOUT=$OPTARG;;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

# make sure we don't pause twice
[ "${PAUSE}" = "yes" ] && PAUSE_ON_FAIL=no

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit $ksft_skip;
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

# Check a flag that is added to flush command as part of VXLAN flush support
bridge fdb help 2>&1 | grep -q "\[no\]router"
if [ $? -ne 0 ]; then
	echo "SKIP: iproute2 too old, missing flush command for VXLAN"
	exit $ksft_skip
fi

ip link add dev vx10 type vxlan id 1000 2> /dev/null
out=$(bridge fdb flush dev vx10 2>&1 | grep -q "Operation not supported")
if [ $? -eq 0 ]; then
	echo "SKIP: kernel lacks vxlan flush support"
	exit $ksft_skip
fi
ip link del dev vx10

for t in $TESTS
do
	setup; $t; cleanup;
done
