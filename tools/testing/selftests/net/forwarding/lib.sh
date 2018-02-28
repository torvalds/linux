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

if [[ -f forwarding.config ]]; then
	source forwarding.config
fi

##############################################################################
# Sanity checks

if [[ "$(id -u)" -ne 0 ]]; then
	echo "SKIP: need root privileges"
	exit 0
fi

tc -j &> /dev/null
if [[ $? -ne 0 ]]; then
	echo "SKIP: iproute2 too old, missing JSON support"
	exit 0
fi

if [[ ! -x "$(command -v jq)" ]]; then
	echo "SKIP: jq not installed"
	exit 0
fi

if [[ ! -x "$(command -v $MZ)" ]]; then
	echo "SKIP: $MZ not installed"
	exit 0
fi

if [[ ! -v NUM_NETIFS ]]; then
	echo "SKIP: importer does not define \"NUM_NETIFS\""
	exit 0
fi

##############################################################################
# Network interfaces configuration

for i in $(eval echo {1..$NUM_NETIFS}); do
	ip link show dev ${NETIFS[p$i]} &> /dev/null
	if [[ $? -ne 0 ]]; then
		echo "SKIP: could not find all required interfaces"
		exit 0
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

master_name_get()
{
	local if_name=$1

	ip -j link show dev $if_name | jq -r '.[]["master"]'
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
