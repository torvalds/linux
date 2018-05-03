#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

##############################################################################
# Defines

# Can be overridden by the configuration file.
PING=${PING:=ping}
PING6=${PING6:=ping6}
MZ=${MZ:=mausezahn}
WAIT_TIME=${WAIT_TIME:=5}
PAUSE_ON_FAIL=${PAUSE_ON_FAIL:=no}
PAUSE_ON_CLEANUP=${PAUSE_ON_CLEANUP:=no}
NETIF_TYPE=${NETIF_TYPE:=veth}
NETIF_CREATE=${NETIF_CREATE:=yes}

if [[ -f forwarding.config ]]; then
	source forwarding.config
fi

##############################################################################
# Sanity checks

check_tc_version()
{
	tc -j &> /dev/null
	if [[ $? -ne 0 ]]; then
		echo "SKIP: iproute2 too old; tc is missing JSON support"
		exit 1
	fi

	tc filter help 2>&1 | grep block &> /dev/null
	if [[ $? -ne 0 ]]; then
		echo "SKIP: iproute2 too old; tc is missing shared block support"
		exit 1
	fi
}

if [[ "$(id -u)" -ne 0 ]]; then
	echo "SKIP: need root privileges"
	exit 0
fi

if [[ "$CHECK_TC" = "yes" ]]; then
	check_tc_version
fi

if [[ ! -x "$(command -v jq)" ]]; then
	echo "SKIP: jq not installed"
	exit 1
fi

if [[ ! -x "$(command -v $MZ)" ]]; then
	echo "SKIP: $MZ not installed"
	exit 1
fi

if [[ ! -v NUM_NETIFS ]]; then
	echo "SKIP: importer does not define \"NUM_NETIFS\""
	exit 1
fi

##############################################################################
# Command line options handling

count=0

while [[ $# -gt 0 ]]; do
	if [[ "$count" -eq "0" ]]; then
		unset NETIFS
		declare -A NETIFS
	fi
	count=$((count + 1))
	NETIFS[p$count]="$1"
	shift
done

##############################################################################
# Network interfaces configuration

create_netif_veth()
{
	local i

	for i in $(eval echo {1..$NUM_NETIFS}); do
		local j=$((i+1))

		ip link show dev ${NETIFS[p$i]} &> /dev/null
		if [[ $? -ne 0 ]]; then
			ip link add ${NETIFS[p$i]} type veth \
				peer name ${NETIFS[p$j]}
			if [[ $? -ne 0 ]]; then
				echo "Failed to create netif"
				exit 1
			fi
		fi
		i=$j
	done
}

create_netif()
{
	case "$NETIF_TYPE" in
	veth) create_netif_veth
	      ;;
	*) echo "Can not create interfaces of type \'$NETIF_TYPE\'"
	   exit 1
	   ;;
	esac
}

if [[ "$NETIF_CREATE" = "yes" ]]; then
	create_netif
fi

for i in $(eval echo {1..$NUM_NETIFS}); do
	ip link show dev ${NETIFS[p$i]} &> /dev/null
	if [[ $? -ne 0 ]]; then
		echo "SKIP: could not find all required interfaces"
		exit 1
	fi
done

##############################################################################
# Helpers

# Exit status to return at the end. Set in case one of the tests fails.
EXIT_STATUS=0
# Per-test return value. Clear at the beginning of each test.
RET=0

check_err()
{
	local err=$1
	local msg=$2

	if [[ $RET -eq 0 && $err -ne 0 ]]; then
		RET=$err
		retmsg=$msg
	fi
}

check_fail()
{
	local err=$1
	local msg=$2

	if [[ $RET -eq 0 && $err -eq 0 ]]; then
		RET=1
		retmsg=$msg
	fi
}

