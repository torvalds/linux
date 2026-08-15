#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2022 Song Liu <song@kernel.org>

. $(dirname $0)/functions.sh

MOD_LIVEPATCH=test_klp_livepatch
MOD_LIVEPATCH2=test_klp_callbacks_demo
MOD_LIVEPATCH3=test_klp_syscall

HAS_PATCH_ATTR=0
HAS_REPLACE_ATTR=0
HAS_STACK_ORDER_ATTR=0

setup_config

# - load a livepatch and verifies the sysfs entries work as expected

start_test "sysfs test"

load_lp $MOD_LIVEPATCH

check_sysfs_rights "$MOD_LIVEPATCH" "" "drwxr-xr-x"
check_sysfs_rights "$MOD_LIVEPATCH" "enabled" "-rw-r--r--"
check_sysfs_value  "$MOD_LIVEPATCH" "enabled" "1"
check_sysfs_rights "$MOD_LIVEPATCH" "force" "--w-------"
check_sysfs_rights "$MOD_LIVEPATCH" "transition" "-r--r--r--"
check_sysfs_value  "$MOD_LIVEPATCH" "transition" "0"

if does_sysfs_exist "$MOD_LIVEPATCH/vmlinux" "patched"; then
	check_sysfs_rights "$MOD_LIVEPATCH" "vmlinux/patched" "-r--r--r--"
	check_sysfs_value  "$MOD_LIVEPATCH" "vmlinux/patched" "1"
	HAS_PATCH_ATTR=1
fi

if does_sysfs_exist "$MOD_LIVEPATCH" "replace"; then
	check_sysfs_rights "$MOD_LIVEPATCH" "replace" "-r--r--r--"
	HAS_REPLACE_ATTR=1
fi

if does_sysfs_exist "$MOD_LIVEPATCH" "stack_order"; then
	check_sysfs_rights "$MOD_LIVEPATCH" "stack_order" "-r--r--r--"
	check_sysfs_value  "$MOD_LIVEPATCH" "stack_order" "1"
	HAS_STACK_ORDER_ATTR=1
fi

disable_lp $MOD_LIVEPATCH

unload_lp $MOD_LIVEPATCH

check_result "% insmod test_modules/$MOD_LIVEPATCH.ko
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
livepatch: '$MOD_LIVEPATCH': patching complete
% echo 0 > $SYSFS_KLP_DIR/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"

if [[ "$HAS_PATCH_ATTR" == "1" ]]; then
	start_test "sysfs test object/patched"

	MOD_TARGET=test_klp_callbacks_mod
	load_lp $MOD_LIVEPATCH2

	# check the "patch" file changes as target module loads/unloads
	check_sysfs_value  "$MOD_LIVEPATCH2" "$MOD_TARGET/patched" "0"
	load_mod $MOD_TARGET
	check_sysfs_value  "$MOD_LIVEPATCH2" "$MOD_TARGET/patched" "1"
	unload_mod $MOD_TARGET
	check_sysfs_value  "$MOD_LIVEPATCH2" "$MOD_TARGET/patched" "0"

	disable_lp $MOD_LIVEPATCH2
	unload_lp $MOD_LIVEPATCH2

	check_result "% insmod test_modules/$MOD_LIVEPATCH2.ko
livepatch: enabling patch '$MOD_LIVEPATCH2'
livepatch: '$MOD_LIVEPATCH2': initializing patching transition
$MOD_LIVEPATCH2: pre_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': starting patching transition
livepatch: '$MOD_LIVEPATCH2': completing patching transition
$MOD_LIVEPATCH2: post_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': patching complete
% insmod test_modules/$MOD_TARGET.ko
livepatch: applying patch '$MOD_LIVEPATCH2' to loading module '$MOD_TARGET'
$MOD_LIVEPATCH2: pre_patch_callback: $MOD_TARGET -> [MODULE_STATE_COMING] Full formed, running module_init
$MOD_LIVEPATCH2: post_patch_callback: $MOD_TARGET -> [MODULE_STATE_COMING] Full formed, running module_init
$MOD_TARGET: test_klp_callbacks_mod_init
% rmmod $MOD_TARGET
$MOD_TARGET: test_klp_callbacks_mod_exit
$MOD_LIVEPATCH2: pre_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_GOING] Going away
livepatch: reverting patch '$MOD_LIVEPATCH2' on unloading module '$MOD_TARGET'
$MOD_LIVEPATCH2: post_unpatch_callback: $MOD_TARGET -> [MODULE_STATE_GOING] Going away
% echo 0 > $SYSFS_KLP_DIR/$MOD_LIVEPATCH2/enabled
livepatch: '$MOD_LIVEPATCH2': initializing unpatching transition
$MOD_LIVEPATCH2: pre_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': starting unpatching transition
livepatch: '$MOD_LIVEPATCH2': completing unpatching transition
$MOD_LIVEPATCH2: post_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': unpatching complete
% rmmod $MOD_LIVEPATCH2"
fi

if [[ "$HAS_REPLACE_ATTR" == "1" ]]; then
	start_test "sysfs test replace enabled"

	MOD_ATOMIC_REPLACE=test_klp_atomic_replace
	load_lp $MOD_ATOMIC_REPLACE replace=1

	check_sysfs_rights "$MOD_ATOMIC_REPLACE" "replace" "-r--r--r--"
	check_sysfs_value  "$MOD_ATOMIC_REPLACE" "replace" "1"

	disable_lp $MOD_ATOMIC_REPLACE
	unload_lp $MOD_ATOMIC_REPLACE

	check_result "% insmod test_modules/$MOD_ATOMIC_REPLACE.ko replace=1
