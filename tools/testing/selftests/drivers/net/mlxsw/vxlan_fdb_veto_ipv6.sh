#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# A wrapper to run VXLAN test for IPv6.

LOCAL_IP=2001:db8:1::1
REMOTE_IP_1=2001:db8:2::1
REMOTE_IP_2=2001:db8:3::1
UDPCSUM_FLAFS="udp6zerocsumrx udp6zerocsumtx"
MC_IP=FF02::2

source vxlan_fdb_veto.sh
