#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

. $(dirname $0)/functions.sh

MOD_LIVEPATCH1=test_klp_livepatch
MOD_LIVEPATCH2=test_klp_syscall
MOD_LIVEPATCH3=test_klp_callbacks_demo
MOD_REPLACE=test_klp_atomic_replace

setup_config


# - load a livepatch that modifies the output from /proc/cmdline and
#   verify correct behavior
# - unload the livepatch and make sure the patch was removed

start_test "basic function patching"

load_lp $MOD_LIVEPATCH1

if [[ "$(cat /proc/cmdline)" != "$MOD_LIVEPATCH1: this has been live patched" ]] ; then
	echo -e "FAIL\n\n"
	die "livepatch kselftest(s) failed"
fi

disable_lp $MOD_LIVEPATCH1
unload_lp $MOD_LIVEPATCH1

if [[ "$(cat /proc/cmdline)" == "$MOD_LIVEPATCH1: this has been live patched" ]] ; then
	echo -e "FAIL\n\n"
	die "livepatch kselftest(s) failed"
fi

check_result "% insmod test_modules/$MOD_LIVEPATCH1.ko
livepatch: enabling patch '$MOD_LIVEPATCH1'
livepatch: '$MOD_LIVEPATCH1': initializing patching transition
livepatch: '$MOD_LIVEPATCH1': starting patching transition
livepatch: '$MOD_LIVEPATCH1': completing patching transition
livepatch: '$MOD_LIVEPATCH1': patching complete
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH1/enabled
livepatch: '$MOD_LIVEPATCH1': initializing unpatching transition
livepatch: '$MOD_LIVEPATCH1': starting unpatching transition
livepatch: '$MOD_LIVEPATCH1': completing unpatching transition
livepatch: '$MOD_LIVEPATCH1': unpatching complete
% rmmod $MOD_LIVEPATCH1"


# - load a livepatch that modifies the output from /proc/cmdline and
#   verify correct behavior
# - load another livepatch and verify that both livepatches are active
# - unload the second livepatch and verify that the first is still active
# - unload the first livepatch and verify none are active

start_test "multiple livepatches"

load_lp $MOD_LIVEPATCH1

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

load_lp $MOD_REPLACE replace=0

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

disable_lp $MOD_REPLACE
unload_lp $MOD_REPLACE

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

disable_lp $MOD_LIVEPATCH1
unload_lp $MOD_LIVEPATCH1

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

check_result "% insmod test_modules/$MOD_LIVEPATCH1.ko
livepatch: enabling patch '$MOD_LIVEPATCH1'
livepatch: '$MOD_LIVEPATCH1': initializing patching transition
livepatch: '$MOD_LIVEPATCH1': starting patching transition
livepatch: '$MOD_LIVEPATCH1': completing patching transition
livepatch: '$MOD_LIVEPATCH1': patching complete
$MOD_LIVEPATCH1: this has been live patched
% insmod test_modules/$MOD_REPLACE.ko replace=0
livepatch: enabling patch '$MOD_REPLACE'
livepatch: '$MOD_REPLACE': initializing patching transition
livepatch: '$MOD_REPLACE': starting patching transition
livepatch: '$MOD_REPLACE': completing patching transition
livepatch: '$MOD_REPLACE': patching complete
$MOD_LIVEPATCH1: this has been live patched
$MOD_REPLACE: this has been live patched
% echo 0 > /sys/kernel/livepatch/$MOD_REPLACE/enabled
livepatch: '$MOD_REPLACE': initializing unpatching transition
livepatch: '$MOD_REPLACE': starting unpatching transition
livepatch: '$MOD_REPLACE': completing unpatching transition
livepatch: '$MOD_REPLACE': unpatching complete
% rmmod $MOD_REPLACE
$MOD_LIVEPATCH1: this has been live patched
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH1/enabled
livepatch: '$MOD_LIVEPATCH1': initializing unpatching transition
livepatch: '$MOD_LIVEPATCH1': starting unpatching transition
livepatch: '$MOD_LIVEPATCH1': completing unpatching transition
livepatch: '$MOD_LIVEPATCH1': unpatching complete
% rmmod $MOD_LIVEPATCH1"


