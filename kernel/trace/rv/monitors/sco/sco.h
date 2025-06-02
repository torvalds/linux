/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of sco automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

enum states_sco {
	thread_context_sco = 0,
	scheduling_context_sco,
	state_max_sco
};

#define INVALID_STATE state_max_sco

enum events_sco {
	sched_set_state_sco = 0,
	schedule_entry_sco,
	schedule_exit_sco,
	event_max_sco
};

struct automaton_sco {
	char *state_names[state_max_sco];
	char *event_names[event_max_sco];
	unsigned char function[state_max_sco][event_max_sco];
	unsigned char initial_state;
	bool final_states[state_max_sco];
};

static const struct automaton_sco automaton_sco = {
	.state_names = {
		"thread_context",
		"scheduling_context"
	},
	.event_names = {
		"sched_set_state",
		"schedule_entry",
		"schedule_exit"
	},
	.function = {
		{     thread_context_sco, scheduling_context_sco,          INVALID_STATE },
		{          INVALID_STATE,          INVALID_STATE,     thread_context_sco },
	},
	.initial_state = thread_context_sco,
	.final_states = { 1, 0 },
};
