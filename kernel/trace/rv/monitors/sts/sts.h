/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of sts automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

enum states_sts {
	can_sched_sts = 0,
	cant_sched_sts,
	disable_to_switch_sts,
	enable_to_exit_sts,
	in_irq_sts,
	scheduling_sts,
	switching_sts,
	state_max_sts
};

#define INVALID_STATE state_max_sts

enum events_sts {
	irq_disable_sts = 0,
	irq_enable_sts,
	irq_entry_sts,
	sched_switch_sts,
	schedule_entry_sts,
	schedule_exit_sts,
	event_max_sts
};

struct automaton_sts {
	char *state_names[state_max_sts];
	char *event_names[event_max_sts];
	unsigned char function[state_max_sts][event_max_sts];
	unsigned char initial_state;
	bool final_states[state_max_sts];
};

static const struct automaton_sts automaton_sts = {
	.state_names = {
		"can_sched",
		"cant_sched",
		"disable_to_switch",
		"enable_to_exit",
		"in_irq",
		"scheduling",
		"switching"
	},
	.event_names = {
		"irq_disable",
		"irq_enable",
		"irq_entry",
		"sched_switch",
		"schedule_entry",
		"schedule_exit"
	},
	.function = {
		{
			cant_sched_sts,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
			scheduling_sts,
			INVALID_STATE
		},
		{
			INVALID_STATE,
			can_sched_sts,
			cant_sched_sts,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE
		},
		{
			INVALID_STATE,
			enable_to_exit_sts,
			in_irq_sts,
			switching_sts,
			INVALID_STATE,
			INVALID_STATE
		},
		{
			enable_to_exit_sts,
			enable_to_exit_sts,
			enable_to_exit_sts,
			INVALID_STATE,
			INVALID_STATE,
			can_sched_sts
		},
		{
			INVALID_STATE,
			scheduling_sts,
			in_irq_sts,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE
		},
		{
			disable_to_switch_sts,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE
		},
		{
			INVALID_STATE,
			enable_to_exit_sts,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE,
			INVALID_STATE
		},
	},
	.initial_state = can_sched_sts,
	.final_states = { 1, 0, 0, 0, 0, 0, 0 },
};
