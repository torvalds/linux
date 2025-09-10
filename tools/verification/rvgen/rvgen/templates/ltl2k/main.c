// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>

#define MODULE_NAME "%%MODEL_NAME%%"

/*
 * XXX: include required tracepoint headers, e.g.,
 * #include <trace/events/sched.h>
 */
#include <rv_trace.h>
%%INCLUDE_PARENT%%

/*
 * This is the self-generated part of the monitor. Generally, there is no need
 * to touch this section.
 */
#include "%%MODEL_NAME%%.h"
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
%%ATOMS_INIT%%
}

/*
 * This is the instrumentation part of the monitor.
 *
 * This is the section where manual work is required. Here the kernel events
 * are translated into model's event.
 */
%%TRACEPOINT_HANDLERS_SKEL%%
static int enable_%%MODEL_NAME%%(void)
{
	int retval;

	retval = ltl_monitor_init();
	if (retval)
		return retval;

%%TRACEPOINT_ATTACH%%

	return 0;
}

static void disable_%%MODEL_NAME%%(void)
{
%%TRACEPOINT_DETACH%%

	ltl_monitor_destroy();
}

/*
 * This is the monitor register section.
 */
static struct rv_monitor rv_%%MODEL_NAME%% = {
	.name = "%%MODEL_NAME%%",
	.description = "%%DESCRIPTION%%",
	.enable = enable_%%MODEL_NAME%%,
	.disable = disable_%%MODEL_NAME%%,
};

static int __init register_%%MODEL_NAME%%(void)
{
	return rv_register_monitor(&rv_%%MODEL_NAME%%, %%PARENT%%);
}

static void __exit unregister_%%MODEL_NAME%%(void)
{
	rv_unregister_monitor(&rv_%%MODEL_NAME%%);
}

module_init(register_%%MODEL_NAME%%);
module_exit(unregister_%%MODEL_NAME%%);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(/* TODO */);
MODULE_DESCRIPTION("%%MODEL_NAME%%: %%DESCRIPTION%%");
