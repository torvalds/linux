#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Exercise RDMA device name handling across network namespaces.

source "$(dirname "$0")/../kselftest/ktap_helpers.sh"

NAME_PREFIX="rxe_netns_names_$$"
NETDEV_PREFIX="rxn$$"
NS1="${NAME_PREFIX}ns1"
NS2="${NAME_PREFIX}ns2"
RXE_A="${NAME_PREFIX}rxe_a"
RXE_B="${NAME_PREFIX}rxe_b"
RXE_SAME="${NAME_PREFIX}rxe_same"
RXE_NEW="${NAME_PREFIX}rxe_new"
DUMMY_A="${NETDEV_PREFIX}a"
DUMMY_B="${NETDEV_PREFIX}b"
OLD_MODE=""
MODE_CHANGED=0
MODS=("dummy" "rdma_rxe")
TEST_SAME_NAMES="same RDMA device name can exist in two net namespaces"
TEST_MOVE_CONFLICT="move without rename fails on destination name conflict"
TEST_MOVE_RENAME="move then rename succeeds"
TEST_COMBINED_MOVE_RENAME="move with requested destination name succeeds"
TEST_SAME_NETNS_DUP_RENAME="same-netns rename rejects duplicate name"
TEST_TEARDOWN_RETURN="netns delete returns device to init_net and renames on conflict"

ksft_skip()
{
	ktap_skip_all "$*"
	exit "$KSFT_SKIP"
}

fail()
{
	ktap_exit_fail_msg "$*"
}

need_cmd()
{
	command -v "$1" >/dev/null 2>&1 || ksft_skip "missing command: $1"
}

rdma_ns()
{
	local ns=$1

	shift
	ip netns exec "$ns" rdma "$@"
}

rdma_dev_exists()
{
	local ns=$1
	local dev=$2

	if [ -n "$ns" ]; then
		rdma_ns "$ns" dev show "$dev" >/dev/null 2>&1
	else
		rdma dev show "$dev" >/dev/null 2>&1
	fi
}

add_dummy()
{
	local netdev=$1

	ip link add "$netdev" type dummy || return 1
	ip link set "$netdev" up || return 1
}

add_rxe()
{
	local dev=$1
	local netdev=$2

	rdma link add "$dev" type rxe netdev "$netdev"
}

rdma_dev_on_netdev()
{
	local netdev=$1

	rdma link show 2>/dev/null | awk -v want="$netdev" '
		{
			for (i = 1; i < NF; i++)
				if ($i == "netdev" && $(i + 1) == want) {
					dev = $2
					sub(/\/.*/, "", dev)
					print dev
					exit
				}
		}'
}

wait_rdma_dev_on_netdev()
{
	local netdev=$1
	local dev
	local i

	for i in $(seq 1 50); do
		dev=$(rdma_dev_on_netdev "$netdev")
		if [ -n "$dev" ]; then
			echo "$dev"
			return 0
		fi
		sleep 0.1
	done

	return 1
}

# ip link del returns after NETDEV_UNREGISTER, but rxe tears the RDMA device
# down asynchronously via ib_unregister_device_queued(). Wait until our names
# are gone.
wait_rdma_devs_gone()
{
	local i name ns
	local names=("$RXE_A" "$RXE_B" "$RXE_SAME" "$RXE_NEW")

	for i in $(seq 1 50); do
		local found=0

		for name in "${names[@]}"; do
			if rdma_dev_exists "" "$name"; then
				found=1
				break
			fi
			for ns in "$NS1" "$NS2"; do
				ip netns exec "$ns" true 2>/dev/null || continue
				if rdma_dev_exists "$ns" "$name"; then
					found=1
					break 2
				fi
			done
		done

		[ "$found" -eq 0 ] && return 0
		sleep 0.1
	done

	return 1
}

setup_devs()
{
	cleanup_devs || return 1

	add_dummy "$DUMMY_A" || return 1
	add_dummy "$DUMMY_B" || return 1

	add_rxe "$RXE_A" "$DUMMY_A" || return 1
	add_rxe "$RXE_B" "$DUMMY_B" || return 1
}

cleanup_devs()
{
	ip link del "$DUMMY_A" 2>/dev/null
	ip link del "$DUMMY_B" 2>/dev/null
	wait_rdma_devs_gone
}

setup()
{
	OLD_MODE=$(rdma system show 2>/dev/null |
		   sed -n 's/.*netns \([^ ]*\).*/\1/p')
	[ -n "$OLD_MODE" ] || ksft_skip "failed to read RDMA netns mode"

	rdma system set netns exclusive >/dev/null 2>&1 ||
		ksft_skip "rdma netns exclusive mode is not supported"
	MODE_CHANGED=1

	ip netns add "$NS1" || return 1
	ip netns add "$NS2" || return 1
}

# ip netns del returns before rdma_dev_exit_net() removes the net from
# rdma_nets. rdma_compatdev_set() returns -EBUSY until that completes, so
# retry the mode restore instead of leaving the system in exclusive mode.
restore_netns_mode()
{
	local i

	[ "$MODE_CHANGED" -eq 1 ] || return 0

	for i in $(seq 1 50); do
		if rdma system set netns "$OLD_MODE" >/dev/null 2>&1; then
			MODE_CHANGED=0
			return 0
		fi
		sleep 0.1
	done

	echo "warning: failed to restore RDMA netns mode to $OLD_MODE" >&2
	return 1
}

cleanup()
{
	cleanup_devs

	ip netns del "$NS1" 2>/dev/null
	ip netns del "$NS2" 2>/dev/null

	restore_netns_mode

	for m in "${MODS[@]}"; do
		modprobe -r "$m" 2>/dev/null
	done
}

