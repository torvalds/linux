/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of scpd automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

enum states_scpd {
	cant_sched_scpd = 0,
	can_sched_scpd,
	state_max_scpd
};

#define INVALID_STATE state_max_scpd

enum events_scpd {
	preempt_disable_scpd = 0,
	preempt_enable_scpd,
	schedule_entry_scpd,
	schedule_exit_scpd,
	event_max_scpd
};

struct automaton_scpd {
	char *state_names[state_max_scpd];
	char *event_names[event_max_scpd];
	unsigned char function[state_max_scpd][event_max_scpd];
	unsigned char initial_state;
	bool final_states[state_max_scpd];
};

static const struct automaton_scpd automaton_scpd = {
	.state_names = {
		"cant_sched",
		"can_sched"
	},
	.event_names = {
		"preempt_disable",
		"preempt_enable",
		"schedule_entry",
		"schedule_exit"
	},
	.function = {
		{     can_sched_scpd,     INVALID_STATE,     INVALID_STATE,     INVALID_STATE },
		{     INVALID_STATE,    cant_sched_scpd,     can_sched_scpd,     can_sched_scpd },
	},
	.initial_state = cant_sched_scpd,
	.final_states = { 1, 0 },
};
