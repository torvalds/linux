// SPDX-License-Identifier: GPL-2.0
/*
 * Fprobe-based tracing events
 * Copyright (C) 2022 Google LLC.
 */
#define pr_fmt(fmt)	"trace_fprobe: " fmt

#include <linux/fprobe.h>
#include <linux/module.h>
#include <linux/rculist.h>
#include <linux/security.h>
#include <linux/tracepoint.h>
#include <linux/uaccess.h>

#include "trace_dynevent.h"
#include "trace_probe.h"
#include "trace_probe_kernel.h"
#include "trace_probe_tmpl.h"

#define FPROBE_EVENT_SYSTEM "fprobes"
#define TRACEPOINT_EVENT_SYSTEM "tracepoints"
#define RETHOOK_MAXACTIVE_MAX 4096

static int trace_fprobe_create(const char *raw_command);
static int trace_fprobe_show(struct seq_file *m, struct dyn_event *ev);
static int trace_fprobe_release(struct dyn_event *ev);
static bool trace_fprobe_is_busy(struct dyn_event *ev);
static bool trace_fprobe_match(const char *system, const char *event,
			int argc, const char **argv, struct dyn_event *ev);

static struct dyn_event_operations trace_fprobe_ops = {
	.create = trace_fprobe_create,
	.show = trace_fprobe_show,
	.is_busy = trace_fprobe_is_busy,
	.free = trace_fprobe_release,
	.match = trace_fprobe_match,
};

/*
 * Fprobe event core functions
 */
struct trace_fprobe {
	struct dyn_event	devent;
	struct fprobe		fp;
	const char		*symbol;
	struct tracepoint	*tpoint;
	struct module		*mod;
	struct trace_probe	tp;
};

static bool is_trace_fprobe(struct dyn_event *ev)
{
	return ev->ops == &trace_fprobe_ops;
}

static struct trace_fprobe *to_trace_fprobe(struct dyn_event *ev)
{
	return container_of(ev, struct trace_fprobe, devent);
}

/**
 * for_each_trace_fprobe - iterate over the trace_fprobe list
 * @pos:	the struct trace_fprobe * for each entry
 * @dpos:	the struct dyn_event * to use as a loop cursor
 */
#define for_each_trace_fprobe(pos, dpos)	\
	for_each_dyn_event(dpos)		\
		if (is_trace_fprobe(dpos) && (pos = to_trace_fprobe(dpos)))

static bool trace_fprobe_is_return(struct trace_fprobe *tf)
{
	return tf->fp.exit_handler != NULL;
}

static bool trace_fprobe_is_tracepoint(struct trace_fprobe *tf)
{
	return tf->tpoint != NULL;
}

static const char *trace_fprobe_symbol(struct trace_fprobe *tf)
{
	return tf->symbol ? tf->symbol : "unknown";
}

static bool trace_fprobe_is_busy(struct dyn_event *ev)
{
	struct trace_fprobe *tf = to_trace_fprobe(ev);

	return trace_probe_is_enabled(&tf->tp);
}

static bool trace_fprobe_match_command_head(struct trace_fprobe *tf,
					    int argc, const char **argv)
{
	char buf[MAX_ARGSTR_LEN + 1];

	if (!argc)
		return true;

	snprintf(buf, sizeof(buf), "%s", trace_fprobe_symbol(tf));
	if (strcmp(buf, argv[0]))
		return false;
	argc--; argv++;

	return trace_probe_match_command_args(&tf->tp, argc, argv);
}

static bool trace_fprobe_match(const char *system, const char *event,
			int argc, const char **argv, struct dyn_event *ev)
{
	struct trace_fprobe *tf = to_trace_fprobe(ev);

	if (event[0] != '\0' && strcmp(trace_probe_name(&tf->tp), event))
		return false;

	if (system && strcmp(trace_probe_group_name(&tf->tp), system))
		return false;

	return trace_fprobe_match_command_head(tf, argc, argv);
}

static bool trace_fprobe_is_registered(struct trace_fprobe *tf)
{
	return fprobe_is_registered(&tf->fp);
}

/*
 * Note that we don't verify the fetch_insn code, since it does not come
 * from user space.
 */
static int
process_fetch_insn(struct fetch_insn *code, void *rec, void *dest,
		   void *base)
{
	struct pt_regs *regs = rec;
	unsigned long val;
	int ret;

retry:
	/* 1st stage: get value from context */
	switch (code->op) {
	case FETCH_OP_STACK:
		val = regs_get_kernel_stack_nth(regs, code->param);
		break;
	case FETCH_OP_STACKP:
		val = kernel_stack_pointer(regs);
		break;
	case FETCH_OP_RETVAL:
		val = regs_return_value(regs);
		break;
#ifdef CONFIG_HAVE_FUNCTION_ARG_ACCESS_API
	case FETCH_OP_ARG:
		val = regs_get_kernel_argument(regs, code->param);
		break;
#endif
	case FETCH_NOP_SYMBOL:	/* Ignore a place holder */
		code++;
		goto retry;
	default:
		ret = process_common_fetch_insn(code, &val);
		if (ret < 0)
			return ret;
	}
	code++;

	return process_fetch_insn_bottom(code, val, dest, base);
}
NOKPROBE_SYMBOL(process_fetch_insn)

