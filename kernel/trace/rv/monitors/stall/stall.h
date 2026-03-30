/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of stall automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

#define MONITOR_NAME stall

enum states_stall {
	dequeued_stall,
	enqueued_stall,
	running_stall,
	state_max_stall,
};

#define INVALID_STATE state_max_stall

enum events_stall {
	sched_switch_in_stall,
	sched_switch_preempt_stall,
	sched_switch_wait_stall,
	sched_wakeup_stall,
	event_max_stall,
};

enum envs_stall {
	clk_stall,
	env_max_stall,
	env_max_stored_stall = env_max_stall,
};

_Static_assert(env_max_stored_stall <= MAX_HA_ENV_LEN, "Not enough slots");

struct automaton_stall {
	char *state_names[state_max_stall];
	char *event_names[event_max_stall];
	char *env_names[env_max_stall];
	unsigned char function[state_max_stall][event_max_stall];
	unsigned char initial_state;
	bool final_states[state_max_stall];
};

static const struct automaton_stall automaton_stall = {
	.state_names = {
		"dequeued",
		"enqueued",
		"running",
	},
	.event_names = {
		"sched_switch_in",
		"sched_switch_preempt",
		"sched_switch_wait",
		"sched_wakeup",
	},
	.env_names = {
		"clk",
	},
	.function = {
		{
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
			enqueued_stall,
		},
		{
			running_stall,
			INVALID_STATE,
			INVALID_STATE,
			enqueued_stall,
		},
		{
			running_stall,
			enqueued_stall,
			dequeued_stall,
			running_stall,
		},
	},
	.initial_state = dequeued_stall,
	.final_states = { 1, 0, 0 },
};
