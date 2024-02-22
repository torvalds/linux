#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test bonding options with mode 1,5,6

ALL_TESTS="
	prio
	arp_validate
	num_grat_arp
"

lib_dir=$(dirname "$0")
source ${lib_dir}/bond_topo_3d1c.sh

skip_prio()
{
	local skip=1

	# check if iproute support prio option
	ip -n ${s_ns} link set eth0 type bond_slave prio 10
	[[ $? -ne 0 ]] && skip=0

	# check if kernel support prio option
	ip -n ${s_ns} -d link show eth0 | grep -q "prio 10"
	[[ $? -ne 0 ]] && skip=0

	return $skip
}

skip_ns()
{
	local skip=1

	# check if iproute support ns_ip6_target option
	ip -n ${s_ns} link add bond1 type bond ns_ip6_target ${g_ip6}
	[[ $? -ne 0 ]] && skip=0

	# check if kernel support ns_ip6_target option
	ip -n ${s_ns} -d link show bond1 | grep -q "ns_ip6_target ${g_ip6}"
	[[ $? -ne 0 ]] && skip=0

	ip -n ${s_ns} link del bond1

	return $skip
}

active_slave=""
check_active_slave()
{
	local target_active_slave=$1
	active_slave=$(cmd_jq "ip -n ${s_ns} -d -j link show bond0" ".[].linkinfo.info_data.active_slave")
	test "$active_slave" = "$target_active_slave"
	check_err $? "Current active slave is $active_slave but not $target_active_slave"
}


# Test bonding prio option
prio_test()
{
	local param="$1"
	RET=0

	# create bond
	bond_reset "${param}"
	# set active_slave to primary eth1 specifically
	ip -n ${s_ns} link set bond0 type bond active_slave eth1

	# check bonding member prio value
	ip -n ${s_ns} link set eth0 type bond_slave prio 0
	ip -n ${s_ns} link set eth1 type bond_slave prio 10
	ip -n ${s_ns} link set eth2 type bond_slave prio 11
	cmd_jq "ip -n ${s_ns} -d -j link show eth0" \
		".[].linkinfo.info_slave_data | select (.prio == 0)" "-e" &> /dev/null
	check_err $? "eth0 prio is not 0"
	cmd_jq "ip -n ${s_ns} -d -j link show eth1" \
		".[].linkinfo.info_slave_data | select (.prio == 10)" "-e" &> /dev/null
	check_err $? "eth1 prio is not 10"
	cmd_jq "ip -n ${s_ns} -d -j link show eth2" \
		".[].linkinfo.info_slave_data | select (.prio == 11)" "-e" &> /dev/null
	check_err $? "eth2 prio is not 11"

	bond_check_connection "setup"

	# active slave should be the primary slave
	check_active_slave eth1

	# active slave should be the higher prio slave
	ip -n ${s_ns} link set $active_slave down
	bond_check_connection "fail over"
	check_active_slave eth2

	# when only 1 slave is up
	ip -n ${s_ns} link set $active_slave down
	bond_check_connection "only 1 slave up"
	check_active_slave eth0

	# when a higher prio slave change to up
	ip -n ${s_ns} link set eth2 up
	bond_check_connection "higher prio slave up"
	case $primary_reselect in
		"0")
			check_active_slave "eth2"
			;;
		"1")
			check_active_slave "eth0"
			;;
		"2")
			check_active_slave "eth0"
			;;
	esac
	local pre_active_slave=$active_slave

	# when the primary slave change to up
	ip -n ${s_ns} link set eth1 up
	bond_check_connection "primary slave up"
	case $primary_reselect in
		"0")
			check_active_slave "eth1"
			;;
		"1")
			check_active_slave "$pre_active_slave"
			;;
		"2")
			check_active_slave "$pre_active_slave"
			ip -n ${s_ns} link set $active_slave down
			bond_check_connection "pre_active slave down"
			check_active_slave "eth1"
			;;
	esac

	# Test changing bond slave prio
	if [[ "$primary_reselect" == "0" ]];then
		ip -n ${s_ns} link set eth0 type bond_slave prio 1000000
		ip -n ${s_ns} link set eth1 type bond_slave prio 0
		ip -n ${s_ns} link set eth2 type bond_slave prio -50
		ip -n ${s_ns} -d link show eth0 | grep -q 'prio 1000000'
		check_err $? "eth0 prio is not 1000000"
		ip -n ${s_ns} -d link show eth1 | grep -q 'prio 0'
		check_err $? "eth1 prio is not 0"
		ip -n ${s_ns} -d link show eth2 | grep -q 'prio -50'
		check_err $? "eth3 prio is not -50"
		check_active_slave "eth1"

		ip -n ${s_ns} link set $active_slave down
		bond_check_connection "change slave prio"
		check_active_slave "eth0"
	fi
}

