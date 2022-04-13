#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

# Shell functions for the rest of the scripts.

MAX_RETRIES=600
RETRY_INTERVAL=".1"	# seconds

# Kselftest framework requirement - SKIP code is 4
ksft_skip=4

# log(msg) - write message to kernel log
#	msg - insightful words
function log() {
	echo "$1" > /dev/kmsg
}

# skip(msg) - testing can't proceed
#	msg - explanation
function skip() {
	log "SKIP: $1"
	echo "SKIP: $1" >&2
	exit $ksft_skip
}

# root test
function is_root() {
	uid=$(id -u)
	if [ $uid -ne 0 ]; then
		echo "skip all tests: must be run as root" >&2
		exit $ksft_skip
	fi
}

# die(msg) - game over, man
#	msg - dying words
function die() {
	log "ERROR: $1"
	echo "ERROR: $1" >&2
	exit 1
}

# save existing dmesg so we can detect new content
function save_dmesg() {
	SAVED_DMESG=$(mktemp --tmpdir -t klp-dmesg-XXXXXX)
	dmesg > "$SAVED_DMESG"
}

# cleanup temporary dmesg file from save_dmesg()
function cleanup_dmesg_file() {
	rm -f "$SAVED_DMESG"
}

function push_config() {
	DYNAMIC_DEBUG=$(grep '^kernel/livepatch' /sys/kernel/debug/dynamic_debug/control | \
			awk -F'[: ]' '{print "file " $1 " line " $2 " " $4}')
	FTRACE_ENABLED=$(sysctl --values kernel.ftrace_enabled)
}

function pop_config() {
	if [[ -n "$DYNAMIC_DEBUG" ]]; then
		echo -n "$DYNAMIC_DEBUG" > /sys/kernel/debug/dynamic_debug/control
	fi
	if [[ -n "$FTRACE_ENABLED" ]]; then
		sysctl kernel.ftrace_enabled="$FTRACE_ENABLED" &> /dev/null
	fi
}

