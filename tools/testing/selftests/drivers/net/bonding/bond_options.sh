#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test bonding options with mode 1,5,6

ALL_TESTS="
	prio
	arp_validate
	num_grat_arp
	fail_over_mac
	vlan_over_bond
"

lib_dir=$(dirname "$0")
source ${lib_dir}/bond_topo_3d1c.sh
c_maddr="33:33:ff:00:00:10"
g_maddr="33:33:ff:00:02:54"

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
active_slave_changed()
{
	local old_active_slave=$1
	local new_active_slave=$(cmd_jq "ip -n ${s_ns} -d -j link show bond0" \
				".[].linkinfo.info_data.active_slave")
	[ "$new_active_slave" != "$old_active_slave" -a "$new_active_slave" != "null" ]
}

check_active_slave()
{
	local target_active_slave=$1
	slowwait 5 active_slave_changed $active_slave
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
	check_active_slave eth2
	bond_check_connection "fail over"

	# when only 1 slave is up
	ip -n ${s_ns} link set $active_slave down
	check_active_slave eth0
	bond_check_connection "only 1 slave up"

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
		check_active_slave "eth0"
		bond_check_connection "change slave prio"
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

wait_mii_up()
{
	for i in $(seq 0 2); do
		mii_status=$(cmd_jq "ip -n ${s_ns} -j -d link show eth$i" ".[].linkinfo.info_slave_data.mii_status")
		[ ${mii_status} != "UP" ] && return 1
	done
	return 0
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
	slowwait 5 wait_mii_up
	for i in $(seq 0 2); do
		mii_status=$(cmd_jq "ip -n ${s_ns} -j -d link show eth$i" ".[].linkinfo.info_slave_data.mii_status")
		if [ ${mii_status} != "UP" ]; then
			RET=1
			log_test "arp_validate" "interface eth$i mii_status $mii_status"
		fi
	done
}

# Testing correct multicast groups are added to slaves for ns targets
arp_validate_mcast()
{
	RET=0
	local arp_valid=$(cmd_jq "ip -n ${s_ns} -j -d link show bond0" ".[].linkinfo.info_data.arp_validate")
	local active_slave=$(cmd_jq "ip -n ${s_ns} -d -j link show bond0" ".[].linkinfo.info_data.active_slave")

	for i in $(seq 0 2); do
		maddr_list=$(ip -n ${s_ns} maddr show dev eth${i})

		# arp_valid == 0 or active_slave should not join any maddrs
		if { [ "$arp_valid" == "null" ] || [ "eth${i}" == ${active_slave} ]; } && \
			echo "$maddr_list" | grep -qE "${c_maddr}|${g_maddr}"; then
			RET=1
			check_err 1 "arp_valid $arp_valid active_slave $active_slave, eth$i has mcast group"
		# arp_valid != 0 and backup_slave should join both maddrs
		elif [ "$arp_valid" != "null" ] && [ "eth${i}" != ${active_slave} ] && \
		     ( ! echo "$maddr_list" | grep -q "${c_maddr}" || \
		       ! echo "$maddr_list" | grep -q "${m_maddr}"); then
			RET=1
			check_err 1 "arp_valid $arp_valid active_slave $active_slave, eth$i has mcast group"
		fi
	done

	# Do failover
	ip -n ${s_ns} link set ${active_slave} down
	# wait for active link change
	slowwait 2 active_slave_changed $active_slave
	active_slave=$(cmd_jq "ip -n ${s_ns} -d -j link show bond0" ".[].linkinfo.info_data.active_slave")

	for i in $(seq 0 2); do
		maddr_list=$(ip -n ${s_ns} maddr show dev eth${i})

		# arp_valid == 0 or active_slave should not join any maddrs
		if { [ "$arp_valid" == "null" ] || [ "eth${i}" == ${active_slave} ]; } && \
			echo "$maddr_list" | grep -qE "${c_maddr}|${g_maddr}"; then
			RET=1
			check_err 1 "arp_valid $arp_valid active_slave $active_slave, eth$i has mcast group"
		# arp_valid != 0 and backup_slave should join both maddrs
		elif [ "$arp_valid" != "null" ] && [ "eth${i}" != ${active_slave} ] && \
		     ( ! echo "$maddr_list" | grep -q "${c_maddr}" || \
		       ! echo "$maddr_list" | grep -q "${m_maddr}"); then
			RET=1
			check_err 1 "arp_valid $arp_valid active_slave $active_slave, eth$i has mcast group"
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
		arp_validate_test "mode $mode arp_interval 100 ns_ip6_target ${g_ip6},${c_ip6} arp_validate $val"
		log_test "arp_validate" "$mode ns_ip6_target arp_validate $val"
		arp_validate_mcast
		log_test "arp_validate" "join mcast group"
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

	# wait for active link change
	slowwait 2 active_slave_changed $active_slave

	exp_num=$(echo "${param}" | cut -f6 -d ' ')
	active_slave=$(cmd_jq "ip -n ${s_ns} -d -j link show bond0" ".[].linkinfo.info_data.active_slave")
	slowwait_for_counter $((exp_num + 5)) $exp_num tc_rule_handle_stats_get \
		"dev s${active_slave#eth} ingress" 101 ".packets" "-n ${g_ns}" &> /dev/null

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
	for val in 10 20 30; do
		garp_test "mode active-backup miimon 10 num_grat_arp $val peer_notify_delay 100"
		log_test "num_grat_arp" "active-backup miimon num_grat_arp $val"
	done
}

check_all_mac_same()
{
	RET=0
	# all slaves should have same mac address (with the first port's mac)
	local bond_mac=$(ip -n "$s_ns" -j link show bond0 | jq -r '.[]["address"]')
	local eth0_mac=$(ip -n "$s_ns" -j link show eth0 | jq -r '.[]["address"]')
	local eth1_mac=$(ip -n "$s_ns" -j link show eth1 | jq -r '.[]["address"]')
	local eth2_mac=$(ip -n "$s_ns" -j link show eth2 | jq -r '.[]["address"]')
	if [ "$bond_mac" != "${mac[0]}" ] || [ "$eth0_mac" != "$bond_mac" ] || \
		[ "$eth1_mac" != "$bond_mac" ] || [ "$eth2_mac" != "$bond_mac" ]; then
		RET=1
	fi
}

check_bond_mac_same_with_first()
{
	RET=0
	# bond mac address should be same with the first added slave
	local bond_mac=$(ip -n "$s_ns" -j link show bond0 | jq -r '.[]["address"]')
	if [ "$bond_mac" != "${mac[0]}" ]; then
		RET=1
	fi
}

check_bond_mac_same_with_active()
{
	RET=0
	# bond mac address should be same with active slave
	local bond_mac=$(ip -n "$s_ns" -j link show bond0 | jq -r '.[]["address"]')
	local active_slave=$(cmd_jq "ip -n ${s_ns} -d -j link show bond0" ".[].linkinfo.info_data.active_slave")
	local active_slave_mac=$(ip -n "$s_ns" -j link show "$active_slave" | jq -r '.[]["address"]')
	if [ "$bond_mac" != "$active_slave_mac" ]; then
		RET=1
	fi
}

check_backup_slave_mac_not_change()
{
	RET=0
	# backup slave's mac address is not changed
	if ip -n "$s_ns" -d -j link show type bond_slave | jq -e '.[]
		| select(.linkinfo.info_slave_data.state=="BACKUP")
		| select(.address != .linkinfo.info_slave_data.perm_hwaddr)' &> /dev/null; then
		RET=1
	fi
}

check_backup_slave_mac_inherit()
{
	local backup_mac
	RET=0

	# backup slaves should use mac[1] or mac[2]
	local backup_macs=$(ip -n "$s_ns" -d -j link show type bond_slave | \
		jq -r '.[] | select(.linkinfo.info_slave_data.state=="BACKUP") | .address')
	for backup_mac in $backup_macs; do
		if [ "$backup_mac" != "${mac[1]}" ] && [ "$backup_mac" != "${mac[2]}" ]; then
			RET=1
		fi
	done
}

check_first_slave_random_mac()
{
	RET=0
	# remove the first added slave and added it back
	ip -n "$s_ns" link set eth0 nomaster
	ip -n "$s_ns" link set eth0 master bond0

	# the first slave should use random mac address
	eth0_mac=$(ip -n "$s_ns" -j link show eth0 | jq -r '.[]["address"]')
	[ "$eth0_mac" = "${mac[0]}" ] && RET=1
	log_test "bond fail_over_mac follow" "random first slave mac"

	# remove the first slave, the permanent MAC address should be restored back
	ip -n "$s_ns" link set eth0 nomaster
	eth0_mac=$(ip -n "$s_ns" -j link show eth0 | jq -r '.[]["address"]')
	[ "$eth0_mac" != "${mac[0]}" ] && RET=1
}

do_active_backup_failover()
{
	local active_slave=$(cmd_jq "ip -n ${s_ns} -d -j link show bond0" ".[].linkinfo.info_data.active_slave")
	ip -n ${s_ns} link set ${active_slave} down
	slowwait 2 active_slave_changed $active_slave
	ip -n ${s_ns} link set ${active_slave} up
}

fail_over_mac()
{
	# Bring down the first interface on the switch to force the bond to
	# select another active interface instead of the first one that joined.
	ip -n "$g_ns" link set s0 down

	# fail_over_mac none
	bond_reset "mode active-backup miimon 100 fail_over_mac 0"
	check_all_mac_same
	log_test "fail_over_mac 0" "all slaves have same mac"
	do_active_backup_failover
	check_all_mac_same
	log_test "fail_over_mac 0" "failover: all slaves have same mac"

	# fail_over_mac active
	bond_reset "mode active-backup miimon 100 fail_over_mac 1"
	check_bond_mac_same_with_active
	log_test "fail_over_mac 1" "bond mac is same with active slave mac"
	check_backup_slave_mac_not_change
	log_test "fail_over_mac 1" "backup slave mac is not changed"
	do_active_backup_failover
	check_bond_mac_same_with_active
	log_test "fail_over_mac 1" "failover: bond mac is same with active slave mac"
	check_backup_slave_mac_not_change
	log_test "fail_over_mac 1" "failover: backup slave mac is not changed"

	# fail_over_mac follow
	bond_reset "mode active-backup miimon 100 fail_over_mac 2"
	check_bond_mac_same_with_first
	log_test "fail_over_mac 2" "bond mac is same with first slave mac"
	check_bond_mac_same_with_active
	log_test "fail_over_mac 2" "bond mac is same with active slave mac"
	check_backup_slave_mac_inherit
	log_test "fail_over_mac 2" "backup slave mac inherit"
	do_active_backup_failover
	check_bond_mac_same_with_first
	log_test "fail_over_mac 2" "failover: bond mac is same with first slave mac"
	check_bond_mac_same_with_active
	log_test "fail_over_mac 2" "failover: bond mac is same with active slave mac"
	check_backup_slave_mac_inherit
	log_test "fail_over_mac 2" "failover: backup slave mac inherit"
	check_first_slave_random_mac
	log_test "fail_over_mac 2" "first slave mac random"
}

vlan_over_bond_arp()
{
	local mode="$1"
	RET=0

	bond_reset "mode $mode arp_interval 100 arp_ip_target 192.0.3.10"
	ip -n "${s_ns}" link add bond0.3 link bond0 type vlan id 3
	ip -n "${s_ns}" link set bond0.3 up
	ip -n "${s_ns}" addr add 192.0.3.1/24 dev bond0.3
	ip -n "${s_ns}" addr add 2001:db8::3:1/64 dev bond0.3

	slowwait_for_counter 5 5 tc_rule_handle_stats_get \
		"dev eth0.3 ingress" 101 ".packets" "-n ${c_ns}" &> /dev/null || RET=1
	log_test "vlan over bond arp" "$mode"
}

vlan_over_bond_ns()
{
	local mode="$1"
	RET=0

	if skip_ns; then
		log_test_skip "vlan_over_bond ns" "$mode"
		return 0
	fi

	bond_reset "mode $mode arp_interval 100 ns_ip6_target 2001:db8::3:10"
	ip -n "${s_ns}" link add bond0.3 link bond0 type vlan id 3
	ip -n "${s_ns}" link set bond0.3 up
	ip -n "${s_ns}" addr add 192.0.3.1/24 dev bond0.3
	ip -n "${s_ns}" addr add 2001:db8::3:1/64 dev bond0.3

	slowwait_for_counter 5 5 tc_rule_handle_stats_get \
		"dev eth0.3 ingress" 102 ".packets" "-n ${c_ns}" &> /dev/null || RET=1
	log_test "vlan over bond ns" "$mode"
}

vlan_over_bond()
{
	# add vlan 3 for client
	ip -n "${c_ns}" link add eth0.3 link eth0 type vlan id 3
	ip -n "${c_ns}" link set eth0.3 up
	ip -n "${c_ns}" addr add 192.0.3.10/24 dev eth0.3
	ip -n "${c_ns}" addr add 2001:db8::3:10/64 dev eth0.3

	# Add tc rule to check the vlan pkts
	tc -n "${c_ns}" qdisc add dev eth0.3 clsact
	tc -n "${c_ns}" filter add dev eth0.3 ingress protocol arp \
		handle 101 flower skip_hw arp_op request \
		arp_sip 192.0.3.1 arp_tip 192.0.3.10 action pass
	tc -n "${c_ns}" filter add dev eth0.3 ingress protocol ipv6 \
		handle 102 flower skip_hw ip_proto icmpv6 \
		type 135 src_ip 2001:db8::3:1 action pass

	vlan_over_bond_arp "active-backup"
	vlan_over_bond_ns "active-backup"
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
