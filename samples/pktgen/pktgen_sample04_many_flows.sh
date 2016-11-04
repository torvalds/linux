#!/bin/bash
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
[ -z "$DEST_IP" ]   && DEST_IP="198.18.0.42"
[ -z "$DST_MAC" ]   && DST_MAC="90:e2:ba:ff:ff:ff"
[ -z "$CLONE_SKB" ] && CLONE_SKB="0"

# NOTICE:  Script specific settings
# =======
# Limiting the number of concurrent flows ($FLOWS)
# and also set how many packets each flow contains ($FLOWLEN)
#
[ -z "$FLOWS" ]     && FLOWS="8000"
[ -z "$FLOWLEN" ]   && FLOWLEN="10"

# Base Config
DELAY="0"  # Zero means max speed
COUNT="0"  # Zero means indefinitely

if [[ -n "$BURST" ]]; then
    err 1 "Bursting not supported for this mode"
fi

# General cleanup everything since last run
pg_ctrl "reset"

# Threads are specified with parameter -t value in $THREADS
for ((thread = 0; thread < $THREADS; thread++)); do
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
    pg_set $dev "dst $DEST_IP"

    # Randomize source IP-addresses
    pg_set $dev "flag IPSRC_RND"
    pg_set $dev "src_min 198.18.0.0"
    pg_set $dev "src_max 198.19.255.255"

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
    for ((thread = 0; thread < $THREADS; thread++)); do
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
