#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="match_cfm_opcode match_cfm_level match_cfm_level_and_opcode"
NUM_NETIFS=2
source tc_common.sh
source lib.sh

h1_create()
{
	simple_if_init $h1
}

h1_destroy()
{
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
	tc qdisc add dev $h2 clsact
}

h2_destroy()
{
	tc qdisc del dev $h2 clsact
	simple_if_fini $h2
}

u8_to_hex()
{
	local u8=$1; shift

	printf "%02x" $u8
}

generate_cfm_hdr()
{
	local mdl=$1; shift
	local op=$1; shift
	local flags=$1; shift
	local tlv_offset=$1; shift

	local cfm_hdr=$(:
	               )"$(u8_to_hex $((mdl << 5))):"$( 	: MD level and Version
	               )"$(u8_to_hex $op):"$(			: OpCode
	               )"$(u8_to_hex $flags):"$(		: Flags
	               )"$(u8_to_hex $tlv_offset)"$(		: TLV offset
	               )

	echo $cfm_hdr
}

match_cfm_opcode()
{
	local ethtype="89 02"; readonly ethtype
	RET=0

	tc filter add dev $h2 ingress protocol cfm pref 1 handle 101 \
	   flower cfm op 47 action drop
	tc filter add dev $h2 ingress protocol cfm pref 1 handle 102 \
	   flower cfm op 43 action drop

	pkt="$ethtype $(generate_cfm_hdr 7 47 0 32)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q
	pkt="$ethtype $(generate_cfm_hdr 6 5 0 4)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match on correct opcode"

	tc_check_packets "dev $h2 ingress" 102 0
	check_err $? "Matched on the wrong opcode"

	pkt="$ethtype $(generate_cfm_hdr 0 43 0 12)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Matched on the wrong opcode"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct opcode"

	tc filter del dev $h2 ingress protocol cfm pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol cfm pref 1 handle 102 flower

	log_test "CFM opcode match test"
}

match_cfm_level()
{
	local ethtype="89 02"; readonly ethtype
	RET=0

	tc filter add dev $h2 ingress protocol cfm pref 1 handle 101 \
	   flower cfm mdl 5 action drop
	tc filter add dev $h2 ingress protocol cfm pref 1 handle 102 \
	   flower cfm mdl 3 action drop
	tc filter add dev $h2 ingress protocol cfm pref 1 handle 103 \
	   flower cfm mdl 0 action drop

	pkt="$ethtype $(generate_cfm_hdr 5 42 0 12)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q
	pkt="$ethtype $(generate_cfm_hdr 6 1 0 70)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q
	pkt="$ethtype $(generate_cfm_hdr 0 1 0 70)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match on correct level"

	tc_check_packets "dev $h2 ingress" 102 0
	check_err $? "Matched on the wrong level"

	tc_check_packets "dev $h2 ingress" 103 1
	check_err $? "Did not match on correct level"

	pkt="$ethtype $(generate_cfm_hdr 3 0 0 4)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Matched on the wrong level"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct level"

	tc_check_packets "dev $h2 ingress" 103 1
	check_err $? "Matched on the wrong level"

	tc filter del dev $h2 ingress protocol cfm pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol cfm pref 1 handle 102 flower
	tc filter del dev $h2 ingress protocol cfm pref 1 handle 103 flower

	log_test "CFM level match test"
}

match_cfm_level_and_opcode()
{
	local ethtype="89 02"; readonly ethtype
	RET=0

	tc filter add dev $h2 ingress protocol cfm pref 1 handle 101 \
	   flower cfm mdl 5 op 41 action drop
	tc filter add dev $h2 ingress protocol cfm pref 1 handle 102 \
	   flower cfm mdl 7 op 42 action drop

	pkt="$ethtype $(generate_cfm_hdr 5 41 0 4)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q
	pkt="$ethtype $(generate_cfm_hdr 7 3 0 4)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q
	pkt="$ethtype $(generate_cfm_hdr 3 42 0 12)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match on correct level and opcode"

	tc_check_packets "dev $h2 ingress" 102 0
	check_err $? "Matched on the wrong level and opcode"

	pkt="$ethtype $(generate_cfm_hdr 7 42 0 12)"
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac "$pkt" -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Matched on the wrong level and opcode"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct level and opcode"

	tc filter del dev $h2 ingress protocol cfm pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol cfm pref 1 handle 102 flower

	log_test "CFM opcode and level match test"
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}
	h1mac=$(mac_get $h1)
	h2mac=$(mac_get $h2)

	vrf_prepare

	h1_create
	h2_create
}

cleanup()
{
	pre_cleanup

	h2_destroy
	h1_destroy

	vrf_cleanup
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
