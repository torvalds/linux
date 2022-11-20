// SPDX-License-Identifier: GPL-2.0
/*
 * event probes
 *
 * Part of this code was copied from kernel/trace/trace_kprobe.c written by
 * Masami Hiramatsu <mhiramat@kernel.org>
 *
 * Copyright (C) 2021, VMware Inc, Steven Rostedt <rostedt@goodmis.org>
 * Copyright (C) 2021, VMware Inc, Tzvetomir Stoyanov tz.stoyanov@gmail.com>
 *
 */
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/ftrace.h>

#include "trace_dynevent.h"
#include "trace_probe.h"
#include "trace_probe_tmpl.h"
#include "trace_probe_kernel.h"

#define EPROBE_EVENT_SYSTEM "eprobes"

struct trace_eprobe {
	/* tracepoint system */
	const char *event_system;

	/* tracepoint event */
	const char *event_name;

	/* filter string for the tracepoint */
	char *filter_str;

	struct trace_event_call *event;

	struct dyn_event	devent;
	struct trace_probe	tp;
};

struct eprobe_data {
	struct trace_event_file	*file;
	struct trace_eprobe	*ep;
};

static int __trace_eprobe_create(int argc, const char *argv[]);

static void trace_event_probe_cleanup(struct trace_eprobe *ep)
{
	if (!ep)
		return;
	trace_probe_cleanup(&ep->tp);
	kfree(ep->event_name);
	kfree(ep->event_system);
	if (ep->event)
		trace_event_put_ref(ep->event);
	kfree(ep->filter_str);
	kfree(ep);
}

static struct trace_eprobe *to_trace_eprobe(struct dyn_event *ev)
{
	return container_of(ev, struct trace_eprobe, devent);
}

static int eprobe_dyn_event_create(const char *raw_command)
{
	return trace_probe_create(raw_command, __trace_eprobe_create);
}

static int eprobe_dyn_event_show(struct seq_file *m, struct dyn_event *ev)
{
	struct trace_eprobe *ep = to_trace_eprobe(ev);
	int i;

	seq_printf(m, "e:%s/%s", trace_probe_group_name(&ep->tp),
				trace_probe_name(&ep->tp));
	seq_printf(m, " %s.%s", ep->event_system, ep->event_name);

	for (i = 0; i < ep->tp.nr_args; i++)
		seq_printf(m, " %s=%s", ep->tp.args[i].name, ep->tp.args[i].comm);
	seq_putc(m, '\n');

	return 0;
}

static int unregister_trace_eprobe(struct trace_eprobe *ep)
{
	/* If other probes are on the event, just unregister eprobe */
	if (trace_probe_has_sibling(&ep->tp))
		goto unreg;

	/* Enabled event can not be unregistered */
	if (trace_probe_is_enabled(&ep->tp))
		return -EBUSY;

	/* Will fail if probe is being used by ftrace or perf */
	if (trace_probe_unregister_event_call(&ep->tp))
		return -EBUSY;

unreg:
	dyn_event_remove(&ep->devent);
	trace_probe_unlink(&ep->tp);

	return 0;
}

static int eprobe_dyn_event_release(struct dyn_event *ev)
{
	struct trace_eprobe *ep = to_trace_eprobe(ev);
	int ret = unregister_trace_eprobe(ep);

	if (!ret)
		trace_event_probe_cleanup(ep);
	return ret;
}

static bool eprobe_dyn_event_is_busy(struct dyn_event *ev)
{
	struct trace_eprobe *ep = to_trace_eprobe(ev);

	return trace_probe_is_enabled(&ep->tp);
}

static bool eprobe_dyn_event_match(const char *system, const char *event,
			int argc, const char **argv, struct dyn_event *ev)
{
	struct trace_eprobe *ep = to_trace_eprobe(ev);
	const char *slash;

	/*
	 * We match the following:
	 *  event only			- match all eprobes with event name
	 *  system and event only	- match all system/event probes
	 *  system only			- match all system probes
	 *
	 * The below has the above satisfied with more arguments:
	 *
	 *  attached system/event	- If the arg has the system and event
	 *				  the probe is attached to, match
	 *				  probes with the attachment.
	 *
	 *  If any more args are given, then it requires a full match.
	 */

	/*
	 * If system exists, but this probe is not part of that system
	 * do not match.
	 */
	if (system && strcmp(trace_probe_group_name(&ep->tp), system) != 0)
		return false;

	/* Must match the event name */
	if (event[0] != '\0' && strcmp(trace_probe_name(&ep->tp), event) != 0)
		return false;

	/* No arguments match all */
	if (argc < 1)
		return true;

	/* First argument is the system/event the probe is attached to */

	slash = strchr(argv[0], '/');
	if (!slash)
		slash = strchr(argv[0], '.');
	if (!slash)
		return false;

	if (strncmp(ep->event_system, argv[0], slash - argv[0]))
		return false;
	if (strcmp(ep->event_name, slash + 1))
		return false;

	argc--;
	argv++;

	/* If there are no other args, then match */
	if (argc < 1)
		return true;

	return trace_probe_match_command_args(&ep->tp, argc, argv);
}

static struct dyn_event_operations eprobe_dyn_event_ops = {
	.create = eprobe_dyn_event_create,
	.show = eprobe_dyn_event_show,
	.is_busy = eprobe_dyn_event_is_busy,
	.free = eprobe_dyn_event_release,
	.match = eprobe_dyn_event_match,
};

static struct trace_eprobe *alloc_event_probe(const char *group,
					      const char *this_event,
					      struct trace_event_call *event,
					      int nargs)
{
	struct trace_eprobe *ep;
	const char *event_name;
	const char *sys_name;
	int ret = -ENOMEM;

	if (!event)
		return ERR_PTR(-ENODEV);

	sys_name = event->class->system;
	event_name = trace_event_name(event);

	ep = kzalloc(struct_size(ep, tp.args, nargs), GFP_KERNEL);
	if (!ep) {
		trace_event_put_ref(event);
		goto error;
	}
	ep->event = event;
	ep->event_name = kstrdup(event_name, GFP_KERNEL);
	if (!ep->event_name)
		goto error;
	ep->event_system = kstrdup(sys_name, GFP_KERNEL);
	if (!ep->event_system)
		goto error;

	ret = trace_probe_init(&ep->tp, this_event, group, false);
	if (ret < 0)
		goto error;

	dyn_event_init(&ep->devent, &eprobe_dyn_event_ops);
	return ep;
error:
	trace_event_probe_cleanup(ep);
	return ERR_PTR(ret);
}

static int trace_eprobe_tp_arg_update(struct trace_eprobe *ep, int i)
{
	struct probe_arg *parg = &ep->tp.args[i];
	struct ftrace_event_field *field;
	struct list_head *head;
	int ret = -ENOENT;

	head = trace_get_fields(ep->event);
	list_for_each_entry(field, head, link) {
		if (!strcmp(parg->code->data, field->name)) {
			kfree(parg->code->data);
			parg->code->data = field;
			return 0;
		}
	}

	/*
	 * Argument not found on event. But allow for comm and COMM
	 * to be used to get the current->comm.
	 */
	if (strcmp(parg->code->data, "COMM") == 0 ||
	    strcmp(parg->code->data, "comm") == 0) {
		parg->code->op = FETCH_OP_COMM;
		ret = 0;
	}

	kfree(parg->code->data);
	parg->code->data = NULL;
	return ret;
}

static int eprobe_event_define_fields(struct trace_event_call *event_call)
{
	struct eprobe_trace_entry_head field;
	struct trace_probe *tp;

	tp = trace_probe_primary_from_call(event_call);
	if (WARN_ON_ONCE(!tp))
		return -ENOENT;

	return traceprobe_define_arg_fields(event_call, sizeof(field), tp);
}

static struct trace_event_fields eprobe_fields_array[] = {
	{ .type = TRACE_FUNCTION_TYPE,
	  .define_fields = eprobe_event_define_fields },
	{}
};

/* Event entry printers */
static enum print_line_t
print_eprobe_event(struct trace_iterator *iter, int flags,
		   struct trace_event *event)
{
	struct eprobe_trace_entry_head *field;
	struct trace_event_call *pevent;
	struct trace_event *probed_event;
	struct trace_seq *s = &iter->seq;
	struct trace_eprobe *ep;
	struct trace_probe *tp;
	unsigned int type;

	field = (struct eprobe_trace_entry_head *)iter->ent;
	tp = trace_probe_primary_from_call(
		container_of(event, struct trace_event_call, event));
	if (WARN_ON_ONCE(!tp))
		goto out;

	ep = container_of(tp, struct trace_eprobe, tp);
	type = ep->event->event.type;

	trace_seq_printf(s, "%s: (", trace_probe_name(tp));

	probed_event = ftrace_find_event(type);
	if (probed_event) {
		pevent = container_of(probed_event, struct trace_event_call, event);
		trace_seq_printf(s, "%s.%s", pevent->class->system,
				 trace_event_name(pevent));
	} else {
		trace_seq_printf(s, "%u", type);
	}

	trace_seq_putc(s, ')');

	if (print_probe_args(s, tp->args, tp->nr_args,
			     (u8 *)&field[1], field) < 0)
		goto out;

	trace_seq_putc(s, '\n');
 out:
	return trace_handle_return(s);
}

static unsigned long get_event_field(struct fetch_insn *code, void *rec)
{
	struct ftrace_event_field *field = code->data;
	unsigned long val;
	void *addr;

	addr = rec + field->offset;

	if (is_string_field(field)) {
		switch (field->filter_type) {
		case FILTER_DYN_STRING:
			val = (unsigned long)(rec + (*(unsigned int *)addr & 0xffff));
			break;
		case FILTER_RDYN_STRING:
			val = (unsigned long)(addr + (*(unsigned int *)addr & 0xffff));
			break;
		case FILTER_STATIC_STRING:
			val = (unsigned long)addr;
			break;
		case FILTER_PTR_STRING:
			val = (unsigned long)(*(char *)addr);
			break;
		default:
			WARN_ON_ONCE(1);
			return 0;
		}
		return val;
	}

	switch (field->size) {
	case 1:
		if (field->is_signed)
			val = *(char *)addr;
		else
			val = *(unsigned char *)addr;
		break;
	case 2:
		if (field->is_signed)
			val = *(short *)addr;
		else
			val = *(unsigned short *)addr;
		break;
	case 4:
		if (field->is_signed)
			val = *(int *)addr;
		else
			val = *(unsigned int *)addr;
		break;
	default:
		if (field->is_signed)
			val = *(long *)addr;
		else
			val = *(unsigned long *)addr;
		break;
	}
	return val;
}

static int get_eprobe_size(struct trace_probe *tp, void *rec)
{
	struct fetch_insn *code;
	struct probe_arg *arg;
	int i, len, ret = 0;

	for (i = 0; i < tp->nr_args; i++) {
		arg = tp->args + i;
		if (arg->dynamic) {
			unsigned long val;

			code = arg->code;
 retry:
			switch (code->op) {
			case FETCH_OP_TP_ARG:
				val = get_event_field(code, rec);
				break;
			case FETCH_OP_IMM:
				val = code->immediate;
				break;
			case FETCH_OP_COMM:
				val = (unsigned long)current->comm;
				break;
			case FETCH_OP_DATA:
				val = (unsigned long)code->data;
				break;
			case FETCH_NOP_SYMBOL:	/* Ignore a place holder */
				code++;
				goto retry;
			default:
				continue;
			}
			code++;
			len = process_fetch_insn_bottom(code, val, NULL, NULL);
			if (len > 0)
				ret += len;
		}
	}

	return ret;
}

/* Kprobe specific fetch functions */

/* Note that we don't verify it, since the code does not come from user space */
static int
process_fetch_insn(struct fetch_insn *code, void *rec, void *dest,
		   void *base)
{
	unsigned long val;

 retry:
	switch (code->op) {
	case FETCH_OP_TP_ARG:
		val = get_event_field(code, rec);
		break;
	case FETCH_OP_IMM:
		val = code->immediate;
		break;
	case FETCH_OP_COMM:
		val = (unsigned long)current->comm;
		break;
	case FETCH_OP_DATA:
		val = (unsigned long)code->data;
		break;
	case FETCH_NOP_SYMBOL:	/* Ignore a place holder */
		code++;
		goto retry;
	default:
		return -EILSEQ;
	}
	code++;
	return process_fetch_insn_bottom(code, val, dest, base);
}
NOKPROBE_SYMBOL(process_fetch_insn)

/* Return the length of string -- including null terminal byte */
static nokprobe_inline int
fetch_store_strlen_user(unsigned long addr)
{
	return kern_fetch_store_strlen_user(addr);
}

/* Return the length of string -- including null terminal byte */
static nokprobe_inline int
fetch_store_strlen(unsigned long addr)
{
	return kern_fetch_store_strlen(addr);
}

/*
 * Fetch a null-terminated string from user. Caller MUST set *(u32 *)buf
 * with max length and relative data location.
 */
static nokprobe_inline int
fetch_store_string_user(unsigned long addr, void *dest, void *base)
{
	return kern_fetch_store_string_user(addr, dest, base);
}

/*
 * Fetch a null-terminated string. Caller MUST set *(u32 *)buf with max
 * length and relative data location.
 */
static nokprobe_inline int
fetch_store_string(unsigned long addr, void *dest, void *base)
{
	return kern_fetch_store_string(addr, dest, base);
}

static nokprobe_inline int
probe_mem_read_user(void *dest, void *src, size_t size)
{
	const void __user *uaddr =  (__force const void __user *)src;

	return copy_from_user_nofault(dest, uaddr, size);
}

static nokprobe_inline int
probe_mem_read(void *dest, void *src, size_t size)
{
#ifdef CONFIG_ARCH_HAS_NON_OVERLAPPING_ADDRESS_SPACE
	if ((unsigned long)src < TASK_SIZE)
		return probe_mem_read_user(dest, src, size);
#endif
	return copy_from_kernel_nofault(dest, src, size);
}

/* eprobe handler */
static inline void
__eprobe_trace_func(struct eprobe_data *edata, void *rec)
{
	struct eprobe_trace_entry_head *entry;
	struct trace_event_call *call = trace_probe_event_call(&edata->ep->tp);
	struct trace_event_buffer fbuffer;
	int dsize;

	if (WARN_ON_ONCE(call != edata->file->event_call))
		return;

	if (trace_trigger_soft_disabled(edata->file))
		return;

	dsize = get_eprobe_size(&edata->ep->tp, rec);

	entry = trace_event_buffer_reserve(&fbuffer, edata->file,
					   sizeof(*entry) + edata->ep->tp.size + dsize);

	if (!entry)
		return;

	entry = fbuffer.entry = ring_buffer_event_data(fbuffer.event);
	store_trace_args(&entry[1], &edata->ep->tp, rec, sizeof(*entry), dsize);

	trace_event_buffer_commit(&fbuffer);
}

/*
 * The event probe implementation uses event triggers to get access to
 * the event it is attached to, but is not an actual trigger. The below
 * functions are just stubs to fulfill what is needed to use the trigger
 * infrastructure.
 */
static int eprobe_trigger_init(struct event_trigger_data *data)
{
	return 0;
}

static void eprobe_trigger_free(struct event_trigger_data *data)
{

}

static int eprobe_trigger_print(struct seq_file *m,
				struct event_trigger_data *data)
{
	/* Do not print eprobe event triggers */
	return 0;
}

static void eprobe_trigger_func(struct event_trigger_data *data,
				struct trace_buffer *buffer, void *rec,
				struct ring_buffer_event *rbe)
{
	struct eprobe_data *edata = data->private_data;

	if (unlikely(!rec))
		return;

	__eprobe_trace_func(edata, rec);
}

static struct event_trigger_ops eprobe_trigger_ops = {
	.trigger		= eprobe_trigger_func,
	.print			= eprobe_trigger_print,
	.init			= eprobe_trigger_init,
	.free			= eprobe_trigger_free,
};

static int eprobe_trigger_cmd_parse(struct event_command *cmd_ops,
				    struct trace_event_file *file,
				    char *glob, char *cmd,
				    char *param_and_filter)
{
	return -1;
}

static int eprobe_trigger_reg_func(char *glob,
				   struct event_trigger_data *data,
				   struct trace_event_file *file)
{
	return -1;
}

static void eprobe_trigger_unreg_func(char *glob,
				      struct event_trigger_data *data,
				      struct trace_event_file *file)
{

}

static struct event_trigger_ops *eprobe_trigger_get_ops(char *cmd,
							char *param)
{
	return &eprobe_trigger_ops;
}

static struct event_command event_trigger_cmd = {
	.name			= "eprobe",
	.trigger_type		= ETT_EVENT_EPROBE,
	.flags			= EVENT_CMD_FL_NEEDS_REC,
	.parse			= eprobe_trigger_cmd_parse,
	.reg			= eprobe_trigger_reg_func,
	.unreg			= eprobe_trigger_unreg_func,
	.unreg_all		= NULL,
	.get_trigger_ops	= eprobe_trigger_get_ops,
	.set_filter		= NULL,
};

static struct event_trigger_data *
new_eprobe_trigger(struct trace_eprobe *ep, struct trace_event_file *file)
{
	struct event_trigger_data *trigger;
	struct event_filter *filter = NULL;
	struct eprobe_data *edata;
	int ret;

	edata = kzalloc(sizeof(*edata), GFP_KERNEL);
	trigger = kzalloc(sizeof(*trigger), GFP_KERNEL);
	if (!trigger || !edata) {
		ret = -ENOMEM;
		goto error;
	}

	trigger->flags = EVENT_TRIGGER_FL_PROBE;
	trigger->count = -1;
	trigger->ops = &eprobe_trigger_ops;

	/*
	 * EVENT PROBE triggers are not registered as commands with
	 * register_event_command(), as they are not controlled by the user
	 * from the trigger file
	 */
	trigger->cmd_ops = &event_trigger_cmd;

	INIT_LIST_HEAD(&trigger->list);

	if (ep->filter_str) {
		ret = create_event_filter(file->tr, ep->event,
					ep->filter_str, false, &filter);
		if (ret)
			goto error;
	}
	RCU_INIT_POINTER(trigger->filter, filter);

	edata->file = file;
	edata->ep = ep;
	trigger->private_data = edata;

	return trigger;
error:
	free_event_filter(filter);
	kfree(edata);
	kfree(trigger);
	return ERR_PTR(ret);
}

static int enable_eprobe(struct trace_eprobe *ep,
			 struct trace_event_file *eprobe_file)
{
	struct event_trigger_data *trigger;
	struct trace_event_file *file;
	struct trace_array *tr = eprobe_file->tr;

	file = find_event_file(tr, ep->event_system, ep->event_name);
	if (!file)
		return -ENOENT;
	trigger = new_eprobe_trigger(ep, eprobe_file);
	if (IS_ERR(trigger))
		return PTR_ERR(trigger);

	list_add_tail_rcu(&trigger->list, &file->triggers);

	trace_event_trigger_enable_disable(file, 1);
	update_cond_flag(file);

	return 0;
}

static struct trace_event_functions eprobe_funcs = {
	.trace		= print_eprobe_event
};

static int disable_eprobe(struct trace_eprobe *ep,
			  struct trace_array *tr)
{
	struct event_trigger_data *trigger = NULL, *iter;
	struct trace_event_file *file;
	struct event_filter *filter;
	struct eprobe_data *edata;

	file = find_event_file(tr, ep->event_system, ep->event_name);
	if (!file)
		return -ENOENT;

	list_for_each_entry(iter, &file->triggers, list) {
		if (!(iter->flags & EVENT_TRIGGER_FL_PROBE))
			continue;
		edata = iter->private_data;
		if (edata->ep == ep) {
			trigger = iter;
			break;
		}
	}
	if (!trigger)
		return -ENODEV;

	list_del_rcu(&trigger->list);

	trace_event_trigger_enable_disable(file, 0);
	update_cond_flag(file);

	/* Make sure nothing is using the edata or trigger */
	tracepoint_synchronize_unregister();

	filter = rcu_access_pointer(trigger->filter);

	if (filter)
		free_event_filter(filter);
	kfree(edata);
	kfree(trigger);

	return 0;
}

static int enable_trace_eprobe(struct trace_event_call *call,
			       struct trace_event_file *file)
{
	struct trace_probe *pos, *tp;
	struct trace_eprobe *ep;
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

	if (enabled)
		return 0;

	list_for_each_entry(pos, trace_probe_probe_list(tp), list) {
		ep = container_of(pos, struct trace_eprobe, tp);
		ret = enable_eprobe(ep, file);
		if (ret)
			break;
		enabled = true;
	}

	if (ret) {
		/* Failed to enable one of them. Roll back all */
		if (enabled)
			disable_eprobe(ep, file->tr);
		if (file)
			trace_probe_remove_file(tp, file);
		else
			trace_probe_clear_flag(tp, TP_FLAG_PROFILE);
	}

	return ret;
}

static int disable_trace_eprobe(struct trace_event_call *call,
				struct trace_event_file *file)
{
	struct trace_probe *pos, *tp;
	struct trace_eprobe *ep;

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
		list_for_each_entry(pos, trace_probe_probe_list(tp), list) {
			ep = container_of(pos, struct trace_eprobe, tp);
			disable_eprobe(ep, file->tr);
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

static int eprobe_register(struct trace_event_call *event,
			   enum trace_reg type, void *data)
{
	struct trace_event_file *file = data;

	switch (type) {
	case TRACE_REG_REGISTER:
		return enable_trace_eprobe(event, file);
	case TRACE_REG_UNREGISTER:
		return disable_trace_eprobe(event, file);
#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
	case TRACE_REG_PERF_UNREGISTER:
	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		return 0;
#endif
	}
	return 0;
}

static inline void init_trace_eprobe_call(struct trace_eprobe *ep)
{
	struct trace_event_call *call = trace_probe_event_call(&ep->tp);

	call->flags = TRACE_EVENT_FL_EPROBE;
	call->event.funcs = &eprobe_funcs;
	call->class->fields_array = eprobe_fields_array;
	call->class->reg = eprobe_register;
}

static struct trace_event_call *
find_and_get_event(const char *system, const char *event_name)
{
	struct trace_event_call *tp_event;
	const char *name;

	list_for_each_entry(tp_event, &ftrace_events, list) {
		/* Skip other probes and ftrace events */
		if (tp_event->flags &
		    (TRACE_EVENT_FL_IGNORE_ENABLE |
		     TRACE_EVENT_FL_KPROBE |
		     TRACE_EVENT_FL_UPROBE |
		     TRACE_EVENT_FL_EPROBE))
			continue;
		if (!tp_event->class->system ||
		    strcmp(system, tp_event->class->system))
			continue;
		name = trace_event_name(tp_event);
		if (!name || strcmp(event_name, name))
			continue;
		if (!trace_event_try_get_ref(tp_event)) {
			return NULL;
			break;
		}
		return tp_event;
		break;
	}
	return NULL;
}

static int trace_eprobe_tp_update_arg(struct trace_eprobe *ep, const char *argv[], int i)
{
	unsigned int flags = TPARG_FL_KERNEL | TPARG_FL_TPOINT;
	int ret;

	ret = traceprobe_parse_probe_arg(&ep->tp, i, argv[i], flags);
	if (ret)
		return ret;

	if (ep->tp.args[i].code->op == FETCH_OP_TP_ARG) {
		ret = trace_eprobe_tp_arg_update(ep, i);
		if (ret)
			trace_probe_log_err(0, BAD_ATTACH_ARG);
	}

	/* Handle symbols "@" */
	if (!ret)
		ret = traceprobe_update_arg(&ep->tp.args[i]);

	return ret;
}

static int trace_eprobe_parse_filter(struct trace_eprobe *ep, int argc, const char *argv[])
{
	struct event_filter *dummy = NULL;
	int i, ret, len = 0;
	char *p;

	if (argc == 0) {
		trace_probe_log_err(0, NO_EP_FILTER);
		return -EINVAL;
	}

	/* Recover the filter string */
	for (i = 0; i < argc; i++)
		len += strlen(argv[i]) + 1;

	ep->filter_str = kzalloc(len, GFP_KERNEL);
	if (!ep->filter_str)
		return -ENOMEM;

	p = ep->filter_str;
	for (i = 0; i < argc; i++) {
		ret = snprintf(p, len, "%s ", argv[i]);
		if (ret < 0)
			goto error;
		if (ret > len) {
			ret = -E2BIG;
			goto error;
		}
		p += ret;
		len -= ret;
	}
	p[-1] = '\0';

	/*
	 * Ensure the filter string can be parsed correctly. Note, this
	 * filter string is for the original event, not for the eprobe.
	 */
	ret = create_event_filter(top_trace_array(), ep->event, ep->filter_str,
				  true, &dummy);
	free_event_filter(dummy);
	if (ret)
		goto error;

	return 0;
error:
	kfree(ep->filter_str);
	ep->filter_str = NULL;
	return ret;
}

static int __trace_eprobe_create(int argc, const char *argv[])
{
	/*
	 * Argument syntax:
	 *      e[:[GRP/][ENAME]] SYSTEM.EVENT [FETCHARGS] [if FILTER]
	 * Fetch args (no space):
	 *  <name>=$<field>[:TYPE]
	 */
	const char *event = NULL, *group = EPROBE_EVENT_SYSTEM;
	const char *sys_event = NULL, *sys_name = NULL;
	struct trace_event_call *event_call;
	struct trace_eprobe *ep = NULL;
	char buf1[MAX_EVENT_NAME_LEN];
	char buf2[MAX_EVENT_NAME_LEN];
	char gbuf[MAX_EVENT_NAME_LEN];
	int ret = 0, filter_idx = 0;
	int i, filter_cnt;

	if (argc < 2 || argv[0][0] != 'e')
		return -ECANCELED;

	trace_probe_log_init("event_probe", argc, argv);

	event = strchr(&argv[0][1], ':');
	if (event) {
		event++;
		ret = traceprobe_parse_event_name(&event, &group, gbuf,
						  event - argv[0]);
		if (ret)
			goto parse_error;
	}

	trace_probe_log_set_index(1);
	sys_event = argv[1];
	ret = traceprobe_parse_event_name(&sys_event, &sys_name, buf2, 0);
	if (ret || !sys_event || !sys_name) {
		trace_probe_log_err(0, NO_EVENT_INFO);
		goto parse_error;
	}

	if (!event) {
		strscpy(buf1, sys_event, MAX_EVENT_NAME_LEN);
		event = buf1;
	}

	for (i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "if")) {
			filter_idx = i + 1;
			filter_cnt = argc - filter_idx;
			argc = i;
			break;
		}
	}

