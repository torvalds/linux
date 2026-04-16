/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of nomiss automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

#define MONITOR_NAME nomiss

enum states_nomiss {
	ready_nomiss,
	idle_nomiss,
	running_nomiss,
	sleeping_nomiss,
	throttled_nomiss,
	state_max_nomiss,
};

#define INVALID_STATE state_max_nomiss

enum events_nomiss {
	dl_replenish_nomiss,
	dl_server_idle_nomiss,
	dl_server_stop_nomiss,
	dl_throttle_nomiss,
	sched_switch_in_nomiss,
	sched_switch_suspend_nomiss,
	sched_wakeup_nomiss,
	event_max_nomiss,
};

enum envs_nomiss {
	clk_nomiss,
	is_constr_dl_nomiss,
	is_defer_nomiss,
	env_max_nomiss,
	env_max_stored_nomiss = is_constr_dl_nomiss,
};

_Static_assert(env_max_stored_nomiss <= MAX_HA_ENV_LEN, "Not enough slots");
#define HA_CLK_NS

struct automaton_nomiss {
	char *state_names[state_max_nomiss];
	char *event_names[event_max_nomiss];
	char *env_names[env_max_nomiss];
	unsigned char function[state_max_nomiss][event_max_nomiss];
	unsigned char initial_state;
	bool final_states[state_max_nomiss];
};

static const struct automaton_nomiss automaton_nomiss = {
	.state_names = {
		"ready",
		"idle",
		"running",
		"sleeping",
		"throttled",
	},
	.event_names = {
		"dl_replenish",
		"dl_server_idle",
		"dl_server_stop",
		"dl_throttle",
		"sched_switch_in",
		"sched_switch_suspend",
		"sched_wakeup",
	},
	.env_names = {
		"clk",
		"is_constr_dl",
		"is_defer",
	},
	.function = {
		{
			ready_nomiss,
			idle_nomiss,
			sleeping_nomiss,
			throttled_nomiss,
			running_nomiss,
			INVALID_STATE,
			ready_nomiss,
		},
		{
			ready_nomiss,
			idle_nomiss,
			sleeping_nomiss,
			throttled_nomiss,
			running_nomiss,
			INVALID_STATE,
			INVALID_STATE,
		},
		{
			running_nomiss,
			idle_nomiss,
			sleeping_nomiss,
			throttled_nomiss,
			running_nomiss,
			sleeping_nomiss,
			running_nomiss,
		},
		{
			ready_nomiss,
			sleeping_nomiss,
			sleeping_nomiss,
			throttled_nomiss,
			running_nomiss,
			INVALID_STATE,
			ready_nomiss,
		},
		{
			ready_nomiss,
			throttled_nomiss,
			INVALID_STATE,
			throttled_nomiss,
			INVALID_STATE,
			throttled_nomiss,
			throttled_nomiss,
		},
	},
	.initial_state = ready_nomiss,
	.final_states = { 1, 0, 0, 0, 0 },
};
