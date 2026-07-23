/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of test_da automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

#define MONITOR_NAME test_da

enum states_test_da {
	state_a_test_da,
	state_b_test_da,
	state_max_test_da,
};

#define INVALID_STATE state_max_test_da

enum events_test_da {
	event_1_test_da,
	event_2_test_da,
	event_max_test_da,
};

struct automaton_test_da {
	char *state_names[state_max_test_da];
	char *event_names[event_max_test_da];
	unsigned char function[state_max_test_da][event_max_test_da];
	unsigned char initial_state;
	bool final_states[state_max_test_da];
};

static const struct automaton_test_da automaton_test_da = {
	.state_names = {
		"state_a",
		"state_b",
	},
	.event_names = {
		"event_1",
		"event_2",
	},
	.function = {
		{       state_b_test_da,       state_a_test_da },
		{         INVALID_STATE,       state_a_test_da },
	},
	.initial_state = state_a_test_da,
	.final_states = { 1, 0 },
};
