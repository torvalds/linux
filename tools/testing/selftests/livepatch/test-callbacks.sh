#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

. $(dirname $0)/functions.sh

MOD_LIVEPATCH=test_klp_callbacks_demo
MOD_LIVEPATCH2=test_klp_callbacks_demo2
MOD_TARGET=test_klp_callbacks_mod
MOD_TARGET_BUSY=test_klp_callbacks_busy

setup_config


# TEST: target module before livepatch
#
# Test a combination of loading a kernel module and a livepatch that
# patches a function in the first module.  Load the target module
# before the livepatch module.  Unload them in the same order.
#
# - On livepatch enable, before the livepatch transition starts,
#   pre-patch callbacks are executed for vmlinux and $MOD_TARGET (those
#   klp_objects currently loaded).  After klp_objects are patched
#   according to the klp_patch, their post-patch callbacks run and the
#   transition completes.
#
# - Similarly, on livepatch disable, pre-patch callbacks run before the
#   unpatching transition starts.  klp_objects are reverted, post-patch
#   callbacks execute and the transition completes.

echo -n "TEST: target module before livepatch ... "
dmesg -C

load_mod $MOD_TARGET
load_lp $MOD_LIVEPATCH
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH
unload_mod $MOD_TARGET

check_result "% modprobe $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_init
% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
$MOD_LIVEPATCH: pre_patch_callback: $MOD_TARGET -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
$MOD_LIVEPATCH: post_patch_callback: vmlinux
$MOD_LIVEPATCH: post_patch_callback: $MOD_TARGET -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': patching complete
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
$MOD_LIVEPATCH: pre_unpatch_callback: vmlinux
$MOD_LIVEPATCH: pre_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
$MOD_LIVEPATCH: post_unpatch_callback: vmlinux
$MOD_LIVEPATCH: post_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH
% rmmod $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_exit"


# TEST: module_coming notifier
#
# This test is similar to the previous test, but (un)load the livepatch
# module before the target kernel module.  This tests the livepatch
# core's module_coming handler.
#
# - On livepatch enable, only pre/post-patch callbacks are executed for
#   currently loaded klp_objects, in this case, vmlinux.
#
# - When a targeted module is subsequently loaded, only its
#   pre/post-patch callbacks are executed.
#
# - On livepatch disable, all currently loaded klp_objects' (vmlinux and
#   $MOD_TARGET) pre/post-unpatch callbacks are executed.

echo -n "TEST: module_coming notifier ... "
dmesg -C

load_lp $MOD_LIVEPATCH
load_mod $MOD_TARGET
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH
unload_mod $MOD_TARGET

check_result "% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
$MOD_LIVEPATCH: post_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': patching complete
% modprobe $MOD_TARGET
livepatch: applying patch '$MOD_LIVEPATCH' to loading module '$MOD_TARGET'
$MOD_LIVEPATCH: pre_patch_callback: $MOD_TARGET -> [MODULE_STATE_COMING] Full formed, running module_init
$MOD_LIVEPATCH: post_patch_callback: $MOD_TARGET -> [MODULE_STATE_COMING] Full formed, running module_init
$MOD_TARGET: ${MOD_TARGET}_init
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
$MOD_LIVEPATCH: pre_unpatch_callback: vmlinux
$MOD_LIVEPATCH: pre_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
$MOD_LIVEPATCH: post_unpatch_callback: vmlinux
$MOD_LIVEPATCH: post_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH
% rmmod $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_exit"


# TEST: module_going notifier
#
# Test loading the livepatch after a targeted kernel module, then unload
# the kernel module before disabling the livepatch.  This tests the
# livepatch core's module_going handler.
#
# - First load a target module, then the livepatch.
#
# - When a target module is unloaded, the livepatch is only reverted
#   from that klp_object ($MOD_TARGET).  As such, only its pre and
#   post-unpatch callbacks are executed when this occurs.
#
# - When the livepatch is disabled, pre and post-unpatch callbacks are
#   run for the remaining klp_object, vmlinux.

echo -n "TEST: module_going notifier ... "
dmesg -C

load_mod $MOD_TARGET
load_lp $MOD_LIVEPATCH
unload_mod $MOD_TARGET
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH

check_result "% modprobe $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_init
% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
$MOD_LIVEPATCH: pre_patch_callback: $MOD_TARGET -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
$MOD_LIVEPATCH: post_patch_callback: vmlinux
$MOD_LIVEPATCH: post_patch_callback: $MOD_TARGET -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': patching complete
% rmmod $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_exit
$MOD_LIVEPATCH: pre_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_GOING] Going away
livepatch: reverting patch '$MOD_LIVEPATCH' on unloading module '$MOD_TARGET'
$MOD_LIVEPATCH: post_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_GOING] Going away
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
$MOD_LIVEPATCH: pre_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
$MOD_LIVEPATCH: post_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"