# - load a livepatch that modifies the output from /proc/cmdline and
#   verify correct behavior
# - load two additional livepatches and check the number of livepatch modules
#   applied
# - load an atomic replace livepatch and check that the other three modules were
#   disabled
# - remove all livepatches besides the atomic replace one and verify that the
#   atomic replace livepatch is still active
# - remove the atomic replace livepatch and verify that none are active

start_test "atomic replace livepatch"

load_lp $MOD_LIVEPATCH1

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

for mod in $MOD_LIVEPATCH2 $MOD_LIVEPATCH3; do
	load_lp "$mod"
done

mods=(/sys/kernel/livepatch/*)
nmods=${#mods[@]}
if [ "$nmods" -ne 3 ]; then
	die "Expecting three modules listed, found $nmods"
fi

load_lp $MOD_REPLACE replace=1

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

loop_until 'mods=(/sys/kernel/livepatch/*); nmods=${#mods[@]}; [[ "$nmods" -eq 1 ]]' ||
        die "Expecting only one moduled listed, found $nmods"

# These modules were disabled by the atomic replace
for mod in $MOD_LIVEPATCH3 $MOD_LIVEPATCH2 $MOD_LIVEPATCH1; do
	unload_lp "$mod"
done

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

disable_lp $MOD_REPLACE
unload_lp $MOD_REPLACE

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

check_result "% insmod test_modules/$MOD_LIVEPATCH1.ko
livepatch: enabling patch '$MOD_LIVEPATCH1'
livepatch: '$MOD_LIVEPATCH1': initializing patching transition
livepatch: '$MOD_LIVEPATCH1': starting patching transition
livepatch: '$MOD_LIVEPATCH1': completing patching transition
livepatch: '$MOD_LIVEPATCH1': patching complete
$MOD_LIVEPATCH1: this has been live patched
% insmod test_modules/$MOD_LIVEPATCH2.ko
livepatch: enabling patch '$MOD_LIVEPATCH2'
livepatch: '$MOD_LIVEPATCH2': initializing patching transition
livepatch: '$MOD_LIVEPATCH2': starting patching transition
livepatch: '$MOD_LIVEPATCH2': completing patching transition
livepatch: '$MOD_LIVEPATCH2': patching complete
% insmod test_modules/$MOD_LIVEPATCH3.ko
livepatch: enabling patch '$MOD_LIVEPATCH3'
livepatch: '$MOD_LIVEPATCH3': initializing patching transition
$MOD_LIVEPATCH3: pre_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH3': starting patching transition
livepatch: '$MOD_LIVEPATCH3': completing patching transition
$MOD_LIVEPATCH3: post_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH3': patching complete
% insmod test_modules/$MOD_REPLACE.ko replace=1
livepatch: enabling patch '$MOD_REPLACE'
livepatch: '$MOD_REPLACE': initializing patching transition
livepatch: '$MOD_REPLACE': starting patching transition
livepatch: '$MOD_REPLACE': completing patching transition
livepatch: '$MOD_REPLACE': patching complete
$MOD_REPLACE: this has been live patched
% rmmod $MOD_LIVEPATCH3
% rmmod $MOD_LIVEPATCH2
% rmmod $MOD_LIVEPATCH1
$MOD_REPLACE: this has been live patched
% echo 0 > /sys/kernel/livepatch/$MOD_REPLACE/enabled
livepatch: '$MOD_REPLACE': initializing unpatching transition
livepatch: '$MOD_REPLACE': starting unpatching transition
livepatch: '$MOD_REPLACE': completing unpatching transition
livepatch: '$MOD_REPLACE': unpatching complete
% rmmod $MOD_REPLACE"


exit 0
