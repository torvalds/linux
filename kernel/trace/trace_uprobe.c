/*
 * uprobes-based tracing events
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Copyright (C) IBM Corporation, 2010-2012
 * Author:	Srikar Dronamraju <srikar@linux.vnet.ibm.com>
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/uprobes.h>
#include <linux/namei.h>
#include <linux/string.h>

#include "trace_probe.h"

#define UPROBE_EVENT_SYSTEM	"uprobes"

struct trace_uprobe_filter {
	rwlock_t		rwlock;
	int			nr_systemwide;
	struct list_head	perf_events;
};

/*
 * uprobe event core functions
 */
struct trace_uprobe {
	struct list_head		list;
	struct ftrace_event_class	class;
	struct ftrace_event_call	call;
	struct trace_uprobe_filter	filter;
	struct uprobe_consumer		consumer;
	struct inode			*inode;
	char				*filename;
	unsigned long			offset;
	unsigned long			nhit;
	unsigned int			flags;	/* For TP_FLAG_* */
	ssize_t				size;	/* trace entry size */
	unsigned int			nr_args;
	struct probe_arg		args[];
};

#define SIZEOF_TRACE_UPROBE(n)			\
	(offsetof(struct trace_uprobe, args) +	\
	(sizeof(struct probe_arg) * (n)))

static int register_uprobe_event(struct trace_uprobe *tu);
static void unregister_uprobe_event(struct trace_uprobe *tu);

static DEFINE_MUTEX(uprobe_lock);
static LIST_HEAD(uprobe_list);

static int uprobe_dispatcher(struct uprobe_consumer *con, struct pt_regs *regs);

static inline void init_trace_uprobe_filter(struct trace_uprobe_filter *filter)
{
	rwlock_init(&filter->rwlock);
	filter->nr_systemwide = 0;
	INIT_LIST_HEAD(&filter->perf_events);
}

static inline bool uprobe_filter_is_empty(struct trace_uprobe_filter *filter)
{
	return !filter->nr_systemwide && list_empty(&filter->perf_events);
}

/*
 * Allocate new trace_uprobe and initialize it (including uprobes).
 */
static struct trace_uprobe *
alloc_trace_uprobe(const char *group, const char *event, int nargs)
{
	struct trace_uprobe *tu;

	if (!event || !is_good_name(event))
		return ERR_PTR(-EINVAL);

	if (!group || !is_good_name(group))
		return ERR_PTR(-EINVAL);

	tu = kzalloc(SIZEOF_TRACE_UPROBE(nargs), GFP_KERNEL);
	if (!tu)
		return ERR_PTR(-ENOMEM);

	tu->call.class = &tu->class;
	tu->call.name = kstrdup(event, GFP_KERNEL);
	if (!tu->call.name)
		goto error;

	tu->class.system = kstrdup(group, GFP_KERNEL);
	if (!tu->class.system)
		goto error;

	INIT_LIST_HEAD(&tu->list);
	tu->consumer.handler = uprobe_dispatcher;
	init_trace_uprobe_filter(&tu->filter);
	return tu;

error:
	kfree(tu->call.name);
	kfree(tu);

	return ERR_PTR(-ENOMEM);
}

static void free_trace_uprobe(struct trace_uprobe *tu)
{
	int i;

	for (i = 0; i < tu->nr_args; i++)
		traceprobe_free_probe_arg(&tu->args[i]);

	iput(tu->inode);
	kfree(tu->call.class->system);
	kfree(tu->call.name);
	kfree(tu->filename);
	kfree(tu);
}

static struct trace_uprobe *find_probe_event(const char *event, const char *group)
{
	struct trace_uprobe *tu;

	list_for_each_entry(tu, &uprobe_list, list)
		if (strcmp(tu->call.name, event) == 0 &&
		    strcmp(tu->call.class->system, group) == 0)
			return tu;

	return NULL;
}

/* Unregister a trace_uprobe and probe_event: call with locking uprobe_lock */
static void unregister_trace_uprobe(struct trace_uprobe *tu)
{
	list_del(&tu->list);
	unregister_uprobe_event(tu);
	free_trace_uprobe(tu);
}