# TEST: module_coming and module_going notifiers
#
# This test is similar to the previous test, however the livepatch is
# loaded first.  This tests the livepatch core's module_coming and
# module_going handlers.
#
# - First load the livepatch.
#
# - When a targeted kernel module is subsequently loaded, only its
#   pre/post-patch callbacks are executed.
#
# - When the target module is unloaded, the livepatch is only reverted
#   from the $MOD_TARGET klp_object.  As such, only pre and
#   post-unpatch callbacks are executed when this occurs.

echo -n "TEST: module_coming and module_going notifiers ... "
dmesg -C

load_lp $MOD_LIVEPATCH
load_mod $MOD_TARGET
unload_mod $MOD_TARGET
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH

check_result "% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
$MOD_LIVEPATCH: post_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': patching complete
% modprobe $MOD_TARGET
livepatch: applying patch '$MOD_LIVEPATCH' to loading module '$MOD_TARGET'
$MOD_LIVEPATCH: pre_patch_callback: $MOD_TARGET -> [MODULE_STATE_COMING] Full formed, running module_init
$MOD_LIVEPATCH: post_patch_callback: $MOD_TARGET -> [MODULE_STATE_COMING] Full formed, running module_init
$MOD_TARGET: ${MOD_TARGET}_init
% rmmod $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_exit
$MOD_LIVEPATCH: pre_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_GOING] Going away
livepatch: reverting patch '$MOD_LIVEPATCH' on unloading module '$MOD_TARGET'
$MOD_LIVEPATCH: post_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_GOING] Going away
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
$MOD_LIVEPATCH: pre_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
$MOD_LIVEPATCH: post_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"


# TEST: target module not present
#
# A simple test of loading a livepatch without one of its patch target
# klp_objects ever loaded ($MOD_TARGET).
#
# - Load the livepatch.
#
# - As expected, only pre/post-(un)patch handlers are executed for
#   vmlinux.

echo -n "TEST: target module not present ... "
dmesg -C

load_lp $MOD_LIVEPATCH
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH

check_result "% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
$MOD_LIVEPATCH: post_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': patching complete
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
$MOD_LIVEPATCH: pre_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
$MOD_LIVEPATCH: post_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"


# TEST: pre-patch callback -ENODEV
#
# Test a scenario where a vmlinux pre-patch callback returns a non-zero
# status (ie, failure).
#
# - First load a target module.
#
# - Load the livepatch module, setting its 'pre_patch_ret' value to -19
#   (-ENODEV).  When its vmlinux pre-patch callback executes, this
#   status code will propagate back to the module-loading subsystem.
#   The result is that the insmod command refuses to load the livepatch
#   module.

echo -n "TEST: pre-patch callback -ENODEV ... "
dmesg -C

load_mod $MOD_TARGET
load_failing_mod $MOD_LIVEPATCH pre_patch_ret=-19
unload_mod $MOD_TARGET

check_result "% modprobe $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_init
% modprobe $MOD_LIVEPATCH pre_patch_ret=-19
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
test_klp_callbacks_demo: pre_patch_callback: vmlinux
livepatch: pre-patch callback failed for object 'vmlinux'
livepatch: failed to enable patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': canceling patching transition, going to unpatch
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
modprobe: ERROR: could not insert '$MOD_LIVEPATCH': No such device
% rmmod $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_exit"


# TEST: module_coming + pre-patch callback -ENODEV
#
# Similar to the previous test, setup a livepatch such that its vmlinux
# pre-patch callback returns success.  However, when a targeted kernel
# module is later loaded, have the livepatch return a failing status
# code.
#
# - Load the livepatch, vmlinux pre-patch callback succeeds.
#
# - Set a trap so subsequent pre-patch callbacks to this livepatch will
#   return -ENODEV.
#
# - The livepatch pre-patch callback for subsequently loaded target
#   modules will return failure, so the module loader refuses to load
#   the kernel module.  No post-patch or pre/post-unpatch callbacks are
#   executed for this klp_object.
#
# - Pre/post-unpatch callbacks are run for the vmlinux klp_object.

echo -n "TEST: module_coming + pre-patch callback -ENODEV ... "
dmesg -C

load_lp $MOD_LIVEPATCH
set_pre_patch_ret $MOD_LIVEPATCH -19
load_failing_mod $MOD_TARGET
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH

