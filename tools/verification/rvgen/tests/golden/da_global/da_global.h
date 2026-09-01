/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of da_global automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

#define MONITOR_NAME da_global

enum states_da_global {
	state_a_da_global,
	state_b_da_global,
	state_max_da_global,
};

#define INVALID_STATE state_max_da_global

enum events_da_global {
	event_1_da_global,
	event_2_da_global,
	event_max_da_global,
};

struct automaton_da_global {
	char *state_names[state_max_da_global];
	char *event_names[event_max_da_global];
	unsigned char function[state_max_da_global][event_max_da_global];
	unsigned char initial_state;
	bool final_states[state_max_da_global];
};

static const struct automaton_da_global automaton_da_global = {
	.state_names = {
		"state_a",
		"state_b",
	},
	.event_names = {
		"event_1",
		"event_2",
	},
	.function = {
		{       state_b_da_global,       state_a_da_global },
		{           INVALID_STATE,       state_a_da_global },
	},
	.initial_state = state_a_da_global,
	.final_states = { 1, 0 },
};
