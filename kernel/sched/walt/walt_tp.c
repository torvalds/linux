// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/tracepoint.h>
#include <trace/hooks/sched.h>
#include "trace.h"

unsigned int sysctl_sched_dynamic_tp_enable;

static void sched_overutilized(void *data, struct root_domain *rd,
				 bool overutilized)
{
	if (trace_sched_overutilized_enabled()) {
		char span[SPAN_SIZE];

		cpumap_print_to_pagebuf(false, span, sched_trace_rd_span(rd));
		trace_sched_overutilized(overutilized, span);
	}
}

static void walt_register_dynamic_tp_events(void)
{
	register_trace_sched_overutilized_tp(sched_overutilized, NULL);
}

static void walt_unregister_dynamic_tp_events(void)
{
	unregister_trace_sched_overutilized_tp(sched_overutilized, NULL);
}

int sched_dynamic_tp_handler(struct ctl_table *table, int write,
			void __user *buffer, size_t *lenp, loff_t *ppos)
{
	static DEFINE_MUTEX(mutex);
	int ret = 0, *val = (unsigned int *)table->data;
	unsigned int old_val;

	mutex_lock(&mutex);
	old_val = sysctl_sched_dynamic_tp_enable;

	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (ret || !write || (old_val == sysctl_sched_dynamic_tp_enable))
		goto done;

	if (*val)
		walt_register_dynamic_tp_events();
	else
		walt_unregister_dynamic_tp_events();
done:
	mutex_unlock(&mutex);
	return ret;
}