check_result "% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
$MOD_LIVEPATCH: post_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': patching complete
% echo -19 > /sys/module/$MOD_LIVEPATCH/parameters/pre_patch_ret
% modprobe $MOD_TARGET
livepatch: applying patch '$MOD_LIVEPATCH' to loading module '$MOD_TARGET'
$MOD_LIVEPATCH: pre_patch_callback: $MOD_TARGET -> [MODULE_STATE_COMING] Full formed, running module_init
livepatch: pre-patch callback failed for object '$MOD_TARGET'
livepatch: patch '$MOD_LIVEPATCH' failed for module '$MOD_TARGET', refusing to load module '$MOD_TARGET'
modprobe: ERROR: could not insert '$MOD_TARGET': No such device
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
$MOD_LIVEPATCH: pre_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
$MOD_LIVEPATCH: post_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"


# TEST: multiple target modules
#
# Test loading multiple targeted kernel modules.  This test-case is
# mainly for comparing with the next test-case.
#
# - Load a target "busy" kernel module which kicks off a worker function
#   that immediately exits.
#
# - Proceed with loading the livepatch and another ordinary target
#   module.  Post-patch callbacks are executed and the transition
#   completes quickly.

echo -n "TEST: multiple target modules ... "
dmesg -C

load_mod $MOD_TARGET_BUSY sleep_secs=0
# give $MOD_TARGET_BUSY::busymod_work_func() a chance to run
sleep 5
load_lp $MOD_LIVEPATCH
load_mod $MOD_TARGET
unload_mod $MOD_TARGET
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH
unload_mod $MOD_TARGET_BUSY

check_result "% modprobe $MOD_TARGET_BUSY sleep_secs=0
$MOD_TARGET_BUSY: ${MOD_TARGET_BUSY}_init
$MOD_TARGET_BUSY: busymod_work_func, sleeping 0 seconds ...
$MOD_TARGET_BUSY: busymod_work_func exit
% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
$MOD_LIVEPATCH: pre_patch_callback: $MOD_TARGET_BUSY -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
$MOD_LIVEPATCH: post_patch_callback: vmlinux
$MOD_LIVEPATCH: post_patch_callback: $MOD_TARGET_BUSY -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': patching complete
% modprobe $MOD_TARGET
livepatch: applying patch '$MOD_LIVEPATCH' to loading module '$MOD_TARGET'
$MOD_LIVEPATCH: pre_patch_callback: $MOD_TARGET -> [MODULE_STATE_COMING] Full formed, running module_init
$MOD_LIVEPATCH: post_patch_callback: $MOD_TARGET -> [MODULE_STATE_COMING] Full formed, running module_init
$MOD_TARGET: ${MOD_TARGET}_init
% rmmod $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_exit
$MOD_LIVEPATCH: pre_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_GOING] Going away
livepatch: reverting patch '$MOD_LIVEPATCH' on unloading module '$MOD_TARGET'
$MOD_LIVEPATCH: post_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_GOING] Going away
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
$MOD_LIVEPATCH: pre_unpatch_callback: vmlinux
$MOD_LIVEPATCH: pre_unpatch_callback: $MOD_TARGET_BUSY -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
$MOD_LIVEPATCH: post_unpatch_callback: vmlinux
$MOD_LIVEPATCH: post_unpatch_callback: $MOD_TARGET_BUSY -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH
% rmmod $MOD_TARGET_BUSY
$MOD_TARGET_BUSY: ${MOD_TARGET_BUSY}_exit"



# TEST: busy target module
#
# A similar test as the previous one, but force the "busy" kernel module
# to do longer work.
#
# The livepatching core will refuse to patch a task that is currently
# executing a to-be-patched function -- the consistency model stalls the
# current patch transition until this safety-check is met.  Test a
# scenario where one of a livepatch's target klp_objects sits on such a
# function for a long time.  Meanwhile, load and unload other target
# kernel modules while the livepatch transition is in progress.
#
# - Load the "busy" kernel module, this time make it do 10 seconds worth
#   of work.
#
# - Meanwhile, the livepatch is loaded.  Notice that the patch
#   transition does not complete as the targeted "busy" module is
#   sitting on a to-be-patched function.
#
# - Load a second target module (this one is an ordinary idle kernel
#   module).  Note that *no* post-patch callbacks will be executed while
#   the livepatch is still in transition.
#
# - Request an unload of the simple kernel module.  The patch is still
#   transitioning, so its pre-unpatch callbacks are skipped.
#
# - Finally the livepatch is disabled.  Since none of the patch's
#   klp_object's post-patch callbacks executed, the remaining
#   klp_object's pre-unpatch callbacks are skipped.

echo -n "TEST: busy target module ... "
dmesg -C

load_mod $MOD_TARGET_BUSY sleep_secs=10
load_lp_nowait $MOD_LIVEPATCH
# Don't wait for transition, load $MOD_TARGET while the transition
# is still stalled in $MOD_TARGET_BUSY::busymod_work_func()
sleep 5
load_mod $MOD_TARGET
unload_mod $MOD_TARGET
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH
unload_mod $MOD_TARGET_BUSY

