/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of test_da_kunit automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

#define MONITOR_NAME test_da_kunit

enum states_test_da_kunit {
	state_a_test_da_kunit,
	state_b_test_da_kunit,
	state_max_test_da_kunit,
};

#define INVALID_STATE state_max_test_da_kunit

enum events_test_da_kunit {
	event_1_test_da_kunit,
	event_2_test_da_kunit,
	event_max_test_da_kunit,
};

struct automaton_test_da_kunit {
	char *state_names[state_max_test_da_kunit];
	char *event_names[event_max_test_da_kunit];
	unsigned char function[state_max_test_da_kunit][event_max_test_da_kunit];
	unsigned char initial_state;
	bool final_states[state_max_test_da_kunit];
};

static const struct automaton_test_da_kunit automaton_test_da_kunit = {
	.state_names = {
		"state_a",
		"state_b",
	},
	.event_names = {
		"event_1",
		"event_2",
	},
	.function = {
		{       state_b_test_da_kunit,       state_a_test_da_kunit },
		{               INVALID_STATE,       state_a_test_da_kunit },
	},
	.initial_state = state_a_test_da_kunit,
	.final_states = { 1, 0 },
};
