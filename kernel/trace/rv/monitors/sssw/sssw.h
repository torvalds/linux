/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of sssw automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

enum states_sssw {
	runnable_sssw = 0,
	signal_wakeup_sssw,
	sleepable_sssw,
	sleeping_sssw,
	state_max_sssw
};

#define INVALID_STATE state_max_sssw

enum events_sssw {
	sched_set_state_runnable_sssw = 0,
	sched_set_state_sleepable_sssw,
	sched_switch_blocking_sssw,
	sched_switch_in_sssw,
	sched_switch_preempt_sssw,
	sched_switch_suspend_sssw,
	sched_switch_yield_sssw,
	sched_wakeup_sssw,
	signal_deliver_sssw,
	event_max_sssw
};

struct automaton_sssw {
	char *state_names[state_max_sssw];
	char *event_names[event_max_sssw];
	unsigned char function[state_max_sssw][event_max_sssw];
	unsigned char initial_state;
	bool final_states[state_max_sssw];
};

static const struct automaton_sssw automaton_sssw = {
	.state_names = {
		"runnable",
		"signal_wakeup",
		"sleepable",
		"sleeping"
	},
	.event_names = {
		"sched_set_state_runnable",
		"sched_set_state_sleepable",
		"sched_switch_blocking",
		"sched_switch_in",
		"sched_switch_preempt",
		"sched_switch_suspend",
		"sched_switch_yield",
		"sched_wakeup",
		"signal_deliver"
	},
	.function = {
		{
			runnable_sssw,
			sleepable_sssw,
			sleeping_sssw,
			runnable_sssw,
			runnable_sssw,
			INVALID_STATE,
			runnable_sssw,
			runnable_sssw,
			runnable_sssw
		},
		{
			INVALID_STATE,
			sleepable_sssw,
			INVALID_STATE,
			signal_wakeup_sssw,
			signal_wakeup_sssw,
			INVALID_STATE,
			signal_wakeup_sssw,
			signal_wakeup_sssw,
			runnable_sssw
		},
		{
			runnable_sssw,
			sleepable_sssw,
			sleeping_sssw,
			sleepable_sssw,
			sleepable_sssw,
			sleeping_sssw,
			signal_wakeup_sssw,
			runnable_sssw,
			sleepable_sssw
		},
		{
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
			runnable_sssw,
			INVALID_STATE
		},
	},
	.initial_state = runnable_sssw,
	.final_states = { 1, 0, 0, 0 },
};
