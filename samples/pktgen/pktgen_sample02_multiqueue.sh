#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Multiqueue: Using pktgen threads for sending on multiple CPUs
#  * adding devices to kernel threads
#  * notice the naming scheme for keeping device names unique
#  * nameing scheme: dev@thread_number
#  * flow variation via random UDP source port
#
basedir=`dirname $0`
source ${basedir}/functions.sh
root_check_run_with_sudo "$@"
#
# Required param: -i dev in $DEV
source ${basedir}/parameters.sh

# Trap EXIT first
trap_exit

[ -z "$COUNT" ] && COUNT="100000" # Zero means indefinitely

# Base Config
[ -z "$CLONE_SKB" ] && CLONE_SKB="0"

# Flow variation random source port between min and max
UDP_SRC_MIN=9
UDP_SRC_MAX=109

# (example of setting default params in your script)
if [ -z "$DEST_IP" ]; then
    [ -z "$IP6" ] && DEST_IP="198.18.0.42" || DEST_IP="FD00::1"
fi
[ -z "$DST_MAC" ] && DST_MAC="90:e2:ba:ff:ff:ff"
if [ -n "$DEST_IP" ]; then
    validate_addr${IP6} $DEST_IP
    read -r DST_MIN DST_MAX <<< $(parse_addr${IP6} $DEST_IP)
fi
if [ -n "$DST_PORT" ]; then
    read -r UDP_DST_MIN UDP_DST_MAX <<< $(parse_ports $DST_PORT)
    validate_ports $UDP_DST_MIN $UDP_DST_MAX
fi

# General cleanup everything since last run
[ -z "$APPEND" ] && pg_ctrl "reset"

# Threads are specified with parameter -t value in $THREADS
for ((thread = $F_THREAD; thread <= $L_THREAD; thread++)); do
    # The device name is extended with @name, using thread number to
    # make then unique, but any name will do.
    dev=${DEV}@${thread}

    # Add remove all other devices and add_device $dev to thread
    [ -z "$APPEND" ] && pg_thread $thread "rem_device_all"
    pg_thread $thread "add_device" $dev

    # Notice config queue to map to cpu (mirrors smp_processor_id())
    # It is beneficial to map IRQ /proc/irq/*/smp_affinity 1:1 to CPU number
    pg_set $dev "flag QUEUE_MAP_CPU"

    # Base config of dev
    pg_set $dev "count $COUNT"
    pg_set $dev "clone_skb $CLONE_SKB"
    pg_set $dev "pkt_size $PKT_SIZE"
    pg_set $dev "delay $DELAY"

    # Flag example disabling timestamping
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

    [ ! -z "$UDP_CSUM" ] && pg_set $dev "flag UDPCSUM"

    # Setup random UDP port src range
    pg_set $dev "flag UDPSRC_RND"
    pg_set $dev "udp_src_min $UDP_SRC_MIN"
    pg_set $dev "udp_src_max $UDP_SRC_MAX"
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

if [ -z "$APPEND" ]; then
    # start_run
    echo "Running... ctrl^C to stop" >&2
    pg_ctrl "start"
    echo "Done" >&2

    print_result
else
    echo "Append mode: config done. Do more or use 'pg_ctrl start' to run"
fi
