#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test qdisc offload indication


ALL_TESTS="
	test_root
	test_etsprio
"
NUM_NETIFS=1
lib_dir=$(dirname $0)/../../../net/forwarding
source $lib_dir/lib.sh

check_not_offloaded()
{
	local handle=$1; shift
	local h
	local offloaded

	h=$(qdisc_stats_get $h1 "$handle" .handle)
	[[ $h == '"'$handle'"' ]]
	check_err $? "Qdisc with handle $handle does not exist"

	offloaded=$(qdisc_stats_get $h1 "$handle" .offloaded)
	[[ $offloaded == true ]]
	check_fail $? "Qdisc with handle $handle offloaded, but should not be"
}

check_all_offloaded()
{
	local handle=$1; shift

	if [[ ! -z $handle ]]; then
		local offloaded=$(qdisc_stats_get $h1 "$handle" .offloaded)
		[[ $offloaded == true ]]
		check_err $? "Qdisc with handle $handle not offloaded"
	fi

	local unoffloaded=$(tc q sh dev $h1 invisible |
				grep -v offloaded |
				sed s/root/parent\ root/ |
				cut -d' ' -f 5)
	[[ -z $unoffloaded ]]
	check_err $? "Qdiscs with following parents not offloaded: $unoffloaded"

	pre_cleanup
}

with_ets()
{
	local handle=$1; shift
	local locus=$1; shift

	tc qdisc add dev $h1 $locus handle $handle \
	   ets bands 8 priomap 7 6 5 4 3 2 1 0
	"$@"
	tc qdisc del dev $h1 $locus
}

with_prio()
{
	local handle=$1; shift
	local locus=$1; shift

	tc qdisc add dev $h1 $locus handle $handle \
	   prio bands 8 priomap 7 6 5 4 3 2 1 0
	"$@"
	tc qdisc del dev $h1 $locus
}

with_red()
{
	local handle=$1; shift
	local locus=$1; shift

	tc qdisc add dev $h1 $locus handle $handle \
	   red limit 1000000 min 200000 max 300000 probability 0.5 avpkt 1500
	"$@"
	tc qdisc del dev $h1 $locus
}

with_tbf()
{
	local handle=$1; shift
	local locus=$1; shift

	tc qdisc add dev $h1 $locus handle $handle \
	   tbf rate 400Mbit burst 128K limit 1M
	"$@"
	tc qdisc del dev $h1 $locus
}

with_pfifo()
{
	local handle=$1; shift
	local locus=$1; shift

	tc qdisc add dev $h1 $locus handle $handle pfifo limit 100K
	"$@"
	tc qdisc del dev $h1 $locus
}

with_bfifo()
{
	local handle=$1; shift
	local locus=$1; shift

	tc qdisc add dev $h1 $locus handle $handle bfifo limit 100K
	"$@"
	tc qdisc del dev $h1 $locus
}

with_drr()
{
	local handle=$1; shift
	local locus=$1; shift

	tc qdisc add dev $h1 $locus handle $handle drr
	"$@"
	tc qdisc del dev $h1 $locus
}

with_qdiscs()
{
	local handle=$1; shift
	local parent=$1; shift
	local kind=$1; shift
	local next_handle=$((handle * 2))
	local locus;

	if [[ $kind == "--" ]]; then
		local cmd=$1; shift
		$cmd $(printf %x: $parent) "$@"
	else
		if ((parent == 0)); then
			locus=root
		else
			locus=$(printf "parent %x:1" $parent)
		fi

		with_$kind $(printf %x: $handle) "$locus" \
			with_qdiscs $next_handle $handle "$@"
	fi
}

get_name()
{
	local parent=$1; shift
	local name=$(echo "" "${@^^}" | tr ' ' -)

	if ((parent != 0)); then
		kind=$(qdisc_stats_get $h1 $parent: .kind)
		kind=${kind%\"}
		kind=${kind#\"}
		name="-${kind^^}$name"
	fi

	echo root$name
}

do_test_offloaded()
{
	local handle=$1; shift
	local parent=$1; shift

	RET=0
	with_qdiscs $handle $parent "$@" -- check_all_offloaded
	log_test $(get_name $parent "$@")" offloaded"
}

do_test_nooffload()
{
	local handle=$1; shift
	local parent=$1; shift

	local name=$(echo "${@^^}" | tr ' ' -)
	local kind

	RET=0
	with_qdiscs $handle $parent "$@" -- check_not_offloaded
	log_test $(get_name $parent "$@")" not offloaded"
}

do_test_combinations()
{
	local handle=$1; shift
	local parent=$1; shift

	local cont
	local leaf
	local fifo

	for cont in "" ets prio; do
		for leaf in "" red tbf "red tbf" "tbf red"; do
			for fifo in "" pfifo bfifo; do
				if [[ -z "$cont$leaf$fifo" ]]; then
					continue
				fi
				do_test_offloaded $handle $parent \
						  $cont $leaf $fifo
			done
		done
	done

	for cont in ets prio; do
		for leaf in red tbf; do
			do_test_nooffload $handle $parent $cont red tbf $leaf
			do_test_nooffload $handle $parent $cont tbf red $leaf
		done
		for leaf in "red red" "tbf tbf"; do
			do_test_nooffload $handle $parent $cont $leaf
		done
	done

	do_test_nooffload $handle $parent drr
}

test_root()
{
	do_test_combinations 1 0
}

do_test_etsprio()
{
	local parent=$1; shift
	local tbfpfx=$1; shift
	local cont

	for cont in ets prio; do
		RET=0
		with_$cont 8: "$parent" \
			with_red 11: "parent 8:1" \
			with_red 12: "parent 8:2" \
			with_tbf 13: "parent 8:3" \
			with_tbf 14: "parent 8:4" \
			check_all_offloaded
		log_test "root$tbfpfx-ETS-{RED,TBF} offloaded"

		RET=0
		with_$cont 8: "$parent" \
			with_red 81: "parent 8:1" \
				with_tbf 811: "parent 81:1" \
			with_tbf 84: "parent 8:4" \
				with_red 841: "parent 84:1" \
			check_all_offloaded
		log_test "root$tbfpfx-ETS-{RED-TBF,TBF-RED} offloaded"

		RET=0
		with_$cont 8: "$parent" \
			with_red 81: "parent 8:1" \
				with_tbf 811: "parent 81:1" \
					with_bfifo 8111: "parent 811:1" \
			with_tbf 82: "parent 8:2" \
				with_red 821: "parent 82:1" \
					with_bfifo 8211: "parent 821:1" \
			check_all_offloaded
		log_test "root$tbfpfx-ETS-{RED-TBF-bFIFO,TBF-RED-bFIFO} offloaded"
	done
}

test_etsprio()
{
	do_test_etsprio root ""
}

cleanup()
{
	tc qdisc del dev $h1 root &>/dev/null
}

trap cleanup EXIT
h1=${NETIFS[p1]}
tests_run

exit $EXIT_STATUS
