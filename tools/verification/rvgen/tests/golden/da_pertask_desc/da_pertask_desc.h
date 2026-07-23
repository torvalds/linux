/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of da_pertask_desc automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

#define MONITOR_NAME da_pertask_desc

enum states_da_pertask_desc {
	state_a_da_pertask_desc,
	state_b_da_pertask_desc,
	state_c_da_pertask_desc,
	state_max_da_pertask_desc,
};

#define INVALID_STATE state_max_da_pertask_desc

enum events_da_pertask_desc {
	event_1_da_pertask_desc,
	event_2_da_pertask_desc,
	event_3_da_pertask_desc,
	event_max_da_pertask_desc,
};

struct automaton_da_pertask_desc {
	char *state_names[state_max_da_pertask_desc];
	char *event_names[event_max_da_pertask_desc];
	unsigned char function[state_max_da_pertask_desc][event_max_da_pertask_desc];
	unsigned char initial_state;
	bool final_states[state_max_da_pertask_desc];
};

static const struct automaton_da_pertask_desc automaton_da_pertask_desc = {
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
			state_b_da_pertask_desc,
			state_c_da_pertask_desc,
			INVALID_STATE,
		},
		{
			INVALID_STATE,
			state_a_da_pertask_desc,
			state_c_da_pertask_desc,
		},
		{
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
		},
	},
	.initial_state = state_a_da_pertask_desc,
	.final_states = { 1, 0, 0 },
};
