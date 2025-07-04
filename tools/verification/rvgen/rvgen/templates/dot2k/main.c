// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rv.h>
#include <rv/instrumentation.h>
#include <rv/da_monitor.h>

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

/*
 * Declare the deterministic automata monitor.
 *
 * The rv monitor reference is needed for the monitor declaration.
 */
static struct rv_monitor rv_%%MODEL_NAME%%;
DECLARE_DA_MON_%%MONITOR_TYPE%%(%%MODEL_NAME%%, %%MIN_TYPE%%);

/*
 * This is the instrumentation part of the monitor.
 *
 * This is the section where manual work is required. Here the kernel events
 * are translated into model's event.
 *
 */
%%TRACEPOINT_HANDLERS_SKEL%%
static int enable_%%MODEL_NAME%%(void)
{
	int retval;

	retval = da_monitor_init_%%MODEL_NAME%%();
	if (retval)
		return retval;

%%TRACEPOINT_ATTACH%%

	return 0;
}

static void disable_%%MODEL_NAME%%(void)
{
	rv_%%MODEL_NAME%%.enabled = 0;

%%TRACEPOINT_DETACH%%

	da_monitor_destroy_%%MODEL_NAME%%();
}

/*
 * This is the monitor register section.
 */
static struct rv_monitor rv_%%MODEL_NAME%% = {
	.name = "%%MODEL_NAME%%",
	.description = "%%DESCRIPTION%%",
	.enable = enable_%%MODEL_NAME%%,
	.disable = disable_%%MODEL_NAME%%,
	.reset = da_monitor_reset_all_%%MODEL_NAME%%,
	.enabled = 0,
};

static int __init register_%%MODEL_NAME%%(void)
{
	rv_register_monitor(&rv_%%MODEL_NAME%%, %%PARENT%%);
	return 0;
}

static void __exit unregister_%%MODEL_NAME%%(void)
{
	rv_unregister_monitor(&rv_%%MODEL_NAME%%);
}

module_init(register_%%MODEL_NAME%%);
module_exit(unregister_%%MODEL_NAME%%);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dot2k: auto-generated");
MODULE_DESCRIPTION("%%MODEL_NAME%%: %%DESCRIPTION%%");
