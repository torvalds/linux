#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	test_set_remote
	test_change_mc_remote
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

fmt_remote()
{
	local addr=$1; shift

	if [[ $addr == 224.* ]]; then
		echo "group $addr"
	else
		echo "remote $addr"
	fi
}

change_remote()
{
	local remote=$1; shift

	ip link set dev vx type vxlan $(fmt_remote $remote) dev v1
}

check_membership()
{
	local check_vec=("$@")

	local memberships
	memberships=$(
	    netstat -n --groups |
		sed -n '/^v1\b/p' |
		grep -o '[^ ]*$'
	)
	check_err $? "Couldn't obtain group memberships"

	local item
	for item in "${check_vec[@]}"; do
		eval "local $item"
		echo "$memberships" | grep -q "\b$group\b"
		check_err_fail $fail $? "$group is_ex reported in IGMP query response"
	done
}

test_change_mc_remote()
{
	check_command netstat || return

	ip_link_add v1 up type veth peer name v2
	ip_link_set_up v2

	RET=0

	ip_link_add vx up type vxlan dstport 4789 \
		local 192.0.2.1 $(fmt_remote 224.1.1.1) dev v1 vni 1000

	check_membership "group=224.1.1.1 fail=0" \
			 "group=224.1.1.2 fail=1" \
			 "group=224.1.1.3 fail=1"

	log_test "MC group report after VXLAN creation"

	RET=0

	change_remote 224.1.1.2
	check_membership "group=224.1.1.1 fail=1" \
			 "group=224.1.1.2 fail=0" \
			 "group=224.1.1.3 fail=1"

	log_test "MC group report after changing VXLAN remote MC->MC"

	RET=0

	change_remote 192.0.2.2
	check_membership "group=224.1.1.1 fail=1" \
			 "group=224.1.1.2 fail=1" \
			 "group=224.1.1.3 fail=1"

	log_test "MC group report after changing VXLAN remote MC->UC"
}

trap defer_scopes_cleanup EXIT

tests_run

exit $EXIT_STATUS
