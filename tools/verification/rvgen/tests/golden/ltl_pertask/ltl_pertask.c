// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "ltl_pertask"

/*
 * XXX: include required tracepoint headers, e.g.,
 * #include <trace/events/sched.h>
 */
#include <rv_trace.h>


/*
 * This is the self-generated part of the monitor. Generally, there is no need
 * to touch this section.
 */
#include "ltl_pertask.h"
#include <rv/ltl_monitor.h>

static void ltl_atoms_fetch(struct task_struct *task, struct ltl_monitor *mon)
{
	/*
	 * This is called everytime the Buchi automaton is triggered.
	 *
	 * This function could be used to fetch the atomic propositions which
	 * are expensive to trace. It is possible only if the atomic proposition
	 * does not need to be updated at precise time.
	 *
	 * It is recommended to use tracepoints and ltl_atom_update() instead.
	 */
}

static void ltl_atoms_init(struct task_struct *task, struct ltl_monitor *mon, bool task_creation)
{
	/*
	 * This should initialize as many atomic propositions as possible.
	 *
	 * @task_creation indicates whether the task is being created. This is
	 * false if the task is already running before the monitor is enabled.
	 */
	ltl_atom_set(mon, LTL_EVENT_A, true/false);
	ltl_atom_set(mon, LTL_EVENT_B, true/false);
}

/*
 * This is the instrumentation part of the monitor.
 *
 * This is the section where manual work is required. Here the kernel events
 * are translated into model's event.
 */
static void handle_example_event(void *data, /* XXX: fill header */)
{
	ltl_atom_update(task, LTL_EVENT_A, true/false);
}

static int enable_ltl_pertask(void)
{
	int retval;

	retval = ltl_monitor_init();
	if (retval)
		return retval;

	rv_attach_trace_probe("ltl_pertask", /* XXX: tracepoint */, handle_example_event);

	return 0;
}

static void disable_ltl_pertask(void)
{
	rv_detach_trace_probe("ltl_pertask", /* XXX: tracepoint */, handle_example_event);

	ltl_monitor_destroy();
}

/*
 * This is the monitor register section.
 */
static struct rv_monitor rv_this = {
	.name = "ltl_pertask",
	.description = "auto-generated",
	.enable = enable_ltl_pertask,
	.disable = disable_ltl_pertask,
};

static int __init register_ltl_pertask(void)
{
	return rv_register_monitor(&rv_this, NULL);
}

static void __exit unregister_ltl_pertask(void)
{
	rv_unregister_monitor(&rv_this);
}

module_init(register_ltl_pertask);
module_exit(unregister_ltl_pertask);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rvgen: auto-generated");
MODULE_DESCRIPTION("ltl_pertask: auto-generated");
