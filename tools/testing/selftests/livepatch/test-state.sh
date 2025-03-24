#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2019 SUSE

. $(dirname $0)/functions.sh

MOD_LIVEPATCH=test_klp_state
MOD_LIVEPATCH2=test_klp_state2
MOD_LIVEPATCH3=test_klp_state3

setup_config


# Load and remove a module that modifies the system state

start_test "system state modification"

load_lp $MOD_LIVEPATCH
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH

check_result "% insmod test_modules/$MOD_LIVEPATCH.ko
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
$MOD_LIVEPATCH: allocate_loglevel_state: allocating space to store console_loglevel
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
$MOD_LIVEPATCH: post_patch_callback: vmlinux
$MOD_LIVEPATCH: fix_console_loglevel: fixing console_loglevel
livepatch: '$MOD_LIVEPATCH': patching complete
% echo 0 > $SYSFS_KLP_DIR/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
$MOD_LIVEPATCH: pre_unpatch_callback: vmlinux
$MOD_LIVEPATCH: restore_console_loglevel: restoring console_loglevel
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
$MOD_LIVEPATCH: post_unpatch_callback: vmlinux
$MOD_LIVEPATCH: free_loglevel_state: freeing space for the stored console_loglevel
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"


# Take over system state change by a cumulative patch

start_test "taking over system state modification"

load_lp $MOD_LIVEPATCH
load_lp $MOD_LIVEPATCH2
unload_lp $MOD_LIVEPATCH
disable_lp $MOD_LIVEPATCH2
unload_lp $MOD_LIVEPATCH2

check_result "% insmod test_modules/$MOD_LIVEPATCH.ko
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
$MOD_LIVEPATCH: pre_patch_callback: vmlinux
$MOD_LIVEPATCH: allocate_loglevel_state: allocating space to store console_loglevel
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
$MOD_LIVEPATCH: post_patch_callback: vmlinux
$MOD_LIVEPATCH: fix_console_loglevel: fixing console_loglevel
livepatch: '$MOD_LIVEPATCH': patching complete
% insmod test_modules/$MOD_LIVEPATCH2.ko
livepatch: enabling patch '$MOD_LIVEPATCH2'
livepatch: '$MOD_LIVEPATCH2': initializing patching transition
$MOD_LIVEPATCH2: pre_patch_callback: vmlinux
$MOD_LIVEPATCH2: allocate_loglevel_state: space to store console_loglevel already allocated
livepatch: '$MOD_LIVEPATCH2': starting patching transition
livepatch: '$MOD_LIVEPATCH2': completing patching transition
$MOD_LIVEPATCH2: post_patch_callback: vmlinux
$MOD_LIVEPATCH2: fix_console_loglevel: taking over the console_loglevel change
livepatch: '$MOD_LIVEPATCH2': patching complete
% rmmod $MOD_LIVEPATCH
% echo 0 > $SYSFS_KLP_DIR/$MOD_LIVEPATCH2/enabled
livepatch: '$MOD_LIVEPATCH2': initializing unpatching transition
$MOD_LIVEPATCH2: pre_unpatch_callback: vmlinux
$MOD_LIVEPATCH2: restore_console_loglevel: restoring console_loglevel
livepatch: '$MOD_LIVEPATCH2': starting unpatching transition
livepatch: '$MOD_LIVEPATCH2': completing unpatching transition
$MOD_LIVEPATCH2: post_unpatch_callback: vmlinux
$MOD_LIVEPATCH2: free_loglevel_state: freeing space for the stored console_loglevel
livepatch: '$MOD_LIVEPATCH2': unpatching complete
% rmmod $MOD_LIVEPATCH2"


# Take over system state change by a cumulative patch

start_test "compatible cumulative livepatches"

load_lp $MOD_LIVEPATCH2
load_lp $MOD_LIVEPATCH3
unload_lp $MOD_LIVEPATCH2
load_lp $MOD_LIVEPATCH2
disable_lp $MOD_LIVEPATCH2
unload_lp $MOD_LIVEPATCH2
unload_lp $MOD_LIVEPATCH3

