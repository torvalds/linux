/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of tss automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

enum states_tss {
	thread_tss = 0,
	sched_tss,
	state_max_tss
};

#define INVALID_STATE state_max_tss

enum events_tss {
	sched_switch_tss = 0,
	schedule_entry_tss,
	schedule_exit_tss,
	event_max_tss
};

struct automaton_tss {
	char *state_names[state_max_tss];
	char *event_names[event_max_tss];
	unsigned char function[state_max_tss][event_max_tss];
	unsigned char initial_state;
	bool final_states[state_max_tss];
};

static const struct automaton_tss automaton_tss = {
	.state_names = {
		"thread",
		"sched"
	},
	.event_names = {
		"sched_switch",
		"schedule_entry",
		"schedule_exit"
	},
	.function = {
		{     INVALID_STATE,         sched_tss,     INVALID_STATE },
		{         sched_tss,     INVALID_STATE,        thread_tss },
	},
	.initial_state = thread_tss,
	.final_states = { 1, 0 },
};
