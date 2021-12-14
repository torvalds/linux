#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Script example for many flows testing
#
# Number of simultaneous flows limited by variable $FLOWS
# and number of packets per flow controlled by variable $FLOWLEN
#
basedir=`dirname $0`
source ${basedir}/functions.sh
root_check_run_with_sudo "$@"

# Parameter parsing via include
source ${basedir}/parameters.sh
# Set some default params, if they didn't get set
if [ -z "$DEST_IP" ]; then
    [ -z "$IP6" ] && DEST_IP="198.18.0.42" || DEST_IP="FD00::1"
fi
[ -z "$DST_MAC" ]   && DST_MAC="90:e2:ba:ff:ff:ff"
[ -z "$CLONE_SKB" ] && CLONE_SKB="0"
[ -z "$COUNT" ]     && COUNT="0" # Zero means indefinitely
if [ -n "$DEST_IP" ]; then
    validate_addr${IP6} $DEST_IP
    read -r DST_MIN DST_MAX <<< $(parse_addr${IP6} $DEST_IP)
fi
if [ -n "$DST_PORT" ]; then
    read -r UDP_DST_MIN UDP_DST_MAX <<< $(parse_ports $DST_PORT)
    validate_ports $UDP_DST_MIN $UDP_DST_MAX
fi

# NOTICE:  Script specific settings
# =======
# Limiting the number of concurrent flows ($FLOWS)
# and also set how many packets each flow contains ($FLOWLEN)
#
[ -z "$FLOWS" ]     && FLOWS="8000"
[ -z "$FLOWLEN" ]   && FLOWLEN="10"

# Base Config
DELAY="0"  # Zero means max speed

if [[ -n "$BURST" ]]; then
    err 1 "Bursting not supported for this mode"
fi

# 198.18.0.0 / 198.19.255.255
read -r SRC_MIN SRC_MAX <<< $(parse_addr 198.18.0.0/15)

# General cleanup everything since last run
pg_ctrl "reset"

# Threads are specified with parameter -t value in $THREADS
for ((thread = $F_THREAD; thread <= $L_THREAD; thread++)); do
    dev=${DEV}@${thread}

    # Add remove all other devices and add_device $dev to thread
    pg_thread $thread "rem_device_all"
    pg_thread $thread "add_device" $dev

    # Base config
    pg_set $dev "flag QUEUE_MAP_CPU"
    pg_set $dev "count $COUNT"
    pg_set $dev "clone_skb $CLONE_SKB"
    pg_set $dev "pkt_size $PKT_SIZE"
    pg_set $dev "delay $DELAY"
    pg_set $dev "flag NO_TIMESTAMP"

    # Single destination
    pg_set $dev "dst_mac $DST_MAC"
    pg_set $dev "dst${IP6}_min $DST_MIN"
    pg_set $dev "dst${IP6}_max $DST_MAX"

    if [ -n "$DST_PORT" ]; then
	# Single destination port or random port range
	pg_set $dev "flag UDPDST_RND"
	pg_set $dev "udp_dst_min $UDP_DST_MIN"
	pg_set $dev "udp_dst_max $UDP_DST_MAX"
    fi

    # Randomize source IP-addresses
    pg_set $dev "flag IPSRC_RND"
    pg_set $dev "src_min $SRC_MIN"
    pg_set $dev "src_max $SRC_MAX"

    # Limit number of flows (max 65535)
    pg_set $dev "flows $FLOWS"
    #
    # How many packets a flow will send, before flow "entry" is
    # re-generated/setup.
    pg_set $dev "flowlen $FLOWLEN"
    #
    # Flag FLOW_SEQ will cause $FLOWLEN packets from the same flow
    # being send back-to-back, before next flow is selected
    # incrementally.  This helps lookup caches, and is more realistic.
    #
    pg_set $dev "flag FLOW_SEQ"

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

echo "Running... ctrl^C to stop" >&2
pg_ctrl "start"

print_result
