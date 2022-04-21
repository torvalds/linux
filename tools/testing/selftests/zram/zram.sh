#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
TCID="zram.sh"

. ./zram_lib.sh

run_zram () {
echo "--------------------"
echo "running zram tests"
echo "--------------------"
./zram01.sh
echo ""
./zram02.sh
}

check_prereqs

run_zram
