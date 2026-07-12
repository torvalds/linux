#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Exercise the dax/kmem "state" sysfs attribute:
#   /sys/bus/dax/devices/daxX.Y/state  ->  unplugged | online | online_kernel | online_movable
#
# The test needs a dax device already bound to the kmem driver.
#
# This test mutates a device's memory: online/offline cycles migrate any
# in-use pages, and the optional unbind subtest wedges the device until
# reboot. The tester must identify the target device and opt into the
# destructive unbind tests.
#
#   DAX_KMEM_TEST_DEV=daxX.Y   test this specific device
#   DAX_KMEM_TEST_DEV=auto     auto-discover the first kmem-bound dax device
#                              (best-effort: it may be a device in use!)
#   DAX_KMEM_TEST_UNBIND=1     also run the destructive unbind-while-online test
#
# If DAX_KMEM_TEST_DEV is unset the whole test SKIPs.
#
# A dax device can be provisioned with the memmap= boot param, e.g.:
#   memmap=2G!4G
#
# then, in the booted system:
#
#   ndctl create-namespace -m devdax -e namespace0.0 -f
#   daxctl reconfigure-device -N -m system-ram dax0.0   # bind kmem
#   DAX_KMEM_TEST_DEV=auto ./dax-kmem-hotplug.sh

# shellcheck disable=SC1091
DIR="$(dirname "$(readlink -f "$0")")"
. "$DIR"/../kselftest/ktap_helpers.sh

DAX_BASE=/sys/bus/dax/devices
MEM_BASE=/sys/devices/system/memory

memtotal_kb() { awk '/^MemTotal:/ {print $2}' /proc/meminfo; }
get_state() { cat "$HP" 2>/dev/null; }
# set_state STATE -- write a state to the state attribute; returns the
# write's exit status (0 = accepted by the kernel)
set_state() { echo "$1" > "$HP" 2>/dev/null; }

is_kmem_dax() {
	local drv
	[ -e "$DAX_BASE/$1/state" ] || return 1
	drv=$(readlink "$DAX_BASE/$1/driver" 2>/dev/null)
	[ "$(basename "${drv:-}")" = kmem ]
}

find_kmem_dax() {
	local d
	for d in "$DAX_BASE"/dax*; do
		is_kmem_dax "$(basename "$d")" || continue
		basename "$d"
		return 0
	done
	return 1
}

