#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2019 Joe Lawrence <joe.lawrence@redhat.com>

. $(dirname $0)/functions.sh

MOD_LIVEPATCH=test_klp_livepatch

setup_config


# - turn ftrace_enabled OFF and verify livepatches can't load
# - turn ftrace_enabled ON and verify livepatch can load
# - verify that ftrace_enabled can't be turned OFF while a livepatch is loaded

start_test "livepatch interaction with ftrace_enabled sysctl"

set_ftrace_enabled 0
load_failing_mod $MOD_LIVEPATCH

set_ftrace_enabled 1
load_lp $MOD_LIVEPATCH
if [[ "$(cat /proc/cmdline)" != "$MOD_LIVEPATCH: this has been live patched" ]] ; then
	echo -e "FAIL\n\n"
	die "livepatch kselftest(s) failed"
fi

# Check that ftrace could not get disabled when a livepatch is enabled
set_ftrace_enabled --fail 0
if [[ "$(cat /proc/cmdline)" != "$MOD_LIVEPATCH: this has been live patched" ]] ; then
	echo -e "FAIL\n\n"
	die "livepatch kselftest(s) failed"
fi
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH

check_result "livepatch: kernel.ftrace_enabled = 0
% insmod test_modules/$MOD_LIVEPATCH.ko
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: failed to register ftrace handler for function 'cmdline_proc_show' (-16)
livepatch: failed to patch object 'vmlinux'
livepatch: failed to enable patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': canceling patching transition, going to unpatch
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
insmod: ERROR: could not insert module test_modules/$MOD_LIVEPATCH.ko: Device or resource busy
livepatch: kernel.ftrace_enabled = 1
% insmod test_modules/$MOD_LIVEPATCH.ko
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
livepatch: '$MOD_LIVEPATCH': patching complete
livepatch: sysctl: setting key \"kernel.ftrace_enabled\": Device or resource busy
% echo 0 > $SYSFS_KLP_DIR/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"


# - verify livepatch can load
# - check if traces have a patched function
# - reset trace and unload livepatch

start_test "trace livepatched function and check that the live patch remains in effect"

FUNCTION_NAME="livepatch_cmdline_proc_show"

load_lp $MOD_LIVEPATCH
trace_function "$FUNCTION_NAME"

if [[ "$(cat /proc/cmdline)" == "$MOD_LIVEPATCH: this has been live patched" ]] ; then
	log "livepatch: ok"
fi

check_traced_functions "$FUNCTION_NAME"

disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH

check_result "% insmod test_modules/$MOD_LIVEPATCH.ko
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
livepatch: '$MOD_LIVEPATCH': patching complete
livepatch: ok
% echo 0 > $SYSFS_KLP_DIR/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"

exit 0
