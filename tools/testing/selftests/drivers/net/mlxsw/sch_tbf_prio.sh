#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source qos_lib.sh
bail_on_lldpad

lib_dir=$(dirname $0)/../../../net/forwarding
TCFLAGS=skip_sw
source $lib_dir/sch_tbf_prio.sh
