#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test bridge VLAN range grouping. VLANs are collapsed into a range entry in
# the dump if they have the same per-VLAN options. These tests verify that
# VLANs with different per-VLAN option values are not grouped together.

# shellcheck disable=SC1091,SC2034,SC2154,SC2317
source lib.sh

ALL_TESTS="
	vlan_range_neigh_suppress
	vlan_range_mcast_max_groups
	vlan_range_mcast_n_groups
	vlan_range_mcast_enabled
"

setup_prepare()
{
	setup_ns NS
	defer cleanup_all_ns

	ip -n "$NS" link add name br0 type bridge vlan_filtering 1 \
		vlan_default_pvid 0 mcast_snooping 1 mcast_vlan_snooping 1
	ip -n "$NS" link set dev br0 up

	ip -n "$NS" link add name dummy0 type dummy
	ip -n "$NS" link set dev dummy0 master br0
	ip -n "$NS" link set dev dummy0 up
}

vlan_range_neigh_suppress()
{
	RET=0

	# Add two new consecutive VLANs for range grouping test
	bridge -n "$NS" vlan add vid 10 dev dummy0
	defer bridge -n "$NS" vlan del vid 10 dev dummy0

	bridge -n "$NS" vlan add vid 11 dev dummy0
	defer bridge -n "$NS" vlan del vid 11 dev dummy0

	# Configure different neigh_suppress values and verify no range grouping
	bridge -n "$NS" vlan set vid 10 dev dummy0 neigh_suppress on
	check_err $? "Failed to set neigh_suppress for VLAN 10"

	bridge -n "$NS" vlan set vid 11 dev dummy0 neigh_suppress off
	check_err $? "Failed to set neigh_suppress for VLAN 11"

	# Verify VLANs are not shown as a range, but individual entries exist
	bridge -n "$NS" -d vlan show dev dummy0 | grep -q "10-11"
	check_fail $? "VLANs with different neigh_suppress incorrectly grouped"

	bridge -n "$NS" -d vlan show dev dummy0 | grep -Eq "^\S+\s+10$|^\s+10$"
	check_err $? "VLAN 10 individual entry not found"

	bridge -n "$NS" -d vlan show dev dummy0 | grep -Eq "^\S+\s+11$|^\s+11$"
	check_err $? "VLAN 11 individual entry not found"

	# Configure same neigh_suppress value and verify range grouping
	bridge -n "$NS" vlan set vid 11 dev dummy0 neigh_suppress on
	check_err $? "Failed to set neigh_suppress for VLAN 11"

	bridge -n "$NS" -d vlan show dev dummy0 | grep -q "10-11"
	check_err $? "VLANs with same neigh_suppress not grouped"

	log_test "VLAN range grouping with neigh_suppress"
}

vlan_range_mcast_max_groups()
{
	RET=0

	# Add two new consecutive VLANs for range grouping test
	bridge -n "$NS" vlan add vid 10 dev dummy0
	defer bridge -n "$NS" vlan del vid 10 dev dummy0

	bridge -n "$NS" vlan add vid 11 dev dummy0
	defer bridge -n "$NS" vlan del vid 11 dev dummy0

	# Configure different mcast_max_groups values and verify no range grouping
	bridge -n "$NS" vlan set vid 10 dev dummy0 mcast_max_groups 100
	check_err $? "Failed to set mcast_max_groups for VLAN 10"

	bridge -n "$NS" vlan set vid 11 dev dummy0 mcast_max_groups 200
	check_err $? "Failed to set mcast_max_groups for VLAN 11"

	# Verify VLANs are not shown as a range, but individual entries exist
	bridge -n "$NS" -d vlan show dev dummy0 | grep -q "10-11"
	check_fail $? "VLANs with different mcast_max_groups incorrectly grouped"

	bridge -n "$NS" -d vlan show dev dummy0 | grep -Eq "^\S+\s+10$|^\s+10$"
	check_err $? "VLAN 10 individual entry not found"

	bridge -n "$NS" -d vlan show dev dummy0 | grep -Eq "^\S+\s+11$|^\s+11$"
	check_err $? "VLAN 11 individual entry not found"

	# Configure same mcast_max_groups value and verify range grouping
	bridge -n "$NS" vlan set vid 11 dev dummy0 mcast_max_groups 100
	check_err $? "Failed to set mcast_max_groups for VLAN 11"

	bridge -n "$NS" -d vlan show dev dummy0 | grep -q "10-11"
	check_err $? "VLANs with same mcast_max_groups not grouped"

	log_test "VLAN range grouping with mcast_max_groups"
}

