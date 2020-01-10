#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2019 Joe Lawrence <joe.lawrence@redhat.com>

. $(dirname $0)/functions.sh

MOD_LIVEPATCH=test_klp_livepatch

setup_config


# TEST: livepatch interaction with ftrace_enabled sysctl
# - turn ftrace_enabled OFF and verify livepatches can't load
# - turn ftrace_enabled ON and verify livepatch can load
# - verify that ftrace_enabled can't be turned OFF while a livepatch is loaded

echo -n "TEST: livepatch interaction with ftrace_enabled sysctl ... "
dmesg -C

set_ftrace_enabled 0
load_failing_mod $MOD_LIVEPATCH

set_ftrace_enabled 1
load_lp $MOD_LIVEPATCH
if [[ "$(cat /proc/cmdline)" != "$MOD_LIVEPATCH: this has been live patched" ]] ; then
	echo -e "FAIL\n\n"
	die "livepatch kselftest(s) failed"
fi

set_ftrace_enabled 0
if [[ "$(cat /proc/cmdline)" != "$MOD_LIVEPATCH: this has been live patched" ]] ; then
	echo -e "FAIL\n\n"
	die "livepatch kselftest(s) failed"
fi
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH

check_result "livepatch: kernel.ftrace_enabled = 0
% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: failed to register ftrace handler for function 'cmdline_proc_show' (-16)
livepatch: failed to patch object 'vmlinux'
livepatch: failed to enable patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': canceling patching transition, going to unpatch
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
modprobe: ERROR: could not insert '$MOD_LIVEPATCH': Device or resource busy
livepatch: kernel.ftrace_enabled = 1
% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
livepatch: '$MOD_LIVEPATCH': patching complete
livepatch: sysctl: setting key \"kernel.ftrace_enabled\": Device or resource busy kernel.ftrace_enabled = 0
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"


exit 0
