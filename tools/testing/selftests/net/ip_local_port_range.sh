#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

./in_netns.sh \
  sh -c 'sysctl -q -w net.ipv4.ip_local_port_range="40000 49999" && ./ip_local_port_range'