prio_miimon()
{
	local primary_reselect
	local mode=$1

	for primary_reselect in 0 1 2; do
		prio_test "mode $mode miimon 100 primary eth1 primary_reselect $primary_reselect"
		log_test "prio" "$mode miimon primary_reselect $primary_reselect"
	done
}

prio_arp()
{
	local primary_reselect
	local mode=$1

	for primary_reselect in 0 1 2; do
		prio_test "mode $mode arp_interval 100 arp_ip_target ${g_ip4} primary eth1 primary_reselect $primary_reselect"
		log_test "prio" "$mode arp_ip_target primary_reselect $primary_reselect"
	done
}

prio_ns()
{
	local primary_reselect
	local mode=$1

	if skip_ns; then
		log_test_skip "prio ns" "Current iproute or kernel doesn't support bond option 'ns_ip6_target'."
		return 0
	fi

	for primary_reselect in 0 1 2; do
		prio_test "mode $mode arp_interval 100 ns_ip6_target ${g_ip6} primary eth1 primary_reselect $primary_reselect"
		log_test "prio" "$mode ns_ip6_target primary_reselect $primary_reselect"
	done
}

prio()
{
	local mode modes="active-backup balance-tlb balance-alb"

	if skip_prio; then
		log_test_skip "prio" "Current iproute or kernel doesn't support bond option 'prio'."
		return 0
	fi

	for mode in $modes; do
		prio_miimon $mode
	done
	prio_arp "active-backup"
	prio_ns "active-backup"
}

arp_validate_test()
{
	local param="$1"
	RET=0

	# create bond
	bond_reset "${param}"

	bond_check_connection
	[ $RET -ne 0 ] && log_test "arp_validate" "$retmsg"

	# wait for a while to make sure the mii status stable
	sleep 5
	for i in $(seq 0 2); do
		mii_status=$(cmd_jq "ip -n ${s_ns} -j -d link show eth$i" ".[].linkinfo.info_slave_data.mii_status")
		if [ ${mii_status} != "UP" ]; then
			RET=1
			log_test "arp_validate" "interface eth$i mii_status $mii_status"
		fi
	done
}

arp_validate_arp()
{
	local mode=$1
	local val
	for val in $(seq 0 6); do
		arp_validate_test "mode $mode arp_interval 100 arp_ip_target ${g_ip4} arp_validate $val"
		log_test "arp_validate" "$mode arp_ip_target arp_validate $val"
	done
}

arp_validate_ns()
{
	local mode=$1
	local val

	if skip_ns; then
		log_test_skip "arp_validate ns" "Current iproute or kernel doesn't support bond option 'ns_ip6_target'."
		return 0
	fi

	for val in $(seq 0 6); do
		arp_validate_test "mode $mode arp_interval 100 ns_ip6_target ${g_ip6} arp_validate $val"
		log_test "arp_validate" "$mode ns_ip6_target arp_validate $val"
	done
}

arp_validate()
{
	arp_validate_arp "active-backup"
	arp_validate_ns "active-backup"
}

garp_test()
{
	local param="$1"
	local active_slave exp_num real_num i
	RET=0

	# create bond
	bond_reset "${param}"

	bond_check_connection
	[ $RET -ne 0 ] && log_test "num_grat_arp" "$retmsg"


	# Add tc rules to count GARP number
	for i in $(seq 0 2); do
		tc -n ${g_ns} filter add dev s$i ingress protocol arp pref 1 handle 101 \
			flower skip_hw arp_op request arp_sip ${s_ip4} arp_tip ${s_ip4} action pass
	done

	# Do failover
	active_slave=$(cmd_jq "ip -n ${s_ns} -d -j link show bond0" ".[].linkinfo.info_data.active_slave")
	ip -n ${s_ns} link set ${active_slave} down

	exp_num=$(echo "${param}" | cut -f6 -d ' ')
	sleep $((exp_num + 2))

	active_slave=$(cmd_jq "ip -n ${s_ns} -d -j link show bond0" ".[].linkinfo.info_data.active_slave")

	# check result
	real_num=$(tc_rule_handle_stats_get "dev s${active_slave#eth} ingress" 101 ".packets" "-n ${g_ns}")
	if [ "${real_num}" -ne "${exp_num}" ]; then
		echo "$real_num garp packets sent on active slave ${active_slave}"
		RET=1
	fi

	for i in $(seq 0 2); do
		tc -n ${g_ns} filter del dev s$i ingress
	done
}

num_grat_arp()
{
	local val
	for val in 10 20 30 50; do
		garp_test "mode active-backup miimon 100 num_grat_arp $val peer_notify_delay 1000"
		log_test "num_grat_arp" "active-backup miimon num_grat_arp $val"
	done
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