/* function entry handler */
static nokprobe_inline void
__fentry_trace_func(struct trace_fprobe *tf, unsigned long entry_ip,
		    struct pt_regs *regs,
		    struct trace_event_file *trace_file)
{
	struct fentry_trace_entry_head *entry;
	struct trace_event_call *call = trace_probe_event_call(&tf->tp);
	struct trace_event_buffer fbuffer;
	int dsize;

	if (WARN_ON_ONCE(call != trace_file->event_call))
		return;

	if (trace_trigger_soft_disabled(trace_file))
		return;

	dsize = __get_data_size(&tf->tp, regs);

	entry = trace_event_buffer_reserve(&fbuffer, trace_file,
					   sizeof(*entry) + tf->tp.size + dsize);
	if (!entry)
		return;

	fbuffer.regs = regs;
	entry = fbuffer.entry = ring_buffer_event_data(fbuffer.event);
	entry->ip = entry_ip;
	store_trace_args(&entry[1], &tf->tp, regs, sizeof(*entry), dsize);

	trace_event_buffer_commit(&fbuffer);
}

static void
fentry_trace_func(struct trace_fprobe *tf, unsigned long entry_ip,
		  struct pt_regs *regs)
{
	struct event_file_link *link;

	trace_probe_for_each_link_rcu(link, &tf->tp)
		__fentry_trace_func(tf, entry_ip, regs, link->file);
}
NOKPROBE_SYMBOL(fentry_trace_func);

/* Kretprobe handler */
static nokprobe_inline void
__fexit_trace_func(struct trace_fprobe *tf, unsigned long entry_ip,
		   unsigned long ret_ip, struct pt_regs *regs,
		   struct trace_event_file *trace_file)
{
	struct fexit_trace_entry_head *entry;
	struct trace_event_buffer fbuffer;
	struct trace_event_call *call = trace_probe_event_call(&tf->tp);
	int dsize;

	if (WARN_ON_ONCE(call != trace_file->event_call))
		return;

	if (trace_trigger_soft_disabled(trace_file))
		return;

	dsize = __get_data_size(&tf->tp, regs);

	entry = trace_event_buffer_reserve(&fbuffer, trace_file,
					   sizeof(*entry) + tf->tp.size + dsize);
	if (!entry)
		return;

	fbuffer.regs = regs;
	entry = fbuffer.entry = ring_buffer_event_data(fbuffer.event);
	entry->func = entry_ip;
	entry->ret_ip = ret_ip;
	store_trace_args(&entry[1], &tf->tp, regs, sizeof(*entry), dsize);

	trace_event_buffer_commit(&fbuffer);
}

static void
fexit_trace_func(struct trace_fprobe *tf, unsigned long entry_ip,
		 unsigned long ret_ip, struct pt_regs *regs)
{
	struct event_file_link *link;

	trace_probe_for_each_link_rcu(link, &tf->tp)
		__fexit_trace_func(tf, entry_ip, ret_ip, regs, link->file);
}
NOKPROBE_SYMBOL(fexit_trace_func);

#ifdef CONFIG_PERF_EVENTS

static int fentry_perf_func(struct trace_fprobe *tf, unsigned long entry_ip,
			    struct pt_regs *regs)
{
	struct trace_event_call *call = trace_probe_event_call(&tf->tp);
	struct fentry_trace_entry_head *entry;
	struct hlist_head *head;
	int size, __size, dsize;
	int rctx;

	head = this_cpu_ptr(call->perf_events);
	if (hlist_empty(head))
		return 0;