/* Register a trace_uprobe and probe_event */
static int register_trace_uprobe(struct trace_uprobe *tu)
{
	struct trace_uprobe *old_tp;
	int ret;

	mutex_lock(&uprobe_lock);

	/* register as an event */
	old_tp = find_probe_event(tu->call.name, tu->call.class->system);
	if (old_tp)
		/* delete old event */
		unregister_trace_uprobe(old_tp);

	ret = register_uprobe_event(tu);
	if (ret) {
		pr_warning("Failed to register probe event(%d)\n", ret);
		goto end;
	}

	list_add_tail(&tu->list, &uprobe_list);

end:
	mutex_unlock(&uprobe_lock);

	return ret;
}

/*
 * Argument syntax:
 *  - Add uprobe: p[:[GRP/]EVENT] PATH:SYMBOL[+offs] [FETCHARGS]
 *
 *  - Remove uprobe: -:[GRP/]EVENT
 */
static int create_trace_uprobe(int argc, char **argv)
{
	struct trace_uprobe *tu;
	struct inode *inode;
	char *arg, *event, *group, *filename;
	char buf[MAX_EVENT_NAME_LEN];
	struct path path;
	unsigned long offset;
	bool is_delete;
	int i, ret;

	inode = NULL;
	ret = 0;
	is_delete = false;
	event = NULL;
	group = NULL;

	/* argc must be >= 1 */
	if (argv[0][0] == '-')
		is_delete = true;
	else if (argv[0][0] != 'p') {
		pr_info("Probe definition must be started with 'p' or '-'.\n");
		return -EINVAL;
	}

	if (argv[0][1] == ':') {
		event = &argv[0][2];
		arg = strchr(event, '/');

		if (arg) {
			group = event;
			event = arg + 1;
			event[-1] = '\0';

			if (strlen(group) == 0) {
				pr_info("Group name is not specified\n");
				return -EINVAL;
			}
		}
		if (strlen(event) == 0) {
			pr_info("Event name is not specified\n");
			return -EINVAL;
		}
	}
	if (!group)
		group = UPROBE_EVENT_SYSTEM;

	if (is_delete) {
		if (!event) {
			pr_info("Delete command needs an event name.\n");
			return -EINVAL;
		}
		mutex_lock(&uprobe_lock);
		tu = find_probe_event(event, group);

		if (!tu) {
			mutex_unlock(&uprobe_lock);
			pr_info("Event %s/%s doesn't exist.\n", group, event);
			return -ENOENT;
		}
		/* delete an event */
		unregister_trace_uprobe(tu);
		mutex_unlock(&uprobe_lock);
		return 0;
	}

	if (argc < 2) {
		pr_info("Probe point is not specified.\n");
		return -EINVAL;
	}
	if (isdigit(argv[1][0])) {
		pr_info("probe point must be have a filename.\n");
		return -EINVAL;
	}
	arg = strchr(argv[1], ':');
	if (!arg)
		goto fail_address_parse;

	*arg++ = '\0';
	filename = argv[1];
	ret = kern_path(filename, LOOKUP_FOLLOW, &path);
	if (ret)
		goto fail_address_parse;

	inode = igrab(path.dentry->d_inode);
	path_put(&path);

	if (!inode || !S_ISREG(inode->i_mode)) {
		ret = -EINVAL;
		goto fail_address_parse;
	}

	ret = kstrtoul(arg, 0, &offset);
	if (ret)
		goto fail_address_parse;

	argc -= 2;
	argv += 2;

	/* setup a probe */
	if (!event) {
		char *tail;
		char *ptr;

		tail = kstrdup(kbasename(filename), GFP_KERNEL);
		if (!tail) {
			ret = -ENOMEM;
			goto fail_address_parse;
		}

		ptr = strpbrk(tail, ".-_");
		if (ptr)
			*ptr = '\0';

		snprintf(buf, MAX_EVENT_NAME_LEN, "%c_%s_0x%lx", 'p', tail, offset);
		event = buf;
		kfree(tail);
	}

	tu = alloc_trace_uprobe(group, event, argc);
	if (IS_ERR(tu)) {
		pr_info("Failed to allocate trace_uprobe.(%d)\n", (int)PTR_ERR(tu));
		ret = PTR_ERR(tu);
		goto fail_address_parse;
	}
	tu->offset = offset;
	tu->inode = inode;
	tu->filename = kstrdup(filename, GFP_KERNEL);

	if (!tu->filename) {
		pr_info("Failed to allocate filename.\n");
		ret = -ENOMEM;
		goto error;
	}

	/* parse arguments */
	ret = 0;
	for (i = 0; i < argc && i < MAX_TRACE_ARGS; i++) {
		/* Increment count for freeing args in error case */
		tu->nr_args++;

		/* Parse argument name */
		arg = strchr(argv[i], '=');
		if (arg) {
			*arg++ = '\0';
			tu->args[i].name = kstrdup(argv[i], GFP_KERNEL);
		} else {
			arg = argv[i];
			/* If argument name is omitted, set "argN" */
			snprintf(buf, MAX_EVENT_NAME_LEN, "arg%d", i + 1);
			tu->args[i].name = kstrdup(buf, GFP_KERNEL);
		}

		if (!tu->args[i].name) {
			pr_info("Failed to allocate argument[%d] name.\n", i);
			ret = -ENOMEM;
			goto error;
		}

		if (!is_good_name(tu->args[i].name)) {
			pr_info("Invalid argument[%d] name: %s\n", i, tu->args[i].name);
			ret = -EINVAL;
			goto error;
		}

		if (traceprobe_conflict_field_name(tu->args[i].name, tu->args, i)) {
			pr_info("Argument[%d] name '%s' conflicts with "
				"another field.\n", i, argv[i]);
			ret = -EINVAL;
			goto error;
		}

		/* Parse fetch argument */
		ret = traceprobe_parse_probe_arg(arg, &tu->size, &tu->args[i], false, false);
		if (ret) {
			pr_info("Parse error at argument[%d]. (%d)\n", i, ret);
			goto error;
		}
	}

	ret = register_trace_uprobe(tu);
	if (ret)
		goto error;
	return 0;

error:
	free_trace_uprobe(tu);
	return ret;

fail_address_parse:
	if (inode)
		iput(inode);

	pr_info("Failed to parse address or file.\n");

	return ret;
}

