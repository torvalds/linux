/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of opid automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

#define MONITOR_NAME opid

enum states_opid {
	any_opid,
	state_max_opid,
};

#define INVALID_STATE state_max_opid

enum events_opid {
	sched_need_resched_opid,
	sched_waking_opid,
	event_max_opid,
};

enum envs_opid {
	irq_off_opid,
	preempt_off_opid,
	env_max_opid,
	env_max_stored_opid = irq_off_opid,
};

_Static_assert(env_max_stored_opid <= MAX_HA_ENV_LEN, "Not enough slots");

struct automaton_opid {
	char *state_names[state_max_opid];
	char *event_names[event_max_opid];
	char *env_names[env_max_opid];
	unsigned char function[state_max_opid][event_max_opid];
	unsigned char initial_state;
	bool final_states[state_max_opid];
};

static const struct automaton_opid automaton_opid = {
	.state_names = {
		"any",
	},
	.event_names = {
		"sched_need_resched",
		"sched_waking",
	},
	.env_names = {
		"irq_off",
		"preempt_off",
	},
	.function = {
		{           any_opid,           any_opid },
	},
	.initial_state = any_opid,
	.final_states = { 1 },
};
