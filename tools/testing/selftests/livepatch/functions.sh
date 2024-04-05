#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

# Shell functions for the rest of the scripts.

MAX_RETRIES=600
RETRY_INTERVAL=".1"	# seconds
KLP_SYSFS_DIR="/sys/kernel/livepatch"

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

# Check if we can compile the modules before loading them
function has_kdir() {
	if [ -z "$KDIR" ]; then
		KDIR="/lib/modules/$(uname -r)/build"
	fi

	if [ ! -d "$KDIR" ]; then
		echo "skip all tests: KDIR ($KDIR) not available to compile modules."
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
			echo "livepatch: $err" | sed 's#/proc/sys/kernel/#kernel.#' > /dev/kmsg
			return
		fi

		skip "failed to set kernel.ftrace_enabled = $1"
	fi

	echo "livepatch: kernel.ftrace_enabled = $result" > /dev/kmsg
}

function cleanup() {
	pop_config
}

# setup_config - save the current config and set a script exit trap that
#		 restores the original config.  Setup the dynamic debug
#		 for verbose livepatching output and turn on
#		 the ftrace_enabled sysctl.
function setup_config() {
	is_root
	has_kdir
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

function is_livepatch_mod() {
	local mod="$1"

	if [[ ! -f "test_modules/$mod.ko" ]]; then
		die "Can't find \"test_modules/$mod.ko\", try \"make\""
	fi

	if [[ $(modinfo "test_modules/$mod.ko" | awk '/^livepatch:/{print $NF}') == "Y" ]]; then
		return 0
	fi

	return 1
}

function __load_mod() {
	local mod="$1"; shift

	local msg="% insmod test_modules/$mod.ko $*"
	log "${msg%% }"
	ret=$(insmod "test_modules/$mod.ko" "$@" 2>&1)
	if [[ "$ret" != "" ]]; then
		die "$ret"
	fi

	# Wait for module in sysfs ...
	loop_until '[[ -e "/sys/module/$mod" ]]' ||
		die "failed to load module $mod"
}


# load_mod(modname, params) - load a kernel module
#	modname - module name to load
#	params  - module parameters to pass to insmod
function load_mod() {
	local mod="$1"; shift

	is_livepatch_mod "$mod" &&
		die "use load_lp() to load the livepatch module $mod"

	__load_mod "$mod" "$@"
}

# load_lp_nowait(modname, params) - load a kernel module with a livepatch
#			but do not wait on until the transition finishes
#	modname - module name to load
#	params  - module parameters to pass to insmod
function load_lp_nowait() {
	local mod="$1"; shift

	is_livepatch_mod "$mod" ||
		die "module $mod is not a livepatch"

	__load_mod "$mod" "$@"

	# Wait for livepatch in sysfs ...
	loop_until '[[ -e "/sys/kernel/livepatch/$mod" ]]' ||
		die "failed to load module $mod (sysfs)"
}

# load_lp(modname, params) - load a kernel module with a livepatch
#	modname - module name to load
#	params  - module parameters to pass to insmod
function load_lp() {
	local mod="$1"; shift

	load_lp_nowait "$mod" "$@"

	# Wait until the transition finishes ...
	loop_until 'grep -q '^0$' /sys/kernel/livepatch/$mod/transition' ||
		die "failed to complete transition"
}

# load_failing_mod(modname, params) - load a kernel module, expect to fail
#	modname - module name to load
#	params  - module parameters to pass to insmod
function load_failing_mod() {
	local mod="$1"; shift

	local msg="% insmod test_modules/$mod.ko $*"
	log "${msg%% }"
	ret=$(insmod "test_modules/$mod.ko" "$@" 2>&1)
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

	# Dump something unique into the dmesg log, then stash the entry
	# in LAST_DMESG.  The check_result() function will use it to
	# find new kernel messages since the test started.
	local last_dmesg_msg="livepatch kselftest timestamp: $(date --rfc-3339=ns)"
	log "$last_dmesg_msg"
	loop_until 'dmesg | grep -q "$last_dmesg_msg"' ||
		die "buffer busy? can't find canary dmesg message: $last_dmesg_msg"
	LAST_DMESG=$(dmesg | grep "$last_dmesg_msg")

	echo -n "TEST: $test ... "
	log "===== TEST: $test ====="
}

# check_result() - verify dmesg output
#	TODO - better filter, out of order msgs, etc?
function check_result {
	local expect="$*"
	local result

	# Test results include any new dmesg entry since LAST_DMESG, then:
	# - include lines matching keywords
	# - exclude lines matching keywords
	# - filter out dmesg timestamp prefixes
	result=$(dmesg | awk -v last_dmesg="$LAST_DMESG" 'p; $0 == last_dmesg { p=1 }' | \
		 grep -e 'livepatch:' -e 'test_klp' | \
		 grep -v '\(tainting\|taints\) kernel' | \
		 sed 's/^\[[ 0-9.]*\] //')

	if [[ "$expect" == "$result" ]] ; then
		echo "ok"
	elif [[ "$result" == "" ]] ; then
		echo -e "not ok\n\nbuffer overrun? can't find canary dmesg entry: $LAST_DMESG\n"
		die "livepatch kselftest(s) failed"
	else
		echo -e "not ok\n\n$(diff -upr --label expected --label result <(echo "$expect") <(echo "$result"))\n"
		die "livepatch kselftest(s) failed"
	fi
}

# check_sysfs_rights(modname, rel_path, expected_rights) - check sysfs
# path permissions
#	modname - livepatch module creating the sysfs interface
#	rel_path - relative path of the sysfs interface
#	expected_rights - expected access rights
function check_sysfs_rights() {
	local mod="$1"; shift
	local rel_path="$1"; shift
	local expected_rights="$1"; shift

	local path="$KLP_SYSFS_DIR/$mod/$rel_path"
	local rights=$(/bin/stat --format '%A' "$path")
	if test "$rights" != "$expected_rights" ; then
		die "Unexpected access rights of $path: $expected_rights vs. $rights"
	fi
}

# check_sysfs_value(modname, rel_path, expected_value) - check sysfs value
#	modname - livepatch module creating the sysfs interface
#	rel_path - relative path of the sysfs interface
#	expected_value - expected value read from the file
function check_sysfs_value() {
	local mod="$1"; shift
	local rel_path="$1"; shift
	local expected_value="$1"; shift

	local path="$KLP_SYSFS_DIR/$mod/$rel_path"
	local value=`cat $path`
	if test "$value" != "$expected_value" ; then
		die "Unexpected value in $path: $expected_value vs. $value"
	fi
}
