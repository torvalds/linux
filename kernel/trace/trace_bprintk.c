/*
 * trace binary printk
 *
 * Copyright (C) 2008 Lai Jiangshan <laijs@cn.fujitsu.com>
 *
 */
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/marker.h>
#include <linux/uaccess.h>

#include "trace.h"

/* binary printk basic */
static DEFINE_MUTEX(btrace_mutex);
static int btrace_metadata_count;

static inline void lock_btrace(void)
{
	mutex_lock(&btrace_mutex);
}

static inline void unlock_btrace(void)
{
	mutex_unlock(&btrace_mutex);
}

static void get_btrace_metadata(void)
{
	lock_btrace();
	btrace_metadata_count++;
	unlock_btrace();
}

static void put_btrace_metadata(void)
{
	lock_btrace();
	btrace_metadata_count--;
	unlock_btrace();
}

/* events tracer */
int trace_bprintk_enable;

static void start_bprintk_trace(struct trace_array *tr)
{
	get_btrace_metadata();
	tracing_reset_online_cpus(tr);
	trace_bprintk_enable = 1;
}

static void stop_bprintk_trace(struct trace_array *tr)
{
	trace_bprintk_enable = 0;
	tracing_reset_online_cpus(tr);
	put_btrace_metadata();
}

static int init_bprintk_trace(struct trace_array *tr)
{
	start_bprintk_trace(tr);
	return 0;
}

static struct tracer bprintk_trace __read_mostly =
{
	.name	     = "events",
	.init	     = init_bprintk_trace,
	.reset	     = stop_bprintk_trace,
	.start	     = start_bprintk_trace,
	.stop	     = stop_bprintk_trace,
};

static __init int init_bprintk(void)
{
	return register_tracer(&bprintk_trace);
}

device_initcall(init_bprintk);
