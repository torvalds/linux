// SPDX-License-Identifier: GPL-2.0
/*
 * event tracer
 *
 * Copyright (C) 2022 Google Inc, Steven Rostedt <rostedt@goodmis.org>
 */

#define pr_fmt(fmt) fmt

#include <linux/trace_events.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <trace/events/sched.h>

#define THIS_SYSTEM "custom_sched"

#define SCHED_PRINT_FMT							\
	C("prev_prio=%d next_pid=%d next_prio=%d", REC->prev_prio, REC->next_pid, \
	  REC->next_prio)

#define SCHED_WAKING_FMT				\
	C("pid=%d prio=%d", REC->pid, REC->prio)

#undef C
#define C(a, b...) a, b

static struct trace_event_fields sched_switch_fields[] = {
	{
		.type = "unsigned short",
		.name = "prev_prio",
		.size = sizeof(short),
		.align = __alignof__(short),
		.is_signed = 0,
		.filter_type = FILTER_OTHER,
	},
	{
		.type = "unsigned short",
		.name = "next_prio",
		.size = sizeof(short),
		.align = __alignof__(short),
		.is_signed = 0,
		.filter_type = FILTER_OTHER,
	},
	{
		.type = "unsigned int",
		.name = "next_prio",
		.size = sizeof(int),
		.align = __alignof__(int),
		.is_signed = 0,
		.filter_type = FILTER_OTHER,
	},
	{}
};

struct sched_event {
	struct trace_entry	ent;
	unsigned short		prev_prio;
	unsigned short		next_prio;
	unsigned int		next_pid;
};

static struct trace_event_fields sched_waking_fields[] = {
	{
		.type = "unsigned int",
		.name = "pid",
		.size = sizeof(int),
		.align = __alignof__(int),
		.is_signed = 0,
		.filter_type = FILTER_OTHER,
	},
	{
		.type = "unsigned short",
		.name = "prio",
		.size = sizeof(short),
		.align = __alignof__(short),
		.is_signed = 0,
		.filter_type = FILTER_OTHER,
	},
	{}
};

struct wake_event {
	struct trace_entry	ent;
	unsigned int		pid;
	unsigned short		prio;
};

static void sched_switch_probe(void *data, bool preempt, struct task_struct *prev,
			       struct task_struct *next)
{
	struct trace_event_file *trace_file = data;
	struct trace_event_buffer fbuffer;
	struct sched_event *entry;

	if (trace_trigger_soft_disabled(trace_file))
		return;

	entry = trace_event_buffer_reserve(&fbuffer, trace_file,
					   sizeof(*entry));

	if (!entry)
		return;

	entry->prev_prio = prev->prio;
	entry->next_prio = next->prio;
	entry->next_pid = next->pid;

	trace_event_buffer_commit(&fbuffer);
}

static struct trace_event_class sched_switch_class = {
	.system			= THIS_SYSTEM,
	.reg			= trace_event_reg,
	.fields_array		= sched_switch_fields,
	.fields			= LIST_HEAD_INIT(sched_switch_class.fields),
	.probe			= sched_switch_probe,
};

static void sched_waking_probe(void *data, struct task_struct *t)
{
	struct trace_event_file *trace_file = data;
	struct trace_event_buffer fbuffer;
	struct wake_event *entry;

	if (trace_trigger_soft_disabled(trace_file))
		return;

	entry = trace_event_buffer_reserve(&fbuffer, trace_file,
					   sizeof(*entry));

	if (!entry)
		return;

	entry->prio = t->prio;
	entry->pid = t->pid;

	trace_event_buffer_commit(&fbuffer);
}

static struct trace_event_class sched_waking_class = {
	.system			= THIS_SYSTEM,
	.reg			= trace_event_reg,
	.fields_array		= sched_waking_fields,
	.fields			= LIST_HEAD_INIT(sched_waking_class.fields),
	.probe			= sched_waking_probe,
};

static enum print_line_t sched_switch_output(struct trace_iterator *iter, int flags,
					     struct trace_event *trace_event)
{
	struct trace_seq *s = &iter->seq;
	struct sched_event *REC = (struct sched_event *)iter->ent;
	int ret;

	ret = trace_raw_output_prep(iter, trace_event);
	if (ret != TRACE_TYPE_HANDLED)
		return ret;

	trace_seq_printf(s, SCHED_PRINT_FMT);
	trace_seq_putc(s, '\n');

	return trace_handle_return(s);
}

static struct trace_event_functions sched_switch_funcs = {
	.trace			= sched_switch_output,
};

static enum print_line_t sched_waking_output(struct trace_iterator *iter, int flags,
					     struct trace_event *trace_event)
{
	struct trace_seq *s = &iter->seq;
	struct wake_event *REC = (struct wake_event *)iter->ent;
	int ret;

	ret = trace_raw_output_prep(iter, trace_event);
	if (ret != TRACE_TYPE_HANDLED)
		return ret;

	trace_seq_printf(s, SCHED_WAKING_FMT);
	trace_seq_putc(s, '\n');

	return trace_handle_return(s);
}

static struct trace_event_functions sched_waking_funcs = {
	.trace			= sched_waking_output,
};

#undef C
#define C(a, b...) #a "," __stringify(b)

static struct trace_event_call sched_switch_call = {
	.class			= &sched_switch_class,
	.event			= {
		.funcs			= &sched_switch_funcs,
	},
	.print_fmt		= SCHED_PRINT_FMT,
	.module			= THIS_MODULE,
	.flags			= TRACE_EVENT_FL_TRACEPOINT,
};

static struct trace_event_call sched_waking_call = {
	.class			= &sched_waking_class,
	.event			= {
		.funcs			= &sched_waking_funcs,
	},
	.print_fmt		= SCHED_WAKING_FMT,
	.module			= THIS_MODULE,
	.flags			= TRACE_EVENT_FL_TRACEPOINT,
};

static void fct(struct tracepoint *tp, void *priv)
{
	if (tp->name && strcmp(tp->name, "sched_switch") == 0)
		sched_switch_call.tp = tp;
	else if (tp->name && strcmp(tp->name, "sched_waking") == 0)
		sched_waking_call.tp = tp;
}

static int add_event(struct trace_event_call *call)
{
	int ret;

	ret = register_trace_event(&call->event);
	if (WARN_ON(!ret))
		return -ENODEV;

	ret = trace_add_event_call(call);
	if (WARN_ON(ret))
		unregister_trace_event(&call->event);

	return ret;
}

static int __init trace_sched_init(void)
{
	int ret;

	check_trace_callback_type_sched_switch(sched_switch_probe);
	check_trace_callback_type_sched_waking(sched_waking_probe);

	for_each_kernel_tracepoint(fct, NULL);

	ret = add_event(&sched_switch_call);
	if (ret)
		return ret;

	ret = add_event(&sched_waking_call);
	if (ret)
		trace_remove_event_call(&sched_switch_call);

	return ret;
}

static void __exit trace_sched_exit(void)
{
	trace_set_clr_event(THIS_SYSTEM, "sched_switch", 0);
	trace_set_clr_event(THIS_SYSTEM, "sched_waking", 0);

	trace_remove_event_call(&sched_switch_call);
	trace_remove_event_call(&sched_waking_call);
}

module_init(trace_sched_init);
module_exit(trace_sched_exit);

MODULE_AUTHOR("Steven Rostedt");
MODULE_DESCRIPTION("Custom scheduling events");
MODULE_LICENSE("GPL");
