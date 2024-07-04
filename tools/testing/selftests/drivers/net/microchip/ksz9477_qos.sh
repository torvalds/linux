#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2024 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>

# The script is adopted to work with the Microchip KSZ switch driver.

ETH_FCS_LEN=4

WAIT_TIME=1
NUM_NETIFS=4
REQUIRE_JQ="yes"
REQUIRE_MZ="yes"
STABLE_MAC_ADDRS=yes
NETIF_CREATE=no
lib_dir=$(dirname $0)/../../../net/forwarding
source $lib_dir/tc_common.sh
source $lib_dir/lib.sh

require_command dcb

h1=${NETIFS[p1]}
swp1=${NETIFS[p2]}
swp2=${NETIFS[p3]}
h2=${NETIFS[p4]}

H1_IPV4="192.0.2.1"
H2_IPV4="192.0.2.2"
H1_IPV6="2001:db8:1::1"
H2_IPV6="2001:db8:1::2"

# On h1_ and h2_create do not set IP addresses to avoid interaction with the
# system, to keep packet counters clean.
h1_create()
{
	simple_if_init $h1
	sysctl_set net.ipv6.conf.${h1}.disable_ipv6 1
	# Get the MAC address of the interface to use it with mausezahn
	h1_mac=$(ip -j link show dev ${h1} | jq -e '.[].address')
}

h1_destroy()
{
	sysctl_restore net.ipv6.conf.${h1}.disable_ipv6
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
	sysctl_set net.ipv6.conf.${h2}.disable_ipv6 1
	h2_mac=$(ip -j link show dev ${h2} | jq -e '.[].address')
}

h2_destroy()
{
	sysctl_restore net.ipv6.conf.${h2}.disable_ipv6
	simple_if_fini $h2
}

switch_create()
{
	ip link set ${swp1} up
	ip link set ${swp2} up
	sysctl_set net.ipv6.conf.${swp1}.disable_ipv6 1
	sysctl_set net.ipv6.conf.${swp2}.disable_ipv6 1

	# Ports should trust VLAN PCP even with vlan_filtering=0
	ip link add br0 type bridge
	ip link set ${swp1} master br0
	ip link set ${swp2} master br0
	ip link set br0 up
	sysctl_set net.ipv6.conf.br0.disable_ipv6 1
}

switch_destroy()
{
	sysctl_restore net.ipv6.conf.${swp2}.disable_ipv6
	sysctl_restore net.ipv6.conf.${swp1}.disable_ipv6

	ip link del br0
}

setup_prepare()
{
	vrf_prepare

	h1_create
	h2_create
	switch_create
}

cleanup()
{
	pre_cleanup

	h2_destroy
	h1_destroy
	switch_destroy

	vrf_cleanup
}

set_apptrust_order()
{
	local if_name=$1
	local order=$2

	dcb apptrust set dev ${if_name} order ${order}
}

# Function to extract a specified field from a given JSON stats string
extract_network_stat() {
	local stats_json=$1
	local field_name=$2

	echo $(echo "$stats_json" | jq -r "$field_name")
}

