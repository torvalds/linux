/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of snroc automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

enum states_snroc {
	other_context_snroc = 0,
	own_context_snroc,
	state_max_snroc
};

#define INVALID_STATE state_max_snroc

enum events_snroc {
	sched_set_state_snroc = 0,
	sched_switch_in_snroc,
	sched_switch_out_snroc,
	event_max_snroc
};

struct automaton_snroc {
	char *state_names[state_max_snroc];
	char *event_names[event_max_snroc];
	unsigned char function[state_max_snroc][event_max_snroc];
	unsigned char initial_state;
	bool final_states[state_max_snroc];
};

static const struct automaton_snroc automaton_snroc = {
	.state_names = {
		"other_context",
		"own_context"
	},
	.event_names = {
		"sched_set_state",
		"sched_switch_in",
		"sched_switch_out"
	},
	.function = {
		{      INVALID_STATE,  own_context_snroc,       INVALID_STATE },
		{  own_context_snroc,      INVALID_STATE, other_context_snroc },
	},
	.initial_state = other_context_snroc,
	.final_states = { 1, 0 },
};
