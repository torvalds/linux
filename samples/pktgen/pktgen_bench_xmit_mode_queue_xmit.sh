#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Benchmark script:
#  - developed for benchmarking egress qdisc path, derived (more
#    like cut'n'pasted) from ingress benchmark script.
#
# Script for injecting packets into egress qdisc path of the stack
# with pktgen "xmit_mode queue_xmit".
#
basedir=`dirname $0`
source ${basedir}/functions.sh
root_check_run_with_sudo "$@"

# Parameter parsing via include
source ${basedir}/parameters.sh
if [ -z "$DEST_IP" ]; then
    [ -z "$IP6" ] && DEST_IP="198.18.0.42" || DEST_IP="FD00::1"
fi
[ -z "$DST_MAC" ] && DST_MAC="90:e2:ba:ff:ff:ff"

# Burst greater than 1 are invalid for queue_xmit mode
if [[ -n "$BURST" ]]; then
    err 1 "Bursting not supported for this mode"
fi
[ -z "$COUNT" ] && COUNT="10000000" # Zero means indefinitely

# Base Config
DELAY="0"        # Zero means max speed

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
    pg_set $dev "dst$IP6 $DEST_IP"

    # Inject packet into TX qdisc egress path of stack
    pg_set $dev "xmit_mode queue_xmit"
done

# start_run
echo "Running... ctrl^C to stop" >&2
pg_ctrl "start"
echo "Done" >&2

# Print results
for ((thread = $F_THREAD; thread <= $L_THREAD; thread++)); do
    dev=${DEV}@${thread}
    echo "Device: $dev"
    cat /proc/net/pktgen/$dev | grep -A2 "Result:"
done