static void cleanup_all_probes(void)
{
	struct trace_uprobe *tu;

	mutex_lock(&uprobe_lock);
	while (!list_empty(&uprobe_list)) {
		tu = list_entry(uprobe_list.next, struct trace_uprobe, list);
		unregister_trace_uprobe(tu);
	}
	mutex_unlock(&uprobe_lock);
}

/* Probes listing interfaces */
static void *probes_seq_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&uprobe_lock);
	return seq_list_start(&uprobe_list, *pos);
}

static void *probes_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &uprobe_list, pos);
}

static void probes_seq_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&uprobe_lock);
}

static int probes_seq_show(struct seq_file *m, void *v)
{
	struct trace_uprobe *tu = v;
	int i;

	seq_printf(m, "p:%s/%s", tu->call.class->system, tu->call.name);
	seq_printf(m, " %s:0x%p", tu->filename, (void *)tu->offset);

	for (i = 0; i < tu->nr_args; i++)
		seq_printf(m, " %s=%s", tu->args[i].name, tu->args[i].comm);

	seq_printf(m, "\n");
	return 0;
}

static const struct seq_operations probes_seq_op = {
	.start	= probes_seq_start,
	.next	= probes_seq_next,
	.stop	= probes_seq_stop,
	.show	= probes_seq_show
};

static int probes_open(struct inode *inode, struct file *file)
{
	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC))
		cleanup_all_probes();

	return seq_open(file, &probes_seq_op);
}

static ssize_t probes_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	return traceprobe_probes_write(file, buffer, count, ppos, create_trace_uprobe);
}

static const struct file_operations uprobe_events_ops = {
	.owner		= THIS_MODULE,
	.open		= probes_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
	.write		= probes_write,
};

/* Probes profiling interfaces */
static int probes_profile_seq_show(struct seq_file *m, void *v)
{
	struct trace_uprobe *tu = v;

	seq_printf(m, "  %s %-44s %15lu\n", tu->filename, tu->call.name, tu->nhit);
	return 0;
}

static const struct seq_operations profile_seq_op = {
	.start	= probes_seq_start,
	.next	= probes_seq_next,
	.stop	= probes_seq_stop,
	.show	= probes_profile_seq_show
};

static int profile_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &profile_seq_op);
}

static const struct file_operations uprobe_profile_ops = {
	.owner		= THIS_MODULE,
	.open		= profile_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/* uprobe handler */
static int uprobe_trace_func(struct trace_uprobe *tu, struct pt_regs *regs)
{
	struct uprobe_trace_entry_head *entry;
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	u8 *data;
	int size, i, pc;
	unsigned long irq_flags;
	struct ftrace_event_call *call = &tu->call;

	local_save_flags(irq_flags);
	pc = preempt_count();

	size = sizeof(*entry) + tu->size;

	event = trace_current_buffer_lock_reserve(&buffer, call->event.type,
						  size, irq_flags, pc);
	if (!event)
		return 0;

	entry = ring_buffer_event_data(event);
	entry->ip = instruction_pointer(task_pt_regs(current));
	data = (u8 *)&entry[1];
	for (i = 0; i < tu->nr_args; i++)
		call_fetch(&tu->args[i].fetch, regs, data + tu->args[i].offset);

	if (!filter_current_check_discard(buffer, call, entry, event))
		trace_buffer_unlock_commit(buffer, event, irq_flags, pc);

	return 0;
}

/* Event entry printers */
static enum print_line_t
print_uprobe_event(struct trace_iterator *iter, int flags, struct trace_event *event)
{
	struct uprobe_trace_entry_head *field;
	struct trace_seq *s = &iter->seq;
	struct trace_uprobe *tu;
	u8 *data;
	int i;

	field = (struct uprobe_trace_entry_head *)iter->ent;
	tu = container_of(event, struct trace_uprobe, call.event);

	if (!trace_seq_printf(s, "%s: (", tu->call.name))
		goto partial;

	if (!seq_print_ip_sym(s, field->ip, flags | TRACE_ITER_SYM_OFFSET))
		goto partial;

	if (!trace_seq_puts(s, ")"))
		goto partial;

	data = (u8 *)&field[1];
	for (i = 0; i < tu->nr_args; i++) {
		if (!tu->args[i].type->print(s, tu->args[i].name,
					     data + tu->args[i].offset, field))
			goto partial;
	}

	if (trace_seq_puts(s, "\n"))
		return TRACE_TYPE_HANDLED;

partial:
	return TRACE_TYPE_PARTIAL_LINE;
}

static inline bool is_trace_uprobe_enabled(struct trace_uprobe *tu)
{
	return tu->flags & (TP_FLAG_TRACE | TP_FLAG_PROFILE);
}

typedef bool (*filter_func_t)(struct uprobe_consumer *self,
				enum uprobe_filter_ctx ctx,
				struct mm_struct *mm);

static int
probe_event_enable(struct trace_uprobe *tu, int flag, filter_func_t filter)
{
	int ret = 0;

	if (is_trace_uprobe_enabled(tu))
		return -EINTR;

	WARN_ON(!uprobe_filter_is_empty(&tu->filter));

	tu->flags |= flag;
	tu->consumer.filter = filter;
	ret = uprobe_register(tu->inode, tu->offset, &tu->consumer);
	if (ret)
		tu->flags &= ~flag;

	return ret;
}

static void probe_event_disable(struct trace_uprobe *tu, int flag)
{
	if (!is_trace_uprobe_enabled(tu))
		return;

	WARN_ON(!uprobe_filter_is_empty(&tu->filter));

	uprobe_unregister(tu->inode, tu->offset, &tu->consumer);
	tu->flags &= ~flag;
}

static int uprobe_event_define_fields(struct ftrace_event_call *event_call)
{
	int ret, i;
	struct uprobe_trace_entry_head field;
	struct trace_uprobe *tu = (struct trace_uprobe *)event_call->data;

	DEFINE_FIELD(unsigned long, ip, FIELD_STRING_IP, 0);
	/* Set argument names as fields */
	for (i = 0; i < tu->nr_args; i++) {
		ret = trace_define_field(event_call, tu->args[i].type->fmttype,
					 tu->args[i].name,
					 sizeof(field) + tu->args[i].offset,
					 tu->args[i].type->size,
					 tu->args[i].type->is_signed,
					 FILTER_OTHER);

		if (ret)
			return ret;
	}
	return 0;
}

#define LEN_OR_ZERO		(len ? len - pos : 0)
static int __set_print_fmt(struct trace_uprobe *tu, char *buf, int len)
{
	const char *fmt, *arg;
	int i;
	int pos = 0;

	fmt = "(%lx)";
	arg = "REC->" FIELD_STRING_IP;

	/* When len=0, we just calculate the needed length */

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\"%s", fmt);

	for (i = 0; i < tu->nr_args; i++) {
		pos += snprintf(buf + pos, LEN_OR_ZERO, " %s=%s",
				tu->args[i].name, tu->args[i].type->fmt);
	}

	pos += snprintf(buf + pos, LEN_OR_ZERO, "\", %s", arg);

	for (i = 0; i < tu->nr_args; i++) {
		pos += snprintf(buf + pos, LEN_OR_ZERO, ", REC->%s",
				tu->args[i].name);
	}

	return pos;	/* return the length of print_fmt */
}
#undef LEN_OR_ZERO

