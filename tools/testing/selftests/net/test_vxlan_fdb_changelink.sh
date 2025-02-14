#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	test_set_remote
"
source lib.sh

check_remotes()
{
	local what=$1; shift
	local N=$(bridge fdb sh dev vx | grep 00:00:00:00:00:00 | wc -l)

	((N == 2))
	check_err $? "expected 2 remotes after $what, got $N"
}

# Check FDB default-remote handling across "ip link set".
test_set_remote()
{
	RET=0

	ip_link_add vx up type vxlan id 2000 dstport 4789
	bridge fdb ap dev vx 00:00:00:00:00:00 dst 192.0.2.20 self permanent
	bridge fdb ap dev vx 00:00:00:00:00:00 dst 192.0.2.30 self permanent
	check_remotes "fdb append"

	ip link set dev vx type vxlan remote 192.0.2.30
	check_remotes "link set"

	log_test 'FDB default-remote handling across "ip link set"'
}

trap defer_scopes_cleanup EXIT

tests_run

exit $EXIT_STATUS
