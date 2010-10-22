/*
 * event tracer
 *
 * Copyright (C) 2008 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 *  - Added format output of fields of the trace point.
 *    This was based off of work by Tom Zanussi <tzanussi@gmail.com>.
 *
 */

#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <asm/setup.h>

#include "trace_output.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM "TRACE_SYSTEM"

DEFINE_MUTEX(event_mutex);

LIST_HEAD(ftrace_events);
LIST_HEAD(ftrace_common_fields);

struct list_head *
trace_get_fields(struct ftrace_event_call *event_call)
{
	if (!event_call->class->get_fields)
		return &event_call->class->fields;
	return event_call->class->get_fields(event_call);
}

static int __trace_define_field(struct list_head *head, const char *type,
				const char *name, int offset, int size,
				int is_signed, int filter_type)
{
	struct ftrace_event_field *field;

	field = kzalloc(sizeof(*field), GFP_KERNEL);
	if (!field)
		goto err;

	field->name = kstrdup(name, GFP_KERNEL);
	if (!field->name)
		goto err;

	field->type = kstrdup(type, GFP_KERNEL);
	if (!field->type)
		goto err;

	if (filter_type == FILTER_OTHER)
		field->filter_type = filter_assign_type(type);
	else
		field->filter_type = filter_type;

	field->offset = offset;
	field->size = size;
	field->is_signed = is_signed;

	list_add(&field->link, head);

	return 0;

err:
	if (field)
		kfree(field->name);
	kfree(field);

	return -ENOMEM;
}

int trace_define_field(struct ftrace_event_call *call, const char *type,
		       const char *name, int offset, int size, int is_signed,
		       int filter_type)
{
	struct list_head *head;

	if (WARN_ON(!call->class))
		return 0;

	head = trace_get_fields(call);
	return __trace_define_field(head, type, name, offset, size,
				    is_signed, filter_type);
}
EXPORT_SYMBOL_GPL(trace_define_field);

#define __common_field(type, item)					\
	ret = __trace_define_field(&ftrace_common_fields, #type,	\
				   "common_" #item,			\
				   offsetof(typeof(ent), item),		\
				   sizeof(ent.item),			\
				   is_signed_type(type), FILTER_OTHER);	\
	if (ret)							\
		return ret;

static int trace_define_common_fields(void)
{
	int ret;
	struct trace_entry ent;

	__common_field(unsigned short, type);
	__common_field(unsigned char, flags);
	__common_field(unsigned char, preempt_count);
	__common_field(int, pid);
	__common_field(int, lock_depth);

	return ret;
}

void trace_destroy_fields(struct ftrace_event_call *call)
{
	struct ftrace_event_field *field, *next;
	struct list_head *head;

	head = trace_get_fields(call);
	list_for_each_entry_safe(field, next, head, link) {
		list_del(&field->link);
		kfree(field->type);
		kfree(field->name);
		kfree(field);
	}
}

int trace_event_raw_init(struct ftrace_event_call *call)
{
	int id;

	id = register_ftrace_event(&call->event);
	if (!id)
		return -ENODEV;

	return 0;
}
EXPORT_SYMBOL_GPL(trace_event_raw_init);