check_result "% modprobe $MOD_TARGET_BUSY sleep_secs=10
$MOD_TARGET_BUSY: ${MOD_TARGET_BUSY}_init
$MOD_TARGET_BUSY: busymod_work_func, sleeping 10 seconds ...
% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
$MOD_LIVEPATCH: pre_patch_callback: $MOD_TARGET_BUSY -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': starting patching transition
% modprobe $MOD_TARGET
livepatch: applying patch '$MOD_LIVEPATCH' to loading module '$MOD_TARGET'
$MOD_LIVEPATCH: pre_patch_callback: $MOD_TARGET -> [MODULE_STATE_COMING] Full formed, running module_init
$MOD_TARGET: ${MOD_TARGET}_init
% rmmod $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_exit
livepatch: reverting patch '$MOD_LIVEPATCH' on unloading module '$MOD_TARGET'
$MOD_LIVEPATCH: post_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_GOING] Going away
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': reversing transition from patching to unpatching
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
$MOD_LIVEPATCH: post_unpatch_callback: vmlinux
$MOD_LIVEPATCH: post_unpatch_callback: $MOD_TARGET_BUSY -> [MODULE_STATE_LIVE] Normal state
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH
% rmmod $MOD_TARGET_BUSY
$MOD_TARGET_BUSY: busymod_work_func exit
$MOD_TARGET_BUSY: ${MOD_TARGET_BUSY}_exit"


# TEST: multiple livepatches
#
# Test loading multiple livepatches.  This test-case is mainly for comparing
# with the next test-case.
#
# - Load and unload two livepatches, pre and post (un)patch callbacks
#   execute as each patch progresses through its (un)patching
#   transition.

echo -n "TEST: multiple livepatches ... "
dmesg -C

load_lp $MOD_LIVEPATCH
load_lp $MOD_LIVEPATCH2
disable_lp $MOD_LIVEPATCH2
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH2
unload_lp $MOD_LIVEPATCH

check_result "% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
$MOD_LIVEPATCH: post_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': patching complete
% modprobe $MOD_LIVEPATCH2
livepatch: enabling patch '$MOD_LIVEPATCH2'
livepatch: '$MOD_LIVEPATCH2': initializing patching transition
$MOD_LIVEPATCH2: pre_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': starting patching transition
livepatch: '$MOD_LIVEPATCH2': completing patching transition
$MOD_LIVEPATCH2: post_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': patching complete
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH2/enabled
livepatch: '$MOD_LIVEPATCH2': initializing unpatching transition
$MOD_LIVEPATCH2: pre_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': starting unpatching transition
livepatch: '$MOD_LIVEPATCH2': completing unpatching transition
$MOD_LIVEPATCH2: post_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': unpatching complete
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
$MOD_LIVEPATCH: pre_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
$MOD_LIVEPATCH: post_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH2
% rmmod $MOD_LIVEPATCH"


# TEST: atomic replace
#
# Load multiple livepatches, but the second as an 'atomic-replace'
# patch.  When the latter loads, the original livepatch should be
# disabled and *none* of its pre/post-unpatch callbacks executed.  On
# the other hand, when the atomic-replace livepatch is disabled, its
# pre/post-unpatch callbacks *should* be executed.
#
# - Load and unload two livepatches, the second of which has its
#   .replace flag set true.
#
# - Pre and post patch callbacks are executed for both livepatches.
#
# - Once the atomic replace module is loaded, only its pre and post
#   unpatch callbacks are executed.

echo -n "TEST: atomic replace ... "
dmesg -C

load_lp $MOD_LIVEPATCH
load_lp $MOD_LIVEPATCH2 replace=1
disable_lp $MOD_LIVEPATCH2
unload_lp $MOD_LIVEPATCH2
unload_lp $MOD_LIVEPATCH

check_result "% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
$MOD_LIVEPATCH: post_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH': patching complete
% modprobe $MOD_LIVEPATCH2 replace=1
livepatch: enabling patch '$MOD_LIVEPATCH2'
livepatch: '$MOD_LIVEPATCH2': initializing patching transition
$MOD_LIVEPATCH2: pre_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': starting patching transition
livepatch: '$MOD_LIVEPATCH2': completing patching transition
$MOD_LIVEPATCH2: post_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': patching complete
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH2/enabled
livepatch: '$MOD_LIVEPATCH2': initializing unpatching transition
$MOD_LIVEPATCH2: pre_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': starting unpatching transition
livepatch: '$MOD_LIVEPATCH2': completing unpatching transition
$MOD_LIVEPATCH2: post_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': unpatching complete
% rmmod $MOD_LIVEPATCH2
% rmmod $MOD_LIVEPATCH"


exit 0