run_test()
{
	local test_name=$1;
	local apptrust_order=$2;
	local port_prio=$3;
	local dscp_ipv=$4;
	local dscp=$5;
	local have_vlan=$6;
	local pcp_ipv=$7;
	local vlan_pcp=$8;
	local ip_v6=$9

	local rx_ipv
	local tx_ipv

	RET=0

	# Send some packet to populate the switch MAC table
	$MZ ${h2} -a ${h2_mac} -b ${h1_mac} -p 64 -t icmp echores -c 1

	# Based on the apptrust order, set the expected Internal Priority values
	# for the RX and TX paths.
	if [ "${apptrust_order}" == "" ]; then
		echo "Apptrust order not set."
		rx_ipv=${port_prio}
		tx_ipv=${port_prio}
	elif [ "${apptrust_order}" == "dscp" ]; then
		echo "Apptrust order is DSCP."
		rx_ipv=${dscp_ipv}
		tx_ipv=${dscp_ipv}
	elif [ "${apptrust_order}" == "pcp" ]; then
		echo "Apptrust order is PCP."
		rx_ipv=${pcp_ipv}
		tx_ipv=${pcp_ipv}
	elif [ "${apptrust_order}" == "pcp dscp" ]; then
		echo "Apptrust order is PCP DSCP."
		if [ ${have_vlan} -eq 1 ]; then
			rx_ipv=$((dscp_ipv > pcp_ipv ? dscp_ipv : pcp_ipv))
			tx_ipv=${pcp_ipv}
		else
			rx_ipv=${dscp_ipv}
			tx_ipv=${dscp_ipv}
		fi
	else
		RET=1
		echo "Error: Unknown apptrust order ${apptrust_order}"
		log_test "${test_name}"
		return
	fi

	# Most/all? of the KSZ switches do not provide per-TC counters. There
	# are only tx_hi and rx_hi counters, which are used to count packets
	# which are considered as high priority and most likely not assigned
	# to the queue 0.
	# On the ingress path, packets seem to get high priority status
	# independently of the DSCP or PCP global mapping. On the egress path,
	# the high priority status is assigned based on the DSCP or PCP global
	# map configuration.
	# The thresholds for the high priority status are not documented, but
	# it seems that the switch considers packets as high priority on the
	# ingress path if detected Internal Priority is greater than 0. On the
	# egress path, the switch considers packets as high priority if
	# detected Internal Priority is greater than 1.
	if [ ${rx_ipv} -ge 1 ]; then
		local expect_rx_high_prio=1
	else
		local expect_rx_high_prio=0
	fi

	if [ ${tx_ipv} -ge 2 ]; then
		local expect_tx_high_prio=1
	else
		local expect_tx_high_prio=0
	fi

	# Use ip tool to get the current switch packet counters. ethool stats
	# need to be recalculated to get the correct values.
	local swp1_stats=$(ip -s -j link show dev ${swp1})
	local swp2_stats=$(ip -s -j link show dev ${swp2})
	local swp1_rx_packets_before=$(extract_network_stat "$swp1_stats" \
				       '.[0].stats64.rx.packets')
	local swp1_rx_bytes_before=$(extract_network_stat "$swp1_stats" \
				     '.[0].stats64.rx.bytes')
	local swp2_tx_packets_before=$(extract_network_stat "$swp2_stats" \
				       '.[0].stats64.tx.packets')
	local swp2_tx_bytes_before=$(extract_network_stat "$swp2_stats" \
				     '.[0].stats64.tx.bytes')
	local swp1_rx_hi_before=$(ethtool_stats_get ${swp1} "rx_hi")
	local swp2_tx_hi_before=$(ethtool_stats_get ${swp2} "tx_hi")

	# Assamble the mausezahn command based on the test parameters
	# For the testis with ipv4 or ipv6, use icmp response packets,
	# to avoid interaction with the system, to keep packet counters
	# clean.
	if [ ${ip_v6} -eq 0 ]; then
		local ip="-a ${h1_mac} -b ${h2_mac} -A ${H1_IPV4} \
			  -B ${H2_IPV4} -t icmp unreach,code=1,dscp=${dscp}"
	else
		local ip="-6 -a ${h1_mac} -b ${h2_mac} -A ${H1_IPV6} \
			  -B ${H2_IPV6} -t icmp6 type=1,code=0,dscp=${dscp}"
	fi

	if [ ${have_vlan} -eq 1 ]; then
		local vlan_pcp_opt="-Q ${vlan_pcp}:0"
	else
		local vlan_pcp_opt=""
	fi
	$MZ ${h1} ${ip} -c ${PING_COUNT} -d 10msec ${vlan_pcp_opt}

	# Wait until the switch packet counters are updated
	sleep 6

	local swp1_stats=$(ip -s -j link show dev ${swp1})
	local swp2_stats=$(ip -s -j link show dev ${swp2})

	local swp1_rx_packets_after=$(extract_network_stat "$swp1_stats" \
				      '.[0].stats64.rx.packets')
	local swp1_rx_bytes_after=$(extract_network_stat "$swp1_stats" \
				    '.[0].stats64.rx.bytes')
	local swp2_tx_packets_after=$(extract_network_stat "$swp2_stats" \
				      '.[0].stats64.tx.packets')
	local swp2_tx_bytes_after=$(extract_network_stat "$swp2_stats" \
				    '.[0].stats64.tx.bytes')

	local swp1_rx_packets_diff=$((${swp1_rx_packets_after} - \
				      ${swp1_rx_packets_before}))
	local swp2_tx_packets_diff=$((${swp2_tx_packets_after} - \
				      ${swp2_tx_packets_before}))

	local swp1_rx_hi_after=$(ethtool_stats_get ${swp1} "rx_hi")
	local swp2_tx_hi_after=$(ethtool_stats_get ${swp2} "tx_hi")

	# Test if any packets were received on swp1, we will rx before and after
	if [ ${swp1_rx_packets_diff} -lt ${PING_COUNT} ]; then
		echo "Not expected amount of received packets on ${swp1}"
		echo "before ${swp1_rx_packets_before} after ${swp1_rx_packets_after}"
		RET=1
	fi

	# Test if any packets were transmitted on swp2, we will tx before and after
	if [ ${swp2_tx_packets_diff} -lt ${PING_COUNT} ]; then
		echo "Not expected amount of transmitted packets on ${swp2}"
		echo "before ${swp2_tx_packets_before} after ${swp2_tx_packets_after}"
		RET=1
	fi

	# tx/rx_hi counted in bytes. So, we need to compare the difference in bytes
	local swp1_rx_bytes_diff=$(($swp1_rx_bytes_after - $swp1_rx_bytes_before))
	local swp2_tx_bytes_diff=$(($swp2_tx_bytes_after - $swp2_tx_bytes_before))
	local swp1_rx_hi_diff=$(($swp1_rx_hi_after - $swp1_rx_hi_before))
	local swp2_tx_hi_diff=$(($swp2_tx_hi_after - $swp2_tx_hi_before))

	if [ ${expect_rx_high_prio} -eq 1 ]; then
		swp1_rx_hi_diff=$((${swp1_rx_hi_diff} - \
				   ${swp1_rx_packets_diff} * ${ETH_FCS_LEN}))
		if [ ${swp1_rx_hi_diff} -ne ${swp1_rx_bytes_diff} ]; then
			echo "Not expected amount of high priority packets received on ${swp1}"
			echo "RX hi diff: ${swp1_rx_hi_diff}, expected RX bytes diff: ${swp1_rx_bytes_diff}"
			RET=1
		fi
	else
		if [ ${swp1_rx_hi_diff} -ne 0 ]; then
			echo "Unexpected amount of high priority packets received on ${swp1}"
			echo "RX hi diff: ${swp1_rx_hi_diff}, expected 0"
			RET=1
		fi
	fi

	if [ ${expect_tx_high_prio} -eq 1 ]; then
		swp2_tx_hi_diff=$((${swp2_tx_hi_diff} - \
				   ${swp2_tx_packets_diff} * ${ETH_FCS_LEN}))
		if [ ${swp2_tx_hi_diff} -ne ${swp2_tx_bytes_diff} ]; then
			echo "Not expected amount of high priority packets transmitted on ${swp2}"
			echo "TX hi diff: ${swp2_tx_hi_diff}, expected TX bytes diff: ${swp2_tx_bytes_diff}"
			RET=1
		fi
	else
		if [ ${swp2_tx_hi_diff} -ne 0 ]; then
			echo "Unexpected amount of high priority packets transmitted on ${swp2}"
			echo "TX hi diff: ${swp2_tx_hi_diff}, expected 0"
			RET=1
		fi
	fi

	log_test "${test_name}"
}

