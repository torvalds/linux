#!/bin/sh

# Test various socket options that can be set by attaching programs to cgroups.

CGRP_MNT="/tmp/cgroupv2-test_cgrp2_sock"

################################################################################
#
print_result()
{
	local rc=$1
	local status=" OK "

	[ $rc -ne 0 ] && status="FAIL"

	printf "%-50s    [%4s]\n" "$2" "$status"
}

check_sock()
{
	out=$(test_cgrp2_sock)
	echo $out | grep -q "$1"
	if [ $? -ne 0 ]; then
		print_result 1 "IPv4: $2"
		echo "    expected: $1"
		echo "        have: $out"
		rc=1
	else
		print_result 0 "IPv4: $2"
	fi
}

check_sock6()
{
	out=$(test_cgrp2_sock -6)
	echo $out | grep -q "$1"
	if [ $? -ne 0 ]; then
		print_result 1 "IPv6: $2"
		echo "    expected: $1"
		echo "        have: $out"
		rc=1
	else
		print_result 0 "IPv6: $2"
	fi
}

################################################################################
#

cleanup()
{
	echo $$ >> ${CGRP_MNT}/cgroup.procs
	rmdir ${CGRP_MNT}/sockopts
}

cleanup_and_exit()
{
	local rc=$1
	local msg="$2"

	[ -n "$msg" ] && echo "ERROR: $msg"

	ip li del cgrp2_sock
	umount ${CGRP_MNT}

	exit $rc
}


################################################################################
# main

rc=0

ip li add cgrp2_sock type dummy 2>/dev/null

set -e
mkdir -p ${CGRP_MNT}
mount -t cgroup2 none ${CGRP_MNT}
set +e


# make sure we have a known start point
cleanup 2>/dev/null

mkdir -p ${CGRP_MNT}/sockopts
[ $? -ne 0 ] && cleanup_and_exit 1 "Failed to create cgroup hierarchy"


# set pid into cgroup
echo $$ > ${CGRP_MNT}/sockopts/cgroup.procs

# no bpf program attached, so socket should show no settings
check_sock "dev , mark 0, priority 0" "No programs attached"
check_sock6 "dev , mark 0, priority 0" "No programs attached"

# verify device is set
#
test_cgrp2_sock -b cgrp2_sock ${CGRP_MNT}/sockopts
if [ $? -ne 0 ]; then
	cleanup_and_exit 1 "Failed to install program to set device"
fi
check_sock "dev cgrp2_sock, mark 0, priority 0" "Device set"
check_sock6 "dev cgrp2_sock, mark 0, priority 0" "Device set"

# verify mark is set
#
test_cgrp2_sock -m 666 ${CGRP_MNT}/sockopts
if [ $? -ne 0 ]; then
	cleanup_and_exit 1 "Failed to install program to set mark"
fi
check_sock "dev , mark 666, priority 0" "Mark set"
check_sock6 "dev , mark 666, priority 0" "Mark set"

# verify priority is set
#
test_cgrp2_sock -p 123 ${CGRP_MNT}/sockopts
if [ $? -ne 0 ]; then
	cleanup_and_exit 1 "Failed to install program to set priority"
fi
check_sock "dev , mark 0, priority 123" "Priority set"
check_sock6 "dev , mark 0, priority 123" "Priority set"

# all 3 at once
#
test_cgrp2_sock -b cgrp2_sock -m 666 -p 123 ${CGRP_MNT}/sockopts
if [ $? -ne 0 ]; then
	cleanup_and_exit 1 "Failed to install program to set device, mark and priority"
fi
check_sock "dev cgrp2_sock, mark 666, priority 123" "Priority set"
check_sock6 "dev cgrp2_sock, mark 666, priority 123" "Priority set"

cleanup_and_exit $rc
