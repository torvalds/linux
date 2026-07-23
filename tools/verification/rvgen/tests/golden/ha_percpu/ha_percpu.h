/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of ha_percpu automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

#define MONITOR_NAME ha_percpu

enum states_ha_percpu {
	S0_ha_percpu,
	S1_ha_percpu,
	S2_ha_percpu,
	S3_ha_percpu,
	state_max_ha_percpu,
};

#define INVALID_STATE state_max_ha_percpu

enum events_ha_percpu {
	event0_ha_percpu,
	event1_ha_percpu,
	event2_ha_percpu,
	event_max_ha_percpu,
};

enum envs_ha_percpu {
	clk_ha_percpu,
	env1_ha_percpu,
	env2_ha_percpu,
	env_max_ha_percpu,
	env_max_stored_ha_percpu = env1_ha_percpu,
};

_Static_assert(env_max_stored_ha_percpu <= MAX_HA_ENV_LEN, "Not enough slots");
#define HA_CLK_NS

struct automaton_ha_percpu {
	char *state_names[state_max_ha_percpu];
	char *event_names[event_max_ha_percpu];
	char *env_names[env_max_ha_percpu];
	unsigned char function[state_max_ha_percpu][event_max_ha_percpu];
	unsigned char initial_state;
	bool final_states[state_max_ha_percpu];
};

static const struct automaton_ha_percpu automaton_ha_percpu = {
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
		{            S0_ha_percpu,            S1_ha_percpu,           INVALID_STATE },
		{            S0_ha_percpu,           INVALID_STATE,            S2_ha_percpu },
		{           INVALID_STATE,            S2_ha_percpu,            S3_ha_percpu },
		{            S0_ha_percpu,            S1_ha_percpu,           INVALID_STATE },
	},
	.initial_state = S0_ha_percpu,
	.final_states = { 1, 0, 0, 0 },
};
