#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Simple example:
#  * pktgen sending with single thread and single interface
#  * flow variation via random UDP source port
#
basedir=`dirname $0`
source ${basedir}/functions.sh
root_check_run_with_sudo "$@"

# Parameter parsing via include
# - go look in parameters.sh to see which setting are avail
# - required param is the interface "-i" stored in $DEV
source ${basedir}/parameters.sh
#
# Set some default params, if they didn't get set
if [ -z "$DEST_IP" ]; then
    [ -z "$IP6" ] && DEST_IP="198.18.0.42" || DEST_IP="FD00::1"
fi
[ -z "$CLONE_SKB" ] && CLONE_SKB="0"
# Example enforce param "-m" for dst_mac
[ -z "$DST_MAC" ] && usage && err 2 "Must specify -m dst_mac"
[ -z "$COUNT" ]   && COUNT="100000" # Zero means indefinitely
if [ -n "$DST_PORT" ]; then
    read -r DST_MIN DST_MAX <<< $(parse_ports $DST_PORT)
    validate_ports $DST_MIN $DST_MAX
fi

# Base Config
DELAY="0"        # Zero means max speed

# Flow variation random source port between min and max
UDP_MIN=9
UDP_MAX=109

# General cleanup everything since last run
# (especially important if other threads were configured by other scripts)
pg_ctrl "reset"

# Add remove all other devices and add_device $DEV to thread 0
thread=0
pg_thread $thread "rem_device_all"
pg_thread $thread "add_device" $DEV

# How many packets to send (zero means indefinitely)
pg_set $DEV "count $COUNT"

# Reduce alloc cost by sending same SKB many times
# - this obviously affects the randomness within the packet
pg_set $DEV "clone_skb $CLONE_SKB"

# Set packet size
pg_set $DEV "pkt_size $PKT_SIZE"

# Delay between packets (zero means max speed)
pg_set $DEV "delay $DELAY"

# Flag example disabling timestamping
pg_set $DEV "flag NO_TIMESTAMP"

# Destination
pg_set $DEV "dst_mac $DST_MAC"
pg_set $DEV "dst$IP6 $DEST_IP"

if [ -n "$DST_PORT" ]; then
    # Single destination port or random port range
    pg_set $DEV "flag UDPDST_RND"
    pg_set $DEV "udp_dst_min $DST_MIN"
    pg_set $DEV "udp_dst_max $DST_MAX"
fi

# Setup random UDP port src range
pg_set $DEV "flag UDPSRC_RND"
pg_set $DEV "udp_src_min $UDP_MIN"
pg_set $DEV "udp_src_max $UDP_MAX"

# start_run
echo "Running... ctrl^C to stop" >&2
pg_ctrl "start"
echo "Done" >&2

# Print results
echo "Result device: $DEV"
cat /proc/net/pktgen/$DEV
