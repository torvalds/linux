#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# for debugging set net.netfilter.nf_log_all_netns=1 in init_net
# or do not use net namespaces.
modprobe -q nf_conntrack
sysctl -q net.netfilter.nf_conntrack_log_invalid=6

# Flush old cached data (fastopen cookies).
ip tcp_metrics flush all > /dev/null 2>&1

# TCP min, default, and max receive and send buffer sizes.
sysctl -q net.ipv4.tcp_rmem="4096 540000 $((15*1024*1024))"
sysctl -q net.ipv4.tcp_wmem="4096 $((256*1024)) 4194304"

# TCP congestion control.
sysctl -q net.ipv4.tcp_congestion_control=cubic

# TCP slow start after idle.
sysctl -q net.ipv4.tcp_slow_start_after_idle=0

# TCP Explicit Congestion Notification (ECN)
sysctl -q net.ipv4.tcp_ecn=0

sysctl -q net.ipv4.tcp_notsent_lowat=4294967295 > /dev/null 2>&1

# Override the default qdisc on the tun device.
# Many tests fail with timing errors if the default
# is FQ and that paces their flows.
tc qdisc add dev tun0 root pfifo

# Enable conntrack
$xtables -A INPUT -m conntrack --ctstate NEW -p tcp --syn
