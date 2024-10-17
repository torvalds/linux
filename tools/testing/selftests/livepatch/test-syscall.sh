#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023 SUSE
# Author: Marcos Paulo de Souza <mpdesouza@suse.com>

. $(dirname $0)/functions.sh

MOD_SYSCALL=test_klp_syscall

setup_config

# - Start _NRPROC processes calling getpid and load a livepatch to patch the
#   getpid syscall. Check if all the processes transitioned to the livepatched
#   state.

start_test "patch getpid syscall while being heavily hammered"

NPROC=$(getconf _NPROCESSORS_ONLN)
MAXPROC=128

for i in $(seq 1 $(($NPROC < $MAXPROC ? $NPROC : $MAXPROC))); do
	./test_klp-call_getpid &
	pids[$i]="$!"
done

pid_list=$(echo ${pids[@]} | tr ' ' ',')
load_lp $MOD_SYSCALL klp_pids=$pid_list

# wait for all tasks to transition to patched state
loop_until 'grep -q '^0$' /sys/kernel/test_klp_syscall/npids'

pending_pids=$(cat /sys/kernel/test_klp_syscall/npids)
log "$MOD_SYSCALL: Remaining not livepatched processes: $pending_pids"

for pid in ${pids[@]}; do
	kill $pid || true
done

disable_lp $MOD_SYSCALL
unload_lp $MOD_SYSCALL

check_result "% insmod test_modules/$MOD_SYSCALL.ko klp_pids=$pid_list
livepatch: enabling patch '$MOD_SYSCALL'
livepatch: '$MOD_SYSCALL': initializing patching transition
livepatch: '$MOD_SYSCALL': starting patching transition
livepatch: '$MOD_SYSCALL': completing patching transition
livepatch: '$MOD_SYSCALL': patching complete
$MOD_SYSCALL: Remaining not livepatched processes: 0
% echo 0 > $SYSFS_KLP_DIR/$MOD_SYSCALL/enabled
livepatch: '$MOD_SYSCALL': initializing unpatching transition
livepatch: '$MOD_SYSCALL': starting unpatching transition
livepatch: '$MOD_SYSCALL': completing unpatching transition
livepatch: '$MOD_SYSCALL': unpatching complete
% rmmod $MOD_SYSCALL"

exit 0