rdma_supports_combined_move_rename()
{
	rdma dev help 2>&1 | grep -Eq 'netns .*name|name .*netns'
}

[ "$(id -u)" -eq 0 ] || ksft_skip "must be run as root"
need_cmd ip
need_cmd rdma
need_cmd modprobe

trap cleanup EXIT

for m in "${MODS[@]}"; do
	modinfo "$m" >/dev/null 2>&1 || ksft_skip "module $m not found"
	modprobe "$m" || fail "failed to load $m"
done

setup || fail "failed to create net namespaces"

ktap_print_header
ktap_set_plan 6

if setup_devs &&
   rdma dev set "$RXE_A" netns "$NS1" &&
   rdma_ns "$NS1" dev set "$RXE_A" name "$RXE_SAME" &&
   rdma dev set "$RXE_B" netns "$NS2" &&
   rdma_ns "$NS2" dev set "$RXE_B" name "$RXE_SAME" &&
   rdma_dev_exists "$NS1" "$RXE_SAME" &&
   rdma_dev_exists "$NS2" "$RXE_SAME"; then
	ktap_test_pass "$TEST_SAME_NAMES"
else
	ktap_test_fail "$TEST_SAME_NAMES"
fi
cleanup_devs

if ! setup_devs ||
   ! rdma dev set "$RXE_A" netns "$NS1" ||
   ! rdma_ns "$NS1" dev set "$RXE_A" name "$RXE_SAME" ||
   ! rdma dev set "$RXE_B" netns "$NS2" ||
   ! rdma_ns "$NS2" dev set "$RXE_B" name "$RXE_SAME"; then
	ktap_test_fail "$TEST_MOVE_CONFLICT"
elif rdma_ns "$NS1" dev set "$RXE_SAME" netns "$NS2" >/dev/null 2>&1; then
	ktap_test_fail "$TEST_MOVE_CONFLICT"
elif rdma_dev_exists "$NS1" "$RXE_SAME" &&
     rdma_dev_exists "$NS2" "$RXE_SAME"; then
	ktap_test_pass "$TEST_MOVE_CONFLICT"
else
	ktap_test_fail "$TEST_MOVE_CONFLICT"
fi
cleanup_devs

if ! setup_devs; then
	ktap_test_fail "$TEST_MOVE_RENAME"
elif rdma dev set "$RXE_A" netns "$NS2" &&
     rdma_ns "$NS2" dev set "$RXE_A" name "$RXE_NEW"; then
	if rdma_dev_exists "$NS2" "$RXE_NEW" &&
	   ! rdma_dev_exists "" "$RXE_A"; then
		ktap_test_pass "$TEST_MOVE_RENAME"
	else
		ktap_test_fail "$TEST_MOVE_RENAME"
	fi
else
	ktap_test_fail "$TEST_MOVE_RENAME"
fi
cleanup_devs

if ! rdma_supports_combined_move_rename; then
	ktap_test_skip "$TEST_COMBINED_MOVE_RENAME"
elif ! setup_devs; then
	ktap_test_fail "$TEST_COMBINED_MOVE_RENAME"
elif rdma dev set "$RXE_A" netns "$NS2" name "$RXE_NEW"; then
	if rdma_dev_exists "$NS2" "$RXE_NEW" &&
	   ! rdma_dev_exists "" "$RXE_A"; then
		ktap_test_pass "$TEST_COMBINED_MOVE_RENAME"
	else
		ktap_test_fail "$TEST_COMBINED_MOVE_RENAME"
	fi
else
	ktap_test_fail "$TEST_COMBINED_MOVE_RENAME"
fi
cleanup_devs

if ! setup_devs; then
	ktap_test_fail "$TEST_SAME_NETNS_DUP_RENAME"
elif rdma dev set "$RXE_A" name "$RXE_SAME" &&
     rdma dev set "$RXE_B" name "$RXE_NEW"; then
	if rdma dev set "$RXE_SAME" name "$RXE_NEW" >/dev/null 2>&1; then
		ktap_test_fail "$TEST_SAME_NETNS_DUP_RENAME"
	elif rdma_dev_exists "" "$RXE_SAME" &&
	     rdma_dev_exists "" "$RXE_NEW"; then
		ktap_test_pass "$TEST_SAME_NETNS_DUP_RENAME"
	else
		ktap_test_fail "$TEST_SAME_NETNS_DUP_RENAME"
	fi
else
	ktap_test_fail "$TEST_SAME_NETNS_DUP_RENAME"
fi
cleanup_devs

if ! setup_devs; then
	ktap_test_fail "$TEST_TEARDOWN_RETURN"
elif ! rdma dev set "$RXE_A" name "$RXE_SAME" ||
     ! rdma dev set "$RXE_B" netns "$NS2" ||
     ! rdma_ns "$NS2" dev set "$RXE_B" name "$RXE_SAME" ||
     ! rdma_dev_exists "$NS2" "$RXE_SAME"; then
	ktap_test_fail "$TEST_TEARDOWN_RETURN"
else
	ip netns del "$NS2"
	returned=$(wait_rdma_dev_on_netdev "$DUMMY_B")
	ktap_print_msg "device returned to init_net as '${returned:-<missing>}'"
	if rdma_dev_exists "" "$RXE_SAME" &&
	   [ -n "$returned" ] &&
	   [ "$returned" != "$RXE_SAME" ] &&
	   [ "${returned#ibdev}" != "$returned" ]; then
		ktap_test_pass "$TEST_TEARDOWN_RETURN"
	else
		ktap_test_fail "$TEST_TEARDOWN_RETURN"
	fi
fi
cleanup_devs

ktap_finished
