/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of test_ha automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

#define MONITOR_NAME test_ha

enum states_test_ha {
	S0_test_ha,
	S1_test_ha,
	S2_test_ha,
	S3_test_ha,
	state_max_test_ha,
};

#define INVALID_STATE state_max_test_ha

enum events_test_ha {
	event0_test_ha,
	event1_test_ha,
	event2_test_ha,
	event_max_test_ha,
};

enum envs_test_ha {
	clk_test_ha,
	env1_test_ha,
	env2_test_ha,
	env_max_test_ha,
	env_max_stored_test_ha = env1_test_ha,
};

_Static_assert(env_max_stored_test_ha <= MAX_HA_ENV_LEN, "Not enough slots");
#define HA_CLK_NS

struct automaton_test_ha {
	char *state_names[state_max_test_ha];
	char *event_names[event_max_test_ha];
	char *env_names[env_max_test_ha];
	unsigned char function[state_max_test_ha][event_max_test_ha];
	unsigned char initial_state;
	bool final_states[state_max_test_ha];
};

static const struct automaton_test_ha automaton_test_ha = {
	.state_names = {
		"S0",
		"S1",
		"S2",
		"S3",
	},
	.event_names = {
		"event0",
		"event1",
		"event2",
	},
	.env_names = {
		"clk",
		"env1",
		"env2",
	},
	.function = {
		{            S0_test_ha,            S1_test_ha,         INVALID_STATE },
		{            S0_test_ha,         INVALID_STATE,            S2_test_ha },
		{         INVALID_STATE,            S2_test_ha,            S3_test_ha },
		{            S0_test_ha,            S1_test_ha,         INVALID_STATE },
	},
	.initial_state = S0_test_ha,
	.final_states = { 1, 0, 0, 0 },
};
