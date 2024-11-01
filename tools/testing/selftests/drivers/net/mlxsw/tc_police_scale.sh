# SPDX-License-Identifier: GPL-2.0

TC_POLICE_NUM_NETIFS=2

tc_police_h1_create()
{
	simple_if_init $h1
}

tc_police_h1_destroy()
{
	simple_if_fini $h1
}

tc_police_switch_create()
{
	simple_if_init $swp1
	tc qdisc add dev $swp1 clsact
}

tc_police_switch_destroy()
{
	tc qdisc del dev $swp1 clsact
	simple_if_fini $swp1
}

tc_police_addr()
{
       local num=$1; shift

       printf "2001:db8:1::%x" $num
}

tc_police_rules_create()
{
	local count=$1; shift
	local should_fail=$1; shift

	TC_POLICE_BATCH_FILE="$(mktemp)"

	for ((i = 0; i < count; ++i)); do
		cat >> $TC_POLICE_BATCH_FILE <<-EOF
			filter add dev $swp1 ingress \
				prot ipv6 \
				pref 1000 \
				flower skip_sw dst_ip $(tc_police_addr $i) \
				action police rate 10mbit burst 100k \
				conform-exceed drop/ok
		EOF
	done

	tc -b $TC_POLICE_BATCH_FILE
	check_err_fail $should_fail $? "Rule insertion"
}

__tc_police_test()
{
	local count=$1; shift
	local should_fail=$1; shift

	tc_police_rules_create $count $should_fail

	offload_count=$(tc -j filter show dev $swp1 ingress |
			jq "[.[] | select(.options.in_hw == true)] | length")
	((offload_count == count))
	check_err_fail $should_fail $? "tc police offload count"
}

tc_police_test()
{
	local count=$1; shift
	local should_fail=$1; shift

	if ! tc_offload_check $TC_POLICE_NUM_NETIFS; then
		check_err 1 "Could not test offloaded functionality"
		return
	fi

	__tc_police_test $count $should_fail
}

tc_police_setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	vrf_prepare

	tc_police_h1_create
	tc_police_switch_create
}

tc_police_cleanup()
{
	pre_cleanup

	tc_police_switch_destroy
	tc_police_h1_destroy

	vrf_cleanup
}
