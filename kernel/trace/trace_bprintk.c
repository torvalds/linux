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

#ifdef CONFIG_MODULES

/* binary printk basic */
static DEFINE_MUTEX(btrace_mutex);
/*
 * modules trace_bprintk()'s formats are autosaved in struct trace_bprintk_fmt
 * which are queued on trace_bprintk_fmt_list.
 */
static LIST_HEAD(trace_bprintk_fmt_list);

struct trace_bprintk_fmt {
	struct list_head list;
	char fmt[0];
};


static inline void lock_btrace(void)
{
	mutex_lock(&btrace_mutex);
}

static inline void unlock_btrace(void)
{
	mutex_unlock(&btrace_mutex);
}


static inline struct trace_bprintk_fmt *lookup_format(const char *fmt)
{
	struct trace_bprintk_fmt *pos;
	list_for_each_entry(pos, &trace_bprintk_fmt_list, list) {
		if (!strcmp(pos->fmt, fmt))
			return pos;
	}
	return NULL;
}

static
void hold_module_trace_bprintk_format(const char **start, const char **end)
{
	const char **iter;
	lock_btrace();
	for (iter = start; iter < end; iter++) {
		struct trace_bprintk_fmt *tb_fmt = lookup_format(*iter);
		if (tb_fmt) {
			*iter = tb_fmt->fmt;
			continue;
		}

		tb_fmt = kmalloc(offsetof(struct trace_bprintk_fmt, fmt)
				+ strlen(*iter) + 1, GFP_KERNEL);
		if (tb_fmt) {
			list_add_tail(&tb_fmt->list, &trace_bprintk_fmt_list);
			strcpy(tb_fmt->fmt, *iter);
			*iter = tb_fmt->fmt;
		} else
			*iter = NULL;
	}
	unlock_btrace();
}

static int module_trace_bprintk_format_notify(struct notifier_block *self,
		unsigned long val, void *data)
{
	struct module *mod = data;
	if (mod->num_trace_bprintk_fmt) {
		const char **start = mod->trace_bprintk_fmt_start;
		const char **end = start + mod->num_trace_bprintk_fmt;

		if (val == MODULE_STATE_COMING)
			hold_module_trace_bprintk_format(start, end);
	}
	return 0;
}

#else /* !CONFIG_MODULES */
__init static int
module_trace_bprintk_format_notify(struct notifier_block *self,
		unsigned long val, void *data)
{
	return 0;
}
#endif /* CONFIG_MODULES */


__initdata_or_module static
struct notifier_block module_trace_bprintk_format_nb = {
	.notifier_call = module_trace_bprintk_format_notify,
};

/* events tracer */
int trace_bprintk_enable;

static void start_bprintk_trace(struct trace_array *tr)
{
	tracing_reset_online_cpus(tr);
	trace_bprintk_enable = 1;
}

static void stop_bprintk_trace(struct trace_array *tr)
{
	trace_bprintk_enable = 0;
	tracing_reset_online_cpus(tr);
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
	int ret = register_module_notifier(&module_trace_bprintk_format_nb);
	if (ret)
		return ret;

	ret = register_tracer(&bprintk_trace);
	if (ret)
		unregister_module_notifier(&module_trace_bprintk_format_nb);
	return ret;
}

device_initcall(init_bprintk);
