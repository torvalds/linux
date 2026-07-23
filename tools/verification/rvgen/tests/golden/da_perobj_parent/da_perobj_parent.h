/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of da_perobj_parent automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

#define MONITOR_NAME da_perobj_parent

enum states_da_perobj_parent {
	state_a_da_perobj_parent,
	state_b_da_perobj_parent,
	state_c_da_perobj_parent,
	state_max_da_perobj_parent,
};

#define INVALID_STATE state_max_da_perobj_parent

enum events_da_perobj_parent {
	event_1_da_perobj_parent,
	event_2_da_perobj_parent,
	event_3_da_perobj_parent,
	event_max_da_perobj_parent,
};

struct automaton_da_perobj_parent {
	char *state_names[state_max_da_perobj_parent];
	char *event_names[event_max_da_perobj_parent];
	unsigned char function[state_max_da_perobj_parent][event_max_da_perobj_parent];
	unsigned char initial_state;
	bool final_states[state_max_da_perobj_parent];
};

static const struct automaton_da_perobj_parent automaton_da_perobj_parent = {
	.state_names = {
		"state_a",
		"state_b",
		"state_c",
	},
	.event_names = {
		"event_1",
		"event_2",
		"event_3",
	},
	.function = {
		{
			state_b_da_perobj_parent,
			state_c_da_perobj_parent,
			INVALID_STATE,
		},
		{
			INVALID_STATE,
			state_a_da_perobj_parent,
			state_c_da_perobj_parent,
		},
		{
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
		},
	},
	.initial_state = state_a_da_perobj_parent,
	.final_states = { 1, 0, 0 },
};