# find_device_blocks -- print every memoryN block backing this dax device.
# The blocks are derived from the device's own range(s) in /proc/iomem (the
# reserved resource is named after the device), so we act on *its* blocks
# rather than guessing by NUMA node - the target node may also hold unrelated
# (and non-offlineable) memory.
find_device_blocks() {
	local bs
	bs=$(cat "$MEM_BASE/block_size_bytes" 2>/dev/null)	# hex, no leading 0x
	[ -n "$bs" ] || return 1
	grep -E " : ${DAX}\$" /proc/iomem | while read -r line; do
		local range s e i
		range=${line%% :*}; range=${range// /}
		s=${range%-*}; e=${range#*-}
		for (( i = 0x$s / 0x$bs; i <= 0x$e / 0x$bs; i++ )); do
			echo "memory$i"
		done
	done
}

# find_device_block -- print the first online block backing this dax device.
find_device_block() {
	local b
	for b in $(find_device_blocks); do
		[ -f "$MEM_BASE/$b/state" ] || continue
		[ "$(cat "$MEM_BASE/$b/state")" = online ] || continue
		echo "$b"
		return 0
	done
	return 1
}

ktap_print_header

if [ "$UID" != 0 ]; then
	ktap_skip_all "must be run as root"
	exit "$KSFT_SKIP"
fi

# Device selection is opt-in - see the header for why.
DEV_SEL=${DAX_KMEM_TEST_DEV:-}
if [ -z "$DEV_SEL" ]; then
	ktap_skip_all "set DAX_KMEM_TEST_DEV=<daxX.Y|auto> to opt in (mutates device memory)"
	exit "$KSFT_SKIP"
fi
if [ "$DEV_SEL" = auto ]; then
	DAX=$(find_kmem_dax)
else
	DAX=$DEV_SEL
fi
if [ -z "$DAX" ] || ! is_kmem_dax "$DAX"; then
	ktap_skip_all "no kmem-bound dax device with a state attribute (${DEV_SEL})"
	exit "$KSFT_SKIP"
fi
HP=$DAX_BASE/$DAX/state
ORIG=$(get_state)

# A failure to reach the baseline is environmental (memory in use), not an
# interface failure, so skip rather than fail.
set_state unplugged; rc=$?
if [ "$rc" != 0 ] || [ "$(get_state)" != unplugged ]; then
	ktap_skip_all "$DAX: cannot reach 'unplugged' baseline (memory in use?)"
	[ -n "$ORIG" ] && set_state "$ORIG"
	exit "$KSFT_SKIP"
fi
mt_unplugged=$(memtotal_kb)

DRV=/sys/bus/dax/drivers/kmem
AOB=$MEM_BASE/auto_online_blocks

ktap_print_msg "using $DAX (initial state was: $ORIG)"
ktap_set_plan 10

# A public (N_MEMORY) kmem node onlined into a kernel zone (online/online_kernel)
# collects unmovable allocations and can then never be offlined, which would
# wedge the device for the rest of this test.  So this test only ever
# successfully onlines online_movable, the one mode that is reliably unpluggable.

set_state online_movable; rc=$?
mt_online=$(memtotal_kb)
if [ "$rc" = 0 ] && [ "$(get_state)" = online_movable ] && [ "$mt_online" -gt "$mt_unplugged" ]; then
	ktap_test_pass "online_movable: state=online_movable, MemTotal $mt_unplugged -> $mt_online kB"
else
	ktap_test_fail "online_movable: rc=$rc state=$(get_state) MemTotal $mt_unplugged -> $mt_online"
fi

set_state online_movable; rc=$?
if [ "$rc" = 0 ] && [ "$(get_state)" = online_movable ]; then
	ktap_test_pass "online_movable idempotent"
else
	ktap_test_fail "online_movable idempotent: rc=$rc state=$(get_state)"
fi

# A different online type is rejected without an intervening unplug.  The write
# is refused before any hotplug, so this never actually onlines a kernel zone.
set_state online_kernel; rc=$?
if [ "$rc" != 0 ] && [ "$(get_state)" = online_movable ]; then
	ktap_test_pass "reject online_kernel without intervening unplug (no kernel-zone online)"
else
	ktap_test_fail "online_movable->online_kernel not rejected: rc=$rc state=$(get_state)"
fi

set_state unplugged; rc=$?
mt=$(memtotal_kb)
if [ "$rc" = 0 ] && [ "$(get_state)" = unplugged ] && [ "$mt" -lt "$mt_online" ]; then
	ktap_test_pass "unplug from online_movable: MemTotal $mt_online -> $mt kB"
else
	ktap_test_fail "unplug from online_movable: rc=$rc state=$(get_state) MemTotal $mt_online -> $mt"
fi

before=$(get_state)
set_state bogus_state; rc=$?
if [ "$rc" != 0 ] && [ "$(get_state)" = "$before" ]; then
	ktap_test_pass "reject invalid state string"
else
	ktap_test_fail "invalid state not rejected: rc=$rc state=$(get_state)"
fi

# An online_movable -> unplug cycle must re-acquire the per-range resources on
# each online and release them on each unplug.  Assert every iteration grows
# MemTotal past the baseline and returns exactly to it; memory left online after
# unplug (off > baseline) is a partial-free failure.
set_state unplugged
cycle_ok=1; fail_i=0; on=0; off=0
for i in 1 2 3; do
	if ! set_state online_movable; then cycle_ok=0; fail_i=$i; break; fi
	on=$(memtotal_kb)
	if ! set_state unplugged; then cycle_ok=0; fail_i=$i; break; fi
	off=$(memtotal_kb)
	# online must grow past baseline, and unplug must return to it - a
	# partial free (memory left online) is a failure, not just off == on.
	if [ "$on" -le "$mt_unplugged" ] || [ "$off" -gt "$mt_unplugged" ]; then
		cycle_ok=0; fail_i=$i; break
	fi
done
if [ "$cycle_ok" = 1 ]; then
	ktap_test_pass "online_movable/unplug cycle re-acquires resources (3x: added and freed each time)"
else
	ktap_test_fail "online_movable/unplug cycle regressed at iteration $fail_i (on=$on off=$off baseline=$mt_unplugged)"
fi

# Desync: toggle a block through the legacy per-block memoryN/state interface
# behind the driver's back, then unplug the whole device via daxX.Y/state.
#
# The driver only updates daxX.Y/state on its own writes, so it still reports
# online_movable while a block underneath is already offline.
#
# Whole-device unplug must still succeed (within reason, an actor changing a
# device from online_movable to online_kernel can no longer guarantee unplug).
# At the very least, an already-offline block should not produce an error.
set_state unplugged
set_state online_movable
blk=$(find_device_block)
if [ -n "$blk" ] && echo offline > "$MEM_BASE/$blk/state" 2>/dev/null; then
	# daxX.Y/state is now stale (still online_movable); unplug the device.
	set_state unplugged; rc=$?
	mt=$(memtotal_kb)
	if [ "$rc" = 0 ] && [ "$(get_state)" = unplugged ] && [ "$mt" -le "$mt_unplugged" ]; then
		ktap_test_pass "unplug tolerates a block pre-offlined via memoryN/state ($blk)"
	else
		ktap_test_fail "desync unplug: rc=$rc state=$(get_state) MemTotal=$mt baseline=$mt_unplugged"
	fi
else
	set_state unplugged 2>/dev/null
	ktap_test_skip "could not locate a device block to offline for desync test"
fi

# change system default online policy while the device is unbound, and show
# the new system default policy is utilized across bindings.
set_state unplugged
if [ -w "$AOB" ] && [ -w "$DRV/unbind" ] && [ -w "$DRV/bind" ]; then
	orig_aob=$(cat "$AOB")
	echo "$DAX" > "$DRV/unbind" 2>/dev/null
	echo offline > "$AOB" 2>/dev/null
	echo "$DAX" > "$DRV/bind" 2>/dev/null
	sleep 1
	st=$(get_state)
	echo "$orig_aob" > "$AOB" 2>/dev/null		# restore system policy
	if [ "$st" = offline ]; then
		ktap_test_pass "online policy resolved at bind: auto_online_blocks=offline -> state=offline"
	else
		ktap_test_fail "bind-time policy not honored: state=$st (expected offline)"
	fi
	set_state unplugged 2>/dev/null
else
	ktap_test_skip "auto_online_blocks or driver bind/unbind not writable"
fi

# Blocks offlined out-of-band (via memoryN/state) leave daxX.Y/state stale
# (still online_movable) while every block is actually offline.  A driver unbind
# must still hot-remove the offline memory and free its resources rather than
# trust the stale state and leak until reboot.  Unbind uses remove_memory(),
# which never offlines, so removing already-offline blocks is non-destructive and
# the device rebinds cleanly afterwards.
if [ -w "$DRV/unbind" ] && [ -w "$DRV/bind" ]; then
	set_state unplugged
	set_state online_movable
	offl_ok=1
	for b in $(find_device_blocks); do
		[ -f "$MEM_BASE/$b/state" ] || continue
		[ "$(cat "$MEM_BASE/$b/state")" = online ] || continue
		echo offline > "$MEM_BASE/$b/state" 2>/dev/null || offl_ok=0
	done
	# daxX.Y/state is now stale (still online_movable) while all blocks are
	# offline; the unbind must hot-remove them anyway.
	if [ "$offl_ok" = 1 ] && [ "$(get_state)" = online_movable ]; then
		echo "$DAX" > "$DRV/unbind" 2>/dev/null
		mt_after=$(memtotal_kb)
		leaked=$(grep -cE " : ${DAX}\$" /proc/iomem)	# before rebind
		echo "$DAX" > "$DRV/bind" 2>/dev/null		# restore for later steps
		sleep 1
		if [ "$mt_after" -le "$mt_unplugged" ] && [ "$leaked" = 0 ]; then
			ktap_test_pass "unbind with stale online state hot-removes offlined blocks (no leak)"
		else
			ktap_test_fail "desync unbind leaked: MemTotal=$mt_after baseline=$mt_unplugged iomem_left=$leaked"
		fi
		set_state unplugged 2>/dev/null
	else
		ktap_test_skip "could not offline all device blocks for desync-unbind test"
	fi
else
	ktap_test_skip "driver bind/unbind not writable for desync-unbind test"
fi

[ -n "$ORIG" ] && set_state "$ORIG"

# DESTRUCTIVE and opt-in only (DAX_KMEM_TEST_UNBIND=1):
#
# unbinding the driver while memory is online causes the resources to leak - but
# the unbind should not deadlock.  Instead the driver leaks it with a warning.

# This leaves the memory online and the device unbound until reboot, so it runs
# last and only when explicitly requested.  online_movable only: this test
# never onlines a public node into a kernel zone.

if [ "${DAX_KMEM_TEST_UNBIND:-}" = 1 ] && [ -w "$DRV/unbind" ]; then
	set_state unplugged; set_state online_movable
fi
if [ "${DAX_KMEM_TEST_UNBIND:-}" = 1 ] && [ "$(get_state)" = online_movable ] &&
   [ -w "$DRV/unbind" ]; then
	mt_on=$(memtotal_kb)
	dmesg -C 2>/dev/null
	echo "$DAX" > "$DRV/unbind" 2>/dev/null
	mt_after=$(memtotal_kb)
	# The leaked "System RAM (kmem)" regions stay in the iomem tree; reading
	# their names dereferences res_name, which a buggy unbind already freed.
	# Walk /proc/iomem to provoke that use-after-free (caught by KASAN).
	cat /proc/iomem > /dev/null 2>&1
	splat=$(dmesg 2>/dev/null | grep -ciE "KASAN|BUG:|use-after-free|general protection|Oops|refcount_t")
	if [ "$splat" = 0 ] && [ "$mt_after" -ge "$mt_on" ]; then
		ktap_test_pass "unbind while online: memory left online, no UAF/oops (MemTotal $mt_on -> $mt_after kB)"
	else
		ktap_test_fail "unbind while online regressed: splat=$splat MemTotal $mt_on -> $mt_after kB"
	fi
else
	ktap_test_skip "destructive unbind-while-online test (set DAX_KMEM_TEST_UNBIND=1)"
fi

ktap_finished
