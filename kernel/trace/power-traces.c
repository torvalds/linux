/*
 * Power trace points
 *
 * Copyright (C) 2009 Arjan van de Ven <arjan@linux.intel.com>
 */

#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/power.h>

#ifdef EVENT_POWER_TRACING_DEPRECATED
EXPORT_TRACEPOINT_SYMBOL_GPL(power_start);
#endif
EXPORT_TRACEPOINT_SYMBOL_GPL(cpu_idle);

