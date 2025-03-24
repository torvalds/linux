#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Run bpftrace to obtain a histogram of the types of primitives used to
# initiate RCU grace periods.  The count associated with rcu_gp_init()
# is the number of normal (non-expedited) grace periods.
#
# Usage: rcu-updaters.sh [ duration-in-seconds ]
#
# Note that not all kernel builds have all of these functions.  In those
# that do not, this script will issue a diagnostic for each that is not
# found, but continue normally for the rest of the functions.

duration=${1}
if test -n "${duration}"
then
	exitclause='interval:s:'"${duration}"' { exit(); }'
else
	echo 'Hit control-C to end sample and print results.'
fi
bpftrace -e 'kprobe:kvfree_call_rcu,
	     kprobe:call_rcu,
	     kprobe:call_rcu_tasks,
	     kprobe:call_rcu_tasks_trace,
	     kprobe:call_srcu,
	     kprobe:rcu_barrier,
	     kprobe:rcu_barrier_tasks,
	     kprobe:rcu_barrier_tasks_trace,
	     kprobe:srcu_barrier,
	     kprobe:synchronize_rcu,
	     kprobe:synchronize_rcu_expedited,
	     kprobe:synchronize_rcu_tasks,
	     kprobe:synchronize_rcu_tasks_rude,
	     kprobe:synchronize_rcu_tasks_trace,
	     kprobe:synchronize_srcu,
	     kprobe:synchronize_srcu_expedited,
	     kprobe:get_state_synchronize_rcu,
	     kprobe:get_state_synchronize_rcu_full,
	     kprobe:start_poll_synchronize_rcu,
	     kprobe:start_poll_synchronize_rcu_expedited,
	     kprobe:start_poll_synchronize_rcu_full,
	     kprobe:start_poll_synchronize_rcu_expedited_full,
	     kprobe:poll_state_synchronize_rcu,
	     kprobe:poll_state_synchronize_rcu_full,
	     kprobe:cond_synchronize_rcu,
	     kprobe:cond_synchronize_rcu_full,
	     kprobe:start_poll_synchronize_srcu,
	     kprobe:poll_state_synchronize_srcu,
	     kprobe:rcu_gp_init
	     	{ @counts[func] = count(); } '"${exitclause}"
