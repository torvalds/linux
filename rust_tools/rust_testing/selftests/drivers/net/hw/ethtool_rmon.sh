#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	rmon_rx_histogram
	rmon_tx_histogram
"

NUM_NETIFS=2
lib_dir=$(dirname "$0")
source "$lib_dir"/../../../net/forwarding/lib.sh

ETH_FCS_LEN=4
ETH_HLEN=$((6+6+2))

declare -A netif_mtu

ensure_mtu()
{
	local iface=$1; shift
	local len=$1; shift
	local current=$(ip -j link show dev $iface | jq -r '.[0].mtu')
	local required=$((len - ETH_HLEN - ETH_FCS_LEN))

	if [ $current -lt $required ]; then
		ip link set dev $iface mtu $required || return 1
	fi
}

bucket_test()
{
	local iface=$1; shift
	local neigh=$1; shift
	local set=$1; shift
	local bucket=$1; shift
	local len=$1; shift
	local num_rx=10000
	local num_tx=20000
	local expected=
	local before=
	local after=
	local delta=

	# Mausezahn does not include FCS bytes in its length - but the
	# histogram counters do
	len=$((len - ETH_FCS_LEN))
	len=$((len > 0 ? len : 0))

	before=$(ethtool --json -S $iface --groups rmon | \
		jq -r ".[0].rmon[\"${set}-pktsNtoM\"][$bucket].val")

	# Send 10k one way and 20k in the other, to detect counters
	# mapped to the wrong direction
	$MZ $neigh -q -c $num_rx -p $len -a own -b bcast -d 10us
	$MZ $iface -q -c $num_tx -p $len -a own -b bcast -d 10us

	after=$(ethtool --json -S $iface --groups rmon | \
		jq -r ".[0].rmon[\"${set}-pktsNtoM\"][$bucket].val")

	delta=$((after - before))

	expected=$([ $set = rx ] && echo $num_rx || echo $num_tx)

	# Allow some extra tolerance for other packets sent by the stack
	[ $delta -ge $expected ] && [ $delta -le $((expected + 100)) ]
}

rmon_histogram()
{
	local iface=$1; shift
	local neigh=$1; shift
	local set=$1; shift
	local nbuckets=0
	local step=

	RET=0

	while read -r -a bucket; do
		step="$set-pkts${bucket[0]}to${bucket[1]} on $iface"

		for if in $iface $neigh; do
			if ! ensure_mtu $if ${bucket[0]}; then
				log_test_xfail "$if does not support the required MTU for $step"
				return
			fi
		done

		if ! bucket_test $iface $neigh $set $nbuckets ${bucket[0]}; then
			check_err 1 "$step failed"
			return 1
		fi
		log_test "$step"
		nbuckets=$((nbuckets + 1))
	done < <(ethtool --json -S $iface --groups rmon | \
		jq -r ".[0].rmon[\"${set}-pktsNtoM\"][]|[.low, .high]|@tsv" 2>/dev/null)

	if [ $nbuckets -eq 0 ]; then
		log_test_xfail "$iface does not support $set histogram counters"
		return
	fi
}

rmon_rx_histogram()
{
	rmon_histogram $h1 $h2 rx
	rmon_histogram $h2 $h1 rx
}

rmon_tx_histogram()
{
	rmon_histogram $h1 $h2 tx
	rmon_histogram $h2 $h1 tx
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}

	for iface in $h1 $h2; do
		netif_mtu[$iface]=$(ip -j link show dev $iface | jq -r '.[0].mtu')
		ip link set dev $iface up
	done
}

cleanup()
{
	pre_cleanup

	for iface in $h2 $h1; do
		ip link set dev $iface \
			mtu ${netif_mtu[$iface]} \
			down
	done
}

check_ethtool_counter_group_support
trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS
