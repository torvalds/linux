// SPDX-License-Identifier: GPL-2.0
/*
 * Fprobe-based tracing events
 * Copyright (C) 2022 Google LLC.
 */
#define pr_fmt(fmt)	"trace_fprobe: " fmt

#include <linux/fprobe.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/security.h>
#include <linux/tracepoint.h>
#include <linux/uaccess.h>

#include <asm/ptrace.h>

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

/* List of tracepoint_user */
static LIST_HEAD(tracepoint_user_list);
static DEFINE_MUTEX(tracepoint_user_mutex);

/* While living tracepoint_user, @tpoint can be NULL and @refcount != 0. */
struct tracepoint_user {
	struct list_head	list;
	const char		*name;
	struct tracepoint	*tpoint;
	unsigned int		refcount;
};

/* NOTE: you must lock tracepoint_user_mutex. */
#define for_each_tracepoint_user(tuser)		\
	list_for_each_entry(tuser, &tracepoint_user_list, list)

static int tracepoint_user_register(struct tracepoint_user *tuser)
{
	struct tracepoint *tpoint = tuser->tpoint;

	if (!tpoint)
		return 0;

	return tracepoint_probe_register_prio_may_exist(tpoint,
					tpoint->probestub, NULL, 0);
}

static void tracepoint_user_unregister(struct tracepoint_user *tuser)
{
	if (!tuser->tpoint)
		return;

	WARN_ON_ONCE(tracepoint_probe_unregister(tuser->tpoint, tuser->tpoint->probestub, NULL));
	tuser->tpoint = NULL;
}

static unsigned long tracepoint_user_ip(struct tracepoint_user *tuser)
{
	if (!tuser->tpoint)
		return 0UL;

	return (unsigned long)tuser->tpoint->probestub;
}

static void __tracepoint_user_free(struct tracepoint_user *tuser)
{
	if (!tuser)
		return;
	kfree(tuser->name);
	kfree(tuser);
}

DEFINE_FREE(tuser_free, struct tracepoint_user *, __tracepoint_user_free(_T))

static struct tracepoint_user *__tracepoint_user_init(const char *name, struct tracepoint *tpoint)
{
	struct tracepoint_user *tuser __free(tuser_free) = NULL;
	int ret;

	tuser = kzalloc(sizeof(*tuser), GFP_KERNEL);
	if (!tuser)
		return NULL;
	tuser->name = kstrdup(name, GFP_KERNEL);
	if (!tuser->name)
		return NULL;

	if (tpoint) {
		ret = tracepoint_user_register(tuser);
		if (ret)
			return ERR_PTR(ret);
	}

	tuser->tpoint = tpoint;
	tuser->refcount = 1;
	INIT_LIST_HEAD(&tuser->list);
	list_add(&tuser->list, &tracepoint_user_list);

	return_ptr(tuser);
}

static struct tracepoint *find_tracepoint(const char *tp_name,
	struct module **tp_mod);

/*
 * Get tracepoint_user if exist, or allocate new one and register it.
 * If tracepoint is on a module, get its refcounter too.
 * This returns errno or NULL (not loaded yet) or tracepoint_user.
 */
static struct tracepoint_user *tracepoint_user_find_get(const char *name, struct module **pmod)
{
	struct module *mod __free(module_put) = NULL;
	struct tracepoint_user *tuser;
	struct tracepoint *tpoint;

	if (!name || !pmod)
		return ERR_PTR(-EINVAL);

	/* Get and lock the module which has tracepoint. */
	tpoint = find_tracepoint(name, &mod);

	guard(mutex)(&tracepoint_user_mutex);
	/* Search existing tracepoint_user */
	for_each_tracepoint_user(tuser) {
		if (!strcmp(tuser->name, name)) {
			tuser->refcount++;
			*pmod = no_free_ptr(mod);
			return tuser;
		}
	}

	/* The corresponding tracepoint_user is not found. */
	tuser = __tracepoint_user_init(name, tpoint);
	if (!IS_ERR_OR_NULL(tuser))
		*pmod = no_free_ptr(mod);

	return tuser;
}

static void tracepoint_user_put(struct tracepoint_user *tuser)
{
	scoped_guard(mutex, &tracepoint_user_mutex) {
		if (--tuser->refcount > 0)
			return;

		list_del(&tuser->list);
		tracepoint_user_unregister(tuser);
	}

	__tracepoint_user_free(tuser);
}

DEFINE_FREE(tuser_put, struct tracepoint_user *,
	if (!IS_ERR_OR_NULL(_T))
		tracepoint_user_put(_T))

/*
 * Fprobe event core functions
 */

/*
 * @tprobe is true for tracepoint probe.
 * @tuser can be NULL if the trace_fprobe is disabled or the tracepoint is not
 * loaded with a module. If @tuser != NULL, this trace_fprobe is enabled.
 */
struct trace_fprobe {
	struct dyn_event	devent;
	struct fprobe		fp;
	const char		*symbol;
	bool			tprobe;
	struct tracepoint_user	*tuser;
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
	return tf->tprobe;
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
process_fetch_insn(struct fetch_insn *code, void *rec, void *edata,
		   void *dest, void *base)
{
	struct ftrace_regs *fregs = rec;
	unsigned long val;
	int ret;

retry:
	/* 1st stage: get value from context */
	switch (code->op) {
	case FETCH_OP_STACK:
		val = ftrace_regs_get_kernel_stack_nth(fregs, code->param);
		break;
	case FETCH_OP_STACKP:
		val = ftrace_regs_get_stack_pointer(fregs);
		break;
	case FETCH_OP_RETVAL:
		val = ftrace_regs_get_return_value(fregs);
		break;
#ifdef CONFIG_HAVE_FUNCTION_ARG_ACCESS_API
	case FETCH_OP_ARG:
		val = ftrace_regs_get_argument(fregs, code->param);
		break;
	case FETCH_OP_EDATA:
		val = *(unsigned long *)((unsigned long)edata + code->offset);
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
		    struct ftrace_regs *fregs,
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

	dsize = __get_data_size(&tf->tp, fregs, NULL);

	entry = trace_event_buffer_reserve(&fbuffer, trace_file,
					   sizeof(*entry) + tf->tp.size + dsize);
	if (!entry)
		return;

	fbuffer.regs = ftrace_get_regs(fregs);
	entry = fbuffer.entry = ring_buffer_event_data(fbuffer.event);
	entry->ip = entry_ip;
	store_trace_args(&entry[1], &tf->tp, fregs, NULL, sizeof(*entry), dsize);

	trace_event_buffer_commit(&fbuffer);
}

static void
fentry_trace_func(struct trace_fprobe *tf, unsigned long entry_ip,
		  struct ftrace_regs *fregs)
{
	struct event_file_link *link;

	trace_probe_for_each_link_rcu(link, &tf->tp)
		__fentry_trace_func(tf, entry_ip, fregs, link->file);
}
NOKPROBE_SYMBOL(fentry_trace_func);

static nokprobe_inline
void store_fprobe_entry_data(void *edata, struct trace_probe *tp, struct ftrace_regs *fregs)
{
	struct probe_entry_arg *earg = tp->entry_arg;
	unsigned long val = 0;
	int i;

	if (!earg)
		return;

	for (i = 0; i < earg->size; i++) {
		struct fetch_insn *code = &earg->code[i];

		switch (code->op) {
		case FETCH_OP_ARG:
			val = ftrace_regs_get_argument(fregs, code->param);
			break;
		case FETCH_OP_ST_EDATA:
			*(unsigned long *)((unsigned long)edata + code->offset) = val;
			break;
		case FETCH_OP_END:
			goto end;
		default:
			break;
		}
	}
end:
	return;
}

/* function exit handler */
static int trace_fprobe_entry_handler(struct fprobe *fp, unsigned long entry_ip,
				unsigned long ret_ip, struct ftrace_regs *fregs,
				void *entry_data)
{
	struct trace_fprobe *tf = container_of(fp, struct trace_fprobe, fp);

	if (tf->tp.entry_arg)
		store_fprobe_entry_data(entry_data, &tf->tp, fregs);

	return 0;
}
NOKPROBE_SYMBOL(trace_fprobe_entry_handler)

static nokprobe_inline void
__fexit_trace_func(struct trace_fprobe *tf, unsigned long entry_ip,
		   unsigned long ret_ip, struct ftrace_regs *fregs,
		   void *entry_data, struct trace_event_file *trace_file)
{
	struct fexit_trace_entry_head *entry;
	struct trace_event_buffer fbuffer;
	struct trace_event_call *call = trace_probe_event_call(&tf->tp);
	int dsize;

	if (WARN_ON_ONCE(call != trace_file->event_call))
		return;

	if (trace_trigger_soft_disabled(trace_file))
		return;

	dsize = __get_data_size(&tf->tp, fregs, entry_data);

	entry = trace_event_buffer_reserve(&fbuffer, trace_file,
					   sizeof(*entry) + tf->tp.size + dsize);
	if (!entry)
		return;

	fbuffer.regs = ftrace_get_regs(fregs);
	entry = fbuffer.entry = ring_buffer_event_data(fbuffer.event);
	entry->func = entry_ip;
	entry->ret_ip = ret_ip;
	store_trace_args(&entry[1], &tf->tp, fregs, entry_data, sizeof(*entry), dsize);

	trace_event_buffer_commit(&fbuffer);
}

static void
fexit_trace_func(struct trace_fprobe *tf, unsigned long entry_ip,
		 unsigned long ret_ip, struct ftrace_regs *fregs, void *entry_data)
{
	struct event_file_link *link;

	trace_probe_for_each_link_rcu(link, &tf->tp)
		__fexit_trace_func(tf, entry_ip, ret_ip, fregs, entry_data, link->file);
}
NOKPROBE_SYMBOL(fexit_trace_func);

#ifdef CONFIG_PERF_EVENTS

static int fentry_perf_func(struct trace_fprobe *tf, unsigned long entry_ip,
			    struct ftrace_regs *fregs)
{
	struct trace_event_call *call = trace_probe_event_call(&tf->tp);
	struct fentry_trace_entry_head *entry;
	struct hlist_head *head;
	int size, __size, dsize;
	struct pt_regs *regs;
	int rctx;

	head = this_cpu_ptr(call->perf_events);
	if (hlist_empty(head))
		return 0;

	dsize = __get_data_size(&tf->tp, fregs, NULL);
	__size = sizeof(*entry) + tf->tp.size + dsize;
	size = ALIGN(__size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	entry = perf_trace_buf_alloc(size, &regs, &rctx);
	if (!entry)
		return 0;

	regs = ftrace_fill_perf_regs(fregs, regs);

	entry->ip = entry_ip;
	memset(&entry[1], 0, dsize);
	store_trace_args(&entry[1], &tf->tp, fregs, NULL, sizeof(*entry), dsize);
	perf_trace_buf_submit(entry, size, rctx, call->event.type, 1, regs,
			      head, NULL);
	return 0;
}
NOKPROBE_SYMBOL(fentry_perf_func);

static void
fexit_perf_func(struct trace_fprobe *tf, unsigned long entry_ip,
		unsigned long ret_ip, struct ftrace_regs *fregs,
		void *entry_data)
{
	struct trace_event_call *call = trace_probe_event_call(&tf->tp);
	struct fexit_trace_entry_head *entry;
	struct hlist_head *head;
	int size, __size, dsize;
	struct pt_regs *regs;
	int rctx;

	head = this_cpu_ptr(call->perf_events);
	if (hlist_empty(head))
		return;

	dsize = __get_data_size(&tf->tp, fregs, entry_data);
	__size = sizeof(*entry) + tf->tp.size + dsize;
	size = ALIGN(__size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);

	entry = perf_trace_buf_alloc(size, &regs, &rctx);
	if (!entry)
		return;

	regs = ftrace_fill_perf_regs(fregs, regs);

	entry->func = entry_ip;
	entry->ret_ip = ret_ip;
	store_trace_args(&entry[1], &tf->tp, fregs, entry_data, sizeof(*entry), dsize);
	perf_trace_buf_submit(entry, size, rctx, call->event.type, 1, regs,
			      head, NULL);
}
NOKPROBE_SYMBOL(fexit_perf_func);
#endif	/* CONFIG_PERF_EVENTS */

static int fentry_dispatcher(struct fprobe *fp, unsigned long entry_ip,
			     unsigned long ret_ip, struct ftrace_regs *fregs,
			     void *entry_data)
{
	struct trace_fprobe *tf = container_of(fp, struct trace_fprobe, fp);
	int ret = 0;

	if (trace_probe_test_flag(&tf->tp, TP_FLAG_TRACE))
		fentry_trace_func(tf, entry_ip, fregs);

#ifdef CONFIG_PERF_EVENTS
	if (trace_probe_test_flag(&tf->tp, TP_FLAG_PROFILE))
		ret = fentry_perf_func(tf, entry_ip, fregs);
#endif
	return ret;
}
NOKPROBE_SYMBOL(fentry_dispatcher);

static void fexit_dispatcher(struct fprobe *fp, unsigned long entry_ip,
			     unsigned long ret_ip, struct ftrace_regs *fregs,
			     void *entry_data)
{
	struct trace_fprobe *tf = container_of(fp, struct trace_fprobe, fp);

	if (trace_probe_test_flag(&tf->tp, TP_FLAG_TRACE))
		fexit_trace_func(tf, entry_ip, ret_ip, fregs, entry_data);
#ifdef CONFIG_PERF_EVENTS
	if (trace_probe_test_flag(&tf->tp, TP_FLAG_PROFILE))
		fexit_perf_func(tf, entry_ip, ret_ip, fregs, entry_data);
#endif
}
NOKPROBE_SYMBOL(fexit_dispatcher);

static void free_trace_fprobe(struct trace_fprobe *tf)
{
	if (tf) {
		trace_probe_cleanup(&tf->tp);
		if (tf->tuser)
			tracepoint_user_put(tf->tuser);
		kfree(tf->symbol);
		kfree(tf);
	}
}

/* Since alloc_trace_fprobe() can return error, check the pointer is ERR too. */
DEFINE_FREE(free_trace_fprobe, struct trace_fprobe *, if (!IS_ERR_OR_NULL(_T)) free_trace_fprobe(_T))

/*
 * Allocate new trace_probe and initialize it (including fprobe).
 */
static struct trace_fprobe *alloc_trace_fprobe(const char *group,
					       const char *event,
					       const char *symbol,
					       int nargs, bool is_return,
					       bool is_tracepoint)
{
	struct trace_fprobe *tf __free(free_trace_fprobe) = NULL;
	int ret = -ENOMEM;

	tf = kzalloc(struct_size(tf, tp.args, nargs), GFP_KERNEL);
	if (!tf)
		return ERR_PTR(ret);

	tf->symbol = kstrdup(symbol, GFP_KERNEL);
	if (!tf->symbol)
		return ERR_PTR(-ENOMEM);

	if (is_return)
		tf->fp.exit_handler = fexit_dispatcher;
	else
		tf->fp.entry_handler = fentry_dispatcher;

	tf->tprobe = is_tracepoint;

	ret = trace_probe_init(&tf->tp, event, group, false, nargs);
	if (ret < 0)
		return ERR_PTR(ret);

	dyn_event_init(&tf->devent, &trace_fprobe_ops);
	return_ptr(tf);
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

static int __regsiter_tracepoint_fprobe(struct trace_fprobe *tf)
{
	struct tracepoint_user *tuser __free(tuser_put) = NULL;
	struct module *mod __free(module_put) = NULL;
	unsigned long ip;
	int ret;

	if (WARN_ON_ONCE(tf->tuser))
		return -EINVAL;

	/* If the tracepoint is in a module, it must be locked in this function. */
	tuser = tracepoint_user_find_get(tf->symbol, &mod);
	/* This tracepoint is not loaded yet */
	if (IS_ERR(tuser))
		return PTR_ERR(tuser);
	if (!tuser)
		return -ENOMEM;

	/* Register fprobe only if the tracepoint is loaded. */
	if (tuser->tpoint) {
		ip = tracepoint_user_ip(tuser);
		if (WARN_ON_ONCE(!ip))
			return -ENOENT;

		ret = register_fprobe_ips(&tf->fp, &ip, 1);
		if (ret < 0)
			return ret;
	}

	tf->tuser = no_free_ptr(tuser);
	return 0;
}

/* Returns an error if the target function is not available, or 0 */
static int trace_fprobe_verify_target(struct trace_fprobe *tf)
{
	int ret;

	/* Tracepoint should have a stub function. */
	if (trace_fprobe_is_tracepoint(tf))
		return 0;

	/*
	 * Note: since we don't lock the module, even if this succeeded,
	 * register_fprobe() later can fail.
	 */
	ret = fprobe_count_ips_from_filter(tf->symbol, NULL);
	return (ret < 0) ? ret : 0;
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

	tf->fp.flags &= ~FPROBE_FL_DISABLED;

	if (trace_fprobe_is_tracepoint(tf))
		return __regsiter_tracepoint_fprobe(tf);

	/* TODO: handle filter, nofilter or symbol list */
	return register_fprobe(&tf->fp, tf->symbol, NULL);
}

/* Internal unregister function - just handle fprobe and flags */
static void __unregister_trace_fprobe(struct trace_fprobe *tf)
{
	if (trace_fprobe_is_registered(tf))
		unregister_fprobe(&tf->fp);
	if (tf->tuser) {
		tracepoint_user_put(tf->tuser);
		tf->tuser = NULL;
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

static int append_trace_fprobe_event(struct trace_fprobe *tf, struct trace_fprobe *to)
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

	ret = trace_fprobe_verify_target(tf);
	if (ret)
		trace_probe_unlink(&tf->tp);
	else
		dyn_event_add(&tf->devent, trace_probe_event_call(&tf->tp));

	return ret;
}

/* Register a trace_probe and probe_event, and check the fprobe is available. */
static int register_trace_fprobe_event(struct trace_fprobe *tf)
{
	struct trace_fprobe *old_tf;
	int ret;

	guard(mutex)(&event_mutex);

	old_tf = find_trace_fprobe(trace_probe_name(&tf->tp),
				   trace_probe_group_name(&tf->tp));
	if (old_tf)
		return append_trace_fprobe_event(tf, old_tf);

	/* Register new event */
	ret = register_fprobe_event(tf);
	if (ret) {
		if (ret == -EEXIST) {
			trace_probe_log_set_index(0);
			trace_probe_log_err(0, EVENT_EXIST);
		} else
			pr_warn("Failed to register probe event(%d)\n", ret);
		return ret;
	}

	/* Verify fprobe is sane. */
	ret = trace_fprobe_verify_target(tf);
	if (ret < 0)
		unregister_fprobe_event(tf);
	else
		dyn_event_add(&tf->devent, trace_probe_event_call(&tf->tp));

	return ret;
}

struct __find_tracepoint_cb_data {
	const char *tp_name;
	struct tracepoint *tpoint;
	struct module *mod;
};

static void __find_tracepoint_module_cb(struct tracepoint *tp, struct module *mod, void *priv)
{
	struct __find_tracepoint_cb_data *data = priv;

	if (!data->tpoint && !strcmp(data->tp_name, tp->name)) {
		/* If module is not specified, try getting module refcount. */
		if (!data->mod && mod) {
			/* If failed to get refcount, ignore this tracepoint. */
			if (!try_module_get(mod))
				return;

			data->mod = mod;
		}
		data->tpoint = tp;
	}
}

static void __find_tracepoint_cb(struct tracepoint *tp, void *priv)
{
	struct __find_tracepoint_cb_data *data = priv;

	if (!data->tpoint && !strcmp(data->tp_name, tp->name))
		data->tpoint = tp;
}

/*
 * Find a tracepoint from kernel and module. If the tracepoint is on the module,
 * the module's refcount is incremented and returned as *@tp_mod. Thus, if it is
 * not NULL, caller must call module_put(*tp_mod) after used the tracepoint.
 */
static struct tracepoint *find_tracepoint(const char *tp_name,
					  struct module **tp_mod)
{
	struct __find_tracepoint_cb_data data = {
		.tp_name = tp_name,
		.mod = NULL,
	};

	for_each_kernel_tracepoint(__find_tracepoint_cb, &data);

	if (!data.tpoint && IS_ENABLED(CONFIG_MODULES)) {
		for_each_module_tracepoint(__find_tracepoint_module_cb, &data);
		*tp_mod = data.mod;
	}

	return data.tpoint;
}

#ifdef CONFIG_MODULES
/*
 * Find a tracepoint from specified module. In this case, this does not get the
 * module's refcount. The caller must ensure the module is not freed.
 */
static struct tracepoint *find_tracepoint_in_module(struct module *mod,
						    const char *tp_name)
{
	struct __find_tracepoint_cb_data data = {
		.tp_name = tp_name,
		.mod = mod,
	};

	for_each_tracepoint_in_module(mod, __find_tracepoint_module_cb, &data);
	return data.tpoint;
}

/* These are CONFIG_MODULES=y specific functions. */
static bool tracepoint_user_within_module(struct tracepoint_user *tuser,
					  struct module *mod)
{
	return within_module(tracepoint_user_ip(tuser), mod);
}

static int tracepoint_user_register_again(struct tracepoint_user *tuser,
					  struct tracepoint *tpoint)
{
	tuser->tpoint = tpoint;
	return tracepoint_user_register(tuser);
}

static void tracepoint_user_unregister_clear(struct tracepoint_user *tuser)
{
	tracepoint_user_unregister(tuser);
	tuser->tpoint = NULL;
}

/* module callback for tracepoint_user */
static int __tracepoint_probe_module_cb(struct notifier_block *self,
					unsigned long val, void *data)
{
	struct tp_module *tp_mod = data;
	struct tracepoint_user *tuser;
	struct tracepoint *tpoint;

	if (val != MODULE_STATE_GOING && val != MODULE_STATE_COMING)
		return NOTIFY_DONE;

	mutex_lock(&tracepoint_user_mutex);
	for_each_tracepoint_user(tuser) {
		if (val == MODULE_STATE_COMING) {
			/* This is not a tracepoint in this module. Skip it. */
			tpoint = find_tracepoint_in_module(tp_mod->mod, tuser->name);
			if (!tpoint)
				continue;
			WARN_ON_ONCE(tracepoint_user_register_again(tuser, tpoint));
		} else if (val == MODULE_STATE_GOING &&
			  tracepoint_user_within_module(tuser, tp_mod->mod)) {
			/* Unregister all tracepoint_user in this module. */
			tracepoint_user_unregister_clear(tuser);
		}
	}
	mutex_unlock(&tracepoint_user_mutex);

	return NOTIFY_DONE;
}

static struct notifier_block tracepoint_module_nb = {
	.notifier_call = __tracepoint_probe_module_cb,
};

/* module callback for tprobe events */
static int __tprobe_event_module_cb(struct notifier_block *self,
				     unsigned long val, void *data)
{
	struct trace_fprobe *tf;
	struct dyn_event *pos;
	struct module *mod = data;

	if (val != MODULE_STATE_GOING && val != MODULE_STATE_COMING)
		return NOTIFY_DONE;

	mutex_lock(&event_mutex);
	for_each_trace_fprobe(tf, pos) {
		/* Skip fprobe and disabled tprobe events. */
		if (!trace_fprobe_is_tracepoint(tf) || !tf->tuser)
			continue;

		/* Before this notification, tracepoint notifier has already done. */
		if (val == MODULE_STATE_COMING &&
		    tracepoint_user_within_module(tf->tuser, mod)) {
			unsigned long ip = tracepoint_user_ip(tf->tuser);

			WARN_ON_ONCE(register_fprobe_ips(&tf->fp, &ip, 1));
		} else if (val == MODULE_STATE_GOING &&
			   /*
			    * tracepoint_user_within_module() does not work here because
			    * tracepoint_user is already unregistered and cleared tpoint.
			    * Instead, checking whether the fprobe is registered but
			    * tpoint is cleared(unregistered). Such unbalance probes
			    * must be adjusted anyway.
			    */
			    trace_fprobe_is_registered(tf) &&
			    !tf->tuser->tpoint) {
			unregister_fprobe(&tf->fp);
		}
	}
	mutex_unlock(&event_mutex);

	return NOTIFY_DONE;
}

/* NOTE: this must be called after tracepoint callback */
static struct notifier_block tprobe_event_module_nb = {
	.notifier_call = __tprobe_event_module_cb,
	/* Make sure this is later than tracepoint module notifier. */
	.priority = -10,
};
#endif /* CONFIG_MODULES */

static int parse_symbol_and_return(int argc, const char *argv[],
				   char **symbol, bool *is_return,
				   bool is_tracepoint)
{
	char *tmp = strchr(argv[1], '%');
	int i;

	if (tmp) {
		int len = tmp - argv[1];

		if (!is_tracepoint && !strcmp(tmp, "%return")) {
			*is_return = true;
		} else {
			trace_probe_log_err(len, BAD_ADDR_SUFFIX);
			return -EINVAL;
		}
		*symbol = kmemdup_nul(argv[1], len, GFP_KERNEL);
	} else
		*symbol = kstrdup(argv[1], GFP_KERNEL);
	if (!*symbol)
		return -ENOMEM;

	if (*is_return)
		return 0;

	if (is_tracepoint) {
		tmp = *symbol;
		while (*tmp && (isalnum(*tmp) || *tmp == '_'))
			tmp++;
		if (*tmp) {
			/* find a wrong character. */
			trace_probe_log_err(tmp - *symbol, BAD_TP_NAME);
			kfree(*symbol);
			*symbol = NULL;
			return -EINVAL;
		}
	}

	/* If there is $retval, this should be a return fprobe. */
	for (i = 2; i < argc; i++) {
		tmp = strstr(argv[i], "$retval");
		if (tmp && !isalnum(tmp[7]) && tmp[7] != '_') {
			if (is_tracepoint) {
				trace_probe_log_set_index(i);
				trace_probe_log_err(tmp - argv[i], RETVAL_ON_PROBE);
				kfree(*symbol);
				*symbol = NULL;
				return -EINVAL;
			}
			*is_return = true;
			break;
		}
	}
	return 0;
}

static int trace_fprobe_create_internal(int argc, const char *argv[],
					struct traceprobe_parse_context *ctx)
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
	struct trace_fprobe *tf __free(free_trace_fprobe) = NULL;
	const char *event = NULL, *group = FPROBE_EVENT_SYSTEM;
	struct module *mod __free(module_put) = NULL;
	const char **new_argv __free(kfree) = NULL;
	char *symbol __free(kfree) = NULL;
	char *ebuf __free(kfree) = NULL;
	char *gbuf __free(kfree) = NULL;
	char *sbuf __free(kfree) = NULL;
	char *abuf __free(kfree) = NULL;
	char *dbuf __free(kfree) = NULL;
	int i, new_argc = 0, ret = 0;
	bool is_tracepoint = false;
	bool is_return = false;

	if ((argv[0][0] != 'f' && argv[0][0] != 't') || argc < 2)
		return -ECANCELED;

	if (argv[0][0] == 't') {
		is_tracepoint = true;
		group = TRACEPOINT_EVENT_SYSTEM;
	}

	if (argv[0][1] != '\0') {
		if (argv[0][1] != ':') {
			trace_probe_log_set_index(0);
			trace_probe_log_err(1, BAD_MAXACT);
			return -EINVAL;
		}
		event = &argv[0][2];
	}

	trace_probe_log_set_index(1);

	/* a symbol(or tracepoint) must be specified */
	ret = parse_symbol_and_return(argc, argv, &symbol, &is_return, is_tracepoint);
	if (ret < 0)
		return -EINVAL;

	trace_probe_log_set_index(0);
	if (event) {
		gbuf = kmalloc(MAX_EVENT_NAME_LEN, GFP_KERNEL);
		if (!gbuf)
			return -ENOMEM;
		ret = traceprobe_parse_event_name(&event, &group, gbuf,
						  event - argv[0]);
		if (ret)
			return -EINVAL;
	}

	if (!event) {
		ebuf = kmalloc(MAX_EVENT_NAME_LEN, GFP_KERNEL);
		if (!ebuf)
			return -ENOMEM;
		/* Make a new event name */
		if (is_tracepoint)
			snprintf(ebuf, MAX_EVENT_NAME_LEN, "%s%s",
				 isdigit(*symbol) ? "_" : "", symbol);
		else
			snprintf(ebuf, MAX_EVENT_NAME_LEN, "%s__%s", symbol,
				 is_return ? "exit" : "entry");
		sanitize_event_name(ebuf);
		event = ebuf;
	}

	if (is_return)
		ctx->flags |= TPARG_FL_RETURN;
	else
		ctx->flags |= TPARG_FL_FENTRY;

	ctx->funcname = NULL;
	if (is_tracepoint) {
		/* Get tracepoint and lock its module until the end of the registration. */
		struct tracepoint *tpoint;

		ctx->flags |= TPARG_FL_TPOINT;
		mod = NULL;
		tpoint = find_tracepoint(symbol, &mod);
		if (tpoint) {
			sbuf = kmalloc(KSYM_NAME_LEN, GFP_KERNEL);
			if (!sbuf)
				return -ENOMEM;
			ctx->funcname = kallsyms_lookup((unsigned long)tpoint->probestub,
							NULL, NULL, NULL, sbuf);
		}
	}
	if (!ctx->funcname)
		ctx->funcname = symbol;

	abuf = kmalloc(MAX_BTF_ARGS_LEN, GFP_KERNEL);
	if (!abuf)
		return -ENOMEM;
	argc -= 2; argv += 2;
	new_argv = traceprobe_expand_meta_args(argc, argv, &new_argc,
					       abuf, MAX_BTF_ARGS_LEN, ctx);
	if (IS_ERR(new_argv))
		return PTR_ERR(new_argv);
	if (new_argv) {
		argc = new_argc;
		argv = new_argv;
	}
	if (argc > MAX_TRACE_ARGS) {
		trace_probe_log_set_index(2);
		trace_probe_log_err(0, TOO_MANY_ARGS);
		return -E2BIG;
	}

	ret = traceprobe_expand_dentry_args(argc, argv, &dbuf);
	if (ret)
		return ret;

	/* setup a probe */
	tf = alloc_trace_fprobe(group, event, symbol, argc, is_return, is_tracepoint);
	if (IS_ERR(tf)) {
		ret = PTR_ERR(tf);
		/* This must return -ENOMEM, else there is a bug */
		WARN_ON_ONCE(ret != -ENOMEM);
		return ret;
	}

	/* parse arguments */
	for (i = 0; i < argc; i++) {
		trace_probe_log_set_index(i + 2);
		ctx->offset = 0;
		ret = traceprobe_parse_probe_arg(&tf->tp, i, argv[i], ctx);
		if (ret)
			return ret;	/* This can be -ENOMEM */
	}

	if (is_return && tf->tp.entry_arg) {
		tf->fp.entry_handler = trace_fprobe_entry_handler;
		tf->fp.entry_data_size = traceprobe_get_entry_data_size(&tf->tp);
		if (ALIGN(tf->fp.entry_data_size, sizeof(long)) > MAX_FPROBE_DATA_SIZE) {
			trace_probe_log_set_index(2);
			trace_probe_log_err(0, TOO_MANY_EARGS);
			return -E2BIG;
		}
	}

	ret = traceprobe_set_print_fmt(&tf->tp,
			is_return ? PROBE_PRINT_RETURN : PROBE_PRINT_NORMAL);
	if (ret < 0)
		return ret;

	ret = register_trace_fprobe_event(tf);
	if (ret) {
		trace_probe_log_set_index(1);
		if (ret == -EILSEQ)
			trace_probe_log_err(0, BAD_INSN_BNDRY);
		else if (ret == -ENOENT)
			trace_probe_log_err(0, BAD_PROBE_ADDR);
		else if (ret != -ENOMEM && ret != -EEXIST)
			trace_probe_log_err(0, FAIL_REG_PROBE);
		return -EINVAL;
	}

	/* 'tf' is successfully registered. To avoid freeing, assign NULL. */
	tf = NULL;

	return 0;
}

static int trace_fprobe_create_cb(int argc, const char *argv[])
{
	struct traceprobe_parse_context *ctx __free(traceprobe_parse_context) = NULL;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->flags = TPARG_FL_KERNEL | TPARG_FL_FPROBE;

	trace_probe_log_init("trace_fprobe", argc, argv);
	ret = trace_fprobe_create_internal(argc, argv, ctx);
	trace_probe_log_clear();
	return ret;
}

static int trace_fprobe_create(const char *raw_command)
{
	return trace_probe_create(raw_command, trace_fprobe_create_cb);
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
			ret = __register_trace_fprobe(tf);
			if (ret < 0)
				return ret;
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
	struct trace_fprobe *tf;
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

	if (!trace_probe_is_enabled(tp)) {
		list_for_each_entry(tf, trace_probe_probe_list(tp), tp.list) {
			unregister_fprobe(&tf->fp);
		}
	}

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
	ret = register_module_notifier(&tprobe_event_module_nb);
	if (ret)
		return ret;
#endif

	return 0;
}
core_initcall(init_fprobe_trace_early);