int ftrace_event_reg(struct ftrace_event_call *call, enum trace_reg type)
{
	switch (type) {
	case TRACE_REG_REGISTER:
		return tracepoint_probe_register(call->name,
						 call->class->probe,
						 call);
	case TRACE_REG_UNREGISTER:
		tracepoint_probe_unregister(call->name,
					    call->class->probe,
					    call);
		return 0;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return tracepoint_probe_register(call->name,
						 call->class->perf_probe,
						 call);
	case TRACE_REG_PERF_UNREGISTER:
		tracepoint_probe_unregister(call->name,
					    call->class->perf_probe,
					    call);
		return 0;
#endif
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ftrace_event_reg);

void trace_event_enable_cmd_record(bool enable)
{
	struct ftrace_event_call *call;

	mutex_lock(&event_mutex);
	list_for_each_entry(call, &ftrace_events, list) {
		if (!(call->flags & TRACE_EVENT_FL_ENABLED))
			continue;

		if (enable) {
			tracing_start_cmdline_record();
			call->flags |= TRACE_EVENT_FL_RECORDED_CMD;
		} else {
			tracing_stop_cmdline_record();
			call->flags &= ~TRACE_EVENT_FL_RECORDED_CMD;
		}
	}
	mutex_unlock(&event_mutex);
}

static int ftrace_event_enable_disable(struct ftrace_event_call *call,
					int enable)
{
	int ret = 0;

	switch (enable) {
	case 0:
		if (call->flags & TRACE_EVENT_FL_ENABLED) {
			call->flags &= ~TRACE_EVENT_FL_ENABLED;
			if (call->flags & TRACE_EVENT_FL_RECORDED_CMD) {
				tracing_stop_cmdline_record();
				call->flags &= ~TRACE_EVENT_FL_RECORDED_CMD;
			}
			call->class->reg(call, TRACE_REG_UNREGISTER);
		}
		break;
	case 1:
		if (!(call->flags & TRACE_EVENT_FL_ENABLED)) {
			if (trace_flags & TRACE_ITER_RECORD_CMD) {
				tracing_start_cmdline_record();
				call->flags |= TRACE_EVENT_FL_RECORDED_CMD;
			}
			ret = call->class->reg(call, TRACE_REG_REGISTER);
			if (ret) {
				tracing_stop_cmdline_record();
				pr_info("event trace: Could not enable event "
					"%s\n", call->name);
				break;
			}
			call->flags |= TRACE_EVENT_FL_ENABLED;
		}
		break;
	}

	return ret;
}

static void ftrace_clear_events(void)
{
	struct ftrace_event_call *call;

	mutex_lock(&event_mutex);
	list_for_each_entry(call, &ftrace_events, list) {
		ftrace_event_enable_disable(call, 0);
	}
	mutex_unlock(&event_mutex);
}

/*
 * __ftrace_set_clr_event(NULL, NULL, NULL, set) will set/unset all events.
 */
static int __ftrace_set_clr_event(const char *match, const char *sub,
				  const char *event, int set)
{
	struct ftrace_event_call *call;
	int ret = -EINVAL;

	mutex_lock(&event_mutex);
	list_for_each_entry(call, &ftrace_events, list) {

		if (!call->name || !call->class || !call->class->reg)
			continue;

		if (match &&
		    strcmp(match, call->name) != 0 &&
		    strcmp(match, call->class->system) != 0)
			continue;

		if (sub && strcmp(sub, call->class->system) != 0)
			continue;

		if (event && strcmp(event, call->name) != 0)
			continue;

		ftrace_event_enable_disable(call, set);

		ret = 0;
	}
	mutex_unlock(&event_mutex);

	return ret;
}

static int ftrace_set_clr_event(char *buf, int set)
{
	char *event = NULL, *sub = NULL, *match;

	/*
	 * The buf format can be <subsystem>:<event-name>
	 *  *:<event-name> means any event by that name.
	 *  :<event-name> is the same.
	 *
	 *  <subsystem>:* means all events in that subsystem
	 *  <subsystem>: means the same.
	 *
	 *  <name> (no ':') means all events in a subsystem with
	 *  the name <name> or any event that matches <name>
	 */

	match = strsep(&buf, ":");
	if (buf) {
		sub = match;
		event = buf;
		match = NULL;

		if (!strlen(sub) || strcmp(sub, "*") == 0)
			sub = NULL;
		if (!strlen(event) || strcmp(event, "*") == 0)
			event = NULL;
	}

	return __ftrace_set_clr_event(match, sub, event, set);
}

/**
 * trace_set_clr_event - enable or disable an event
 * @system: system name to match (NULL for any system)
 * @event: event name to match (NULL for all events, within system)
 * @set: 1 to enable, 0 to disable
 *
 * This is a way for other parts of the kernel to enable or disable
 * event recording.
 *
 * Returns 0 on success, -EINVAL if the parameters do not match any
 * registered events.
 */
int trace_set_clr_event(const char *system, const char *event, int set)
{
	return __ftrace_set_clr_event(NULL, system, event, set);
}

/* 128 should be much more than enough */
#define EVENT_BUF_SIZE		127

static ssize_t
ftrace_event_write(struct file *file, const char __user *ubuf,
		   size_t cnt, loff_t *ppos)
{
	struct trace_parser parser;
	ssize_t read, ret;

	if (!cnt)
		return 0;

	ret = tracing_update_buffers();
	if (ret < 0)
		return ret;

	if (trace_parser_get_init(&parser, EVENT_BUF_SIZE + 1))
		return -ENOMEM;

	read = trace_get_user(&parser, ubuf, cnt, ppos);

	if (read >= 0 && trace_parser_loaded((&parser))) {
		int set = 1;

		if (*parser.buffer == '!')
			set = 0;

		parser.buffer[parser.idx] = 0;

		ret = ftrace_set_clr_event(parser.buffer + !set, set);
		if (ret)
			goto out_put;
	}

	ret = read;

 out_put:
	trace_parser_put(&parser);

	return ret;
}

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ftrace_event_call *call = v;

	(*pos)++;

	list_for_each_entry_continue(call, &ftrace_events, list) {
		/*
		 * The ftrace subsystem is for showing formats only.
		 * They can not be enabled or disabled via the event files.
		 */
		if (call->class && call->class->reg)
			return call;
	}

	return NULL;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_event_call *call;
	loff_t l;

	mutex_lock(&event_mutex);

	call = list_entry(&ftrace_events, struct ftrace_event_call, list);
	for (l = 0; l <= *pos; ) {
		call = t_next(m, call, &l);
		if (!call)
			break;
	}
	return call;
}

static void *
s_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ftrace_event_call *call = v;

	(*pos)++;

	list_for_each_entry_continue(call, &ftrace_events, list) {
		if (call->flags & TRACE_EVENT_FL_ENABLED)
			return call;
	}

	return NULL;
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_event_call *call;
	loff_t l;

	mutex_lock(&event_mutex);

	call = list_entry(&ftrace_events, struct ftrace_event_call, list);
	for (l = 0; l <= *pos; ) {
		call = s_next(m, call, &l);
		if (!call)
			break;
	}
	return call;
}

static int t_show(struct seq_file *m, void *v)
{
	struct ftrace_event_call *call = v;

	if (strcmp(call->class->system, TRACE_SYSTEM) != 0)
		seq_printf(m, "%s:", call->class->system);
	seq_printf(m, "%s\n", call->name);

	return 0;
}

static void t_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&event_mutex);
}

static int
ftrace_event_seq_open(struct inode *inode, struct file *file)
{
	const struct seq_operations *seq_ops;

	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC))
		ftrace_clear_events();

	seq_ops = inode->i_private;
	return seq_open(file, seq_ops);
}

static ssize_t
event_enable_read(struct file *filp, char __user *ubuf, size_t cnt,
		  loff_t *ppos)
{
	struct ftrace_event_call *call = filp->private_data;
	char *buf;

	if (call->flags & TRACE_EVENT_FL_ENABLED)
		buf = "1\n";
	else
		buf = "0\n";

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, 2);
}

static ssize_t
event_enable_write(struct file *filp, const char __user *ubuf, size_t cnt,
		   loff_t *ppos)
{
	struct ftrace_event_call *call = filp->private_data;
	char buf[64];
	unsigned long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	ret = tracing_update_buffers();
	if (ret < 0)
		return ret;

	switch (val) {
	case 0:
	case 1:
		mutex_lock(&event_mutex);
		ret = ftrace_event_enable_disable(call, val);
		mutex_unlock(&event_mutex);
		break;

	default:
		return -EINVAL;
	}

	*ppos += cnt;

	return ret ? ret : cnt;
}

static ssize_t
system_enable_read(struct file *filp, char __user *ubuf, size_t cnt,
		   loff_t *ppos)
{
	const char set_to_char[4] = { '?', '0', '1', 'X' };
	const char *system = filp->private_data;
	struct ftrace_event_call *call;
	char buf[2];
	int set = 0;
	int ret;

	mutex_lock(&event_mutex);
	list_for_each_entry(call, &ftrace_events, list) {
		if (!call->name || !call->class || !call->class->reg)
			continue;

		if (system && strcmp(call->class->system, system) != 0)
			continue;

		/*
		 * We need to find out if all the events are set
		 * or if all events or cleared, or if we have
		 * a mixture.
		 */
		set |= (1 << !!(call->flags & TRACE_EVENT_FL_ENABLED));

		/*
		 * If we have a mixture, no need to look further.
		 */
		if (set == 3)
			break;
	}
	mutex_unlock(&event_mutex);

	buf[0] = set_to_char[set];
	buf[1] = '\n';

	ret = simple_read_from_buffer(ubuf, cnt, ppos, buf, 2);

	return ret;
}

static ssize_t
system_enable_write(struct file *filp, const char __user *ubuf, size_t cnt,
		    loff_t *ppos)
{
	const char *system = filp->private_data;
	unsigned long val;
	char buf[64];
	ssize_t ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	ret = tracing_update_buffers();
	if (ret < 0)
		return ret;

	if (val != 0 && val != 1)
		return -EINVAL;

	ret = __ftrace_set_clr_event(NULL, system, NULL, val);
	if (ret)
		goto out;

	ret = cnt;

out:
	*ppos += cnt;

	return ret;
}

enum {
	FORMAT_HEADER		= 1,
	FORMAT_FIELD_SEPERATOR	= 2,
	FORMAT_PRINTFMT		= 3,
};

static void *f_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ftrace_event_call *call = m->private;
	struct ftrace_event_field *field;
	struct list_head *common_head = &ftrace_common_fields;
	struct list_head *head = trace_get_fields(call);

	(*pos)++;

	switch ((unsigned long)v) {
	case FORMAT_HEADER:
		if (unlikely(list_empty(common_head)))
			return NULL;

		field = list_entry(common_head->prev,
				   struct ftrace_event_field, link);
		return field;

	case FORMAT_FIELD_SEPERATOR:
		if (unlikely(list_empty(head)))
			return NULL;

		field = list_entry(head->prev, struct ftrace_event_field, link);
		return field;

	case FORMAT_PRINTFMT:
		/* all done */
		return NULL;
	}

	field = v;
	if (field->link.prev == common_head)
		return (void *)FORMAT_FIELD_SEPERATOR;
	else if (field->link.prev == head)
		return (void *)FORMAT_PRINTFMT;

	field = list_entry(field->link.prev, struct ftrace_event_field, link);

	return field;
}

static void *f_start(struct seq_file *m, loff_t *pos)
{
	loff_t l = 0;
	void *p;

	/* Start by showing the header */
	if (!*pos)
		return (void *)FORMAT_HEADER;

	p = (void *)FORMAT_HEADER;
	do {
		p = f_next(m, p, &l);
	} while (p && l < *pos);

	return p;
}

