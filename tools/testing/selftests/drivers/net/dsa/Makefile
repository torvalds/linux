# SPDX-License-Identifier: GPL-2.0+ OR MIT

TEST_PROGS = bridge_locked_port.sh \
	bridge_mdb.sh \
	bridge_mld.sh \
	bridge_vlan_aware.sh \
	bridge_vlan_mcast.sh \
	bridge_vlan_unaware.sh \
	local_termination.sh \
	no_forwarding.sh \
	tc_actions.sh \
	test_bridge_fdb_stress.sh

TEST_FILES := \
	run_net_forwarding_test.sh \
	forwarding.config

TEST_INCLUDES := \
	../../../net/forwarding/bridge_locked_port.sh \
	../../../net/forwarding/bridge_mdb.sh \
	../../../net/forwarding/bridge_mld.sh \
	../../../net/forwarding/bridge_vlan_aware.sh \
	../../../net/forwarding/bridge_vlan_mcast.sh \
	../../../net/forwarding/bridge_vlan_unaware.sh \
	../../../net/forwarding/lib.sh \
	../../../net/forwarding/local_termination.sh \
	../../../net/forwarding/no_forwarding.sh \
	../../../net/forwarding/tc_actions.sh \
	../../../net/forwarding/tc_common.sh \
	../../../net/lib.sh

include ../../../lib.mk