	mutex_lock(&event_mutex);
	event_call = find_and_get_event(sys_name, sys_event);
	ep = alloc_event_probe(group, event, event_call, argc - 2);
	mutex_unlock(&event_mutex);

	if (IS_ERR(ep)) {
		ret = PTR_ERR(ep);
		if (ret == -ENODEV)
			trace_probe_log_err(0, BAD_ATTACH_EVENT);
		/* This must return -ENOMEM or missing event, else there is a bug */
		WARN_ON_ONCE(ret != -ENOMEM && ret != -ENODEV);
		ep = NULL;
		goto error;
	}

	if (filter_idx) {
		trace_probe_log_set_index(filter_idx);
		ret = trace_eprobe_parse_filter(ep, filter_cnt, argv + filter_idx);
		if (ret)
			goto parse_error;
	} else
		ep->filter_str = NULL;

	argc -= 2; argv += 2;
	/* parse arguments */
	for (i = 0; i < argc && i < MAX_TRACE_ARGS; i++) {
		trace_probe_log_set_index(i + 2);
		ret = trace_eprobe_tp_update_arg(ep, argv, i);
		if (ret)
			goto error;
	}
	ret = traceprobe_set_print_fmt(&ep->tp, PROBE_PRINT_EVENT);
	if (ret < 0)
		goto error;
	init_trace_eprobe_call(ep);
	mutex_lock(&event_mutex);
	ret = trace_probe_register_event_call(&ep->tp);
	if (ret) {
		if (ret == -EEXIST) {
			trace_probe_log_set_index(0);
			trace_probe_log_err(0, EVENT_EXIST);
		}
		mutex_unlock(&event_mutex);
		goto error;
	}
	ret = dyn_event_add(&ep->devent, &ep->tp.event->call);
	mutex_unlock(&event_mutex);
	return ret;
parse_error:
	ret = -EINVAL;
error:
	trace_event_probe_cleanup(ep);
	return ret;
}

/*
 * Register dynevent at core_initcall. This allows kernel to setup eprobe
 * events in postcore_initcall without tracefs.
 */
static __init int trace_events_eprobe_init_early(void)
{
	int err = 0;

	err = dyn_event_register(&eprobe_dyn_event_ops);
	if (err)
		pr_warn("Could not register eprobe_dyn_event_ops\n");

	return err;
}
core_initcall(trace_events_eprobe_init_early);
