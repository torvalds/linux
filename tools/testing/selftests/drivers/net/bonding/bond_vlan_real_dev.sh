#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test propagation of a real device's state to the VLANs stacked on top of it
# when the real device is (or becomes) a bond member.
#
# The kernel mirrors a real device's UP/DOWN, MTU and feature changes onto its
# VLANs.  This is done asynchronously (netdev_work): doing it synchronously from
# the real device's notifier could deadlock.  If the real device is brought up
# while enslaved to a bond - so its instance lock is held across NETDEV_UP - and
# a VLAN on top of it is itself a bond member, the synchronous propagation
# re-entered the stack and tried to take the same instance lock again.
#
# Cover both halves:
#  - the deferred UP/DOWN, MTU and feature propagation actually lands on the
#    VLAN (link state and MTU use an ops-locked dummy, i.e. the deferral path),
#  - the deadlock-prone topology - a VLAN on a dummy, with the VLAN and the
#    dummy each enslaved to a different bond - can be built without hanging.

ALL_TESTS="
	vlan_link_state
	vlan_mtu
	vlan_features
	vlan_real_dev_enslave
"

REQUIRE_MZ=no
NUM_NETIFS=0
lib_dir=$(dirname "$0")
source "$lib_dir"/../../../net/forwarding/lib.sh

# Return 0 if $dev in netns $ns has flag $flag set (e.g. UP) in its <...> flags.
link_has_flag()
{
	local ns=$1 dev=$2 flag=$3

	ip -n "$ns" link show dev "$dev" 2>/dev/null | grep -q "[<,]${flag}[,>]"
}

link_lacks_flag()
{
	! link_has_flag "$@"
}

link_mtu_is()
{
	local ns=$1 dev=$2 want=$3 cur

	cur=$(ip -n "$ns" link show dev "$dev" 2>/dev/null | \
		sed -n 's/.* mtu \([0-9]\+\).*/\1/p')
	[ "$cur" = "$want" ]
}

vlan_feature_is()
{
	local ns=$1 dev=$2 feature=$3 value=$4

	ip netns exec "$ns" ethtool -k "$dev" 2>/dev/null | \
		grep -q "^$feature: $value"
}

link_has_master()
{
	local ns=$1 dev=$2 master=$3

	ip -n "$ns" -o link show dev "$dev" 2>/dev/null | grep -q "master $master"
}

vlan_link_state()
{
	RET=0

	ip -n "$NS" link add ls_dummy type dummy
	ip -n "$NS" link add link ls_dummy name ls_vlan type vlan id 100

	# Bringing the real device up must propagate UP to the VLAN.
	ip -n "$NS" link set ls_dummy up
	busywait "$BUSYWAIT_TIMEOUT" link_has_flag "$NS" ls_vlan UP
	check_err $? "VLAN did not go UP after the real device went UP"

	# ... and likewise for DOWN.
	ip -n "$NS" link set ls_dummy down
	busywait "$BUSYWAIT_TIMEOUT" link_lacks_flag "$NS" ls_vlan UP
	check_err $? "VLAN did not go DOWN after the real device went DOWN"

	ip -n "$NS" link del ls_vlan
	ip -n "$NS" link del ls_dummy

	log_test "VLAN link state follows the real device"
}

vlan_mtu()
{
	RET=0

	# The VLAN inherits the real device's MTU (2000) at creation time.
	ip -n "$NS" link add mtu_dummy mtu 2000 type dummy
	ip -n "$NS" link add link mtu_dummy name mtu_vlan type vlan id 100

	# Shrinking the real device's MTU must clamp the VLAN's MTU.
	ip -n "$NS" link set mtu_dummy mtu 1500
	busywait "$BUSYWAIT_TIMEOUT" link_mtu_is "$NS" mtu_vlan 1500
	check_err $? "VLAN MTU not clamped after the real device's MTU shrank"

	ip -n "$NS" link del mtu_vlan
	ip -n "$NS" link del mtu_dummy

	log_test "VLAN MTU clamped to the real device"
}

vlan_features()
{
	RET=0

	# Use veth as the real device: unlike dummy it exports vlan_features, so
	# the VLAN actually inherits a toggleable offload to assert on.
	ip -n "$NS" link add ft_veth type veth peer name ft_veth_pr
	ip -n "$NS" link add link ft_veth name ft_vlan type vlan id 100

	vlan_feature_is "$NS" ft_vlan scatter-gather on
	check_err $? "VLAN did not inherit scatter-gather from the real device"

	# Toggling the offload on the real device must propagate to the VLAN.
	ip netns exec "$NS" ethtool -K ft_veth sg off
	busywait "$BUSYWAIT_TIMEOUT" \
		vlan_feature_is "$NS" ft_vlan scatter-gather off
	check_err $? "VLAN scatter-gather still on after disabling it on real dev"

	ip netns exec "$NS" ethtool -K ft_veth sg on
	busywait "$BUSYWAIT_TIMEOUT" \
		vlan_feature_is "$NS" ft_vlan scatter-gather on
	check_err $? "VLAN scatter-gather still off after enabling it on real dev"

	ip -n "$NS" link del ft_vlan
	ip -n "$NS" link del ft_veth

	log_test "VLAN features follow the real device"
}

vlan_real_dev_enslave()
{
	RET=0

	# dummy <- VLAN -> bond0, then enslave the dummy itself to bond1.  The
	# last step brings the dummy up under bond1's instance lock, which used
	# to deadlock while synchronously propagating UP to the (bond-enslaved)
	# VLAN on top.
	ip -n "$NS" link add dl_dummy type dummy
	ip -n "$NS" link set dl_dummy up
	ip -n "$NS" link add link dl_dummy name dl_vlan type vlan id 100

	ip -n "$NS" link add dl_bond0 type bond mode active-backup
	ip -n "$NS" link set dl_vlan down
	ip -n "$NS" link set dl_vlan master dl_bond0
	check_err $? "could not enslave the VLAN to bond0"

	ip -n "$NS" link add dl_bond1 type bond mode active-backup
	ip -n "$NS" link set dl_dummy down
	ip -n "$NS" link set dl_dummy master dl_bond1
	check_err $? "could not enslave the real device to bond1"

	# If we got here the kernel did not deadlock; make sure it is still
	# responsive and the enslave really took effect.
	link_has_master "$NS" dl_dummy dl_bond1
	check_err $? "real device not enslaved to bond1"

	ip -n "$NS" link del dl_bond1
	ip -n "$NS" link del dl_bond0
	ip -n "$NS" link del dl_vlan
	ip -n "$NS" link del dl_dummy

	log_test "VLAN real device enslaved to a second bond"
}

setup_ns NS
trap 'cleanup_ns $NS' EXIT

tests_run

exit "$EXIT_STATUS"
