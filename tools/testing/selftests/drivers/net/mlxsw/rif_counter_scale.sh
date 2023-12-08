# SPDX-License-Identifier: GPL-2.0

RIF_COUNTER_NUM_NETIFS=2

rif_counter_addr4()
{
	local i=$1; shift
	local p=$1; shift

	printf 192.0.%d.%d $((i / 64)) $(((4 * i % 256) + p))
}

rif_counter_addr4pfx()
{
	rif_counter_addr4 $@
	printf /30
}

rif_counter_h1_create()
{
	simple_if_init $h1
}

rif_counter_h1_destroy()
{
	simple_if_fini $h1
}

rif_counter_h2_create()
{
	simple_if_init $h2
}

rif_counter_h2_destroy()
{
	simple_if_fini $h2
}

rif_counter_setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}

	vrf_prepare

	rif_counter_h1_create
	rif_counter_h2_create
}

rif_counter_cleanup()
{
	local count=$1; shift

	pre_cleanup

	for ((i = 1; i <= count; i++)); do
		vlan_destroy $h2 $i
	done

	rif_counter_h2_destroy
	rif_counter_h1_destroy

	vrf_cleanup

	if [[ -v RIF_COUNTER_BATCH_FILE ]]; then
		rm -f $RIF_COUNTER_BATCH_FILE
	fi
}


rif_counter_test()
{
	local count=$1; shift
	local should_fail=$1; shift

	RIF_COUNTER_BATCH_FILE="$(mktemp)"

	for ((i = 1; i <= count; i++)); do
		vlan_create $h2 $i v$h2 $(rif_counter_addr4pfx $i 2)
	done
	for ((i = 1; i <= count; i++)); do
		cat >> $RIF_COUNTER_BATCH_FILE <<-EOF
			stats set dev $h2.$i l3_stats on
		EOF
	done

	ip -b $RIF_COUNTER_BATCH_FILE
	check_err_fail $should_fail $? "RIF counter enablement"
}

rif_counter_traffic_test()
{
	local count=$1; shift
	local i;

	for ((i = count; i > 0; i /= 2)); do
		$MZ $h1 -Q $i -c 1 -d 20msec -p 100 -a own -b $(mac_get $h2) \
		    -A $(rif_counter_addr4 $i 1) \
		    -B $(rif_counter_addr4 $i 2) \
		    -q -t udp sp=54321,dp=12345
	done
	for ((i = count; i > 0; i /= 2)); do
		busywait "$TC_HIT_TIMEOUT" until_counter_is "== 1" \
			 hw_stats_get l3_stats $h2.$i rx packets > /dev/null
		check_err $? "Traffic not seen at RIF $h2.$i"
	done
}
