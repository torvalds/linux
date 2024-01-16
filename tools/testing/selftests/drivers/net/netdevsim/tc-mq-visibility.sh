#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source ethtool-common.sh

set -o pipefail

n_children() {
    n=$(tc qdisc show dev $NDEV | grep '^qdisc' | wc -l)
    echo $((n - 1))
}

tcq() {
    tc qdisc $1 dev $NDEV ${@:2}
}

n_child_assert() {
    n=$(n_children)
    if [ $n -ne $1 ]; then
	echo "ERROR ($root): ${@:2}, expected $1 have $n"
	((num_errors++))
    else
	((num_passes++))
    fi
}


for root in mq mqprio; do
    NDEV=$(make_netdev 1 4)

    opts=
    [ $root == "mqprio" ] && opts='hw 0 num_tc 1 map 0 0 0 0  queues 1@0'

    tcq add root handle 100: $root $opts
    n_child_assert 4 'Init'

    # All defaults

    for n in 3 2 1 2 3 4 1 4; do
	ethtool -L $NDEV combined $n
	n_child_assert $n "Change queues to $n while down"
    done

    ip link set dev $NDEV up

    for n in 3 2 1 2 3 4 1 4; do
	ethtool -L $NDEV combined $n
	n_child_assert $n "Change queues to $n while up"
    done

    # One real one
    tcq replace parent 100:4 handle 204: pfifo_fast
    n_child_assert 4 "One real queue"

    ethtool -L $NDEV combined 1
    n_child_assert 2 "One real queue, one default"

    ethtool -L $NDEV combined 4
    n_child_assert 4 "One real queue, rest default"

    # Graft some
    tcq replace parent 100:1 handle 204:
    n_child_assert 3 "Grafted"

    ethtool -L $NDEV combined 1
    n_child_assert 1 "Grafted, one"

    cleanup_nsim
done

if [ $num_errors -eq 0 ]; then
    echo "PASSED all $((num_passes)) checks"
    exit 0
else
    echo "FAILED $num_errors/$((num_errors+num_passes)) checks"
    exit 1
fi