	dsize = __get_data_size(&tf->tp, regs);
	__size = sizeof(*entry) + tf->tp.size + dsize;
	size = ALIGN(__size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	entry = perf_trace_buf_alloc(size, NULL, &rctx);
	if (!entry)
		return 0;

	entry->ip = entry_ip;
	memset(&entry[1], 0, dsize);
	store_trace_args(&entry[1], &tf->tp, regs, sizeof(*entry), dsize);
	perf_trace_buf_submit(entry, size, rctx, call->event.type, 1, regs,
			      head, NULL);
	return 0;
}
NOKPROBE_SYMBOL(fentry_perf_func);

static void
fexit_perf_func(struct trace_fprobe *tf, unsigned long entry_ip,
		unsigned long ret_ip, struct pt_regs *regs)
{
	struct trace_event_call *call = trace_probe_event_call(&tf->tp);
	struct fexit_trace_entry_head *entry;
	struct hlist_head *head;
	int size, __size, dsize;
	int rctx;

	head = this_cpu_ptr(call->perf_events);
	if (hlist_empty(head))
		return;

	dsize = __get_data_size(&tf->tp, regs);
	__size = sizeof(*entry) + tf->tp.size + dsize;
	size = ALIGN(__size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	entry = perf_trace_buf_alloc(size, NULL, &rctx);
	if (!entry)
		return;

	entry->func = entry_ip;
	entry->ret_ip = ret_ip;
	store_trace_args(&entry[1], &tf->tp, regs, sizeof(*entry), dsize);
	perf_trace_buf_submit(entry, size, rctx, call->event.type, 1, regs,
			      head, NULL);
}
NOKPROBE_SYMBOL(fexit_perf_func);
#endif	/* CONFIG_PERF_EVENTS */

static int fentry_dispatcher(struct fprobe *fp, unsigned long entry_ip,
			     unsigned long ret_ip, struct pt_regs *regs,
			     void *entry_data)
{
	struct trace_fprobe *tf = container_of(fp, struct trace_fprobe, fp);
	int ret = 0;

	if (trace_probe_test_flag(&tf->tp, TP_FLAG_TRACE))
		fentry_trace_func(tf, entry_ip, regs);
#ifdef CONFIG_PERF_EVENTS
	if (trace_probe_test_flag(&tf->tp, TP_FLAG_PROFILE))
		ret = fentry_perf_func(tf, entry_ip, regs);
#endif
	return ret;
}
NOKPROBE_SYMBOL(fentry_dispatcher);

static void fexit_dispatcher(struct fprobe *fp, unsigned long entry_ip,
			     unsigned long ret_ip, struct pt_regs *regs,
			     void *entry_data)
{
	struct trace_fprobe *tf = container_of(fp, struct trace_fprobe, fp);

	if (trace_probe_test_flag(&tf->tp, TP_FLAG_TRACE))
		fexit_trace_func(tf, entry_ip, ret_ip, regs);
#ifdef CONFIG_PERF_EVENTS
	if (trace_probe_test_flag(&tf->tp, TP_FLAG_PROFILE))
		fexit_perf_func(tf, entry_ip, ret_ip, regs);
#endif
}
NOKPROBE_SYMBOL(fexit_dispatcher);

static void free_trace_fprobe(struct trace_fprobe *tf)
{
	if (tf) {
		trace_probe_cleanup(&tf->tp);
		kfree(tf->symbol);
		kfree(tf);
	}
}

/*
 * Allocate new trace_probe and initialize it (including fprobe).
 */
static struct trace_fprobe *alloc_trace_fprobe(const char *group,
					       const char *event,
					       const char *symbol,
					       struct tracepoint *tpoint,
					       int maxactive,
					       int nargs, bool is_return)
{
	struct trace_fprobe *tf;
	int ret = -ENOMEM;

	tf = kzalloc(struct_size(tf, tp.args, nargs), GFP_KERNEL);
	if (!tf)
		return ERR_PTR(ret);

	tf->symbol = kstrdup(symbol, GFP_KERNEL);
	if (!tf->symbol)
		goto error;

	if (is_return)
		tf->fp.exit_handler = fexit_dispatcher;
	else
		tf->fp.entry_handler = fentry_dispatcher;

	tf->tpoint = tpoint;
	tf->fp.nr_maxactive = maxactive;

	ret = trace_probe_init(&tf->tp, event, group, false);
	if (ret < 0)
		goto error;

	dyn_event_init(&tf->devent, &trace_fprobe_ops);
	return tf;
error:
	free_trace_fprobe(tf);
	return ERR_PTR(ret);
}

static struct trace_fprobe *find_trace_fprobe(const char *event,
					      const char *group)
{
	struct dyn_event *pos;
	struct trace_fprobe *tf;

	for_each_trace_fprobe(tf, pos)
		if (strcmp(trace_probe_name(&tf->tp), event) == 0 &&
		    strcmp(trace_probe_group_name(&tf->tp), group) == 0)
			return tf;
	return NULL;
}

static inline int __enable_trace_fprobe(struct trace_fprobe *tf)
{
	if (trace_fprobe_is_registered(tf))
		enable_fprobe(&tf->fp);

	return 0;
}

static void __disable_trace_fprobe(struct trace_probe *tp)
{
	struct trace_fprobe *tf;

	list_for_each_entry(tf, trace_probe_probe_list(tp), tp.list) {
		if (!trace_fprobe_is_registered(tf))
			continue;
		disable_fprobe(&tf->fp);
	}
}

/*
 * Enable trace_probe
 * if the file is NULL, enable "perf" handler, or enable "trace" handler.
 */
static int enable_trace_fprobe(struct trace_event_call *call,
			       struct trace_event_file *file)
{
	struct trace_probe *tp;
	struct trace_fprobe *tf;
	bool enabled;
	int ret = 0;

	tp = trace_probe_primary_from_call(call);
	if (WARN_ON_ONCE(!tp))
		return -ENODEV;
	enabled = trace_probe_is_enabled(tp);

	/* This also changes "enabled" state */
	if (file) {
		ret = trace_probe_add_file(tp, file);
		if (ret)
			return ret;
	} else
		trace_probe_set_flag(tp, TP_FLAG_PROFILE);

	if (!enabled) {
		list_for_each_entry(tf, trace_probe_probe_list(tp), tp.list) {
			/* TODO: check the fprobe is gone */
			__enable_trace_fprobe(tf);
		}
	}

	return 0;
}

/*
 * Disable trace_probe
 * if the file is NULL, disable "perf" handler, or disable "trace" handler.
 */
static int disable_trace_fprobe(struct trace_event_call *call,
				struct trace_event_file *file)
{
	struct trace_probe *tp;

	tp = trace_probe_primary_from_call(call);
	if (WARN_ON_ONCE(!tp))
		return -ENODEV;

	if (file) {
		if (!trace_probe_get_file_link(tp, file))
			return -ENOENT;
		if (!trace_probe_has_single_file(tp))
			goto out;
		trace_probe_clear_flag(tp, TP_FLAG_TRACE);
	} else
		trace_probe_clear_flag(tp, TP_FLAG_PROFILE);

	if (!trace_probe_is_enabled(tp))
		__disable_trace_fprobe(tp);

 out:
	if (file)
		/*
		 * Synchronization is done in below function. For perf event,
		 * file == NULL and perf_trace_event_unreg() calls
		 * tracepoint_synchronize_unregister() to ensure synchronize
		 * event. We don't need to care about it.
		 */
		trace_probe_remove_file(tp, file);

	return 0;
}

/* Event entry printers */
static enum print_line_t
print_fentry_event(struct trace_iterator *iter, int flags,
		   struct trace_event *event)
{
	struct fentry_trace_entry_head *field;
	struct trace_seq *s = &iter->seq;
	struct trace_probe *tp;

	field = (struct fentry_trace_entry_head *)iter->ent;
	tp = trace_probe_primary_from_call(
		container_of(event, struct trace_event_call, event));
	if (WARN_ON_ONCE(!tp))
		goto out;

	trace_seq_printf(s, "%s: (", trace_probe_name(tp));

	if (!seq_print_ip_sym(s, field->ip, flags | TRACE_ITER_SYM_OFFSET))
		goto out;

	trace_seq_putc(s, ')');

	if (trace_probe_print_args(s, tp->args, tp->nr_args,
			     (u8 *)&field[1], field) < 0)
		goto out;

	trace_seq_putc(s, '\n');
 out:
	return trace_handle_return(s);
}

static enum print_line_t
print_fexit_event(struct trace_iterator *iter, int flags,
		  struct trace_event *event)
{
	struct fexit_trace_entry_head *field;
	struct trace_seq *s = &iter->seq;
	struct trace_probe *tp;

	field = (struct fexit_trace_entry_head *)iter->ent;
	tp = trace_probe_primary_from_call(
		container_of(event, struct trace_event_call, event));
	if (WARN_ON_ONCE(!tp))
		goto out;

	trace_seq_printf(s, "%s: (", trace_probe_name(tp));

	if (!seq_print_ip_sym(s, field->ret_ip, flags | TRACE_ITER_SYM_OFFSET))
		goto out;

	trace_seq_puts(s, " <- ");

	if (!seq_print_ip_sym(s, field->func, flags & ~TRACE_ITER_SYM_OFFSET))
		goto out;

	trace_seq_putc(s, ')');

	if (trace_probe_print_args(s, tp->args, tp->nr_args,
			     (u8 *)&field[1], field) < 0)
		goto out;

	trace_seq_putc(s, '\n');

 out:
	return trace_handle_return(s);
}

static int fentry_event_define_fields(struct trace_event_call *event_call)
{
	int ret;
	struct fentry_trace_entry_head field;
	struct trace_probe *tp;

	tp = trace_probe_primary_from_call(event_call);
	if (WARN_ON_ONCE(!tp))
		return -ENOENT;

	DEFINE_FIELD(unsigned long, ip, FIELD_STRING_IP, 0);

	return traceprobe_define_arg_fields(event_call, sizeof(field), tp);
}

static int fexit_event_define_fields(struct trace_event_call *event_call)
{
	int ret;
	struct fexit_trace_entry_head field;
	struct trace_probe *tp;

	tp = trace_probe_primary_from_call(event_call);
	if (WARN_ON_ONCE(!tp))
		return -ENOENT;

	DEFINE_FIELD(unsigned long, func, FIELD_STRING_FUNC, 0);
	DEFINE_FIELD(unsigned long, ret_ip, FIELD_STRING_RETIP, 0);

	return traceprobe_define_arg_fields(event_call, sizeof(field), tp);
}

static struct trace_event_functions fentry_funcs = {
	.trace		= print_fentry_event
};

static struct trace_event_functions fexit_funcs = {
	.trace		= print_fexit_event
};

static struct trace_event_fields fentry_fields_array[] = {
	{ .type = TRACE_FUNCTION_TYPE,
	  .define_fields = fentry_event_define_fields },
	{}
};

static struct trace_event_fields fexit_fields_array[] = {
	{ .type = TRACE_FUNCTION_TYPE,
	  .define_fields = fexit_event_define_fields },
	{}
};

static int fprobe_register(struct trace_event_call *event,
			   enum trace_reg type, void *data);

static inline void init_trace_event_call(struct trace_fprobe *tf)
{
	struct trace_event_call *call = trace_probe_event_call(&tf->tp);

	if (trace_fprobe_is_return(tf)) {
		call->event.funcs = &fexit_funcs;
		call->class->fields_array = fexit_fields_array;
	} else {
		call->event.funcs = &fentry_funcs;
		call->class->fields_array = fentry_fields_array;
	}

	call->flags = TRACE_EVENT_FL_FPROBE;
	call->class->reg = fprobe_register;
}

static int register_fprobe_event(struct trace_fprobe *tf)
{
	init_trace_event_call(tf);

	return trace_probe_register_event_call(&tf->tp);
}

static int unregister_fprobe_event(struct trace_fprobe *tf)
{
	return trace_probe_unregister_event_call(&tf->tp);
}

/* Internal register function - just handle fprobe and flags */
static int __register_trace_fprobe(struct trace_fprobe *tf)
{
	int i, ret;

	/* Should we need new LOCKDOWN flag for fprobe? */
	ret = security_locked_down(LOCKDOWN_KPROBES);
	if (ret)
		return ret;

	if (trace_fprobe_is_registered(tf))
		return -EINVAL;

	for (i = 0; i < tf->tp.nr_args; i++) {
		ret = traceprobe_update_arg(&tf->tp.args[i]);
		if (ret)
			return ret;
	}

	/* Set/clear disabled flag according to tp->flag */
	if (trace_probe_is_enabled(&tf->tp))
		tf->fp.flags &= ~FPROBE_FL_DISABLED;
	else
		tf->fp.flags |= FPROBE_FL_DISABLED;

	if (trace_fprobe_is_tracepoint(tf)) {
		struct tracepoint *tpoint = tf->tpoint;
		unsigned long ip = (unsigned long)tpoint->probestub;
		/*
		 * Here, we do 2 steps to enable fprobe on a tracepoint.
		 * At first, put __probestub_##TP function on the tracepoint
		 * and put a fprobe on the stub function.
		 */
		ret = tracepoint_probe_register_prio_may_exist(tpoint,
					tpoint->probestub, NULL, 0);
		if (ret < 0)
			return ret;
		return register_fprobe_ips(&tf->fp, &ip, 1);
	}

	/* TODO: handle filter, nofilter or symbol list */
	return register_fprobe(&tf->fp, tf->symbol, NULL);
}

/* Internal unregister function - just handle fprobe and flags */
static void __unregister_trace_fprobe(struct trace_fprobe *tf)
{
	if (trace_fprobe_is_registered(tf)) {
		unregister_fprobe(&tf->fp);
		memset(&tf->fp, 0, sizeof(tf->fp));
		if (trace_fprobe_is_tracepoint(tf)) {
			tracepoint_probe_unregister(tf->tpoint,
					tf->tpoint->probestub, NULL);
			tf->tpoint = NULL;
			tf->mod = NULL;
		}
	}
}

/* TODO: make this trace_*probe common function */
/* Unregister a trace_probe and probe_event */
static int unregister_trace_fprobe(struct trace_fprobe *tf)
{
	/* If other probes are on the event, just unregister fprobe */
	if (trace_probe_has_sibling(&tf->tp))
		goto unreg;

	/* Enabled event can not be unregistered */
	if (trace_probe_is_enabled(&tf->tp))
		return -EBUSY;

	/* If there's a reference to the dynamic event */
	if (trace_event_dyn_busy(trace_probe_event_call(&tf->tp)))
		return -EBUSY;

	/* Will fail if probe is being used by ftrace or perf */
	if (unregister_fprobe_event(tf))
		return -EBUSY;

unreg:
	__unregister_trace_fprobe(tf);
	dyn_event_remove(&tf->devent);
	trace_probe_unlink(&tf->tp);

	return 0;
}

static bool trace_fprobe_has_same_fprobe(struct trace_fprobe *orig,
					 struct trace_fprobe *comp)
{
	struct trace_probe_event *tpe = orig->tp.event;
	int i;

	list_for_each_entry(orig, &tpe->probes, tp.list) {
		if (strcmp(trace_fprobe_symbol(orig),
			   trace_fprobe_symbol(comp)))
			continue;

		/*
		 * trace_probe_compare_arg_type() ensured that nr_args and
		 * each argument name and type are same. Let's compare comm.
		 */
		for (i = 0; i < orig->tp.nr_args; i++) {
			if (strcmp(orig->tp.args[i].comm,
				   comp->tp.args[i].comm))
				break;
		}

		if (i == orig->tp.nr_args)
			return true;
	}

	return false;
}

static int append_trace_fprobe(struct trace_fprobe *tf, struct trace_fprobe *to)
{
	int ret;

	if (trace_fprobe_is_return(tf) != trace_fprobe_is_return(to) ||
	    trace_fprobe_is_tracepoint(tf) != trace_fprobe_is_tracepoint(to)) {
		trace_probe_log_set_index(0);
		trace_probe_log_err(0, DIFF_PROBE_TYPE);
		return -EEXIST;
	}
	ret = trace_probe_compare_arg_type(&tf->tp, &to->tp);
	if (ret) {
		/* Note that argument starts index = 2 */
		trace_probe_log_set_index(ret + 1);
		trace_probe_log_err(0, DIFF_ARG_TYPE);
		return -EEXIST;
	}
	if (trace_fprobe_has_same_fprobe(to, tf)) {
		trace_probe_log_set_index(0);
		trace_probe_log_err(0, SAME_PROBE);
		return -EEXIST;
	}

	/* Append to existing event */
	ret = trace_probe_append(&tf->tp, &to->tp);
	if (ret)
		return ret;

	ret = __register_trace_fprobe(tf);
	if (ret)
		trace_probe_unlink(&tf->tp);
	else
		dyn_event_add(&tf->devent, trace_probe_event_call(&tf->tp));

	return ret;
}

/* Register a trace_probe and probe_event */
static int register_trace_fprobe(struct trace_fprobe *tf)
{
	struct trace_fprobe *old_tf;
	int ret;

	mutex_lock(&event_mutex);

	old_tf = find_trace_fprobe(trace_probe_name(&tf->tp),
				   trace_probe_group_name(&tf->tp));
	if (old_tf) {
		ret = append_trace_fprobe(tf, old_tf);
		goto end;
	}

	/* Register new event */
	ret = register_fprobe_event(tf);
	if (ret) {
		if (ret == -EEXIST) {
			trace_probe_log_set_index(0);
			trace_probe_log_err(0, EVENT_EXIST);
		} else
			pr_warn("Failed to register probe event(%d)\n", ret);
		goto end;
	}

	/* Register fprobe */
	ret = __register_trace_fprobe(tf);
	if (ret < 0)
		unregister_fprobe_event(tf);
	else
		dyn_event_add(&tf->devent, trace_probe_event_call(&tf->tp));

end:
	mutex_unlock(&event_mutex);
	return ret;
}

#ifdef CONFIG_MODULES
static int __tracepoint_probe_module_cb(struct notifier_block *self,
					unsigned long val, void *data)
{
	struct tp_module *tp_mod = data;
	struct trace_fprobe *tf;
	struct dyn_event *pos;

	if (val != MODULE_STATE_GOING)
		return NOTIFY_DONE;

	mutex_lock(&event_mutex);
	for_each_trace_fprobe(tf, pos) {
		if (tp_mod->mod == tf->mod) {
			tracepoint_probe_unregister(tf->tpoint,
					tf->tpoint->probestub, NULL);
			tf->tpoint = NULL;
			tf->mod = NULL;
		}
	}
	mutex_unlock(&event_mutex);

	return NOTIFY_DONE;
}

static struct notifier_block tracepoint_module_nb = {
	.notifier_call = __tracepoint_probe_module_cb,
};
#endif /* CONFIG_MODULES */

struct __find_tracepoint_cb_data {
	const char *tp_name;
	struct tracepoint *tpoint;
};

static void __find_tracepoint_cb(struct tracepoint *tp, void *priv)
{
	struct __find_tracepoint_cb_data *data = priv;

	if (!data->tpoint && !strcmp(data->tp_name, tp->name))
		data->tpoint = tp;
}

static struct tracepoint *find_tracepoint(const char *tp_name)
{
	struct __find_tracepoint_cb_data data = {
		.tp_name = tp_name,
	};

	for_each_kernel_tracepoint(__find_tracepoint_cb, &data);

	return data.tpoint;
}

static int __trace_fprobe_create(int argc, const char *argv[])
{
	/*
	 * Argument syntax:
	 *  - Add fentry probe:
	 *      f[:[GRP/][EVENT]] [MOD:]KSYM [FETCHARGS]
	 *  - Add fexit probe:
	 *      f[N][:[GRP/][EVENT]] [MOD:]KSYM%return [FETCHARGS]
	 *  - Add tracepoint probe:
	 *      t[:[GRP/][EVENT]] TRACEPOINT [FETCHARGS]
	 *
	 * Fetch args:
	 *  $retval	: fetch return value
	 *  $stack	: fetch stack address
	 *  $stackN	: fetch Nth entry of stack (N:0-)
	 *  $argN	: fetch Nth argument (N:1-)
	 *  $comm       : fetch current task comm
	 *  @ADDR	: fetch memory at ADDR (ADDR should be in kernel)
	 *  @SYM[+|-offs] : fetch memory at SYM +|- offs (SYM is a data symbol)
	 * Dereferencing memory fetch:
	 *  +|-offs(ARG) : fetch memory at ARG +|- offs address.
	 * Alias name of args:
	 *  NAME=FETCHARG : set NAME as alias of FETCHARG.
	 * Type of args:
	 *  FETCHARG:TYPE : use TYPE instead of unsigned long.
	 */
	struct trace_fprobe *tf = NULL;
	int i, len, new_argc = 0, ret = 0;
	bool is_return = false;
	char *symbol = NULL, *tmp = NULL;
	const char *event = NULL, *group = FPROBE_EVENT_SYSTEM;
	const char **new_argv = NULL;
	int maxactive = 0;
	char buf[MAX_EVENT_NAME_LEN];
	char gbuf[MAX_EVENT_NAME_LEN];
	char sbuf[KSYM_NAME_LEN];
	char abuf[MAX_BTF_ARGS_LEN];
	bool is_tracepoint = false;
	struct tracepoint *tpoint = NULL;
	struct traceprobe_parse_context ctx = {
		.flags = TPARG_FL_KERNEL | TPARG_FL_FPROBE,
	};

	if ((argv[0][0] != 'f' && argv[0][0] != 't') || argc < 2)
		return -ECANCELED;

	if (argv[0][0] == 't') {
		is_tracepoint = true;
		group = TRACEPOINT_EVENT_SYSTEM;
	}

	trace_probe_log_init("trace_fprobe", argc, argv);

	event = strchr(&argv[0][1], ':');
	if (event)
		event++;

	if (isdigit(argv[0][1])) {
		if (event)
			len = event - &argv[0][1] - 1;
		else
			len = strlen(&argv[0][1]);
		if (len > MAX_EVENT_NAME_LEN - 1) {
			trace_probe_log_err(1, BAD_MAXACT);
			goto parse_error;
		}
		memcpy(buf, &argv[0][1], len);
		buf[len] = '\0';
		ret = kstrtouint(buf, 0, &maxactive);
		if (ret || !maxactive) {
			trace_probe_log_err(1, BAD_MAXACT);
			goto parse_error;
		}
		/* fprobe rethook instances are iterated over via a list. The
		 * maximum should stay reasonable.
		 */
		if (maxactive > RETHOOK_MAXACTIVE_MAX) {
			trace_probe_log_err(1, MAXACT_TOO_BIG);
			goto parse_error;
		}
	}

	trace_probe_log_set_index(1);

	/* a symbol(or tracepoint) must be specified */
	symbol = kstrdup(argv[1], GFP_KERNEL);
	if (!symbol)
		return -ENOMEM;

	tmp = strchr(symbol, '%');
	if (tmp) {
		if (!is_tracepoint && !strcmp(tmp, "%return")) {
			*tmp = '\0';
			is_return = true;
		} else {
			trace_probe_log_err(tmp - symbol, BAD_ADDR_SUFFIX);
			goto parse_error;
		}
	}
	if (!is_return && maxactive) {
		trace_probe_log_set_index(0);
		trace_probe_log_err(1, BAD_MAXACT_TYPE);
		goto parse_error;
	}

	trace_probe_log_set_index(0);
	if (event) {
		ret = traceprobe_parse_event_name(&event, &group, gbuf,
						  event - argv[0]);
		if (ret)
			goto parse_error;
	}

	if (!event) {
		/* Make a new event name */
		if (is_tracepoint)
			snprintf(buf, MAX_EVENT_NAME_LEN, "%s%s",
				 isdigit(*symbol) ? "_" : "", symbol);
		else
			snprintf(buf, MAX_EVENT_NAME_LEN, "%s__%s", symbol,
				 is_return ? "exit" : "entry");
		sanitize_event_name(buf);
		event = buf;
	}

	if (is_return)
		ctx.flags |= TPARG_FL_RETURN;
	else
		ctx.flags |= TPARG_FL_FENTRY;

	if (is_tracepoint) {
		ctx.flags |= TPARG_FL_TPOINT;
		tpoint = find_tracepoint(symbol);
		if (!tpoint) {
			trace_probe_log_set_index(1);
			trace_probe_log_err(0, NO_TRACEPOINT);
			goto parse_error;
		}
		ctx.funcname = kallsyms_lookup(
				(unsigned long)tpoint->probestub,
				NULL, NULL, NULL, sbuf);
	} else
		ctx.funcname = symbol;

	argc -= 2; argv += 2;
	new_argv = traceprobe_expand_meta_args(argc, argv, &new_argc,
					       abuf, MAX_BTF_ARGS_LEN, &ctx);
	if (IS_ERR(new_argv)) {
		ret = PTR_ERR(new_argv);
		new_argv = NULL;
		goto out;
	}
	if (new_argv) {
		argc = new_argc;
		argv = new_argv;
	}

	/* setup a probe */
	tf = alloc_trace_fprobe(group, event, symbol, tpoint, maxactive,
				argc, is_return);
	if (IS_ERR(tf)) {
		ret = PTR_ERR(tf);
		/* This must return -ENOMEM, else there is a bug */
		WARN_ON_ONCE(ret != -ENOMEM);
		goto out;	/* We know tf is not allocated */
	}

	if (is_tracepoint)
		tf->mod = __module_text_address(
				(unsigned long)tf->tpoint->probestub);

	/* parse arguments */
	for (i = 0; i < argc && i < MAX_TRACE_ARGS; i++) {
		trace_probe_log_set_index(i + 2);
		ctx.offset = 0;
		ret = traceprobe_parse_probe_arg(&tf->tp, i, argv[i], &ctx);
		if (ret)
			goto error;	/* This can be -ENOMEM */
	}

	ret = traceprobe_set_print_fmt(&tf->tp,
			is_return ? PROBE_PRINT_RETURN : PROBE_PRINT_NORMAL);
	if (ret < 0)
		goto error;

	ret = register_trace_fprobe(tf);
	if (ret) {
		trace_probe_log_set_index(1);
		if (ret == -EILSEQ)
			trace_probe_log_err(0, BAD_INSN_BNDRY);
		else if (ret == -ENOENT)
			trace_probe_log_err(0, BAD_PROBE_ADDR);
		else if (ret != -ENOMEM && ret != -EEXIST)
			trace_probe_log_err(0, FAIL_REG_PROBE);
		goto error;
	}

out:
	trace_probe_log_clear();
	kfree(new_argv);
	kfree(symbol);
	return ret;

parse_error:
	ret = -EINVAL;
error:
	free_trace_fprobe(tf);
	goto out;
}

static int trace_fprobe_create(const char *raw_command)
{
	return trace_probe_create(raw_command, __trace_fprobe_create);
}

static int trace_fprobe_release(struct dyn_event *ev)
{
	struct trace_fprobe *tf = to_trace_fprobe(ev);
	int ret = unregister_trace_fprobe(tf);

	if (!ret)
		free_trace_fprobe(tf);
	return ret;
}

static int trace_fprobe_show(struct seq_file *m, struct dyn_event *ev)
{
	struct trace_fprobe *tf = to_trace_fprobe(ev);
	int i;

	if (trace_fprobe_is_tracepoint(tf))
		seq_putc(m, 't');
	else
		seq_putc(m, 'f');
	if (trace_fprobe_is_return(tf) && tf->fp.nr_maxactive)
		seq_printf(m, "%d", tf->fp.nr_maxactive);
	seq_printf(m, ":%s/%s", trace_probe_group_name(&tf->tp),
				trace_probe_name(&tf->tp));

	seq_printf(m, " %s%s", trace_fprobe_symbol(tf),
			       trace_fprobe_is_return(tf) ? "%return" : "");

	for (i = 0; i < tf->tp.nr_args; i++)
		seq_printf(m, " %s=%s", tf->tp.args[i].name, tf->tp.args[i].comm);
	seq_putc(m, '\n');

	return 0;
}

/*
 * called by perf_trace_init() or __ftrace_set_clr_event() under event_mutex.
 */
static int fprobe_register(struct trace_event_call *event,
			   enum trace_reg type, void *data)
{
	struct trace_event_file *file = data;

	switch (type) {
	case TRACE_REG_REGISTER:
		return enable_trace_fprobe(event, file);
	case TRACE_REG_UNREGISTER:
		return disable_trace_fprobe(event, file);

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return enable_trace_fprobe(event, NULL);
	case TRACE_REG_PERF_UNREGISTER:
		return disable_trace_fprobe(event, NULL);
	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		return 0;
#endif
	}
	return 0;
}

/*
 * Register dynevent at core_initcall. This allows kernel to setup fprobe
 * events in postcore_initcall without tracefs.
 */
static __init int init_fprobe_trace_early(void)
{
	int ret;

	ret = dyn_event_register(&trace_fprobe_ops);
	if (ret)
		return ret;

#ifdef CONFIG_MODULES
	ret = register_tracepoint_module_notifier(&tracepoint_module_nb);
	if (ret)
		return ret;
#endif

	return 0;
}
core_initcall(init_fprobe_trace_early);
