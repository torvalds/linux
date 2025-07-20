/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of snep automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

enum states_snep {
	non_scheduling_context_snep = 0,
	scheduling_contex_snep,
	state_max_snep
};

#define INVALID_STATE state_max_snep

enum events_snep {
	preempt_disable_snep = 0,
	preempt_enable_snep,
	schedule_entry_snep,
	schedule_exit_snep,
	event_max_snep
};

struct automaton_snep {
	char *state_names[state_max_snep];
	char *event_names[event_max_snep];
	unsigned char function[state_max_snep][event_max_snep];
	unsigned char initial_state;
	bool final_states[state_max_snep];
};

static const struct automaton_snep automaton_snep = {
	.state_names = {
		"non_scheduling_context",
		"scheduling_contex"
	},
	.event_names = {
		"preempt_disable",
		"preempt_enable",
		"schedule_entry",
		"schedule_exit"
	},
	.function = {
		{ non_scheduling_context_snep, non_scheduling_context_snep, scheduling_contex_snep,               INVALID_STATE },
		{               INVALID_STATE,               INVALID_STATE,          INVALID_STATE, non_scheduling_context_snep },
	},
	.initial_state = non_scheduling_context_snep,
	.final_states = { 1, 0 },
};