log_test()
{
	local test_name=$1
	local opt_str=$2

	if [[ $# -eq 2 ]]; then
		opt_str="($opt_str)"
	fi

	if [[ $RET -ne 0 ]]; then
		EXIT_STATUS=1
		printf "TEST: %-60s  [FAIL]\n" "$test_name $opt_str"
		if [[ ! -z "$retmsg" ]]; then
			printf "\t%s\n" "$retmsg"
		fi
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo "Hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
		return 1
	fi

	printf "TEST: %-60s  [PASS]\n" "$test_name $opt_str"
	return 0
}

log_info()
{
	local msg=$1

	echo "INFO: $msg"
}

setup_wait()
{
	for i in $(eval echo {1..$NUM_NETIFS}); do
		while true; do
			ip link show dev ${NETIFS[p$i]} up \
				| grep 'state UP' &> /dev/null
			if [[ $? -ne 0 ]]; then
				sleep 1
			else
				break
			fi
		done
	done

	# Make sure links are ready.
	sleep $WAIT_TIME
}

pre_cleanup()
{
	if [ "${PAUSE_ON_CLEANUP}" = "yes" ]; then
		echo "Pausing before cleanup, hit any key to continue"
		read
	fi
}

vrf_prepare()
{
	ip -4 rule add pref 32765 table local
	ip -4 rule del pref 0
	ip -6 rule add pref 32765 table local
	ip -6 rule del pref 0
}

vrf_cleanup()
{
	ip -6 rule add pref 0 table local
	ip -6 rule del pref 32765
	ip -4 rule add pref 0 table local
	ip -4 rule del pref 32765
}

__last_tb_id=0
declare -A __TB_IDS

__vrf_td_id_assign()
{
	local vrf_name=$1

	__last_tb_id=$((__last_tb_id + 1))
	__TB_IDS[$vrf_name]=$__last_tb_id
	return $__last_tb_id
}

__vrf_td_id_lookup()
{
	local vrf_name=$1

	return ${__TB_IDS[$vrf_name]}
}

vrf_create()
{
	local vrf_name=$1
	local tb_id

	__vrf_td_id_assign $vrf_name
	tb_id=$?

	ip link add dev $vrf_name type vrf table $tb_id
	ip -4 route add table $tb_id unreachable default metric 4278198272
	ip -6 route add table $tb_id unreachable default metric 4278198272
}

vrf_destroy()
{
	local vrf_name=$1
	local tb_id

	__vrf_td_id_lookup $vrf_name
	tb_id=$?

	ip -6 route del table $tb_id unreachable default metric 4278198272
	ip -4 route del table $tb_id unreachable default metric 4278198272
	ip link del dev $vrf_name
}

__addr_add_del()
{
	local if_name=$1
	local add_del=$2
	local array

	shift
	shift
	array=("${@}")

	for addrstr in "${array[@]}"; do
		ip address $add_del $addrstr dev $if_name
	done
}

simple_if_init()
{
	local if_name=$1
	local vrf_name
	local array

	shift
	vrf_name=v$if_name
	array=("${@}")

	vrf_create $vrf_name
	ip link set dev $if_name master $vrf_name
	ip link set dev $vrf_name up
	ip link set dev $if_name up

	__addr_add_del $if_name add "${array[@]}"
}

simple_if_fini()
{
	local if_name=$1
	local vrf_name
	local array

	shift
	vrf_name=v$if_name
	array=("${@}")

	__addr_add_del $if_name del "${array[@]}"

	ip link set dev $if_name down
	vrf_destroy $vrf_name
}

tunnel_create()
{
	local name=$1; shift
	local type=$1; shift
	local local=$1; shift
	local remote=$1; shift

	ip link add name $name type $type \
	   local $local remote $remote "$@"
	ip link set dev $name up
}

tunnel_destroy()
{
	local name=$1; shift

	ip link del dev $name
}

master_name_get()
{
	local if_name=$1

	ip -j link show dev $if_name | jq -r '.[]["master"]'
}

link_stats_tx_packets_get()
{
       local if_name=$1

       ip -j -s link show dev $if_name | jq '.[]["stats64"]["tx"]["packets"]'
}

tc_rule_stats_get()
{
	local dev=$1; shift
	local pref=$1; shift

	tc -j -s filter show dev $dev ingress pref $pref |
	jq '.[1].options.actions[].stats.packets'
}

mac_get()
{
	local if_name=$1

	ip -j link show dev $if_name | jq -r '.[]["address"]'
}

bridge_ageing_time_get()
{
	local bridge=$1
	local ageing_time

	# Need to divide by 100 to convert to seconds.
	ageing_time=$(ip -j -d link show dev $bridge \
		      | jq '.[]["linkinfo"]["info_data"]["ageing_time"]')
	echo $((ageing_time / 100))
}

declare -A SYSCTL_ORIG
sysctl_set()
{
	local key=$1; shift
	local value=$1; shift

	SYSCTL_ORIG[$key]=$(sysctl -n $key)
	sysctl -qw $key=$value
}

sysctl_restore()
{
	local key=$1; shift

	sysctl -qw $key=${SYSCTL_ORIG["$key"]}
}

forwarding_enable()
{
       ipv4_fwd=$(sysctl -n net.ipv4.conf.all.forwarding)
       ipv6_fwd=$(sysctl -n net.ipv6.conf.all.forwarding)

       sysctl -q -w net.ipv4.conf.all.forwarding=1
       sysctl -q -w net.ipv6.conf.all.forwarding=1
}

forwarding_restore()
{
       sysctl -q -w net.ipv6.conf.all.forwarding=$ipv6_fwd
       sysctl -q -w net.ipv4.conf.all.forwarding=$ipv4_fwd
}

tc_offload_check()
{
	for i in $(eval echo {1..$NUM_NETIFS}); do
		ethtool -k ${NETIFS[p$i]} \
			| grep "hw-tc-offload: on" &> /dev/null
		if [[ $? -ne 0 ]]; then
			return 1
		fi
	done

	return 0
}

slow_path_trap_install()
{
	local dev=$1; shift
	local direction=$1; shift

	if [ "${tcflags/skip_hw}" != "$tcflags" ]; then
		# For slow-path testing, we need to install a trap to get to
		# slow path the packets that would otherwise be switched in HW.
		tc filter add dev $dev $direction pref 1 \
		   flower skip_sw action trap
	fi
}

slow_path_trap_uninstall()
{
	local dev=$1; shift
	local direction=$1; shift

	if [ "${tcflags/skip_hw}" != "$tcflags" ]; then
		tc filter del dev $dev $direction pref 1 flower skip_sw
	fi
}

__icmp_capture_add_del()
{
	local add_del=$1; shift
	local pref=$1; shift
	local vsuf=$1; shift
	local tundev=$1; shift
	local filter=$1; shift

	tc filter $add_del dev "$tundev" ingress \
	   proto ip$vsuf pref $pref \
	   flower ip_proto icmp$vsuf $filter \
	   action pass
}

icmp_capture_install()
{
	__icmp_capture_add_del add 100 "" "$@"
}

icmp_capture_uninstall()
{
	__icmp_capture_add_del del 100 "" "$@"
}

icmp6_capture_install()
{
	__icmp_capture_add_del add 100 v6 "$@"
}

icmp6_capture_uninstall()
{
	__icmp_capture_add_del del 100 v6 "$@"
}

matchall_sink_create()
{
	local dev=$1; shift

	tc qdisc add dev $dev clsact
	tc filter add dev $dev ingress \
	   pref 10000 \
	   matchall \
	   action drop
}

tests_run()
{
	local current_test

	for current_test in ${TESTS:-$ALL_TESTS}; do
		$current_test
	done
}

##############################################################################
# Tests

ping_test()
{
	local if_name=$1
	local dip=$2
	local vrf_name

	RET=0

	vrf_name=$(master_name_get $if_name)
	ip vrf exec $vrf_name $PING $dip -c 10 -i 0.1 -w 2 &> /dev/null
	check_err $?
	log_test "ping"
}

ping6_test()
{
	local if_name=$1
	local dip=$2
	local vrf_name

	RET=0

	vrf_name=$(master_name_get $if_name)
	ip vrf exec $vrf_name $PING6 $dip -c 10 -i 0.1 -w 2 &> /dev/null
	check_err $?
	log_test "ping6"
}

learning_test()
{
	local bridge=$1
	local br_port1=$2	# Connected to `host1_if`.
	local host1_if=$3
	local host2_if=$4
	local mac=de:ad:be:ef:13:37
	local ageing_time

	RET=0

	bridge -j fdb show br $bridge brport $br_port1 \
		| jq -e ".[] | select(.mac == \"$mac\")" &> /dev/null
	check_fail $? "Found FDB record when should not"

	# Disable unknown unicast flooding on `br_port1` to make sure
	# packets are only forwarded through the port after a matching
	# FDB entry was installed.
	bridge link set dev $br_port1 flood off

	tc qdisc add dev $host1_if ingress
	tc filter add dev $host1_if ingress protocol ip pref 1 handle 101 \
		flower dst_mac $mac action drop

	$MZ $host2_if -c 1 -p 64 -b $mac -t ip -q
	sleep 1

	tc -j -s filter show dev $host1_if ingress \
		| jq -e ".[] | select(.options.handle == 101) \
		| select(.options.actions[0].stats.packets == 1)" &> /dev/null
	check_fail $? "Packet reached second host when should not"

	$MZ $host1_if -c 1 -p 64 -a $mac -t ip -q
	sleep 1

	bridge -j fdb show br $bridge brport $br_port1 \
		| jq -e ".[] | select(.mac == \"$mac\")" &> /dev/null
	check_err $? "Did not find FDB record when should"

	$MZ $host2_if -c 1 -p 64 -b $mac -t ip -q
	sleep 1

	tc -j -s filter show dev $host1_if ingress \
		| jq -e ".[] | select(.options.handle == 101) \
		| select(.options.actions[0].stats.packets == 1)" &> /dev/null
	check_err $? "Packet did not reach second host when should"

	# Wait for 10 seconds after the ageing time to make sure FDB
	# record was aged-out.
	ageing_time=$(bridge_ageing_time_get $bridge)
	sleep $((ageing_time + 10))

	bridge -j fdb show br $bridge brport $br_port1 \
		| jq -e ".[] | select(.mac == \"$mac\")" &> /dev/null
	check_fail $? "Found FDB record when should not"

	bridge link set dev $br_port1 learning off

	$MZ $host1_if -c 1 -p 64 -a $mac -t ip -q
	sleep 1

	bridge -j fdb show br $bridge brport $br_port1 \
		| jq -e ".[] | select(.mac == \"$mac\")" &> /dev/null
	check_fail $? "Found FDB record when should not"

	bridge link set dev $br_port1 learning on

	tc filter del dev $host1_if ingress protocol ip pref 1 handle 101 flower
	tc qdisc del dev $host1_if ingress

	bridge link set dev $br_port1 flood on

	log_test "FDB learning"
}

flood_test_do()
{
	local should_flood=$1
	local mac=$2
	local ip=$3
	local host1_if=$4
	local host2_if=$5
	local err=0

	# Add an ACL on `host2_if` which will tell us whether the packet
	# was flooded to it or not.
	tc qdisc add dev $host2_if ingress
	tc filter add dev $host2_if ingress protocol ip pref 1 handle 101 \
		flower dst_mac $mac action drop

	$MZ $host1_if -c 1 -p 64 -b $mac -B $ip -t ip -q
	sleep 1

	tc -j -s filter show dev $host2_if ingress \
		| jq -e ".[] | select(.options.handle == 101) \
		| select(.options.actions[0].stats.packets == 1)" &> /dev/null
	if [[ $? -ne 0 && $should_flood == "true" || \
	      $? -eq 0 && $should_flood == "false" ]]; then
		err=1
	fi

	tc filter del dev $host2_if ingress protocol ip pref 1 handle 101 flower
	tc qdisc del dev $host2_if ingress

	return $err
}

flood_unicast_test()
{
	local br_port=$1
	local host1_if=$2
	local host2_if=$3
	local mac=de:ad:be:ef:13:37
	local ip=192.0.2.100

	RET=0

	bridge link set dev $br_port flood off

	flood_test_do false $mac $ip $host1_if $host2_if
	check_err $? "Packet flooded when should not"

	bridge link set dev $br_port flood on

	flood_test_do true $mac $ip $host1_if $host2_if
	check_err $? "Packet was not flooded when should"

	log_test "Unknown unicast flood"
}

flood_multicast_test()
{
	local br_port=$1
	local host1_if=$2
	local host2_if=$3
	local mac=01:00:5e:00:00:01
	local ip=239.0.0.1

	RET=0

	bridge link set dev $br_port mcast_flood off

	flood_test_do false $mac $ip $host1_if $host2_if
	check_err $? "Packet flooded when should not"

	bridge link set dev $br_port mcast_flood on

	flood_test_do true $mac $ip $host1_if $host2_if
	check_err $? "Packet was not flooded when should"

	log_test "Unregistered multicast flood"
}

flood_test()
{
	# `br_port` is connected to `host2_if`
	local br_port=$1
	local host1_if=$2
	local host2_if=$3

	flood_unicast_test $br_port $host1_if $host2_if
	flood_multicast_test $br_port $host1_if $host2_if
}
