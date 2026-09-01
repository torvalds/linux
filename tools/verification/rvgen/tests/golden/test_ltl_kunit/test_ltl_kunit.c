// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "test_ltl_kunit"

/*
 * XXX: include required tracepoint headers, e.g.,
 * #include <trace/events/sched.h>
 */
#include <rv_trace.h>


/*
 * This is the self-generated part of the monitor. Generally, there is no need
 * to touch this section.
 */
#include "test_ltl_kunit.h"
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

static int enable_test_ltl_kunit(void)
{
	int retval;

	retval = ltl_monitor_init();
	if (retval)
		return retval;

	rv_attach_trace_probe("test_ltl_kunit", /* XXX: tracepoint */, handle_example_event);

	return 0;
}

static void disable_test_ltl_kunit(void)
{
	rv_detach_trace_probe("test_ltl_kunit", /* XXX: tracepoint */, handle_example_event);

	ltl_monitor_destroy();
}

/*
 * This is the monitor register section.
 */
static struct rv_monitor rv_this = {
	.name = "test_ltl_kunit",
	.description = "auto-generated",
	.enable = enable_test_ltl_kunit,
	.disable = disable_test_ltl_kunit,
};

static int __init register_test_ltl_kunit(void)
{
	return rv_register_monitor(&rv_this, NULL);
}

static void __exit unregister_test_ltl_kunit(void)
{
	rv_unregister_monitor(&rv_this);
}

module_init(register_test_ltl_kunit);
module_exit(unregister_test_ltl_kunit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rvgen: auto-generated");
MODULE_DESCRIPTION("test_ltl_kunit: auto-generated");