livepatch: enabling patch '$MOD_ATOMIC_REPLACE'
livepatch: '$MOD_ATOMIC_REPLACE': initializing patching transition
livepatch: '$MOD_ATOMIC_REPLACE': starting patching transition
livepatch: '$MOD_ATOMIC_REPLACE': completing patching transition
livepatch: '$MOD_ATOMIC_REPLACE': patching complete
% echo 0 > $SYSFS_KLP_DIR/$MOD_ATOMIC_REPLACE/enabled
livepatch: '$MOD_ATOMIC_REPLACE': initializing unpatching transition
livepatch: '$MOD_ATOMIC_REPLACE': starting unpatching transition
livepatch: '$MOD_ATOMIC_REPLACE': completing unpatching transition
livepatch: '$MOD_ATOMIC_REPLACE': unpatching complete
% rmmod $MOD_ATOMIC_REPLACE"

	start_test "sysfs test replace disabled"

	load_lp $MOD_ATOMIC_REPLACE replace=0

	check_sysfs_rights "$MOD_ATOMIC_REPLACE" "replace" "-r--r--r--"
	check_sysfs_value  "$MOD_ATOMIC_REPLACE" "replace" "0"

	disable_lp $MOD_ATOMIC_REPLACE
	unload_lp $MOD_ATOMIC_REPLACE

	check_result "% insmod test_modules/$MOD_ATOMIC_REPLACE.ko replace=0
livepatch: enabling patch '$MOD_ATOMIC_REPLACE'
livepatch: '$MOD_ATOMIC_REPLACE': initializing patching transition
livepatch: '$MOD_ATOMIC_REPLACE': starting patching transition
livepatch: '$MOD_ATOMIC_REPLACE': completing patching transition
livepatch: '$MOD_ATOMIC_REPLACE': patching complete
% echo 0 > $SYSFS_KLP_DIR/$MOD_ATOMIC_REPLACE/enabled
livepatch: '$MOD_ATOMIC_REPLACE': initializing unpatching transition
livepatch: '$MOD_ATOMIC_REPLACE': starting unpatching transition
livepatch: '$MOD_ATOMIC_REPLACE': completing unpatching transition
livepatch: '$MOD_ATOMIC_REPLACE': unpatching complete
% rmmod $MOD_ATOMIC_REPLACE"
fi

if [[ "$HAS_STACK_ORDER_ATTR" == "1" ]]; then
	start_test "sysfs test stack_order value"

	load_lp $MOD_LIVEPATCH

	check_sysfs_value  "$MOD_LIVEPATCH" "stack_order" "1"

	load_lp $MOD_LIVEPATCH2

	check_sysfs_value  "$MOD_LIVEPATCH2" "stack_order" "2"

	load_lp $MOD_LIVEPATCH3

	check_sysfs_value  "$MOD_LIVEPATCH3" "stack_order" "3"

	disable_lp $MOD_LIVEPATCH2
	unload_lp $MOD_LIVEPATCH2

	check_sysfs_value  "$MOD_LIVEPATCH" "stack_order" "1"
	check_sysfs_value  "$MOD_LIVEPATCH3" "stack_order" "2"

	disable_lp $MOD_LIVEPATCH3
	unload_lp $MOD_LIVEPATCH3

	disable_lp $MOD_LIVEPATCH
	unload_lp $MOD_LIVEPATCH

	check_result "% insmod test_modules/$MOD_LIVEPATCH.ko
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
livepatch: '$MOD_LIVEPATCH': patching complete
% insmod test_modules/$MOD_LIVEPATCH2.ko
livepatch: enabling patch '$MOD_LIVEPATCH2'
livepatch: '$MOD_LIVEPATCH2': initializing patching transition
$MOD_LIVEPATCH2: pre_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': starting patching transition
livepatch: '$MOD_LIVEPATCH2': completing patching transition
$MOD_LIVEPATCH2: post_patch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': patching complete
% insmod test_modules/$MOD_LIVEPATCH3.ko
livepatch: enabling patch '$MOD_LIVEPATCH3'
livepatch: '$MOD_LIVEPATCH3': initializing patching transition
livepatch: '$MOD_LIVEPATCH3': starting patching transition
livepatch: '$MOD_LIVEPATCH3': completing patching transition
livepatch: '$MOD_LIVEPATCH3': patching complete
% echo 0 > $SYSFS_KLP_DIR/$MOD_LIVEPATCH2/enabled
livepatch: '$MOD_LIVEPATCH2': initializing unpatching transition
$MOD_LIVEPATCH2: pre_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': starting unpatching transition
livepatch: '$MOD_LIVEPATCH2': completing unpatching transition
$MOD_LIVEPATCH2: post_unpatch_callback: vmlinux
livepatch: '$MOD_LIVEPATCH2': unpatching complete
% rmmod $MOD_LIVEPATCH2
% echo 0 > $SYSFS_KLP_DIR/$MOD_LIVEPATCH3/enabled
livepatch: '$MOD_LIVEPATCH3': initializing unpatching transition
livepatch: '$MOD_LIVEPATCH3': starting unpatching transition
livepatch: '$MOD_LIVEPATCH3': completing unpatching transition
livepatch: '$MOD_LIVEPATCH3': unpatching complete
% rmmod $MOD_LIVEPATCH3
% echo 0 > $SYSFS_KLP_DIR/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"
fi

exit 0
