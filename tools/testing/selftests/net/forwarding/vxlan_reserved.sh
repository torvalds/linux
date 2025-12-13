#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +--------------------+
# | H1 (vrf)           |
# |    + $h1           |
# |    | 192.0.2.1/28  |
# +----|---------------+
#      |
# +----|--------------------------------+
# | SW |                                |
# | +--|------------------------------+ |
# | |  + $swp1           BR1 (802.1d) | |
# | |                                 | |
# | |  + vx1 (vxlan)                  | |
# | |    local 192.0.2.17             | |
# | |    id 1000 dstport $VXPORT      | |
# | +---------------------------------+ |
# |                                     |
# |  192.0.2.32/28 via 192.0.2.18       |
# |                                     |
# |  + $rp1                             |
# |  | 192.0.2.17/28                    |
# +--|----------------------------------+
#    |
# +--|----------------------------------+
# |  |                                  |
# |  + $rp2                             |
# |    192.0.2.18/28                    |
# |                                     |
# | VRP2 (vrf)                          |
# +-------------------------------------+

: ${VXPORT:=4789}
: ${ALL_TESTS:="
	default_test
	plain_test
	reserved_0_test
	reserved_10_test
	reserved_31_test
	reserved_56_test
	reserved_63_test
    "}

NUM_NETIFS=4
source lib.sh

h1_create()
{
	adf_simple_if_init $h1 192.0.2.1/28

	tc qdisc add dev $h1 clsact
	defer tc qdisc del dev $h1 clsact

	tc filter add dev $h1 ingress pref 77 \
	   prot ip flower skip_hw ip_proto icmp action drop
	defer tc filter del dev $h1 ingress pref 77
}

switch_create()
{
	adf_ip_link_add br1 type bridge vlan_filtering 0 mcast_snooping 0
	# Make sure the bridge uses the MAC address of the local port and not
	# that of the VxLAN's device.
	adf_ip_link_set_addr br1 $(mac_get $swp1)
	adf_ip_link_set_up br1

	adf_ip_link_set_up $rp1
	adf_ip_addr_add $rp1 192.0.2.17/28
	adf_ip_route_add 192.0.2.32/28 nexthop via 192.0.2.18

	adf_ip_link_set_master $swp1 br1
	adf_ip_link_set_up $swp1
}

vrp2_create()
{
	adf_simple_if_init $rp2 192.0.2.18/28
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	rp1=${NETIFS[p3]}
	rp2=${NETIFS[p4]}

	adf_vrf_prepare
	adf_forwarding_enable

	h1_create
	switch_create

	vrp2_create
}

vxlan_header_bytes()
{
	local vni=$1; shift
	local -a extra_bits=("$@")
	local -a bits
	local i

	for ((i=0; i < 64; i++)); do
		bits[i]=0
	done

	# Bit 4 is the I flag and is always on.
	bits[4]=1

	for i in ${extra_bits[@]}; do
		bits[i]=1
	done

	# Bits 32..55 carry the VNI
	local mask=0x800000
	for ((i=0; i < 24; i++)); do
		bits[$((i + 32))]=$(((vni & mask) != 0))
		((mask >>= 1))
	done

	local bytes
	for ((i=0; i < 8; i++)); do
		local byte=0
		local j
		for ((j=0; j < 8; j++)); do
			local bit=${bits[8 * i + j]}
			((byte += bit << (7 - j)))
		done
		bytes+=$(printf %02x $byte):
	done

	echo ${bytes%:}
}

neg_bytes()
{
	local bytes=$1; shift

	local -A neg=([0]=f [1]=e [2]=d [3]=c [4]=b [5]=a [6]=9 [7]=8
		      [8]=7 [9]=6 [a]=5 [b]=4 [c]=3 [d]=2 [e]=1 [f]=0 [:]=:)
	local out
	local i

	for ((i=0; i < ${#bytes}; i++)); do
		local c=${bytes:$i:1}
		out+=${neg[$c]}
	done
	echo $out
}

vxlan_ping_do()
{
	local count=$1; shift
	local dev=$1; shift
	local next_hop_mac=$1; shift
	local dest_ip=$1; shift
	local dest_mac=$1; shift
	local vni=$1; shift
	local reserved_bits=$1; shift

	local vxlan_header=$(vxlan_header_bytes $vni $reserved_bits)

	$MZ $dev -c $count -d 100msec -q \
		-b $next_hop_mac -B $dest_ip \
		-t udp sp=23456,dp=$VXPORT,p=$(:
		    )"$vxlan_header:"$(              : VXLAN
		    )"$dest_mac:"$(                  : ETH daddr
		    )"00:11:22:33:44:55:"$(          : ETH saddr
		    )"08:00:"$(                      : ETH type
		    )"45:"$(                         : IP version + IHL
		    )"00:"$(                         : IP TOS
		    )"00:54:"$(                      : IP total length
		    )"99:83:"$(                      : IP identification
		    )"40:00:"$(                      : IP flags + frag off
		    )"40:"$(                         : IP TTL
		    )"01:"$(                         : IP proto
		    )"00:00:"$(                      : IP header csum
		    )"$(ipv4_to_bytes 192.0.2.3):"$( : IP saddr
		    )"$(ipv4_to_bytes 192.0.2.1):"$( : IP daddr
		    )"08:"$(                         : ICMP type
		    )"00:"$(                         : ICMP code
		    )"8b:f2:"$(                      : ICMP csum
		    )"1f:6a:"$(                      : ICMP request identifier
		    )"00:01:"$(                      : ICMP request seq. number
		    )"4f:ff:c5:5b:00:00:00:00:"$(    : ICMP payload
		    )"6d:74:0b:00:00:00:00:00:"$(    :
		    )"10:11:12:13:14:15:16:17:"$(    :
		    )"18:19:1a:1b:1c:1d:1e:1f:"$(    :
		    )"20:21:22:23:24:25:26:27:"$(    :
		    )"28:29:2a:2b:2c:2d:2e:2f:"$(    :
		    )"30:31:32:33:34:35:36:37"
}

vxlan_device_add()
{
	adf_ip_link_add vx1 up type vxlan id 1000		\
		local 192.0.2.17 dstport "$VXPORT"	\
		nolearning noudpcsum tos inherit ttl 100 "$@"
	adf_ip_link_set_master vx1 br1
}

vxlan_all_reserved_bits()
{
	local i

	for ((i=0; i < 64; i++)); do
		if ((i == 4 || i >= 32 && i < 56)); then
			continue
		fi
		echo $i
	done
}

vxlan_ping_vanilla()
{
	vxlan_ping_do 10 $rp2 $(mac_get $rp1) 192.0.2.17 $(mac_get $h1) 1000
}

vxlan_ping_reserved()
{
	for bit in $(vxlan_all_reserved_bits); do
		vxlan_ping_do 1 $rp2 $(mac_get $rp1) \
			      192.0.2.17 $(mac_get $h1) 1000 "$bit"
		((n++))
	done
}

vxlan_ping_test()
{
	local what=$1; shift
	local get_stat=$1; shift
	local expect=$1; shift

	RET=0

	local t0=$($get_stat)

	"$@"
	check_err $? "Failure when running $@"

	local t1=$($get_stat)
	local delta=$((t1 - t0))

	((expect == delta))
	check_err $? "Expected to capture $expect packets, got $delta."

	log_test "$what"
}

__default_test_do()
{
	local n_allowed_bits=$1; shift
	local what=$1; shift

	vxlan_ping_test "$what: clean packets" \
		"tc_rule_stats_get $h1 77 ingress" \
		10 vxlan_ping_vanilla

	local t0=$(link_stats_get vx1 rx errors)
	vxlan_ping_test "$what: mangled packets" \
		"tc_rule_stats_get $h1 77 ingress" \
		$n_allowed_bits vxlan_ping_reserved
	local t1=$(link_stats_get vx1 rx errors)

	RET=0
	local expect=$((39 - n_allowed_bits))
	local delta=$((t1 - t0))
	((expect == delta))
	check_err $? "Expected $expect error packets, got $delta."
	log_test "$what: drops reported"
}

default_test_do()
{
	vxlan_device_add
	__default_test_do 0 "Default"
}

default_test()
{
	in_defer_scope \
	    default_test_do
}

plain_test_do()
{
	vxlan_device_add reserved_bits 0xf7ffffff000000ff
	__default_test_do 0 "reserved_bits 0xf7ffffff000000ff"
}

plain_test()
{
	in_defer_scope \
	    plain_test_do
}

reserved_test()
{
	local bit=$1; shift

	local allowed_bytes=$(vxlan_header_bytes 0xffffff $bit)
	local reserved_bytes=$(neg_bytes $allowed_bytes)
	local reserved_bits=${reserved_bytes//:/}

	vxlan_device_add reserved_bits 0x$reserved_bits
	__default_test_do 1 "reserved_bits 0x$reserved_bits"
}

reserved_0_test()
{
	in_defer_scope \
	    reserved_test 0
}

reserved_10_test()
{
	in_defer_scope \
	    reserved_test 10
}

reserved_31_test()
{
	in_defer_scope \
	    reserved_test 31
}

reserved_56_test()
{
	in_defer_scope \
	    reserved_test 56
}

reserved_63_test()
{
	in_defer_scope \
	    reserved_test 63
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS
