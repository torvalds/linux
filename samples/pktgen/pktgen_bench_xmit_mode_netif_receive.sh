#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Benchmark script:
#  - developed for benchmarking ingress qdisc path
#
# Script for injecting packets into RX path of the stack with pktgen
# "xmit_mode netif_receive".  With an invalid dst_mac this will only
# measure the ingress code path as packets gets dropped in ip_rcv().
#
# This script don't really need any hardware.  It benchmarks software
# RX path just after NIC driver level.  With bursting is also
# "removes" the SKB alloc/free overhead.
#
# Setup scenarios for measuring ingress qdisc (with invalid dst_mac):
# ------------------------------------------------------------------
# (1) no ingress (uses static_key_false(&ingress_needed))
#
# (2) ingress on other dev (change ingress_needed and calls
#     handle_ing() but exit early)
#
#  config:  tc qdisc add dev $SOMEDEV handle ffff: ingress
#
# (3) ingress on this dev, handle_ing() -> tc_classify()
#
#  config:  tc qdisc add dev $DEV handle ffff: ingress
#
# (4) ingress on this dev + drop at u32 classifier/action.
#
basedir=`dirname $0`
source ${basedir}/functions.sh
root_check_run_with_sudo "$@"

# Parameter parsing via include
source ${basedir}/parameters.sh
# Using invalid DST_MAC will cause the packets to get dropped in
# ip_rcv() which is part of the test
if [ -z "$DEST_IP" ]; then
    [ -z "$IP6" ] && DEST_IP="198.18.0.42" || DEST_IP="FD00::1"
fi
[ -z "$DST_MAC" ] && DST_MAC="90:e2:ba:ff:ff:ff"
[ -z "$BURST" ] && BURST=1024
[ -z "$COUNT" ] && COUNT="10000000" # Zero means indefinitely
if [ -n "$DEST_IP" ]; then
    validate_addr${IP6} $DEST_IP
    read -r DST_MIN DST_MAX <<< $(parse_addr${IP6} $DEST_IP)
fi
if [ -n "$DST_PORT" ]; then
    read -r UDP_DST_MIN UDP_DST_MAX <<< $(parse_ports $DST_PORT)
    validate_ports $UDP_DST_MIN $UDP_DST_MAX
fi

# General cleanup everything since last run
pg_ctrl "reset"

# Threads are specified with parameter -t value in $THREADS
for ((thread = $F_THREAD; thread <= $L_THREAD; thread++)); do
    # The device name is extended with @name, using thread number to
    # make then unique, but any name will do.
    dev=${DEV}@${thread}

    # Add remove all other devices and add_device $dev to thread
    pg_thread $thread "rem_device_all"
    pg_thread $thread "add_device" $dev

    # Base config of dev
    pg_set $dev "flag QUEUE_MAP_CPU"
    pg_set $dev "count $COUNT"
    pg_set $dev "pkt_size $PKT_SIZE"
    pg_set $dev "delay $DELAY"
    pg_set $dev "flag NO_TIMESTAMP"

    # Destination
    pg_set $dev "dst_mac $DST_MAC"
    pg_set $dev "dst${IP6}_min $DST_MIN"
    pg_set $dev "dst${IP6}_max $DST_MAX"

    if [ -n "$DST_PORT" ]; then
	# Single destination port or random port range
	pg_set $dev "flag UDPDST_RND"
	pg_set $dev "udp_dst_min $UDP_DST_MIN"
	pg_set $dev "udp_dst_max $UDP_DST_MAX"
    fi

    # Inject packet into RX path of stack
    pg_set $dev "xmit_mode netif_receive"

    # Burst allow us to avoid measuring SKB alloc/free overhead
    pg_set $dev "burst $BURST"
done

# Run if user hits control-c
function print_result() {
    # Print results
    for ((thread = $F_THREAD; thread <= $L_THREAD; thread++)); do
        dev=${DEV}@${thread}
        echo "Device: $dev"
        cat /proc/net/pktgen/$dev | grep -A2 "Result:"
    done
}
# trap keyboard interrupt (Ctrl-C)
trap true SIGINT

# start_run
echo "Running... ctrl^C to stop" >&2
pg_ctrl "start"
echo "Done" >&2

print_result
