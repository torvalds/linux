/*
 * Memory mapped I/O tracing
 *
 * Copyright (C) 2008 Pekka Paalanen <pq@iki.fi>
 */

#define DEBUG 1

#include <linux/kernel.h>
#include <linux/mmiotrace.h>

#include "trace.h"

extern void
__trace_special(void *__tr, void *__data,
		unsigned long arg1, unsigned long arg2, unsigned long arg3);

static struct trace_array *mmio_trace_array;


static void mmio_trace_init(struct trace_array *tr)
{
	pr_debug("in %s\n", __func__);
	mmio_trace_array = tr;
	if (tr->ctrl)
		enable_mmiotrace();
}

static void mmio_trace_reset(struct trace_array *tr)
{
	pr_debug("in %s\n", __func__);
	if (tr->ctrl)
		disable_mmiotrace();
}

static void mmio_trace_ctrl_update(struct trace_array *tr)
{
	pr_debug("in %s\n", __func__);
	if (tr->ctrl)
		enable_mmiotrace();
	else
		disable_mmiotrace();
}

static struct tracer mmio_tracer __read_mostly =
{
	.name		= "mmiotrace",
	.init		= mmio_trace_init,
	.reset		= mmio_trace_reset,
	.ctrl_update	= mmio_trace_ctrl_update,
};

__init static int init_mmio_trace(void)
{
	int ret = init_mmiotrace();
	if (ret)
		return ret;
	return register_tracer(&mmio_tracer);
}
device_initcall(init_mmio_trace);

void mmio_trace_record(u32 type, unsigned long addr, unsigned long arg)
{
	struct trace_array *tr = mmio_trace_array;
	struct trace_array_cpu *data = tr->data[smp_processor_id()];

	if (!current || current->pid == 0) {
		/*
		 * XXX: This is a problem. We need to able to record, no
		 * matter what. tracing_generic_entry_update() would crash.
		 */
		static unsigned limit;
		if (limit++ < 12)
			pr_err("Error in %s: no current.\n", __func__);
		return;
	}
	if (!tr || !data) {
		static unsigned limit;
		if (limit++ < 12)
			pr_err("%s: no tr or data\n", __func__);
		return;
	}
	__trace_special(tr, data, type, addr, arg);
}
