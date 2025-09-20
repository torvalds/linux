#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# +-------------------------+  +-------------------------+
# |  H1                     |  |                      H2 |
# |               $h1 +     |  | + $h2                   |
# |      192.0.2.1/28 |     |  | | 192.0.2.34/28         |
# |  2001:db8:1::1/64 |     |  | | 2001:db8:3::2/64      |
# +-------------------|-----+  +-|-----------------------+
#                     |          |
# +-------------------|-----+  +-|-----------------------+
# |  R1               |     |  | |                    R2 |
# |             $rp11 +     |  | + $rp21                 |
# |      192.0.2.2/28       |  |   192.0.2.33/28         |
# |  2001:db8:1::2/64       |  |   2001:db8:3::1/64      |
# |                         |  |                         |
# |             $rp12 +     |  | + $rp22                 |
# |     192.0.2.17/28 |     |  | | 192.0.2.18..27/28     |
# | 2001:db8:2::17/64 |     |  | | 2001:db8:2::18..27/64 |
# +-------------------|-----+  +-|-----------------------+
#                     |          |
#                     `----------'

ALL_TESTS="
	ping_ipv4
	ping_ipv6
	test_mpath_seed_stability_ipv4
	test_mpath_seed_stability_ipv6
	test_mpath_seed_get
	test_mpath_seed_ipv4
	test_mpath_seed_ipv6
"
NUM_NETIFS=6
source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/28 2001:db8:1::1/64
	ip -4 route add 192.0.2.32/28 vrf v$h1 nexthop via 192.0.2.2
	ip -6 route add 2001:db8:3::/64 vrf v$h1 nexthop via 2001:db8:1::2
}

h1_destroy()
{
	ip -6 route del 2001:db8:3::/64 vrf v$h1 nexthop via 2001:db8:1::2
	ip -4 route del 192.0.2.32/28 vrf v$h1 nexthop via 192.0.2.2
	simple_if_fini $h1 192.0.2.1/28 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 192.0.2.34/28 2001:db8:3::2/64
	ip -4 route add 192.0.2.0/28 vrf v$h2 nexthop via 192.0.2.33
	ip -6 route add 2001:db8:1::/64 vrf v$h2 nexthop via 2001:db8:3::1
}

h2_destroy()
{
	ip -6 route del 2001:db8:1::/64 vrf v$h2 nexthop via 2001:db8:3::1
	ip -4 route del 192.0.2.0/28 vrf v$h2 nexthop via 192.0.2.33
	simple_if_fini $h2 192.0.2.34/28 2001:db8:3::2/64
}

router1_create()
{
	simple_if_init $rp11 192.0.2.2/28 2001:db8:1::2/64
	__simple_if_init $rp12 v$rp11 192.0.2.17/28 2001:db8:2::17/64
}

router1_destroy()
{
	__simple_if_fini $rp12 192.0.2.17/28 2001:db8:2::17/64
	simple_if_fini $rp11 192.0.2.2/28 2001:db8:1::2/64
}

router2_create()
{
	simple_if_init $rp21 192.0.2.33/28 2001:db8:3::1/64
	__simple_if_init $rp22 v$rp21 192.0.2.18/28 2001:db8:2::18/64
	ip -4 route add 192.0.2.0/28 vrf v$rp21 nexthop via 192.0.2.17
	ip -6 route add 2001:db8:1::/64 vrf v$rp21 nexthop via 2001:db8:2::17
}

router2_destroy()
{
	ip -6 route del 2001:db8:1::/64 vrf v$rp21 nexthop via 2001:db8:2::17
	ip -4 route del 192.0.2.0/28 vrf v$rp21 nexthop via 192.0.2.17
	__simple_if_fini $rp22 192.0.2.18/28 2001:db8:2::18/64
	simple_if_fini $rp21 192.0.2.33/28 2001:db8:3::1/64
}

nexthops_create()
{
	local i
	for i in $(seq 10); do
		ip nexthop add id $((1000 + i)) via 192.0.2.18 dev $rp12
		ip nexthop add id $((2000 + i)) via 2001:db8:2::18 dev $rp12
	done

	ip nexthop add id 1000 group $(seq -s / 1001 1010) hw_stats on
	ip nexthop add id 2000 group $(seq -s / 2001 2010) hw_stats on
	ip -4 route add 192.0.2.32/28 vrf v$rp11 nhid 1000
	ip -6 route add 2001:db8:3::/64 vrf v$rp11 nhid 2000
}

nexthops_destroy()
{
	local i

	ip -6 route del 2001:db8:3::/64 vrf v$rp11 nhid 2000
	ip -4 route del 192.0.2.32/28 vrf v$rp11 nhid 1000
	ip nexthop del id 2000
	ip nexthop del id 1000

	for i in $(seq 10 -1 1); do
		ip nexthop del id $((2000 + i))
		ip nexthop del id $((1000 + i))
	done
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp11=${NETIFS[p2]}

	rp12=${NETIFS[p3]}
	rp22=${NETIFS[p4]}

	rp21=${NETIFS[p5]}
	h2=${NETIFS[p6]}

	sysctl_save net.ipv4.fib_multipath_hash_seed

	vrf_prepare

	h1_create
	h2_create
	router1_create
	router2_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	nexthops_destroy
	router2_destroy
	router1_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup

	sysctl_restore net.ipv4.fib_multipath_hash_seed
}

