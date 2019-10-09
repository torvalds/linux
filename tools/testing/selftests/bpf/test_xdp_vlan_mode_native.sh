#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

# Exit on failure
set -e

# Wrapper script to test native-XDP
export TESTNAME=xdp_vlan_mode_native
./test_xdp_vlan.sh --mode=xdpdrv
