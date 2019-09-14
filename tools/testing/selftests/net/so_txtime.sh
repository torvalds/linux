#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Regression tests for the SO_TXTIME interface

# Run in network namespace
if [[ $# -eq 0 ]]; then
	./in_netns.sh $0 __subprocess
	exit $?
fi

set -e

tc qdisc add dev lo root fq
./so_txtime -4 -6 -c mono a,-1 a,-1
./so_txtime -4 -6 -c mono a,0 a,0
./so_txtime -4 -6 -c mono a,10 a,10
./so_txtime -4 -6 -c mono a,10,b,20 a,10,b,20
./so_txtime -4 -6 -c mono a,20,b,10 b,20,a,20

if tc qdisc replace dev lo root etf clockid CLOCK_TAI delta 200000; then
	! ./so_txtime -4 -6 -c tai a,-1 a,-1
	! ./so_txtime -4 -6 -c tai a,0 a,0
	./so_txtime -4 -6 -c tai a,10 a,10
	./so_txtime -4 -6 -c tai a,10,b,20 a,10,b,20
	./so_txtime -4 -6 -c tai a,20,b,10 b,10,a,20
else
	echo "tc ($(tc -V)) does not support qdisc etf. skipping"
fi

echo OK. All tests passed
