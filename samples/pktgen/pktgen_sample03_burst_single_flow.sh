#!/bin/bash
#
# Script for max single flow performance
#  - If correctly tuned[1], single CPU 10G wirespeed small pkts is possible[2]
#
# Using pktgen "burst" option (use -b $N)
#  - To boost max performance
#  - Avail since: kernel v3.18
#   * commit 38b2cf2982dc73 ("net: pktgen: packet bursting via skb->xmit_more")
#  - This avoids writing the HW tailptr on every driver xmit
#  - The performance boost is impressive, see commit and blog [2]
#
# Notice: On purpose generates a single (UDP) flow towards target,
#   reason behind this is to only overload/activate a single CPU on
#   target host.  And no randomness for pktgen also makes it faster.
#
# Tuning see:
#  [1] http://netoptimizer.blogspot.dk/2014/06/pktgen-for-network-overload-testing.html
#  [2] http://netoptimizer.blogspot.dk/2014/10/unlocked-10gbps-tx-wirespeed-smallest.html
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
[ -z "$BURST" ]     && BURST=32
[ -z "$CLONE_SKB" ] && CLONE_SKB="100000"
[ -z "$COUNT" ]     && COUNT="0" # Zero means indefinitely

# Base Config
DELAY="0"  # Zero means max speed

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

    # Destination
    pg_set $dev "dst_mac $DST_MAC"
    pg_set $dev "dst$IP6 $DEST_IP"

    # Setup burst, for easy testing -b 0 disable bursting
    # (internally in pktgen default and minimum burst=1)
    if [[ ${BURST} -ne 0 ]]; then
	pg_set $dev "burst $BURST"
    else
	info "$dev: Not using burst"
    fi
done

# Run if user hits control-c
function control_c() {
    # Print results
    for ((thread = $F_THREAD; thread <= $L_THREAD; thread++)); do
	dev=${DEV}@${thread}
	echo "Device: $dev"
	cat /proc/net/pktgen/$dev | grep -A2 "Result:"
    done
}
# trap keyboard interrupt (Ctrl-C)
trap control_c SIGINT

echo "Running... ctrl^C to stop" >&2
pg_ctrl "start"
