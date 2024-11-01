#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Regression tests for IPv6 flowlabels
#
# run in separate namespaces to avoid mgmt db conflicts betweent tests

set -e

echo "TEST management"
./in_netns.sh ./ipv6_flowlabel_mgr

echo "TEST datapath"
./in_netns.sh \
  sh -c 'sysctl -q -w net.ipv6.auto_flowlabels=0 && ./ipv6_flowlabel -l 1'

echo "TEST datapath (with auto-flowlabels)"
./in_netns.sh \
  sh -c 'sysctl -q -w net.ipv6.auto_flowlabels=1 && ./ipv6_flowlabel -l 1'

echo "TEST datapath (with ping-sockets)"
./in_netns.sh \
  sh -c 'sysctl -q -w net.ipv6.flowlabel_reflect=4 && \
    sysctl -q -w net.ipv4.ping_group_range="0 2147483647" && \
    ./ipv6_flowlabel -l 1 -p'

echo "TEST datapath (with flowinfo-send)"
./in_netns.sh \
  sh -c './ipv6_flowlabel -l 1 -s'

echo "TEST datapath (with ping-sockets flowinfo-send)"
./in_netns.sh \
  sh -c 'sysctl -q -w net.ipv6.flowlabel_reflect=4 && \
    sysctl -q -w net.ipv4.ping_group_range="0 2147483647" && \
    ./ipv6_flowlabel -l 1 -p -s'

echo OK. All tests passed
