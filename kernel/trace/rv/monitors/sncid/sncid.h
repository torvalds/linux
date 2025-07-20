/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of sncid automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

enum states_sncid {
	can_sched_sncid = 0,
	cant_sched_sncid,
	state_max_sncid
};

#define INVALID_STATE state_max_sncid

enum events_sncid {
	irq_disable_sncid = 0,
	irq_enable_sncid,
	schedule_entry_sncid,
	schedule_exit_sncid,
	event_max_sncid
};

struct automaton_sncid {
	char *state_names[state_max_sncid];
	char *event_names[event_max_sncid];
	unsigned char function[state_max_sncid][event_max_sncid];
	unsigned char initial_state;
	bool final_states[state_max_sncid];
};

static const struct automaton_sncid automaton_sncid = {
	.state_names = {
		"can_sched",
		"cant_sched"
	},
	.event_names = {
		"irq_disable",
		"irq_enable",
		"schedule_entry",
		"schedule_exit"
	},
	.function = {
		{ cant_sched_sncid,   INVALID_STATE, can_sched_sncid, can_sched_sncid },
		{    INVALID_STATE, can_sched_sncid,   INVALID_STATE,   INVALID_STATE },
	},
	.initial_state = can_sched_sncid,
	.final_states = { 1, 0 },
};
