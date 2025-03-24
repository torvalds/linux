#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2024 SUSE
# Author: Michael Vetter <mvetter@suse.com>

. $(dirname $0)/functions.sh

MOD_LIVEPATCH=test_klp_livepatch
MOD_KPROBE=test_klp_kprobe

setup_config

# Kprobe a function and verify that we can't livepatch that same function
# when it uses a post_handler since only one IPMODIFY maybe be registered
# to any given function at a time.

start_test "livepatch interaction with kprobed function with post_handler"

echo 1 > "$SYSFS_KPROBES_DIR/enabled"

load_mod $MOD_KPROBE has_post_handler=true
load_failing_mod $MOD_LIVEPATCH
unload_mod $MOD_KPROBE

check_result "% insmod test_modules/test_klp_kprobe.ko has_post_handler=true
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
% rmmod test_klp_kprobe"

start_test "livepatch interaction with kprobed function without post_handler"

load_mod $MOD_KPROBE has_post_handler=false
load_lp $MOD_LIVEPATCH

unload_mod $MOD_KPROBE
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH

check_result "% insmod test_modules/test_klp_kprobe.ko has_post_handler=false
% insmod test_modules/$MOD_LIVEPATCH.ko
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
livepatch: '$MOD_LIVEPATCH': patching complete
% rmmod test_klp_kprobe
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"

exit 0
