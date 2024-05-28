# SPDX-License-Identifier: GPL-2.0

PORT_RANGE_NUM_NETIFS=2

port_range_h1_create()
{
	simple_if_init $h1
}

port_range_h1_destroy()
{
	simple_if_fini $h1
}

port_range_switch_create()
{
	simple_if_init $swp1
	tc qdisc add dev $swp1 clsact
}

port_range_switch_destroy()
{
	tc qdisc del dev $swp1 clsact
	simple_if_fini $swp1
}

port_range_rules_create()
{
	local count=$1; shift
	local should_fail=$1; shift
	local batch_file="$(mktemp)"

	for ((i = 0; i < count; ++i)); do
		cat >> $batch_file <<-EOF
			filter add dev $swp1 ingress \
				prot ipv4 \
				pref 1000 \
				flower skip_sw \
				ip_proto udp dst_port 1-$((100 + i)) \
				action pass
		EOF
	done

	tc -b $batch_file
	check_err_fail $should_fail $? "Rule insertion"

	rm -f $batch_file
}

__port_range_test()
{
	local count=$1; shift
	local should_fail=$1; shift

	port_range_rules_create $count $should_fail

	offload_count=$(tc -j filter show dev $swp1 ingress |
			jq "[.[] | select(.options.in_hw == true)] | length")
	((offload_count == count))
	check_err_fail $should_fail $? "port range offload count"
}

port_range_test()
{
	local count=$1; shift
	local should_fail=$1; shift

	if ! tc_offload_check $PORT_RANGE_NUM_NETIFS; then
		check_err 1 "Could not test offloaded functionality"
		return
	fi

	__port_range_test $count $should_fail
}

port_range_setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	vrf_prepare

	port_range_h1_create
	port_range_switch_create
}

port_range_cleanup()
{
	pre_cleanup

	port_range_switch_destroy
	port_range_h1_destroy

	vrf_cleanup
}
