/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Automatically generated C representation of nrp automaton
 * For further information about this format, see kernel documentation:
 *   Documentation/trace/rv/deterministic_automata.rst
 */

enum states_nrp {
	preempt_irq_nrp = 0,
	any_thread_running_nrp,
	nested_preempt_nrp,
	rescheduling_nrp,
	state_max_nrp
};

#define INVALID_STATE state_max_nrp

enum events_nrp {
	irq_entry_nrp = 0,
	sched_need_resched_nrp,
	schedule_entry_nrp,
	schedule_entry_preempt_nrp,
	event_max_nrp
};

struct automaton_nrp {
	char *state_names[state_max_nrp];
	char *event_names[event_max_nrp];
	unsigned char function[state_max_nrp][event_max_nrp];
	unsigned char initial_state;
	bool final_states[state_max_nrp];
};

static const struct automaton_nrp automaton_nrp = {
	.state_names = {
		"preempt_irq",
		"any_thread_running",
		"nested_preempt",
		"rescheduling"
	},
	.event_names = {
		"irq_entry",
		"sched_need_resched",
		"schedule_entry",
		"schedule_entry_preempt"
	},
	.function = {
		{
			preempt_irq_nrp,
			preempt_irq_nrp,
			nested_preempt_nrp,
			nested_preempt_nrp
		},
		{
			any_thread_running_nrp,
			rescheduling_nrp,
			any_thread_running_nrp,
			INVALID_STATE
		},
		{
			nested_preempt_nrp,
			preempt_irq_nrp,
			any_thread_running_nrp,
			any_thread_running_nrp
		},
		{
			preempt_irq_nrp,
			rescheduling_nrp,
			any_thread_running_nrp,
			any_thread_running_nrp
		},
	},
	.initial_state = preempt_irq_nrp,
	.final_states = { 0, 1, 0, 0 },
};
