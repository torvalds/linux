#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

CPUS_ONLINE=$(lscpu --online -p=cpu|grep -v -e '#')
#use last CPU for host. Why not the first?
#many devices tend to use cpu0 by default so
#it tends to be busier
HOST_AFFINITY=$(echo "${CPUS_ONLINE}"|tail -n 1)

#run command on all cpus
for cpu in $CPUS_ONLINE
do
	#Don't run guest and host on same CPU
	#It actually works ok if using signalling
	if
		(echo "$@" | grep -e "--sleep" > /dev/null) || \
			test $HOST_AFFINITY '!=' $cpu
	then
		echo "GUEST AFFINITY $cpu"
		"$@" --host-affinity $HOST_AFFINITY --guest-affinity $cpu
	fi
done
echo "NO GUEST AFFINITY"
"$@" --host-affinity $HOST_AFFINITY
echo "NO AFFINITY"
"$@"
