#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Set standard production config values that relate to TCP behavior.

# Flush old cached data (fastopen cookies).
ip tcp_metrics flush all > /dev/null 2>&1

# TCP min, default, and max receive and send buffer sizes.
sysctl -q net.ipv4.tcp_rmem="4096 540000 $((15*1024*1024))"
sysctl -q net.ipv4.tcp_wmem="4096 $((256*1024)) 4194304"

# TCP timestamps.
sysctl -q net.ipv4.tcp_timestamps=1

# TCP SYN(ACK) retry thresholds
sysctl -q net.ipv4.tcp_syn_retries=5
sysctl -q net.ipv4.tcp_synack_retries=5

# TCP Forward RTO-Recovery, RFC 5682.
sysctl -q net.ipv4.tcp_frto=2

# TCP Selective Acknowledgements (SACK)
sysctl -q net.ipv4.tcp_sack=1

# TCP Duplicate Selective Acknowledgements (DSACK)
sysctl -q net.ipv4.tcp_dsack=1

# TCP FACK (Forward Acknowldgement)
sysctl -q net.ipv4.tcp_fack=0

# TCP reordering degree ("dupthresh" threshold for entering Fast Recovery).
sysctl -q net.ipv4.tcp_reordering=3

# TCP congestion control.
sysctl -q net.ipv4.tcp_congestion_control=cubic

# TCP slow start after idle.
sysctl -q net.ipv4.tcp_slow_start_after_idle=0

# TCP RACK and TLP.
sysctl -q net.ipv4.tcp_early_retrans=4 net.ipv4.tcp_recovery=1

# TCP method for deciding when to defer sending to accumulate big TSO packets.
sysctl -q net.ipv4.tcp_tso_win_divisor=3

# TCP Explicit Congestion Notification (ECN)
sysctl -q net.ipv4.tcp_ecn=0

sysctl -q net.ipv4.tcp_pacing_ss_ratio=200
sysctl -q net.ipv4.tcp_pacing_ca_ratio=120
sysctl -q net.ipv4.tcp_notsent_lowat=4294967295 > /dev/null 2>&1

sysctl -q net.ipv4.tcp_fastopen=0x70403
sysctl -q net.ipv4.tcp_fastopen_key=a1a1a1a1-b2b2b2b2-c3c3c3c3-d4d4d4d4

sysctl -q net.ipv4.tcp_syncookies=1

# Override the default qdisc on the tun device.
# Many tests fail with timing errors if the default
# is FQ and that paces their flows.
tc qdisc add dev tun0 root pfifo