static int f_show(struct seq_file *m, void *v)
{
	struct ftrace_event_call *call = m->private;
	struct ftrace_event_field *field;
	const char *array_descriptor;

	switch ((unsigned long)v) {
	case FORMAT_HEADER:
		seq_printf(m, "name: %s\n", call->name);
		seq_printf(m, "ID: %d\n", call->event.type);
		seq_printf(m, "format:\n");
		return 0;

	case FORMAT_FIELD_SEPERATOR:
		seq_putc(m, '\n');
		return 0;

	case FORMAT_PRINTFMT:
		seq_printf(m, "\nprint fmt: %s\n",
			   call->print_fmt);
		return 0;
	}

	field = v;

	/*
	 * Smartly shows the array type(except dynamic array).
	 * Normal:
	 *	field:TYPE VAR
	 * If TYPE := TYPE[LEN], it is shown:
	 *	field:TYPE VAR[LEN]
	 */
	array_descriptor = strchr(field->type, '[');

	if (!strncmp(field->type, "__data_loc", 10))
		array_descriptor = NULL;

	if (!array_descriptor)
		seq_printf(m, "\tfield:%s %s;\toffset:%u;\tsize:%u;\tsigned:%d;\n",
			   field->type, field->name, field->offset,
			   field->size, !!field->is_signed);
	else
		seq_printf(m, "\tfield:%.*s %s%s;\toffset:%u;\tsize:%u;\tsigned:%d;\n",
			   (int)(array_descriptor - field->type),
			   field->type, field->name,
			   array_descriptor, field->offset,
			   field->size, !!field->is_signed);

	return 0;
}

static void f_stop(struct seq_file *m, void *p)
{
}

static const struct seq_operations trace_format_seq_ops = {
	.start		= f_start,
	.next		= f_next,
	.stop		= f_stop,
	.show		= f_show,
};

static int trace_format_open(struct inode *inode, struct file *file)
{
	struct ftrace_event_call *call = inode->i_private;
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &trace_format_seq_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = call;

	return 0;
}

static ssize_t
event_id_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct ftrace_event_call *call = filp->private_data;
	struct trace_seq *s;
	int r;

	if (*ppos)
		return 0;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);
	trace_seq_printf(s, "%d\n", call->event.type);

	r = simple_read_from_buffer(ubuf, cnt, ppos,
				    s->buffer, s->len);
	kfree(s);
	return r;
}

static ssize_t
event_filter_read(struct file *filp, char __user *ubuf, size_t cnt,
		  loff_t *ppos)
{
	struct ftrace_event_call *call = filp->private_data;
	struct trace_seq *s;
	int r;

	if (*ppos)
		return 0;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	print_event_filter(call, s);
	r = simple_read_from_buffer(ubuf, cnt, ppos, s->buffer, s->len);

	kfree(s);

	return r;
}

static ssize_t
event_filter_write(struct file *filp, const char __user *ubuf, size_t cnt,
		   loff_t *ppos)
{
	struct ftrace_event_call *call = filp->private_data;
	char *buf;
	int err;

	if (cnt >= PAGE_SIZE)
		return -EINVAL;

	buf = (char *)__get_free_page(GFP_TEMPORARY);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, cnt)) {
		free_page((unsigned long) buf);
		return -EFAULT;
	}
	buf[cnt] = '\0';

	err = apply_event_filter(call, buf);
	free_page((unsigned long) buf);
	if (err < 0)
		return err;

	*ppos += cnt;

	return cnt;
}

static ssize_t
subsystem_filter_read(struct file *filp, char __user *ubuf, size_t cnt,
		      loff_t *ppos)
{
	struct event_subsystem *system = filp->private_data;
	struct trace_seq *s;
	int r;

	if (*ppos)
		return 0;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	print_subsystem_event_filter(system, s);
	r = simple_read_from_buffer(ubuf, cnt, ppos, s->buffer, s->len);

	kfree(s);

	return r;
}

static ssize_t
subsystem_filter_write(struct file *filp, const char __user *ubuf, size_t cnt,
		       loff_t *ppos)
{
	struct event_subsystem *system = filp->private_data;
	char *buf;
	int err;

	if (cnt >= PAGE_SIZE)
		return -EINVAL;

	buf = (char *)__get_free_page(GFP_TEMPORARY);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, cnt)) {
		free_page((unsigned long) buf);
		return -EFAULT;
	}
	buf[cnt] = '\0';

	err = apply_subsystem_event_filter(system, buf);
	free_page((unsigned long) buf);
	if (err < 0)
		return err;

	*ppos += cnt;

	return cnt;
}

static ssize_t
show_header(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int (*func)(struct trace_seq *s) = filp->private_data;
	struct trace_seq *s;
	int r;

	if (*ppos)
		return 0;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	func(s);
	r = simple_read_from_buffer(ubuf, cnt, ppos, s->buffer, s->len);

	kfree(s);

	return r;
}

