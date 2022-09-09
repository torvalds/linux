#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

QDISC="prio bands"
: ${lib_dir:=.}
source $lib_dir/sch_tbf_etsprio.sh
