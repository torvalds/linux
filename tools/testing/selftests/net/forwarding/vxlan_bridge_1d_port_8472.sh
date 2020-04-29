#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# A wrapper to run VXLAN tests with an unusual port number.

VXPORT=8472
ALL_TESTS="
	ping_ipv4
"
source vxlan_bridge_1d.sh
