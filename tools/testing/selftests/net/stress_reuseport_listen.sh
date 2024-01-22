#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022 Meta Platforms, Inc. and affiliates.

source lib.sh
NR_FILES=24100
SAVED_NR_FILES=$(ulimit -n)

setup() {
	setup_ns NS
	ip netns exec $NS sysctl -q -w net.ipv6.ip_nonlocal_bind=1
	ulimit -n $NR_FILES
}

cleanup() {
	cleanup_ns $NS
	ulimit -n $SAVED_NR_FILES
}

trap cleanup EXIT
setup
# 300 different vips listen on port 443
# Each vip:443 sockaddr has 80 LISTEN sock by using SO_REUSEPORT
# Total 24000 listening socks
ip netns exec $NS ./stress_reuseport_listen 300 80
