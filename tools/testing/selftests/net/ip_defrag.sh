#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Run a couple of IP defragmentation tests.

set +x
set -e

echo "ipv4 defrag"

run_v4() {
sysctl -w net.ipv4.ipfrag_high_thresh=9000000 &> /dev/null
sysctl -w net.ipv4.ipfrag_low_thresh=7000000 &> /dev/null
./ip_defrag -4
}
export -f run_v4

./in_netns.sh "run_v4"

echo "ipv4 defrag with overlaps"
run_v4o() {
sysctl -w net.ipv4.ipfrag_high_thresh=9000000 &> /dev/null
sysctl -w net.ipv4.ipfrag_low_thresh=7000000 &> /dev/null
./ip_defrag -4o
}
export -f run_v4o

./in_netns.sh "run_v4o"