static int set_print_fmt(struct trace_uprobe *tu)
{
	char *print_fmt;
	int len;

	/* First: called with 0 length to calculate the needed length */
	len = __set_print_fmt(tu, NULL, 0);
	print_fmt = kmalloc(len + 1, GFP_KERNEL);
	if (!print_fmt)
		return -ENOMEM;

	/* Second: actually write the @print_fmt */
	__set_print_fmt(tu, print_fmt, len + 1);
	tu->call.print_fmt = print_fmt;

	return 0;
}

#ifdef CONFIG_PERF_EVENTS
static bool
__uprobe_perf_filter(struct trace_uprobe_filter *filter, struct mm_struct *mm)
{
	struct perf_event *event;

	if (filter->nr_systemwide)
		return true;

	list_for_each_entry(event, &filter->perf_events, hw.tp_list) {
		if (event->hw.tp_target->mm == mm)
			return true;
	}

	return false;
}

static int uprobe_perf_open(struct trace_uprobe *tu, struct perf_event *event)
{
	write_lock(&tu->filter.rwlock);
	if (event->hw.tp_target)
		list_add(&event->hw.tp_list, &tu->filter.perf_events);
	else
		tu->filter.nr_systemwide++;
	write_unlock(&tu->filter.rwlock);

	uprobe_apply(tu->inode, tu->offset, &tu->consumer, true);

	return 0;
}

static int uprobe_perf_close(struct trace_uprobe *tu, struct perf_event *event)
{
	write_lock(&tu->filter.rwlock);
	if (event->hw.tp_target)
		list_del(&event->hw.tp_list);
	else
		tu->filter.nr_systemwide--;
	write_unlock(&tu->filter.rwlock);

	uprobe_apply(tu->inode, tu->offset, &tu->consumer, false);

	return 0;
}

static bool uprobe_perf_filter(struct uprobe_consumer *uc,
				enum uprobe_filter_ctx ctx, struct mm_struct *mm)
{
	struct trace_uprobe *tu;
	int ret;

	tu = container_of(uc, struct trace_uprobe, consumer);
	read_lock(&tu->filter.rwlock);
	ret = __uprobe_perf_filter(&tu->filter, mm);
	read_unlock(&tu->filter.rwlock);

	return ret;
}