ping_ipv4()
{
	ping_test $h1 192.0.2.34
}

ping_ipv6()
{
	ping6_test $h1 2001:db8:3::2
}

test_mpath_seed_get()
{
	RET=0

	local i
	for ((i = 0; i < 100; i++)); do
		local seed_w=$((999331 * i))
		sysctl -qw net.ipv4.fib_multipath_hash_seed=$seed_w
		local seed_r=$(sysctl -n net.ipv4.fib_multipath_hash_seed)
		((seed_r == seed_w))
		check_err $? "mpath seed written as $seed_w, but read as $seed_r"
	done

	log_test "mpath seed set/get"
}

nh_stats_snapshot()
{
	local group_id=$1; shift

	ip -j -s -s nexthop show id $group_id |
	    jq -c '[.[].group_stats | sort_by(.id) | .[].packets]'
}

get_active_nh()
{
	local s0=$1; shift
	local s1=$1; shift

	jq -n --argjson s0 "$s0" --argjson s1 "$s1" -f /dev/stdin <<-"EOF"
		[range($s0 | length)] |
		map($s1[.] - $s0[.]) |
		map(if . > 8 then 1 else 0 end) |
		index(1)
	EOF
}

probe_nh()
{
	local group_id=$1; shift
	local -a mz=("$@")

	local s0=$(nh_stats_snapshot $group_id)
	"${mz[@]}"
	local s1=$(nh_stats_snapshot $group_id)

	get_active_nh "$s0" "$s1"
}

probe_seed()
{
	local group_id=$1; shift
	local seed=$1; shift
	local -a mz=("$@")

	sysctl -qw net.ipv4.fib_multipath_hash_seed=$seed
	probe_nh "$group_id" "${mz[@]}"
}

test_mpath_seed()
{
	local group_id=$1; shift
	local what=$1; shift
	local -a mz=("$@")
	local ii

	RET=0

	local -a tally=(0 0 0 0 0 0 0 0 0 0)
	for ((ii = 0; ii < 100; ii++)); do
		local act=$(probe_seed $group_id $((999331 * ii)) "${mz[@]}")
		((tally[act]++))
	done

	local tally_str="${tally[@]}"
	for ((ii = 0; ii < ${#tally[@]}; ii++)); do
		((tally[ii] > 0))
		check_err $? "NH #$ii not hit, tally='$tally_str'"
	done

	log_test "mpath seed $what"
	sysctl -qw net.ipv4.fib_multipath_hash_seed=0
}

test_mpath_seed_ipv4()
{
	test_mpath_seed 1000 IPv4 \
		$MZ $h1 -A 192.0.2.1 -B 192.0.2.34 -q \
			-p 64 -d 0 -c 10 -t udp
}

test_mpath_seed_ipv6()
{
	test_mpath_seed 2000 IPv6 \
		$MZ -6 $h1 -A 2001:db8:1::1 -B 2001:db8:3::2 -q \
			-p 64 -d 0 -c 10 -t udp
}

check_mpath_seed_stability()
{
	local seed=$1; shift
	local act_0=$1; shift
	local act_1=$1; shift

	((act_0 == act_1))
	check_err $? "seed $seed: active NH moved from $act_0 to $act_1 after seed change"
}

test_mpath_seed_stability()
{
	local group_id=$1; shift
	local what=$1; shift
	local -a mz=("$@")

	RET=0

	local seed_0=0
	local seed_1=3221338814
	local seed_2=3735928559

	# Initial active NH before touching the seed at all.
	local act_ini=$(probe_nh $group_id "${mz[@]}")

	local act_0_0=$(probe_seed $group_id $seed_0 "${mz[@]}")
	local act_1_0=$(probe_seed $group_id $seed_1 "${mz[@]}")
	local act_2_0=$(probe_seed $group_id $seed_2 "${mz[@]}")

	local act_0_1=$(probe_seed $group_id $seed_0 "${mz[@]}")
	local act_1_1=$(probe_seed $group_id $seed_1 "${mz[@]}")
	local act_2_1=$(probe_seed $group_id $seed_2 "${mz[@]}")

	check_mpath_seed_stability initial $act_ini $act_0_0
	check_mpath_seed_stability $seed_0 $act_0_0 $act_0_1
	check_mpath_seed_stability $seed_1 $act_1_0 $act_1_1
	check_mpath_seed_stability $seed_2 $act_2_0 $act_2_1

	log_test "mpath seed stability $what"
	sysctl -qw net.ipv4.fib_multipath_hash_seed=0
}

test_mpath_seed_stability_ipv4()
{
	test_mpath_seed_stability 1000 IPv4 \
		$MZ $h1 -A 192.0.2.1 -B 192.0.2.34 -q \
			-p 64 -d 0 -c 10 -t udp
}

test_mpath_seed_stability_ipv6()
{
	test_mpath_seed_stability 2000 IPv6 \
		$MZ -6 $h1 -A 2001:db8:1::1 -B 2001:db8:3::2 -q \
			-p 64 -d 0 -c 10 -t udp
}

trap cleanup EXIT

setup_prepare
setup_wait
nexthops_create

tests_run

exit $EXIT_STATUS