static const struct seq_operations show_event_seq_ops = {
	.start = t_start,
	.next = t_next,
	.show = t_show,
	.stop = t_stop,
};

static const struct seq_operations show_set_event_seq_ops = {
	.start = s_start,
	.next = s_next,
	.show = t_show,
	.stop = t_stop,
};

static const struct file_operations ftrace_avail_fops = {
	.open = ftrace_event_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static const struct file_operations ftrace_set_event_fops = {
	.open = ftrace_event_seq_open,
	.read = seq_read,
	.write = ftrace_event_write,
	.llseek = seq_lseek,
	.release = seq_release,
};

static const struct file_operations ftrace_enable_fops = {
	.open = tracing_open_generic,
	.read = event_enable_read,
	.write = event_enable_write,
	.llseek = default_llseek,
};

static const struct file_operations ftrace_event_format_fops = {
	.open = trace_format_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static const struct file_operations ftrace_event_id_fops = {
	.open = tracing_open_generic,
	.read = event_id_read,
	.llseek = default_llseek,
};

static const struct file_operations ftrace_event_filter_fops = {
	.open = tracing_open_generic,
	.read = event_filter_read,
	.write = event_filter_write,
	.llseek = default_llseek,
};

static const struct file_operations ftrace_subsystem_filter_fops = {
	.open = tracing_open_generic,
	.read = subsystem_filter_read,
	.write = subsystem_filter_write,
	.llseek = default_llseek,
};

static const struct file_operations ftrace_system_enable_fops = {
	.open = tracing_open_generic,
	.read = system_enable_read,
	.write = system_enable_write,
	.llseek = default_llseek,
};

static const struct file_operations ftrace_show_header_fops = {
	.open = tracing_open_generic,
	.read = show_header,
	.llseek = default_llseek,
};

static struct dentry *event_trace_events_dir(void)
{
	static struct dentry *d_tracer;
	static struct dentry *d_events;

	if (d_events)
		return d_events;

	d_tracer = tracing_init_dentry();
	if (!d_tracer)
		return NULL;

	d_events = debugfs_create_dir("events", d_tracer);
	if (!d_events)
		pr_warning("Could not create debugfs "
			   "'events' directory\n");

	return d_events;
}

static LIST_HEAD(event_subsystems);

static struct dentry *
event_subsystem_dir(const char *name, struct dentry *d_events)
{
	struct event_subsystem *system;
	struct dentry *entry;

	/* First see if we did not already create this dir */
	list_for_each_entry(system, &event_subsystems, list) {
		if (strcmp(system->name, name) == 0) {
			system->nr_events++;
			return system->entry;
		}
	}

	/* need to create new entry */
	system = kmalloc(sizeof(*system), GFP_KERNEL);
	if (!system) {
		pr_warning("No memory to create event subsystem %s\n",
			   name);
		return d_events;
	}

	system->entry = debugfs_create_dir(name, d_events);
	if (!system->entry) {
		pr_warning("Could not create event subsystem %s\n",
			   name);
		kfree(system);
		return d_events;
	}

	system->nr_events = 1;
	system->name = kstrdup(name, GFP_KERNEL);
	if (!system->name) {
		debugfs_remove(system->entry);
		kfree(system);
		return d_events;
	}

	list_add(&system->list, &event_subsystems);

	system->filter = NULL;

	system->filter = kzalloc(sizeof(struct event_filter), GFP_KERNEL);
	if (!system->filter) {
		pr_warning("Could not allocate filter for subsystem "
			   "'%s'\n", name);
		return system->entry;
	}

	entry = debugfs_create_file("filter", 0644, system->entry, system,
				    &ftrace_subsystem_filter_fops);
	if (!entry) {
		kfree(system->filter);
		system->filter = NULL;
		pr_warning("Could not create debugfs "
			   "'%s/filter' entry\n", name);
	}

	trace_create_file("enable", 0644, system->entry,
			  (void *)system->name,
			  &ftrace_system_enable_fops);

	return system->entry;
}

static int
event_create_dir(struct ftrace_event_call *call, struct dentry *d_events,
		 const struct file_operations *id,
		 const struct file_operations *enable,
		 const struct file_operations *filter,
		 const struct file_operations *format)
{
	struct list_head *head;
	int ret;

	/*
	 * If the trace point header did not define TRACE_SYSTEM
	 * then the system would be called "TRACE_SYSTEM".
	 */
	if (strcmp(call->class->system, TRACE_SYSTEM) != 0)
		d_events = event_subsystem_dir(call->class->system, d_events);

	call->dir = debugfs_create_dir(call->name, d_events);
	if (!call->dir) {
		pr_warning("Could not create debugfs "
			   "'%s' directory\n", call->name);
		return -1;
	}

	if (call->class->reg)
		trace_create_file("enable", 0644, call->dir, call,
				  enable);

#ifdef CONFIG_PERF_EVENTS
	if (call->event.type && call->class->reg)
		trace_create_file("id", 0444, call->dir, call,
		 		  id);
#endif

	/*
	 * Other events may have the same class. Only update
	 * the fields if they are not already defined.
	 */
	head = trace_get_fields(call);
	if (list_empty(head)) {
		ret = call->class->define_fields(call);
		if (ret < 0) {
			pr_warning("Could not initialize trace point"
				   " events/%s\n", call->name);
			return ret;
		}
	}
	trace_create_file("filter", 0644, call->dir, call,
			  filter);

	trace_create_file("format", 0444, call->dir, call,
			  format);

	return 0;
}

static int
__trace_add_event_call(struct ftrace_event_call *call, struct module *mod,
		       const struct file_operations *id,
		       const struct file_operations *enable,
		       const struct file_operations *filter,
		       const struct file_operations *format)
{
	struct dentry *d_events;
	int ret;

	/* The linker may leave blanks */
	if (!call->name)
		return -EINVAL;

	if (call->class->raw_init) {
		ret = call->class->raw_init(call);
		if (ret < 0) {
			if (ret != -ENOSYS)
				pr_warning("Could not initialize trace events/%s\n",
					   call->name);
			return ret;
		}
	}

	d_events = event_trace_events_dir();
	if (!d_events)
		return -ENOENT;

	ret = event_create_dir(call, d_events, id, enable, filter, format);
	if (!ret)
		list_add(&call->list, &ftrace_events);
	call->mod = mod;

	return ret;
}

/* Add an additional event_call dynamically */
int trace_add_event_call(struct ftrace_event_call *call)
{
	int ret;
	mutex_lock(&event_mutex);
	ret = __trace_add_event_call(call, NULL, &ftrace_event_id_fops,
				     &ftrace_enable_fops,
				     &ftrace_event_filter_fops,
				     &ftrace_event_format_fops);
	mutex_unlock(&event_mutex);
	return ret;
}

static void remove_subsystem_dir(const char *name)
{
	struct event_subsystem *system;

	if (strcmp(name, TRACE_SYSTEM) == 0)
		return;

	list_for_each_entry(system, &event_subsystems, list) {
		if (strcmp(system->name, name) == 0) {
			if (!--system->nr_events) {
				struct event_filter *filter = system->filter;

				debugfs_remove_recursive(system->entry);
				list_del(&system->list);
				if (filter) {
					kfree(filter->filter_string);
					kfree(filter);
				}
				kfree(system->name);
				kfree(system);
			}
			break;
		}
	}
}

/*
 * Must be called under locking both of event_mutex and trace_event_mutex.
 */
static void __trace_remove_event_call(struct ftrace_event_call *call)
{
	ftrace_event_enable_disable(call, 0);
	if (call->event.funcs)
		__unregister_ftrace_event(&call->event);
	debugfs_remove_recursive(call->dir);
	list_del(&call->list);
	trace_destroy_fields(call);
	destroy_preds(call);
	remove_subsystem_dir(call->class->system);
}

/* Remove an event_call */
void trace_remove_event_call(struct ftrace_event_call *call)
{
	mutex_lock(&event_mutex);
	down_write(&trace_event_mutex);
	__trace_remove_event_call(call);
	up_write(&trace_event_mutex);
	mutex_unlock(&event_mutex);
}

#define for_each_event(event, start, end)			\
	for (event = start;					\
	     (unsigned long)event < (unsigned long)end;		\
	     event++)

#ifdef CONFIG_MODULES

static LIST_HEAD(ftrace_module_file_list);

/*
 * Modules must own their file_operations to keep up with
 * reference counting.
 */
struct ftrace_module_file_ops {
	struct list_head		list;
	struct module			*mod;
	struct file_operations		id;
	struct file_operations		enable;
	struct file_operations		format;
	struct file_operations		filter;
};

static struct ftrace_module_file_ops *
trace_create_file_ops(struct module *mod)
{
	struct ftrace_module_file_ops *file_ops;

	/*
	 * This is a bit of a PITA. To allow for correct reference
	 * counting, modules must "own" their file_operations.
	 * To do this, we allocate the file operations that will be
	 * used in the event directory.
	 */

	file_ops = kmalloc(sizeof(*file_ops), GFP_KERNEL);
	if (!file_ops)
		return NULL;

	file_ops->mod = mod;

	file_ops->id = ftrace_event_id_fops;
	file_ops->id.owner = mod;

	file_ops->enable = ftrace_enable_fops;
	file_ops->enable.owner = mod;

	file_ops->filter = ftrace_event_filter_fops;
	file_ops->filter.owner = mod;

	file_ops->format = ftrace_event_format_fops;
	file_ops->format.owner = mod;

	list_add(&file_ops->list, &ftrace_module_file_list);

	return file_ops;
}

static void trace_module_add_events(struct module *mod)
{
	struct ftrace_module_file_ops *file_ops = NULL;
	struct ftrace_event_call *call, *start, *end;

	start = mod->trace_events;
	end = mod->trace_events + mod->num_trace_events;

	if (start == end)
		return;

	file_ops = trace_create_file_ops(mod);
	if (!file_ops)
		return;

	for_each_event(call, start, end) {
		__trace_add_event_call(call, mod,
				       &file_ops->id, &file_ops->enable,
				       &file_ops->filter, &file_ops->format);
	}
}

static void trace_module_remove_events(struct module *mod)
{
	struct ftrace_module_file_ops *file_ops;
	struct ftrace_event_call *call, *p;
	bool found = false;

	down_write(&trace_event_mutex);
	list_for_each_entry_safe(call, p, &ftrace_events, list) {
		if (call->mod == mod) {
			found = true;
			__trace_remove_event_call(call);
		}
	}

	/* Now free the file_operations */
	list_for_each_entry(file_ops, &ftrace_module_file_list, list) {
		if (file_ops->mod == mod)
			break;
	}
	if (&file_ops->list != &ftrace_module_file_list) {
		list_del(&file_ops->list);
		kfree(file_ops);
	}

	/*
	 * It is safest to reset the ring buffer if the module being unloaded
	 * registered any events.
	 */
	if (found)
		tracing_reset_current_online_cpus();
	up_write(&trace_event_mutex);
}

static int trace_module_notify(struct notifier_block *self,
			       unsigned long val, void *data)
{
	struct module *mod = data;

	mutex_lock(&event_mutex);
	switch (val) {
	case MODULE_STATE_COMING:
		trace_module_add_events(mod);
		break;
	case MODULE_STATE_GOING:
		trace_module_remove_events(mod);
		break;
	}
	mutex_unlock(&event_mutex);

	return 0;
}
#else
static int trace_module_notify(struct notifier_block *self,
			       unsigned long val, void *data)
{
	return 0;
}
#endif /* CONFIG_MODULES */

static struct notifier_block trace_module_nb = {
	.notifier_call = trace_module_notify,
	.priority = 0,
};

extern struct ftrace_event_call __start_ftrace_events[];
extern struct ftrace_event_call __stop_ftrace_events[];

static char bootup_event_buf[COMMAND_LINE_SIZE] __initdata;

static __init int setup_trace_event(char *str)
{
	strlcpy(bootup_event_buf, str, COMMAND_LINE_SIZE);
	ring_buffer_expanded = 1;
	tracing_selftest_disabled = 1;

	return 1;
}
__setup("trace_event=", setup_trace_event);

static __init int event_trace_init(void)
{
	struct ftrace_event_call *call;
	struct dentry *d_tracer;
	struct dentry *entry;
	struct dentry *d_events;
	int ret;
	char *buf = bootup_event_buf;
	char *token;

	d_tracer = tracing_init_dentry();
	if (!d_tracer)
		return 0;

	entry = debugfs_create_file("available_events", 0444, d_tracer,
				    (void *)&show_event_seq_ops,
				    &ftrace_avail_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'available_events' entry\n");

	entry = debugfs_create_file("set_event", 0644, d_tracer,
				    (void *)&show_set_event_seq_ops,
				    &ftrace_set_event_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'set_event' entry\n");

	d_events = event_trace_events_dir();
	if (!d_events)
		return 0;

	/* ring buffer internal formats */
	trace_create_file("header_page", 0444, d_events,
			  ring_buffer_print_page_header,
			  &ftrace_show_header_fops);

	trace_create_file("header_event", 0444, d_events,
			  ring_buffer_print_entry_header,
			  &ftrace_show_header_fops);

	trace_create_file("enable", 0644, d_events,
			  NULL, &ftrace_system_enable_fops);

	if (trace_define_common_fields())
		pr_warning("tracing: Failed to allocate common fields");

	for_each_event(call, __start_ftrace_events, __stop_ftrace_events) {
		__trace_add_event_call(call, NULL, &ftrace_event_id_fops,
				       &ftrace_enable_fops,
				       &ftrace_event_filter_fops,
				       &ftrace_event_format_fops);
	}

	while (true) {
		token = strsep(&buf, ",");

		if (!token)
			break;
		if (!*token)
			continue;

		ret = ftrace_set_clr_event(token, 1);
		if (ret)
			pr_warning("Failed to enable trace event: %s\n", token);
	}

	ret = register_module_notifier(&trace_module_nb);
	if (ret)
		pr_warning("Failed to register trace events module notifier\n");

	return 0;
}
fs_initcall(event_trace_init);

#ifdef CONFIG_FTRACE_STARTUP_TEST

static DEFINE_SPINLOCK(test_spinlock);
static DEFINE_SPINLOCK(test_spinlock_irq);
static DEFINE_MUTEX(test_mutex);

static __init void test_work(struct work_struct *dummy)
{
	spin_lock(&test_spinlock);
	spin_lock_irq(&test_spinlock_irq);
	udelay(1);
	spin_unlock_irq(&test_spinlock_irq);
	spin_unlock(&test_spinlock);

	mutex_lock(&test_mutex);
	msleep(1);
	mutex_unlock(&test_mutex);
}

static __init int event_test_thread(void *unused)
{
	void *test_malloc;

	test_malloc = kmalloc(1234, GFP_KERNEL);
	if (!test_malloc)
		pr_info("failed to kmalloc\n");

	schedule_on_each_cpu(test_work);

	kfree(test_malloc);

	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop())
		schedule();

	return 0;
}

/*
 * Do various things that may trigger events.
 */
static __init void event_test_stuff(void)
{
	struct task_struct *test_thread;

	test_thread = kthread_run(event_test_thread, NULL, "test-events");
	msleep(1);
	kthread_stop(test_thread);
}

/*
 * For every trace event defined, we will test each trace point separately,
 * and then by groups, and finally all trace points.
 */
static __init void event_trace_self_tests(void)
{
	struct ftrace_event_call *call;
	struct event_subsystem *system;
	int ret;

	pr_info("Running tests on trace events:\n");

	list_for_each_entry(call, &ftrace_events, list) {

		/* Only test those that have a probe */
		if (!call->class || !call->class->probe)
			continue;

/*
 * Testing syscall events here is pretty useless, but
 * we still do it if configured. But this is time consuming.
 * What we really need is a user thread to perform the
 * syscalls as we test.
 */
#ifndef CONFIG_EVENT_TRACE_TEST_SYSCALLS
		if (call->class->system &&
		    strcmp(call->class->system, "syscalls") == 0)
			continue;
#endif

		pr_info("Testing event %s: ", call->name);

		/*
		 * If an event is already enabled, someone is using
		 * it and the self test should not be on.
		 */
		if (call->flags & TRACE_EVENT_FL_ENABLED) {
			pr_warning("Enabled event during self test!\n");
			WARN_ON_ONCE(1);
			continue;
		}

		ftrace_event_enable_disable(call, 1);
		event_test_stuff();
		ftrace_event_enable_disable(call, 0);

		pr_cont("OK\n");
	}

	/* Now test at the sub system level */

	pr_info("Running tests on trace event systems:\n");

	list_for_each_entry(system, &event_subsystems, list) {

		/* the ftrace system is special, skip it */
		if (strcmp(system->name, "ftrace") == 0)
			continue;

		pr_info("Testing event system %s: ", system->name);

		ret = __ftrace_set_clr_event(NULL, system->name, NULL, 1);
		if (WARN_ON_ONCE(ret)) {
			pr_warning("error enabling system %s\n",
				   system->name);
			continue;
		}

		event_test_stuff();

		ret = __ftrace_set_clr_event(NULL, system->name, NULL, 0);
		if (WARN_ON_ONCE(ret))
			pr_warning("error disabling system %s\n",
				   system->name);

		pr_cont("OK\n");
	}

	/* Test with all events enabled */

	pr_info("Running tests on all trace events:\n");
	pr_info("Testing all events: ");

	ret = __ftrace_set_clr_event(NULL, NULL, NULL, 1);
	if (WARN_ON_ONCE(ret)) {
		pr_warning("error enabling all events\n");
		return;
	}

	event_test_stuff();

	/* reset sysname */
	ret = __ftrace_set_clr_event(NULL, NULL, NULL, 0);
	if (WARN_ON_ONCE(ret)) {
		pr_warning("error disabling all events\n");
		return;
	}

	pr_cont("OK\n");
}

#ifdef CONFIG_FUNCTION_TRACER

static DEFINE_PER_CPU(atomic_t, ftrace_test_event_disable);

static void
function_test_events_call(unsigned long ip, unsigned long parent_ip)
{
	struct ring_buffer_event *event;
	struct ring_buffer *buffer;
	struct ftrace_entry *entry;
	unsigned long flags;
	long disabled;
	int cpu;
	int pc;

	pc = preempt_count();
	preempt_disable_notrace();
	cpu = raw_smp_processor_id();
	disabled = atomic_inc_return(&per_cpu(ftrace_test_event_disable, cpu));

	if (disabled != 1)
		goto out;

	local_save_flags(flags);

	event = trace_current_buffer_lock_reserve(&buffer,
						  TRACE_FN, sizeof(*entry),
						  flags, pc);
	if (!event)
		goto out;
	entry	= ring_buffer_event_data(event);
	entry->ip			= ip;
	entry->parent_ip		= parent_ip;

	trace_nowake_buffer_unlock_commit(buffer, event, flags, pc);

 out:
	atomic_dec(&per_cpu(ftrace_test_event_disable, cpu));
	preempt_enable_notrace();
}

static struct ftrace_ops trace_ops __initdata  =
{
	.func = function_test_events_call,
};

static __init void event_trace_self_test_with_function(void)
{
	register_ftrace_function(&trace_ops);
	pr_info("Running tests again, along with the function tracer\n");
	event_trace_self_tests();
	unregister_ftrace_function(&trace_ops);
}
#else
static __init void event_trace_self_test_with_function(void)
{
}
#endif

static __init int event_trace_self_tests_init(void)
{
	if (!tracing_selftest_disabled) {
		event_trace_self_tests();
		event_trace_self_test_with_function();
	}

	return 0;
}

late_initcall(event_trace_self_tests_init);

#endif
