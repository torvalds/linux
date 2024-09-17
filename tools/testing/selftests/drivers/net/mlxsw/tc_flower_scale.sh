#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test for resource limit of offloaded flower rules. The test adds a given
# number of flower matches for different IPv6 addresses, then check the offload
# indication for all of the tc flower rules. This file contains functions to set
# up a testing topology and run the test, and is meant to be sourced from a test
# script that calls the testing routine with a given number of rules.

TC_FLOWER_NUM_NETIFS=2

tc_flower_h1_create()
{
	simple_if_init $h1
	tc qdisc add dev $h1 clsact
}

tc_flower_h1_destroy()
{
	tc qdisc del dev $h1 clsact
	simple_if_fini $h1
}

tc_flower_h2_create()
{
	simple_if_init $h2
	tc qdisc add dev $h2 clsact
}

tc_flower_h2_destroy()
{
	tc qdisc del dev $h2 clsact
	simple_if_fini $h2
}

tc_flower_setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}

	vrf_prepare

	tc_flower_h1_create
	tc_flower_h2_create
}

tc_flower_cleanup()
{
	pre_cleanup

	tc_flower_h2_destroy
	tc_flower_h1_destroy

	vrf_cleanup

	if [[ -v TC_FLOWER_BATCH_FILE ]]; then
		rm -f $TC_FLOWER_BATCH_FILE
	fi
}

tc_flower_addr()
{
	local num=$1; shift

	printf "2001:db8:1::%x" $num
}

tc_flower_rules_create()
{
	local count=$1; shift
	local should_fail=$1; shift

	TC_FLOWER_BATCH_FILE="$(mktemp)"

	for ((i = 0; i < count; ++i)); do
		cat >> $TC_FLOWER_BATCH_FILE <<-EOF
			filter add dev $h2 ingress \
				prot ipv6 \
				pref 1000 \
				handle 42$i \
				flower $tcflags dst_ip $(tc_flower_addr $i) \
				action drop
		EOF
	done

	tc -b $TC_FLOWER_BATCH_FILE
	check_err_fail $should_fail $? "Rule insertion"
}

__tc_flower_test()
{
	local count=$1; shift
	local should_fail=$1; shift
	local last=$((count - 1))

	tc_flower_rules_create $count $should_fail

	offload_count=$(tc -j -s filter show dev $h2 ingress    |
			jq -r '[ .[] | select(.kind == "flower") |
			.options | .in_hw ]' | jq .[] | wc -l)
	[[ $((offload_count - 1)) -eq $count ]]
	check_err_fail $should_fail $? "Attempt to offload $count rules (actual result $((offload_count - 1)))"
}

tc_flower_test()
{
	local count=$1; shift
	local should_fail=$1; shift

	# We use lower 16 bits of IPv6 address for match. Also there are only 16
	# bits of rule priority space.
	if ((count > 65536)); then
		check_err 1 "Invalid count of $count. At most 65536 rules supported"
		return
	fi

	if ! tc_offload_check $TC_FLOWER_NUM_NETIFS; then
		check_err 1 "Could not test offloaded functionality"
		return
	fi

	tcflags="skip_sw"
	__tc_flower_test $count $should_fail
}

tc_flower_traffic_test()
{
	local count=$1; shift
	local i;

	for ((i = count - 1; i > 0; i /= 2)); do
		$MZ -6 $h1 -c 1 -d 20msec -p 100 -a own -b $(mac_get $h2) \
		    -A $(tc_flower_addr 0) -B $(tc_flower_addr $i) \
		    -q -t udp sp=54321,dp=12345
	done
	for ((i = count - 1; i > 0; i /= 2)); do
		tc_check_packets "dev $h2 ingress" 42$i 1
		check_err $? "Traffic not seen at rule #$i"
	done
}