run_test_dscp()
{
	# IPv4 test
	run_test "$1" "$2" "$3" "$4" "$5" 0 0 0 0
	# IPv6 test
	run_test "$1" "$2" "$3" "$4" "$5" 0 0 0 1
}

run_test_dscp_pcp()
{
	# IPv4 test
	run_test "$1" "$2" "$3" "$4" "$5" 1 "$6" "$7" 0
	# IPv6 test
	run_test "$1" "$2" "$3" "$4" "$5" 1 "$6" "$7" 1
}

port_default_prio_get()
{
	local if_name=$1
	local prio

	prio="$(dcb -j app show dev ${if_name} default-prio | \
		jq '.default_prio[]')"
	if [ -z "${prio}" ]; then
		prio=0
	fi

	echo ${prio}
}

test_port_default()
{
	local orig_apptrust=$(port_get_default_apptrust ${swp1})
	local orig_prio=$(port_default_prio_get ${swp1})
	local apptrust_order=""

	RET=0

	# Make sure no other priority sources will interfere with the test
	set_apptrust_order ${swp1} "${apptrust_order}"

	for val in $(seq 0 7); do
		dcb app replace dev ${swp1} default-prio ${val}
		if [ $val -ne $(port_default_prio_get ${swp1}) ]; then
			RET=1
			break
		fi

		run_test_dscp "Port-default QoS classification, prio: ${val}" \
			"${apptrust_order}" ${val} 0 0
	done

	set_apptrust_order ${swp1} "${orig_apptrust}"
	if [[ "$orig_apptrust" != "$(port_get_default_apptrust ${swp1})" ]]; then
		RET=1
	fi

	dcb app replace dev ${swp1} default-prio ${orig_prio}
	if [ $orig_prio -ne $(port_default_prio_get ${swp1}) ]; then
		RET=1
	fi

	log_test "Port-default QoS classification"
}

