/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of test_ha_kunit automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

#define MONITOR_NAME test_ha_kunit

enum states_test_ha_kunit {
	S0_test_ha_kunit,
	S1_test_ha_kunit,
	S2_test_ha_kunit,
	S3_test_ha_kunit,
	state_max_test_ha_kunit,
};

#define INVALID_STATE state_max_test_ha_kunit

enum events_test_ha_kunit {
	event0_test_ha_kunit,
	event1_test_ha_kunit,
	event2_test_ha_kunit,
	event_max_test_ha_kunit,
};

enum envs_test_ha_kunit {
	clk_test_ha_kunit,
	env1_test_ha_kunit,
	env2_test_ha_kunit,
	env_max_test_ha_kunit,
	env_max_stored_test_ha_kunit = env1_test_ha_kunit,
};

_Static_assert(env_max_stored_test_ha_kunit <= MAX_HA_ENV_LEN, "Not enough slots");
#define HA_CLK_NS

struct automaton_test_ha_kunit {
	char *state_names[state_max_test_ha_kunit];
	char *event_names[event_max_test_ha_kunit];
	char *env_names[env_max_test_ha_kunit];
	unsigned char function[state_max_test_ha_kunit][event_max_test_ha_kunit];
	unsigned char initial_state;
	bool final_states[state_max_test_ha_kunit];
};

static const struct automaton_test_ha_kunit automaton_test_ha_kunit = {
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
		{
			S0_test_ha_kunit,
			S1_test_ha_kunit,
			INVALID_STATE,
		},
		{
			S0_test_ha_kunit,
			INVALID_STATE,
			S2_test_ha_kunit,
		},
		{
			INVALID_STATE,
			S2_test_ha_kunit,
			S3_test_ha_kunit,
		},
		{
			S0_test_ha_kunit,
			S1_test_ha_kunit,
			INVALID_STATE,
		},
	},
	.initial_state = S0_test_ha_kunit,
	.final_states = { 1, 0, 0, 0 },
};
