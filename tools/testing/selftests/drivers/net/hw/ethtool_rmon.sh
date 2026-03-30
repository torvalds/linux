#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#shellcheck disable=SC2034 # SC does not see the global variables
#shellcheck disable=SC2317,SC2329 # unused functions

ALL_TESTS="
	rmon_rx_histogram
	rmon_tx_histogram
"

: "${DRIVER_TEST_CONFORMANT:=yes}"
NUM_NETIFS=2
lib_dir=$(dirname "$0")
source "$lib_dir"/../../../net/forwarding/lib.sh
source "$lib_dir"/../../../kselftest/ktap_helpers.sh

UINT32_MAX=$((2**32 - 1))
ETH_FCS_LEN=4
ETH_HLEN=$((6+6+2))
TEST_NAME=$(basename "$0" .sh)

declare -A netif_mtu

ensure_mtu()
{
	local iface=$1; shift
	local len=$1; shift
	local required=$((len - ETH_HLEN - ETH_FCS_LEN))
	local current

	current=$(run_on "$iface" \
		ip -j link show dev "$iface" | jq -r '.[0].mtu')
	if [ "$current" -lt "$required" ]; then
		run_on "$iface" ip link set dev "$iface" mtu "$required" \
			|| return 1
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

	before=$(run_on "$iface" ethtool --json -S "$iface" --groups rmon | \
		jq -r ".[0].rmon[\"${set}-pktsNtoM\"][$bucket].val")

	# Send 10k one way and 20k in the other, to detect counters
	# mapped to the wrong direction
	run_on "$neigh" \
		"$MZ" "$neigh" -q -c "$num_rx" -p "$len" -a own -b bcast -d 10us
	run_on "$iface" \
		"$MZ" "$iface" -q -c "$num_tx" -p "$len" -a own -b bcast -d 10us

	after=$(run_on "$iface" ethtool --json -S "$iface" --groups rmon | \
		jq -r ".[0].rmon[\"${set}-pktsNtoM\"][$bucket].val")

	delta=$((after - before))

	expected=$([ "$set" = rx ] && echo "$num_rx" || echo "$num_tx")

	[ "$delta" -ge "$expected" ] && [ "$delta" -le "$UINT32_MAX" ]
}

rmon_histogram()
{
	local iface=$1; shift
	local neigh=$1; shift
	local set=$1; shift
	local nbuckets=0
	local step=

	while read -r -a bucket; do
		step="$set-pkts${bucket[0]}to${bucket[1]}"

		for if in "$iface" "$neigh"; do
			if ! ensure_mtu "$if" "${bucket[0]}"; then
				ktap_print_msg "$if does not support the required MTU for $step"
				ktap_test_xfail "$TEST_NAME.$step"
				return
			fi
		done

		if ! bucket_test "$iface" "$neigh" "$set" "$nbuckets" "${bucket[0]}"; then
			ktap_test_fail "$TEST_NAME.$step"
			return 1
		fi
		ktap_test_pass "$TEST_NAME.$step"
		nbuckets=$((nbuckets + 1))
	done < <(run_on "$iface" ethtool --json -S "$iface" --groups rmon | \
		jq -r ".[0].rmon[\"${set}-pktsNtoM\"][]|[.low, .high]|@tsv" 2>/dev/null)

	if [ "$nbuckets" -eq 0 ]; then
		ktap_print_msg "$iface does not support $set histogram counters"
		return
	fi
}

rmon_rx_histogram()
{
	rmon_histogram "$h1" "$h2" rx
}

rmon_tx_histogram()
{
	rmon_histogram "$h1" "$h2" tx
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}

	for iface in "$h1" "$h2"; do
		netif_mtu["$iface"]=$(run_on "$iface" \
			ip -j link show dev "$iface" | jq -r '.[0].mtu')
	done
}

cleanup()
{
	pre_cleanup

	# Do not bring down the interfaces, just configure the initial MTU
	for iface in "$h2" "$h1"; do
		run_on "$iface" ip link set dev "$iface" \
			mtu "${netif_mtu[$iface]}"
	done
}

check_ethtool_counter_group_support
trap cleanup EXIT

bucket_count=$(ethtool --json -S "${NETIFS[p1]}" --groups rmon | \
	jq -r '.[0].rmon |
		"\((."rx-pktsNtoM" | length) +
		   (."tx-pktsNtoM" | length))"')
ktap_print_header
ktap_set_plan "$bucket_count"

setup_prepare
setup_wait

tests_run

ktap_finished