port_get_default_apptrust()
{
	local if_name=$1

	dcb -j apptrust show dev ${if_name} | jq -r '.order[]' | \
		tr '\n' ' ' | xargs
}

test_port_apptrust()
{
	local original_dscp_prios_swp1=$(get_dscp_prios ${swp1})
	local orig_apptrust=$(port_get_default_apptrust ${swp1})
	local orig_port_prio=$(port_default_prio_get ${swp1})
	local order_variants=("pcp dscp" "dscp" "pcp")
	local apptrust_order
	local port_prio
	local dscp_prio
	local pcp_prio
	local dscp
	local pcp

	RET=0

	# First, test if apptrust configuration as taken by the kernel
	for order in "${order_variants[@]}"; do
		set_apptrust_order ${swp1} "${order}"
		if [[ "$order" != "$(port_get_default_apptrust ${swp1})" ]]; then
			RET=1
			break
		fi
	done

	log_test "Apptrust, supported variants"

	# To test if the apptrust configuration is working as expected, we need
	# to set DSCP priorities for the switch port.
	init_dscp_prios "${swp1}" "${original_dscp_prios_swp1}"

	# Start with a simple test where all apptrust sources are disabled
	# default port priority is 0, DSCP priority is mapped to 7.
	# No high priority packets should be received or transmitted.
	port_prio=0
	dscp_prio=7
	dscp=4

	dcb app replace dev ${swp1} default-prio ${port_prio}
	dcb app replace dev ${swp1} dscp-prio ${dscp}:${dscp_prio}

	apptrust_order=""
	set_apptrust_order ${swp1} "${apptrust_order}"
	# Test with apptrust sources disabled, Packets should get port default
	# priority which is 0
	run_test_dscp "Apptrust, all disabled. DSCP-prio ${dscp}:${dscp_prio}" \
		"${apptrust_order}" ${port_prio} ${dscp_prio} ${dscp}

	apptrust_order="pcp"
	set_apptrust_order ${swp1} "${apptrust_order}"
	# If PCP is enabled, packets should get PCP priority, which is not
	# set in this test (no VLAN tags are present in the packet). No high
	# priority packets should be received or transmitted.
	run_test_dscp "Apptrust, PCP enabled. DSCP-prio ${dscp}:${dscp_prio}" \
		"${apptrust_order}" ${port_prio} ${dscp_prio} ${dscp}

	apptrust_order="dscp"
	set_apptrust_order ${swp1} "${apptrust_order}"
	# If DSCP is enabled, packets should get DSCP priority which is set to 7
	# in this test. High priority packets should be received and transmitted.
	run_test_dscp "Apptrust, DSCP enabled. DSCP-prio ${dscp}:${dscp_prio}" \
		"${apptrust_order}" ${port_prio} ${dscp_prio} ${dscp}

	apptrust_order="pcp dscp"
	set_apptrust_order ${swp1} "${apptrust_order}"
	# If PCP and DSCP are enabled, PCP would have higher apptrust priority
	# so packets should get PCP priority. But in this test VLAN PCP is not
	# set, so it should get DSCP priority which is set to 7. High priority
	# packets should be received and transmitted.
	run_test_dscp "Apptrust, PCP and DSCP are enabled. DSCP-prio ${dscp}:${dscp_prio}" \
		"${apptrust_order}" ${port_prio} ${dscp_prio} ${dscp}

	# If VLAN PCP is set, it should have higher apptrust priority than DSCP
	# so packets should get VLAN PCP priority. Send packets with VLAN PCP
	# set to 0, DSCP set to 7. Packets should get VLAN PCP priority.
	# No high priority packets should be transmitted. Due to nature of the
	# switch, high priority packets will be received.
	pcp_prio=0
	pcp=0
	run_test_dscp_pcp "Apptrust, PCP and DSCP are enabled. PCP ${pcp_prio}, DSCP-prio ${dscp}:${dscp_prio}" \
		"${apptrust_order}" ${port_prio} ${dscp_prio} ${dscp} ${pcp_prio} ${pcp}

	# If VLAN PCP is set to 7, it should have higher apptrust priority than
	# DSCP so packets should get VLAN PCP priority. Send packets with VLAN
	# PCP set to 7, DSCP set to 7. Packets should get VLAN PCP priority.
	# High priority packets should be received and transmitted.
	pcp_prio=7
	pcp=7
	run_test_dscp_pcp "Apptrust, PCP and DSCP are enabled. PCP ${pcp_prio}, DSCP-prio ${dscp}:${dscp_prio}" \
		"${apptrust_order}" ${port_prio} ${dscp_prio} ${dscp} ${pcp_prio} ${pcp}
	# Now make sure that the switch is able to handle the case where DSCP
	# priority is set to 0 and PCP priority is set to 7. Packets should get
	# PCP priority. High priority packets should be received and transmitted.
	dscp_prio=0
	dcb app replace dev ${swp1} dscp-prio ${dscp}:${dscp_prio}
	run_test_dscp_pcp "Apptrust, PCP and DSCP are enabled. PCP ${pcp_prio}, DSCP-prio ${dscp}:${dscp_prio}" \
		"${apptrust_order}" ${port_prio} ${dscp_prio} ${dscp} ${pcp_prio} ${pcp}
	# If both VLAN PCP and DSCP are set to 0, packets should get 0 priority.
	# No high priority packets should be received or transmitted.
	pcp_prio=0
	pcp=0
	run_test_dscp_pcp "Apptrust, PCP and DSCP are enabled. PCP ${pcp_prio}, DSCP-prio ${dscp}:${dscp_prio}" \
		"${apptrust_order}" ${port_prio} ${dscp_prio} ${dscp} ${pcp_prio} ${pcp}

	# Restore original priorities
	if ! restore_priorities "${swp1}" "${original_dscp_prios_swp1}"; then
		RET=1
	fi

	set_apptrust_order ${swp1} "${orig_apptrust}"
	if [ "$orig_apptrust" != "$(port_get_default_apptrust ${swp1})" ]; then
		RET=1
	fi

	dcb app replace dev ${swp1} default-prio ${orig_port_prio}
	if [ $orig_port_prio -ne $(port_default_prio_get ${swp1}) ]; then
		RET=1
	fi

	log_test "Apptrust, restore original settings"
}

# Function to get current DSCP priorities
get_dscp_prios() {
	local if_name=$1
	dcb -j app show dev ${if_name} | jq -c '.dscp_prio'
}

# Function to set a specific DSCP priority on a device
replace_dscp_prio() {
	local if_name=$1
	local dscp=$2
	local prio=$3
	dcb app replace dev ${if_name} dscp-prio ${dscp}:${prio}
}

# Function to compare DSCP maps
compare_dscp_maps() {
	local old_json=$1
	local new_json=$2
	local dscp=$3
	local prio=$4

	# Create a modified old_json with the expected change for comparison
	local modified_old_json=$(echo "$old_json" |
		jq --argjson dscp $dscp --argjson prio $prio \
			'map(if .[0] == $dscp then [$dscp, $prio] else . end)' |
		tr -d " \n")

	# Compare new_json with the modified_old_json
	if [[ "$modified_old_json" == "$new_json" ]]; then
		return 0
	else
		return 1
	fi
}

# Function to set DSCP priorities
set_and_verify_dscp() {
	local port=$1
	local dscp=$2
	local new_prio=$3

	local old_prios=$(get_dscp_prios $port)

	replace_dscp_prio "$port" $dscp $new_prio

	# Fetch current settings and compare
	local current_prios=$(get_dscp_prios $port)
	if ! compare_dscp_maps "$old_prios" "$current_prios" $dscp $new_prio; then
		echo "Error: Unintended changes detected in DSCP map for $port after setting DSCP $dscp to $new_prio."
		return 1
	fi
	return 0
}