check_result "% insmod test_modules/$MOD_LIVEPATCH2.ko
livepatch: enabling patch '$MOD_LIVEPATCH2'
livepatch: '$MOD_LIVEPATCH2': initializing patching transition
$MOD_LIVEPATCH2: pre_patch_callback: vmlinux
$MOD_LIVEPATCH2: allocate_loglevel_state: allocating space to store console_loglevel
livepatch: '$MOD_LIVEPATCH2': starting patching transition
livepatch: '$MOD_LIVEPATCH2': completing patching transition
$MOD_LIVEPATCH2: post_patch_callback: vmlinux
$MOD_LIVEPATCH2: fix_console_loglevel: fixing console_loglevel
livepatch: '$MOD_LIVEPATCH2': patching complete
% insmod test_modules/$MOD_LIVEPATCH3.ko
livepatch: enabling patch '$MOD_LIVEPATCH3'
livepatch: '$MOD_LIVEPATCH3': initializing patching transition
$MOD_LIVEPATCH3: pre_patch_callback: vmlinux
$MOD_LIVEPATCH3: allocate_loglevel_state: space to store console_loglevel already allocated
livepatch: '$MOD_LIVEPATCH3': starting patching transition
livepatch: '$MOD_LIVEPATCH3': completing patching transition
$MOD_LIVEPATCH3: post_patch_callback: vmlinux
$MOD_LIVEPATCH3: fix_console_loglevel: taking over the console_loglevel change
livepatch: '$MOD_LIVEPATCH3': patching complete
% rmmod $MOD_LIVEPATCH2
% insmod test_modules/$MOD_LIVEPATCH2.ko
livepatch: enabling patch '$MOD_LIVEPATCH2'
livepatch: '$MOD_LIVEPATCH2': initializing patching transition
$MOD_LIVEPATCH2: pre_patch_callback: vmlinux
$MOD_LIVEPATCH2: allocate_loglevel_state: space to store console_loglevel already allocated
livepatch: '$MOD_LIVEPATCH2': starting patching transition
livepatch: '$MOD_LIVEPATCH2': completing patching transition
$MOD_LIVEPATCH2: post_patch_callback: vmlinux
$MOD_LIVEPATCH2: fix_console_loglevel: taking over the console_loglevel change
livepatch: '$MOD_LIVEPATCH2': patching complete
% echo 0 > $SYSFS_KLP_DIR/$MOD_LIVEPATCH2/enabled
livepatch: '$MOD_LIVEPATCH2': initializing unpatching transition
$MOD_LIVEPATCH2: pre_unpatch_callback: vmlinux
$MOD_LIVEPATCH2: restore_console_loglevel: restoring console_loglevel
livepatch: '$MOD_LIVEPATCH2': starting unpatching transition
livepatch: '$MOD_LIVEPATCH2': completing unpatching transition
$MOD_LIVEPATCH2: post_unpatch_callback: vmlinux
$MOD_LIVEPATCH2: free_loglevel_state: freeing space for the stored console_loglevel
livepatch: '$MOD_LIVEPATCH2': unpatching complete
% rmmod $MOD_LIVEPATCH2
% rmmod $MOD_LIVEPATCH3"


# Failure caused by incompatible cumulative livepatches

start_test "incompatible cumulative livepatches"

load_lp $MOD_LIVEPATCH2
load_failing_mod $MOD_LIVEPATCH
disable_lp $MOD_LIVEPATCH2
unload_lp $MOD_LIVEPATCH2

check_result "% insmod test_modules/$MOD_LIVEPATCH2.ko
livepatch: enabling patch '$MOD_LIVEPATCH2'
livepatch: '$MOD_LIVEPATCH2': initializing patching transition
$MOD_LIVEPATCH2: pre_patch_callback: vmlinux
$MOD_LIVEPATCH2: allocate_loglevel_state: allocating space to store console_loglevel
livepatch: '$MOD_LIVEPATCH2': starting patching transition
livepatch: '$MOD_LIVEPATCH2': completing patching transition
$MOD_LIVEPATCH2: post_patch_callback: vmlinux
$MOD_LIVEPATCH2: fix_console_loglevel: fixing console_loglevel
livepatch: '$MOD_LIVEPATCH2': patching complete
% insmod test_modules/$MOD_LIVEPATCH.ko
livepatch: Livepatch patch ($MOD_LIVEPATCH) is not compatible with the already installed livepatches.
insmod: ERROR: could not insert module test_modules/$MOD_LIVEPATCH.ko: Invalid parameters
% echo 0 > $SYSFS_KLP_DIR/$MOD_LIVEPATCH2/enabled
livepatch: '$MOD_LIVEPATCH2': initializing unpatching transition
$MOD_LIVEPATCH2: pre_unpatch_callback: vmlinux
$MOD_LIVEPATCH2: restore_console_loglevel: restoring console_loglevel
livepatch: '$MOD_LIVEPATCH2': starting unpatching transition
livepatch: '$MOD_LIVEPATCH2': completing unpatching transition
$MOD_LIVEPATCH2: post_unpatch_callback: vmlinux
$MOD_LIVEPATCH2: free_loglevel_state: freeing space for the stored console_loglevel
livepatch: '$MOD_LIVEPATCH2': unpatching complete
% rmmod $MOD_LIVEPATCH2"

exit 0