function set_dynamic_debug() {
        cat <<-EOF > /sys/kernel/debug/dynamic_debug/control
		file kernel/livepatch/* +p
		func klp_try_switch_task -p
		EOF
}

function set_ftrace_enabled() {
	local can_fail=0
	if [[ "$1" == "--fail" ]] ; then
		can_fail=1
		shift
	fi

	local err=$(sysctl -q kernel.ftrace_enabled="$1" 2>&1)
	local result=$(sysctl --values kernel.ftrace_enabled)

	if [[ "$result" != "$1" ]] ; then
		if [[ $can_fail -eq 1 ]] ; then
			echo "livepatch: $err" > /dev/kmsg
			return
		fi

		skip "failed to set kernel.ftrace_enabled = $1"
	fi

	echo "livepatch: kernel.ftrace_enabled = $result" > /dev/kmsg
}

function cleanup() {
	pop_config
	cleanup_dmesg_file
}

# setup_config - save the current config and set a script exit trap that
#		 restores the original config.  Setup the dynamic debug
#		 for verbose livepatching output and turn on
#		 the ftrace_enabled sysctl.
function setup_config() {
	is_root
	push_config
	set_dynamic_debug
	set_ftrace_enabled 1
	trap cleanup EXIT INT TERM HUP
}

# loop_until(cmd) - loop a command until it is successful or $MAX_RETRIES,
#		    sleep $RETRY_INTERVAL between attempts
#	cmd - command and its arguments to run
function loop_until() {
	local cmd="$*"
	local i=0
	while true; do
		eval "$cmd" && return 0
		[[ $((i++)) -eq $MAX_RETRIES ]] && return 1
		sleep $RETRY_INTERVAL
	done
}

function assert_mod() {
	local mod="$1"

	modprobe --dry-run "$mod" &>/dev/null
}

function is_livepatch_mod() {
	local mod="$1"

	if [[ $(modinfo "$mod" | awk '/^livepatch:/{print $NF}') == "Y" ]]; then
		return 0
	fi

	return 1
}

function __load_mod() {
	local mod="$1"; shift

	local msg="% modprobe $mod $*"
	log "${msg%% }"
	ret=$(modprobe "$mod" "$@" 2>&1)
	if [[ "$ret" != "" ]]; then
		die "$ret"
	fi

	# Wait for module in sysfs ...
	loop_until '[[ -e "/sys/module/$mod" ]]' ||
		die "failed to load module $mod"
}


# load_mod(modname, params) - load a kernel module
#	modname - module name to load
#	params  - module parameters to pass to modprobe
function load_mod() {
	local mod="$1"; shift

	assert_mod "$mod" ||
		skip "unable to load module ${mod}, verify CONFIG_TEST_LIVEPATCH=m and run self-tests as root"

	is_livepatch_mod "$mod" &&
		die "use load_lp() to load the livepatch module $mod"

	__load_mod "$mod" "$@"
}

# load_lp_nowait(modname, params) - load a kernel module with a livepatch
#			but do not wait on until the transition finishes
#	modname - module name to load
#	params  - module parameters to pass to modprobe
function load_lp_nowait() {
	local mod="$1"; shift

	assert_mod "$mod" ||
		skip "unable to load module ${mod}, verify CONFIG_TEST_LIVEPATCH=m and run self-tests as root"

	is_livepatch_mod "$mod" ||
		die "module $mod is not a livepatch"

	__load_mod "$mod" "$@"

	# Wait for livepatch in sysfs ...
	loop_until '[[ -e "/sys/kernel/livepatch/$mod" ]]' ||
		die "failed to load module $mod (sysfs)"
}

# load_lp(modname, params) - load a kernel module with a livepatch
#	modname - module name to load
#	params  - module parameters to pass to modprobe
function load_lp() {
	local mod="$1"; shift

	load_lp_nowait "$mod" "$@"

	# Wait until the transition finishes ...
	loop_until 'grep -q '^0$' /sys/kernel/livepatch/$mod/transition' ||
		die "failed to complete transition"
}

# load_failing_mod(modname, params) - load a kernel module, expect to fail
#	modname - module name to load
#	params  - module parameters to pass to modprobe
function load_failing_mod() {
	local mod="$1"; shift

	local msg="% modprobe $mod $*"
	log "${msg%% }"
	ret=$(modprobe "$mod" "$@" 2>&1)
	if [[ "$ret" == "" ]]; then
		die "$mod unexpectedly loaded"
	fi
	log "$ret"
}

# unload_mod(modname) - unload a kernel module
#	modname - module name to unload
function unload_mod() {
	local mod="$1"

	# Wait for module reference count to clear ...
	loop_until '[[ $(cat "/sys/module/$mod/refcnt") == "0" ]]' ||
		die "failed to unload module $mod (refcnt)"

	log "% rmmod $mod"
	ret=$(rmmod "$mod" 2>&1)
	if [[ "$ret" != "" ]]; then
		die "$ret"
	fi

	# Wait for module in sysfs ...
	loop_until '[[ ! -e "/sys/module/$mod" ]]' ||
		die "failed to unload module $mod (/sys/module)"
}

# unload_lp(modname) - unload a kernel module with a livepatch
#	modname - module name to unload
function unload_lp() {
	unload_mod "$1"
}

# disable_lp(modname) - disable a livepatch
#	modname - module name to unload
function disable_lp() {
	local mod="$1"

	log "% echo 0 > /sys/kernel/livepatch/$mod/enabled"
	echo 0 > /sys/kernel/livepatch/"$mod"/enabled

	# Wait until the transition finishes and the livepatch gets
	# removed from sysfs...
	loop_until '[[ ! -e "/sys/kernel/livepatch/$mod" ]]' ||
		die "failed to disable livepatch $mod"
}

# set_pre_patch_ret(modname, pre_patch_ret)
#	modname - module name to set
#	pre_patch_ret - new pre_patch_ret value
function set_pre_patch_ret {
	local mod="$1"; shift
	local ret="$1"

	log "% echo $ret > /sys/module/$mod/parameters/pre_patch_ret"
	echo "$ret" > /sys/module/"$mod"/parameters/pre_patch_ret

	# Wait for sysfs value to hold ...
	loop_until '[[ $(cat "/sys/module/$mod/parameters/pre_patch_ret") == "$ret" ]]' ||
		die "failed to set pre_patch_ret parameter for $mod module"
}

function start_test {
	local test="$1"

	save_dmesg
	echo -n "TEST: $test ... "
	log "===== TEST: $test ====="
}

# check_result() - verify dmesg output
#	TODO - better filter, out of order msgs, etc?
function check_result {
	local expect="$*"
	local result

	# Note: when comparing dmesg output, the kernel log timestamps
	# help differentiate repeated testing runs.  Remove them with a
	# post-comparison sed filter.

	result=$(dmesg | comm --nocheck-order -13 "$SAVED_DMESG" - | \
		 grep -e 'livepatch:' -e 'test_klp' | \
		 grep -v '\(tainting\|taints\) kernel' | \
		 sed 's/^\[[ 0-9.]*\] //')

	if [[ "$expect" == "$result" ]] ; then
		echo "ok"
	else
		echo -e "not ok\n\n$(diff -upr --label expected --label result <(echo "$expect") <(echo "$result"))\n"
		die "livepatch kselftest(s) failed"
	fi

	cleanup_dmesg_file
}