# Function to restore original priorities
restore_priorities() {
	local port=$1
	local original_prios=$2

	echo "Removing test artifacts for $port"
	local current_prios=$(get_dscp_prios $port)
	local prio_str=$(echo "$current_prios" |
		jq -r 'map("\(.[0]):\(.[1])") | join(" ")')
	dcb app del dev $port dscp-prio $prio_str

	echo "Restoring original DSCP priorities for $port"
	local restore_str=$(echo "$original_prios" |
		jq -r 'map("\(.[0]):\(.[1])") | join(" ")')
	dcb app add dev $port dscp-prio $restore_str

	local current_prios=$(get_dscp_prios $port)
	if [[ "$original_prios" != "$current_prios" ]]; then
		echo "Error: Failed to restore original DSCP priorities for $port"
		return 1
	fi
	return 0
}

# Initialize DSCP priorities. Set them to predictable values for testing.
init_dscp_prios() {
	local port=$1
	local original_prios=$2

	echo "Removing any existing DSCP priority mappins for $port"
	local prio_str=$(echo "$original_prios" |
		jq -r 'map("\(.[0]):\(.[1])") | join(" ")')
	dcb app del dev $port dscp-prio $prio_str

	# Initialize DSCP priorities list
	local dscp_prios=""
	for dscp in {0..63}; do
		dscp_prios+=("$dscp:0")
	done

	echo "Setting initial DSCP priorities map to 0 for $port"
	dcb app add dev $port dscp-prio ${dscp_prios[@]}
}

# Main function to test global DSCP map across specified ports
test_global_dscp_map() {
	local ports=("$swp1" "$swp2")
	local original_dscp_prios_port0=$(get_dscp_prios ${ports[0]})
	local orig_apptrust=$(port_get_default_apptrust ${swp1})
	local orig_port_prio=$(port_default_prio_get ${swp1})
	local apptrust_order="dscp"
	local port_prio=0
	local dscp_prio
	local dscp

	RET=0

	set_apptrust_order ${swp1} "${apptrust_order}"
	dcb app replace dev ${swp1} default-prio ${port_prio}

	# Initialize DSCP priorities
	init_dscp_prios "${ports[0]}" "$original_dscp_prios_port0"

	# Loop over each DSCP index
	for dscp in {0..63}; do
		# and test each Internal Priority value
		for dscp_prio in {0..7}; do
			# do it for each port. This is to test if the global DSCP map
			# is accessible from all ports.
			for port in "${ports[@]}"; do
				if ! set_and_verify_dscp "$port" $dscp $dscp_prio; then
					RET=1
				fi
			done

			# Test if the DSCP priority is correctly applied to the packets
			run_test_dscp "DSCP (${dscp}) QoS classification, prio: ${dscp_prio}" \
				"${apptrust_order}" ${port_prio} ${dscp_prio} ${dscp}
			if [ ${RET} -eq 1 ]; then
				break
			fi
		done
	done

	# Restore original priorities
	if ! restore_priorities "${ports[0]}" "${original_dscp_prios_port0}"; then
		RET=1
	fi

	set_apptrust_order ${swp1} "${orig_apptrust}"
	if [[ "$orig_apptrust" != "$(port_get_default_apptrust ${swp1})" ]]; then
		RET=1
	fi

	dcb app replace dev ${swp1} default-prio ${orig_port_prio}
	if [ $orig_port_prio -ne $(port_default_prio_get ${swp1}) ]; then
		RET=1
	fi

	log_test "DSCP global map"
}

trap cleanup EXIT

ALL_TESTS="
	test_port_default
	test_port_apptrust
	test_global_dscp_map
"

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