vlan_range_mcast_n_groups()
{
	RET=0

	# Add two new consecutive VLANs for range grouping test
	bridge -n "$NS" vlan add vid 10 dev dummy0
	defer bridge -n "$NS" vlan del vid 10 dev dummy0

	bridge -n "$NS" vlan add vid 11 dev dummy0
	defer bridge -n "$NS" vlan del vid 11 dev dummy0

	# Add different numbers of multicast groups to each VLAN
	bridge -n "$NS" mdb add dev br0 port dummy0 grp 239.1.1.1 vid 10
	check_err $? "Failed to add mdb entry to VLAN 10"
	defer bridge -n "$NS" mdb del dev br0 port dummy0 grp 239.1.1.1 vid 10

	bridge -n "$NS" mdb add dev br0 port dummy0 grp 239.1.1.2 vid 10
	check_err $? "Failed to add second mdb entry to VLAN 10"
	defer bridge -n "$NS" mdb del dev br0 port dummy0 grp 239.1.1.2 vid 10

	bridge -n "$NS" mdb add dev br0 port dummy0 grp 239.1.1.1 vid 11
	check_err $? "Failed to add mdb entry to VLAN 11"
	defer bridge -n "$NS" mdb del dev br0 port dummy0 grp 239.1.1.1 vid 11

	# Verify VLANs are not shown as a range due to different mcast_n_groups
	bridge -n "$NS" -d vlan show dev dummy0 | grep -q "10-11"
	check_fail $? "VLANs with different mcast_n_groups incorrectly grouped"

	bridge -n "$NS" -d vlan show dev dummy0 | grep -Eq "^\S+\s+10$|^\s+10$"
	check_err $? "VLAN 10 individual entry not found"

	bridge -n "$NS" -d vlan show dev dummy0 | grep -Eq "^\S+\s+11$|^\s+11$"
	check_err $? "VLAN 11 individual entry not found"

	# Add another group to VLAN 11 to match VLAN 10's count
	bridge -n "$NS" mdb add dev br0 port dummy0 grp 239.1.1.2 vid 11
	check_err $? "Failed to add second mdb entry to VLAN 11"
	defer bridge -n "$NS" mdb del dev br0 port dummy0 grp 239.1.1.2 vid 11

	bridge -n "$NS" -d vlan show dev dummy0 | grep -q "10-11"
	check_err $? "VLANs with same mcast_n_groups not grouped"

	log_test "VLAN range grouping with mcast_n_groups"
}

vlan_range_mcast_enabled()
{
	RET=0

	# Add two new consecutive VLANs for range grouping test
	bridge -n "$NS" vlan add vid 10 dev br0 self
	defer bridge -n "$NS" vlan del vid 10 dev br0 self

	bridge -n "$NS" vlan add vid 11 dev br0 self
	defer bridge -n "$NS" vlan del vid 11 dev br0 self

	bridge -n "$NS" vlan add vid 10 dev dummy0
	defer bridge -n "$NS" vlan del vid 10 dev dummy0

	bridge -n "$NS" vlan add vid 11 dev dummy0
	defer bridge -n "$NS" vlan del vid 11 dev dummy0

	# Configure different mcast_snooping for bridge VLANs
	# Port VLANs inherit BR_VLFLAG_MCAST_ENABLED from bridge VLANs
	bridge -n "$NS" vlan global set dev br0 vid 10 mcast_snooping 1
	bridge -n "$NS" vlan global set dev br0 vid 11 mcast_snooping 0

	# Verify port VLANs are not grouped due to different mcast_enabled
	bridge -n "$NS" -d vlan show dev dummy0 | grep -q "10-11"
	check_fail $? "VLANs with different mcast_enabled incorrectly grouped"

	bridge -n "$NS" -d vlan show dev dummy0 | grep -Eq "^\S+\s+10$|^\s+10$"
	check_err $? "VLAN 10 individual entry not found"

	bridge -n "$NS" -d vlan show dev dummy0 | grep -Eq "^\S+\s+11$|^\s+11$"
	check_err $? "VLAN 11 individual entry not found"

	# Configure same mcast_snooping and verify range grouping
	bridge -n "$NS" vlan global set dev br0 vid 11 mcast_snooping 1

	bridge -n "$NS" -d vlan show dev dummy0 | grep -q "10-11"
	check_err $? "VLANs with same mcast_enabled not grouped"

	log_test "VLAN range grouping with mcast_enabled"
}

# Verify the newest tested option is supported
if ! bridge vlan help 2>&1 | grep -q "neigh_suppress"; then
	echo "SKIP: iproute2 too old, missing per-VLAN neighbor suppression support"
	exit "$ksft_skip"
fi

trap defer_scopes_cleanup EXIT
setup_prepare
tests_run

exit "$EXIT_STATUS"
