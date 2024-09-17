#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Check FDB default-remote handling across "ip link set".

check_remotes()
{
	local what=$1; shift
	local N=$(bridge fdb sh dev vx | grep 00:00:00:00:00:00 | wc -l)

	echo -ne "expected two remotes after $what\t"
	if [[ $N != 2 ]]; then
		echo "[FAIL]"
		EXIT_STATUS=1
	else
		echo "[ OK ]"
	fi
}

ip link add name vx up type vxlan id 2000 dstport 4789
bridge fdb ap dev vx 00:00:00:00:00:00 dst 192.0.2.20 self permanent
bridge fdb ap dev vx 00:00:00:00:00:00 dst 192.0.2.30 self permanent
check_remotes "fdb append"

ip link set dev vx type vxlan remote 192.0.2.30
check_remotes "link set"

ip link del dev vx
exit $EXIT_STATUS
