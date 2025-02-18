/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of wip automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

enum states_wip {
	preemptive_wip = 0,
	non_preemptive_wip,
	state_max_wip
};

#define INVALID_STATE state_max_wip

enum events_wip {
	preempt_disable_wip = 0,
	preempt_enable_wip,
	sched_waking_wip,
	event_max_wip
};

struct automaton_wip {
	char *state_names[state_max_wip];
	char *event_names[event_max_wip];
	unsigned char function[state_max_wip][event_max_wip];
	unsigned char initial_state;
	bool final_states[state_max_wip];
};

static const struct automaton_wip automaton_wip = {
	.state_names = {
		"preemptive",
		"non_preemptive"
	},
	.event_names = {
		"preempt_disable",
		"preempt_enable",
		"sched_waking"
	},
	.function = {
		{ non_preemptive_wip,      INVALID_STATE,      INVALID_STATE },
		{      INVALID_STATE,     preemptive_wip, non_preemptive_wip },
	},
	.initial_state = preemptive_wip,
	.final_states = { 1, 0 },
};