/* uprobe profile handler */
static int uprobe_perf_func(struct trace_uprobe *tu, struct pt_regs *regs)
{
	struct ftrace_event_call *call = &tu->call;
	struct uprobe_trace_entry_head *entry;
	struct hlist_head *head;
	u8 *data;
	int size, __size, i;
	int rctx;

	if (!uprobe_perf_filter(&tu->consumer, 0, current->mm))
		return UPROBE_HANDLER_REMOVE;

	__size = sizeof(*entry) + tu->size;
	size = ALIGN(__size + sizeof(u32), sizeof(u64));
	size -= sizeof(u32);
	if (WARN_ONCE(size > PERF_MAX_TRACE_SIZE, "profile buffer not large enough"))
		return 0;

	preempt_disable();

	entry = perf_trace_buf_prepare(size, call->event.type, regs, &rctx);
	if (!entry)
		goto out;

	entry->ip = instruction_pointer(task_pt_regs(current));
	data = (u8 *)&entry[1];
	for (i = 0; i < tu->nr_args; i++)
		call_fetch(&tu->args[i].fetch, regs, data + tu->args[i].offset);

	head = this_cpu_ptr(call->perf_events);
	perf_trace_buf_submit(entry, size, rctx, entry->ip, 1, regs, head, NULL);

 out:
	preempt_enable();
	return 0;
}
#endif	/* CONFIG_PERF_EVENTS */

static
int trace_uprobe_register(struct ftrace_event_call *event, enum trace_reg type, void *data)
{
	struct trace_uprobe *tu = (struct trace_uprobe *)event->data;

	switch (type) {
	case TRACE_REG_REGISTER:
		return probe_event_enable(tu, TP_FLAG_TRACE, NULL);

	case TRACE_REG_UNREGISTER:
		probe_event_disable(tu, TP_FLAG_TRACE);
		return 0;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return probe_event_enable(tu, TP_FLAG_PROFILE, uprobe_perf_filter);

	case TRACE_REG_PERF_UNREGISTER:
		probe_event_disable(tu, TP_FLAG_PROFILE);
		return 0;

	case TRACE_REG_PERF_OPEN:
		return uprobe_perf_open(tu, data);

	case TRACE_REG_PERF_CLOSE:
		return uprobe_perf_close(tu, data);

#endif
	default:
		return 0;
	}
	return 0;
}

static int uprobe_dispatcher(struct uprobe_consumer *con, struct pt_regs *regs)
{
	struct trace_uprobe *tu;
	int ret = 0;

	tu = container_of(con, struct trace_uprobe, consumer);
	tu->nhit++;

	if (tu->flags & TP_FLAG_TRACE)
		ret |= uprobe_trace_func(tu, regs);

#ifdef CONFIG_PERF_EVENTS
	if (tu->flags & TP_FLAG_PROFILE)
		ret |= uprobe_perf_func(tu, regs);
#endif
	return ret;
}

static struct trace_event_functions uprobe_funcs = {
	.trace		= print_uprobe_event
};

static int register_uprobe_event(struct trace_uprobe *tu)
{
	struct ftrace_event_call *call = &tu->call;
	int ret;

	/* Initialize ftrace_event_call */
	INIT_LIST_HEAD(&call->class->fields);
	call->event.funcs = &uprobe_funcs;
	call->class->define_fields = uprobe_event_define_fields;

	if (set_print_fmt(tu) < 0)
		return -ENOMEM;

	ret = register_ftrace_event(&call->event);
	if (!ret) {
		kfree(call->print_fmt);
		return -ENODEV;
	}
	call->flags = 0;
	call->class->reg = trace_uprobe_register;
	call->data = tu;
	ret = trace_add_event_call(call);

	if (ret) {
		pr_info("Failed to register uprobe event: %s\n", call->name);
		kfree(call->print_fmt);
		unregister_ftrace_event(&call->event);
	}

	return ret;
}

static void unregister_uprobe_event(struct trace_uprobe *tu)
{
	/* tu->event is unregistered in trace_remove_event_call() */
	trace_remove_event_call(&tu->call);
	kfree(tu->call.print_fmt);
	tu->call.print_fmt = NULL;
}

/* Make a trace interface for controling probe points */
static __init int init_uprobe_trace(void)
{
	struct dentry *d_tracer;

	d_tracer = tracing_init_dentry();
	if (!d_tracer)
		return 0;

	trace_create_file("uprobe_events", 0644, d_tracer,
				    NULL, &uprobe_events_ops);
	/* Profile interface */
	trace_create_file("uprobe_profile", 0444, d_tracer,
				    NULL, &uprobe_profile_ops);
	return 0;
}

fs_initcall(init_uprobe_trace);
