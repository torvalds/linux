/*
 * event tracer
 *
 * Copyright (C) 2008 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 *  - Added format output of fields of the trace point.
 *    This was based off of work by Tom Zanussi <tzanussi@gmail.com>.
 *
 */

#define pr_fmt(fmt) fmt

#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/tracefs.h>
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
static LIST_HEAD(ftrace_common_fields);

#define GFP_TRACE (GFP_KERNEL | __GFP_ZERO)

static struct kmem_cache *field_cachep;
static struct kmem_cache *file_cachep;

#define SYSTEM_FL_FREE_NAME		(1 << 31)

static inline int system_refcount(struct event_subsystem *system)
{
	return system->ref_count & ~SYSTEM_FL_FREE_NAME;
}

static int system_refcount_inc(struct event_subsystem *system)
{
	return (system->ref_count++) & ~SYSTEM_FL_FREE_NAME;
}

static int system_refcount_dec(struct event_subsystem *system)
{
	return (--system->ref_count) & ~SYSTEM_FL_FREE_NAME;
}

/* Double loops, do not use break, only goto's work */
#define do_for_each_event_file(tr, file)			\
	list_for_each_entry(tr, &ftrace_trace_arrays, list) {	\
		list_for_each_entry(file, &tr->events, list)

#define do_for_each_event_file_safe(tr, file)			\
	list_for_each_entry(tr, &ftrace_trace_arrays, list) {	\
		struct ftrace_event_file *___n;				\
		list_for_each_entry_safe(file, ___n, &tr->events, list)

#define while_for_each_event_file()		\
	}

static struct list_head *
trace_get_fields(struct ftrace_event_call *event_call)
{
	if (!event_call->class->get_fields)
		return &event_call->class->fields;
	return event_call->class->get_fields(event_call);
}

static struct ftrace_event_field *
__find_event_field(struct list_head *head, char *name)
{
	struct ftrace_event_field *field;

	list_for_each_entry(field, head, link) {
		if (!strcmp(field->name, name))
			return field;
	}

	return NULL;
}

struct ftrace_event_field *
trace_find_event_field(struct ftrace_event_call *call, char *name)
{
	struct ftrace_event_field *field;
	struct list_head *head;

	field = __find_event_field(&ftrace_common_fields, name);
	if (field)
		return field;

	head = trace_get_fields(call);
	return __find_event_field(head, name);
}

static int __trace_define_field(struct list_head *head, const char *type,
				const char *name, int offset, int size,
				int is_signed, int filter_type)
{
	struct ftrace_event_field *field;

	field = kmem_cache_alloc(field_cachep, GFP_TRACE);
	if (!field)
		return -ENOMEM;

	field->name = name;
	field->type = type;

	if (filter_type == FILTER_OTHER)
		field->filter_type = filter_assign_type(type);
	else
		field->filter_type = filter_type;

	field->offset = offset;
	field->size = size;
	field->is_signed = is_signed;

	list_add(&field->link, head);

	return 0;
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

	return ret;
}

static void trace_destroy_fields(struct ftrace_event_call *call)
{
	struct ftrace_event_field *field, *next;
	struct list_head *head;

	head = trace_get_fields(call);
	list_for_each_entry_safe(field, next, head, link) {
		list_del(&field->link);
		kmem_cache_free(field_cachep, field);
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

void *ftrace_event_buffer_reserve(struct ftrace_event_buffer *fbuffer,
				  struct ftrace_event_file *ftrace_file,
				  unsigned long len)
{
	struct ftrace_event_call *event_call = ftrace_file->event_call;

	local_save_flags(fbuffer->flags);
	fbuffer->pc = preempt_count();
	fbuffer->ftrace_file = ftrace_file;

	fbuffer->event =
		trace_event_buffer_lock_reserve(&fbuffer->buffer, ftrace_file,
						event_call->event.type, len,
						fbuffer->flags, fbuffer->pc);
	if (!fbuffer->event)
		return NULL;

	fbuffer->entry = ring_buffer_event_data(fbuffer->event);
	return fbuffer->entry;
}
EXPORT_SYMBOL_GPL(ftrace_event_buffer_reserve);

static DEFINE_SPINLOCK(tracepoint_iter_lock);

static void output_printk(struct ftrace_event_buffer *fbuffer)
{
	struct ftrace_event_call *event_call;
	struct trace_event *event;
	unsigned long flags;
	struct trace_iterator *iter = tracepoint_print_iter;

	if (!iter)
		return;

	event_call = fbuffer->ftrace_file->event_call;
	if (!event_call || !event_call->event.funcs ||
	    !event_call->event.funcs->trace)
		return;

	event = &fbuffer->ftrace_file->event_call->event;

	spin_lock_irqsave(&tracepoint_iter_lock, flags);
	trace_seq_init(&iter->seq);
	iter->ent = fbuffer->entry;
	event_call->event.funcs->trace(iter, 0, event);
	trace_seq_putc(&iter->seq, 0);
	printk("%s", iter->seq.buffer);

	spin_unlock_irqrestore(&tracepoint_iter_lock, flags);
}

void ftrace_event_buffer_commit(struct ftrace_event_buffer *fbuffer)
{
	if (tracepoint_printk)
		output_printk(fbuffer);

	event_trigger_unlock_commit(fbuffer->ftrace_file, fbuffer->buffer,
				    fbuffer->event, fbuffer->entry,
				    fbuffer->flags, fbuffer->pc);
}
EXPORT_SYMBOL_GPL(ftrace_event_buffer_commit);

int ftrace_event_reg(struct ftrace_event_call *call,
		     enum trace_reg type, void *data)
{
	struct ftrace_event_file *file = data;

	WARN_ON(!(call->flags & TRACE_EVENT_FL_TRACEPOINT));
	switch (type) {
	case TRACE_REG_REGISTER:
		return tracepoint_probe_register(call->tp,
						 call->class->probe,
						 file);
	case TRACE_REG_UNREGISTER:
		tracepoint_probe_unregister(call->tp,
					    call->class->probe,
					    file);
		return 0;

#ifdef CONFIG_PERF_EVENTS
	case TRACE_REG_PERF_REGISTER:
		return tracepoint_probe_register(call->tp,
						 call->class->perf_probe,
						 call);
	case TRACE_REG_PERF_UNREGISTER:
		tracepoint_probe_unregister(call->tp,
					    call->class->perf_probe,
					    call);
		return 0;
	case TRACE_REG_PERF_OPEN:
	case TRACE_REG_PERF_CLOSE:
	case TRACE_REG_PERF_ADD:
	case TRACE_REG_PERF_DEL:
		return 0;
#endif
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ftrace_event_reg);

void trace_event_enable_cmd_record(bool enable)
{
	struct ftrace_event_file *file;
	struct trace_array *tr;

	mutex_lock(&event_mutex);
	do_for_each_event_file(tr, file) {

		if (!(file->flags & FTRACE_EVENT_FL_ENABLED))
			continue;

		if (enable) {
			tracing_start_cmdline_record();
			set_bit(FTRACE_EVENT_FL_RECORDED_CMD_BIT, &file->flags);
		} else {
			tracing_stop_cmdline_record();
			clear_bit(FTRACE_EVENT_FL_RECORDED_CMD_BIT, &file->flags);
		}
	} while_for_each_event_file();
	mutex_unlock(&event_mutex);
}

static int __ftrace_event_enable_disable(struct ftrace_event_file *file,
					 int enable, int soft_disable)
{
	struct ftrace_event_call *call = file->event_call;
	int ret = 0;
	int disable;

	switch (enable) {
	case 0:
		/*
		 * When soft_disable is set and enable is cleared, the sm_ref
		 * reference counter is decremented. If it reaches 0, we want
		 * to clear the SOFT_DISABLED flag but leave the event in the
		 * state that it was. That is, if the event was enabled and
		 * SOFT_DISABLED isn't set, then do nothing. But if SOFT_DISABLED
		 * is set we do not want the event to be enabled before we
		 * clear the bit.
		 *
		 * When soft_disable is not set but the SOFT_MODE flag is,
		 * we do nothing. Do not disable the tracepoint, otherwise
		 * "soft enable"s (clearing the SOFT_DISABLED bit) wont work.
		 */
		if (soft_disable) {
			if (atomic_dec_return(&file->sm_ref) > 0)
				break;
			disable = file->flags & FTRACE_EVENT_FL_SOFT_DISABLED;
			clear_bit(FTRACE_EVENT_FL_SOFT_MODE_BIT, &file->flags);
		} else
			disable = !(file->flags & FTRACE_EVENT_FL_SOFT_MODE);

		if (disable && (file->flags & FTRACE_EVENT_FL_ENABLED)) {
			clear_bit(FTRACE_EVENT_FL_ENABLED_BIT, &file->flags);
			if (file->flags & FTRACE_EVENT_FL_RECORDED_CMD) {
				tracing_stop_cmdline_record();
				clear_bit(FTRACE_EVENT_FL_RECORDED_CMD_BIT, &file->flags);
			}
			call->class->reg(call, TRACE_REG_UNREGISTER, file);
		}
		/* If in SOFT_MODE, just set the SOFT_DISABLE_BIT, else clear it */
		if (file->flags & FTRACE_EVENT_FL_SOFT_MODE)
			set_bit(FTRACE_EVENT_FL_SOFT_DISABLED_BIT, &file->flags);
		else
			clear_bit(FTRACE_EVENT_FL_SOFT_DISABLED_BIT, &file->flags);
		break;
	case 1:
		/*
		 * When soft_disable is set and enable is set, we want to
		 * register the tracepoint for the event, but leave the event
		 * as is. That means, if the event was already enabled, we do
		 * nothing (but set SOFT_MODE). If the event is disabled, we
		 * set SOFT_DISABLED before enabling the event tracepoint, so
		 * it still seems to be disabled.
		 */
		if (!soft_disable)
			clear_bit(FTRACE_EVENT_FL_SOFT_DISABLED_BIT, &file->flags);
		else {
			if (atomic_inc_return(&file->sm_ref) > 1)
				break;
			set_bit(FTRACE_EVENT_FL_SOFT_MODE_BIT, &file->flags);
		}

		if (!(file->flags & FTRACE_EVENT_FL_ENABLED)) {

			/* Keep the event disabled, when going to SOFT_MODE. */
			if (soft_disable)
				set_bit(FTRACE_EVENT_FL_SOFT_DISABLED_BIT, &file->flags);

			if (trace_flags & TRACE_ITER_RECORD_CMD) {
				tracing_start_cmdline_record();
				set_bit(FTRACE_EVENT_FL_RECORDED_CMD_BIT, &file->flags);
			}
			ret = call->class->reg(call, TRACE_REG_REGISTER, file);
			if (ret) {
				tracing_stop_cmdline_record();
				pr_info("event trace: Could not enable event "
					"%s\n", ftrace_event_name(call));
				break;
			}
			set_bit(FTRACE_EVENT_FL_ENABLED_BIT, &file->flags);

			/* WAS_ENABLED gets set but never cleared. */
			call->flags |= TRACE_EVENT_FL_WAS_ENABLED;
		}
		break;
	}

	return ret;
}

int trace_event_enable_disable(struct ftrace_event_file *file,
			       int enable, int soft_disable)
{
	return __ftrace_event_enable_disable(file, enable, soft_disable);
}

static int ftrace_event_enable_disable(struct ftrace_event_file *file,
				       int enable)
{
	return __ftrace_event_enable_disable(file, enable, 0);
}

static void ftrace_clear_events(struct trace_array *tr)
{
	struct ftrace_event_file *file;

	mutex_lock(&event_mutex);
	list_for_each_entry(file, &tr->events, list) {
		ftrace_event_enable_disable(file, 0);
	}
	mutex_unlock(&event_mutex);
}

static void __put_system(struct event_subsystem *system)
{
	struct event_filter *filter = system->filter;

	WARN_ON_ONCE(system_refcount(system) == 0);
	if (system_refcount_dec(system))
		return;

	list_del(&system->list);

	if (filter) {
		kfree(filter->filter_string);
		kfree(filter);
	}
	if (system->ref_count & SYSTEM_FL_FREE_NAME)
		kfree(system->name);
	kfree(system);
}

static void __get_system(struct event_subsystem *system)
{
	WARN_ON_ONCE(system_refcount(system) == 0);
	system_refcount_inc(system);
}

static void __get_system_dir(struct ftrace_subsystem_dir *dir)
{
	WARN_ON_ONCE(dir->ref_count == 0);
	dir->ref_count++;
	__get_system(dir->subsystem);
}

static void __put_system_dir(struct ftrace_subsystem_dir *dir)
{
	WARN_ON_ONCE(dir->ref_count == 0);
	/* If the subsystem is about to be freed, the dir must be too */
	WARN_ON_ONCE(system_refcount(dir->subsystem) == 1 && dir->ref_count != 1);

	__put_system(dir->subsystem);
	if (!--dir->ref_count)
		kfree(dir);
}

static void put_system(struct ftrace_subsystem_dir *dir)
{
	mutex_lock(&event_mutex);
	__put_system_dir(dir);
	mutex_unlock(&event_mutex);
}

static void remove_subsystem(struct ftrace_subsystem_dir *dir)
{
	if (!dir)
		return;

	if (!--dir->nr_events) {
		tracefs_remove_recursive(dir->entry);
		list_del(&dir->list);
		__put_system_dir(dir);
	}
}

static void remove_event_file_dir(struct ftrace_event_file *file)
{
	struct dentry *dir = file->dir;
	struct dentry *child;

	if (dir) {
		spin_lock(&dir->d_lock);	/* probably unneeded */
		list_for_each_entry(child, &dir->d_subdirs, d_child) {
			if (d_really_is_positive(child))	/* probably unneeded */
				d_inode(child)->i_private = NULL;
		}
		spin_unlock(&dir->d_lock);

		tracefs_remove_recursive(dir);
	}

	list_del(&file->list);
	remove_subsystem(file->system);
	free_event_filter(file->filter);
	kmem_cache_free(file_cachep, file);
}

/*
 * __ftrace_set_clr_event(NULL, NULL, NULL, set) will set/unset all events.
 */
static int
__ftrace_set_clr_event_nolock(struct trace_array *tr, const char *match,
			      const char *sub, const char *event, int set)
{
	struct ftrace_event_file *file;
	struct ftrace_event_call *call;
	const char *name;
	int ret = -EINVAL;

	list_for_each_entry(file, &tr->events, list) {

		call = file->event_call;
		name = ftrace_event_name(call);

		if (!name || !call->class || !call->class->reg)
			continue;

		if (call->flags & TRACE_EVENT_FL_IGNORE_ENABLE)
			continue;

		if (match &&
		    strcmp(match, name) != 0 &&
		    strcmp(match, call->class->system) != 0)
			continue;

		if (sub && strcmp(sub, call->class->system) != 0)
			continue;

		if (event && strcmp(event, name) != 0)
			continue;

		ftrace_event_enable_disable(file, set);

		ret = 0;
	}

	return ret;
}

static int __ftrace_set_clr_event(struct trace_array *tr, const char *match,
				  const char *sub, const char *event, int set)
{
	int ret;

	mutex_lock(&event_mutex);
	ret = __ftrace_set_clr_event_nolock(tr, match, sub, event, set);
	mutex_unlock(&event_mutex);

	return ret;
}

static int ftrace_set_clr_event(struct trace_array *tr, char *buf, int set)
{
	char *event = NULL, *sub = NULL, *match;
	int ret;

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

	ret = __ftrace_set_clr_event(tr, match, sub, event, set);

	/* Put back the colon to allow this to be called again */
	if (buf)
		*(buf - 1) = ':';

	return ret;
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
	struct trace_array *tr = top_trace_array();

	if (!tr)
		return -ENODEV;

	return __ftrace_set_clr_event(tr, NULL, system, event, set);
}
EXPORT_SYMBOL_GPL(trace_set_clr_event);

/* 128 should be much more than enough */
#define EVENT_BUF_SIZE		127

static ssize_t
ftrace_event_write(struct file *file, const char __user *ubuf,
		   size_t cnt, loff_t *ppos)
{
	struct trace_parser parser;
	struct seq_file *m = file->private_data;
	struct trace_array *tr = m->private;
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

		ret = ftrace_set_clr_event(tr, parser.buffer + !set, set);
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
	struct ftrace_event_file *file = v;
	struct ftrace_event_call *call;
	struct trace_array *tr = m->private;

	(*pos)++;

	list_for_each_entry_continue(file, &tr->events, list) {
		call = file->event_call;
		/*
		 * The ftrace subsystem is for showing formats only.
		 * They can not be enabled or disabled via the event files.
		 */
		if (call->class && call->class->reg)
			return file;
	}

	return NULL;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_event_file *file;
	struct trace_array *tr = m->private;
	loff_t l;

	mutex_lock(&event_mutex);

	file = list_entry(&tr->events, struct ftrace_event_file, list);
	for (l = 0; l <= *pos; ) {
		file = t_next(m, file, &l);
		if (!file)
			break;
	}
	return file;
}

static void *
s_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ftrace_event_file *file = v;
	struct trace_array *tr = m->private;

	(*pos)++;

	list_for_each_entry_continue(file, &tr->events, list) {
		if (file->flags & FTRACE_EVENT_FL_ENABLED)
			return file;
	}

	return NULL;
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	struct ftrace_event_file *file;
	struct trace_array *tr = m->private;
	loff_t l;

	mutex_lock(&event_mutex);

	file = list_entry(&tr->events, struct ftrace_event_file, list);
	for (l = 0; l <= *pos; ) {
		file = s_next(m, file, &l);
		if (!file)
			break;
	}
	return file;
}

static int t_show(struct seq_file *m, void *v)
{
	struct ftrace_event_file *file = v;
	struct ftrace_event_call *call = file->event_call;

	if (strcmp(call->class->system, TRACE_SYSTEM) != 0)
		seq_printf(m, "%s:", call->class->system);
	seq_printf(m, "%s\n", ftrace_event_name(call));

	return 0;
}

static void t_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&event_mutex);
}

static ssize_t
event_enable_read(struct file *filp, char __user *ubuf, size_t cnt,
		  loff_t *ppos)
{
	struct ftrace_event_file *file;
	unsigned long flags;
	char buf[4] = "0";

	mutex_lock(&event_mutex);
	file = event_file_data(filp);
	if (likely(file))
		flags = file->flags;
	mutex_unlock(&event_mutex);

	if (!file)
		return -ENODEV;

	if (flags & FTRACE_EVENT_FL_ENABLED &&
	    !(flags & FTRACE_EVENT_FL_SOFT_DISABLED))
		strcpy(buf, "1");

	if (flags & FTRACE_EVENT_FL_SOFT_DISABLED ||
	    flags & FTRACE_EVENT_FL_SOFT_MODE)
		strcat(buf, "*");

	strcat(buf, "\n");

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, strlen(buf));
}

static ssize_t
event_enable_write(struct file *filp, const char __user *ubuf, size_t cnt,
		   loff_t *ppos)
{
	struct ftrace_event_file *file;
	unsigned long val;
	int ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	ret = tracing_update_buffers();
	if (ret < 0)
		return ret;

	switch (val) {
	case 0:
	case 1:
		ret = -ENODEV;
		mutex_lock(&event_mutex);
		file = event_file_data(filp);
		if (likely(file))
			ret = ftrace_event_enable_disable(file, val);
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
	struct ftrace_subsystem_dir *dir = filp->private_data;
	struct event_subsystem *system = dir->subsystem;
	struct ftrace_event_call *call;
	struct ftrace_event_file *file;
	struct trace_array *tr = dir->tr;
	char buf[2];
	int set = 0;
	int ret;

	mutex_lock(&event_mutex);
	list_for_each_entry(file, &tr->events, list) {
		call = file->event_call;
		if (!ftrace_event_name(call) || !call->class || !call->class->reg)
			continue;

		if (system && strcmp(call->class->system, system->name) != 0)
			continue;

		/*
		 * We need to find out if all the events are set
		 * or if all events or cleared, or if we have
		 * a mixture.
		 */
		set |= (1 << !!(file->flags & FTRACE_EVENT_FL_ENABLED));

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
	struct ftrace_subsystem_dir *dir = filp->private_data;
	struct event_subsystem *system = dir->subsystem;
	const char *name = NULL;
	unsigned long val;
	ssize_t ret;

	ret = kstrtoul_from_user(ubuf, cnt, 10, &val);
	if (ret)
		return ret;

	ret = tracing_update_buffers();
	if (ret < 0)
		return ret;

	if (val != 0 && val != 1)
		return -EINVAL;

	/*
	 * Opening of "enable" adds a ref count to system,
	 * so the name is safe to use.
	 */
	if (system)
		name = system->name;

	ret = __ftrace_set_clr_event(dir->tr, NULL, name, NULL, val);
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
	struct ftrace_event_call *call = event_file_data(m->private);
	struct list_head *common_head = &ftrace_common_fields;
	struct list_head *head = trace_get_fields(call);
	struct list_head *node = v;

	(*pos)++;

	switch ((unsigned long)v) {
	case FORMAT_HEADER:
		node = common_head;
		break;

	case FORMAT_FIELD_SEPERATOR:
		node = head;
		break;

	case FORMAT_PRINTFMT:
		/* all done */
		return NULL;
	}

	node = node->prev;
	if (node == common_head)
		return (void *)FORMAT_FIELD_SEPERATOR;
	else if (node == head)
		return (void *)FORMAT_PRINTFMT;
	else
		return node;
}

static int f_show(struct seq_file *m, void *v)
{
	struct ftrace_event_call *call = event_file_data(m->private);
	struct ftrace_event_field *field;
	const char *array_descriptor;

	switch ((unsigned long)v) {
	case FORMAT_HEADER:
		seq_printf(m, "name: %s\n", ftrace_event_name(call));
		seq_printf(m, "ID: %d\n", call->event.type);
		seq_puts(m, "format:\n");
		return 0;

	case FORMAT_FIELD_SEPERATOR:
		seq_putc(m, '\n');
		return 0;

	case FORMAT_PRINTFMT:
		seq_printf(m, "\nprint fmt: %s\n",
			   call->print_fmt);
		return 0;
	}

	field = list_entry(v, struct ftrace_event_field, link);
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

static void *f_start(struct seq_file *m, loff_t *pos)
{
	void *p = (void *)FORMAT_HEADER;
	loff_t l = 0;

	/* ->stop() is called even if ->start() fails */
	mutex_lock(&event_mutex);
	if (!event_file_data(m->private))
		return ERR_PTR(-ENODEV);

	while (l < *pos && p)
		p = f_next(m, p, &l);

	return p;
}

static void f_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&event_mutex);
}

static const struct seq_operations trace_format_seq_ops = {
	.start		= f_start,
	.next		= f_next,
	.stop		= f_stop,
	.show		= f_show,
};

static int trace_format_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &trace_format_seq_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = file;

	return 0;
}

static ssize_t
event_id_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int id = (long)event_file_data(filp);
	char buf[32];
	int len;

	if (*ppos)
		return 0;

	if (unlikely(!id))
		return -ENODEV;

	len = sprintf(buf, "%d\n", id);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}

static ssize_t
event_filter_read(struct file *filp, char __user *ubuf, size_t cnt,
		  loff_t *ppos)
{
	struct ftrace_event_file *file;
	struct trace_seq *s;
	int r = -ENODEV;

	if (*ppos)
		return 0;

	s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	mutex_lock(&event_mutex);
	file = event_file_data(filp);
	if (file)
		print_event_filter(file, s);
	mutex_unlock(&event_mutex);

	if (file)
		r = simple_read_from_buffer(ubuf, cnt, ppos,
					    s->buffer, trace_seq_used(s));

	kfree(s);

	return r;
}

static ssize_t
event_filter_write(struct file *filp, const char __user *ubuf, size_t cnt,
		   loff_t *ppos)
{
	struct ftrace_event_file *file;
	char *buf;
	int err = -ENODEV;

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

	mutex_lock(&event_mutex);
	file = event_file_data(filp);
	if (file)
		err = apply_event_filter(file, buf);
	mutex_unlock(&event_mutex);

	free_page((unsigned long) buf);
	if (err < 0)
		return err;

	*ppos += cnt;

	return cnt;
}

static LIST_HEAD(event_subsystems);

static int subsystem_open(struct inode *inode, struct file *filp)
{
	struct event_subsystem *system = NULL;
	struct ftrace_subsystem_dir *dir = NULL; /* Initialize for gcc */
	struct trace_array *tr;
	int ret;

	if (tracing_is_disabled())
		return -ENODEV;

	/* Make sure the system still exists */
	mutex_lock(&trace_types_lock);
	mutex_lock(&event_mutex);
	list_for_each_entry(tr, &ftrace_trace_arrays, list) {
		list_for_each_entry(dir, &tr->systems, list) {
			if (dir == inode->i_private) {
				/* Don't open systems with no events */
				if (dir->nr_events) {
					__get_system_dir(dir);
					system = dir->subsystem;
				}
				goto exit_loop;
			}
		}
	}
 exit_loop:
	mutex_unlock(&event_mutex);
	mutex_unlock(&trace_types_lock);

	if (!system)
		return -ENODEV;

	/* Some versions of gcc think dir can be uninitialized here */
	WARN_ON(!dir);

	/* Still need to increment the ref count of the system */
	if (trace_array_get(tr) < 0) {
		put_system(dir);
		return -ENODEV;
	}

	ret = tracing_open_generic(inode, filp);
	if (ret < 0) {
		trace_array_put(tr);
		put_system(dir);
	}

	return ret;
}

static int system_tr_open(struct inode *inode, struct file *filp)
{
	struct ftrace_subsystem_dir *dir;
	struct trace_array *tr = inode->i_private;
	int ret;

	if (tracing_is_disabled())
		return -ENODEV;

	if (trace_array_get(tr) < 0)
		return -ENODEV;

	/* Make a temporary dir that has no system but points to tr */
	dir = kzalloc(sizeof(*dir), GFP_KERNEL);
	if (!dir) {
		trace_array_put(tr);
		return -ENOMEM;
	}

	dir->tr = tr;

	ret = tracing_open_generic(inode, filp);
	if (ret < 0) {
		trace_array_put(tr);
		kfree(dir);
		return ret;
	}

	filp->private_data = dir;

	return 0;
}

static int subsystem_release(struct inode *inode, struct file *file)
{
	struct ftrace_subsystem_dir *dir = file->private_data;

	trace_array_put(dir->tr);

	/*
	 * If dir->subsystem is NULL, then this is a temporary
	 * descriptor that was made for a trace_array to enable
	 * all subsystems.
	 */
	if (dir->subsystem)
		put_system(dir);
	else
		kfree(dir);

	return 0;
}

static ssize_t
subsystem_filter_read(struct file *filp, char __user *ubuf, size_t cnt,
		      loff_t *ppos)
{
	struct ftrace_subsystem_dir *dir = filp->private_data;
	struct event_subsystem *system = dir->subsystem;
	struct trace_seq *s;
	int r;

	if (*ppos)
		return 0;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	print_subsystem_event_filter(system, s);
	r = simple_read_from_buffer(ubuf, cnt, ppos,
				    s->buffer, trace_seq_used(s));

	kfree(s);

	return r;
}

static ssize_t
subsystem_filter_write(struct file *filp, const char __user *ubuf, size_t cnt,
		       loff_t *ppos)
{
	struct ftrace_subsystem_dir *dir = filp->private_data;
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

	err = apply_subsystem_event_filter(dir, buf);
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
	r = simple_read_from_buffer(ubuf, cnt, ppos,
				    s->buffer, trace_seq_used(s));

	kfree(s);

	return r;
}

static int ftrace_event_avail_open(struct inode *inode, struct file *file);
static int ftrace_event_set_open(struct inode *inode, struct file *file);
static int ftrace_event_release(struct inode *inode, struct file *file);

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
	.open = ftrace_event_avail_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static const struct file_operations ftrace_set_event_fops = {
	.open = ftrace_event_set_open,
	.read = seq_read,
	.write = ftrace_event_write,
	.llseek = seq_lseek,
	.release = ftrace_event_release,
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
	.open = subsystem_open,
	.read = subsystem_filter_read,
	.write = subsystem_filter_write,
	.llseek = default_llseek,
	.release = subsystem_release,
};

static const struct file_operations ftrace_system_enable_fops = {
	.open = subsystem_open,
	.read = system_enable_read,
	.write = system_enable_write,
	.llseek = default_llseek,
	.release = subsystem_release,
};

static const struct file_operations ftrace_tr_enable_fops = {
	.open = system_tr_open,
	.read = system_enable_read,
	.write = system_enable_write,
	.llseek = default_llseek,
	.release = subsystem_release,
};

static const struct file_operations ftrace_show_header_fops = {
	.open = tracing_open_generic,
	.read = show_header,
	.llseek = default_llseek,
};

static int
ftrace_event_open(struct inode *inode, struct file *file,
		  const struct seq_operations *seq_ops)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, seq_ops);
	if (ret < 0)
		return ret;
	m = file->private_data;
	/* copy tr over to seq ops */
	m->private = inode->i_private;

	return ret;
}

static int ftrace_event_release(struct inode *inode, struct file *file)
{
	struct trace_array *tr = inode->i_private;

	trace_array_put(tr);

	return seq_release(inode, file);
}

static int
ftrace_event_avail_open(struct inode *inode, struct file *file)
{
	const struct seq_operations *seq_ops = &show_event_seq_ops;

	return ftrace_event_open(inode, file, seq_ops);
}

static int
ftrace_event_set_open(struct inode *inode, struct file *file)
{
	const struct seq_operations *seq_ops = &show_set_event_seq_ops;
	struct trace_array *tr = inode->i_private;
	int ret;

	if (trace_array_get(tr) < 0)
		return -ENODEV;

	if ((file->f_mode & FMODE_WRITE) &&
	    (file->f_flags & O_TRUNC))
		ftrace_clear_events(tr);

	ret = ftrace_event_open(inode, file, seq_ops);
	if (ret < 0)
		trace_array_put(tr);
	return ret;
}

static struct event_subsystem *
create_new_subsystem(const char *name)
{
	struct event_subsystem *system;

	/* need to create new entry */
	system = kmalloc(sizeof(*system), GFP_KERNEL);
	if (!system)
		return NULL;

	system->ref_count = 1;

	/* Only allocate if dynamic (kprobes and modules) */
	if (!core_kernel_data((unsigned long)name)) {
		system->ref_count |= SYSTEM_FL_FREE_NAME;
		system->name = kstrdup(name, GFP_KERNEL);
		if (!system->name)
			goto out_free;
	} else
		system->name = name;

	system->filter = NULL;

	system->filter = kzalloc(sizeof(struct event_filter), GFP_KERNEL);
	if (!system->filter)
		goto out_free;

	list_add(&system->list, &event_subsystems);

	return system;

 out_free:
	if (system->ref_count & SYSTEM_FL_FREE_NAME)
		kfree(system->name);
	kfree(system);
	return NULL;
}

static struct dentry *
event_subsystem_dir(struct trace_array *tr, const char *name,
		    struct ftrace_event_file *file, struct dentry *parent)
{
	struct ftrace_subsystem_dir *dir;
	struct event_subsystem *system;
	struct dentry *entry;

	/* First see if we did not already create this dir */
	list_for_each_entry(dir, &tr->systems, list) {
		system = dir->subsystem;
		if (strcmp(system->name, name) == 0) {
			dir->nr_events++;
			file->system = dir;
			return dir->entry;
		}
	}

	/* Now see if the system itself exists. */
	list_for_each_entry(system, &event_subsystems, list) {
		if (strcmp(system->name, name) == 0)
			break;
	}
	/* Reset system variable when not found */
	if (&system->list == &event_subsystems)
		system = NULL;

	dir = kmalloc(sizeof(*dir), GFP_KERNEL);
	if (!dir)
		goto out_fail;

	if (!system) {
		system = create_new_subsystem(name);
		if (!system)
			goto out_free;
	} else
		__get_system(system);

	dir->entry = tracefs_create_dir(name, parent);
	if (!dir->entry) {
		pr_warn("Failed to create system directory %s\n", name);
		__put_system(system);
		goto out_free;
	}

	dir->tr = tr;
	dir->ref_count = 1;
	dir->nr_events = 1;
	dir->subsystem = system;
	file->system = dir;

	entry = tracefs_create_file("filter", 0644, dir->entry, dir,
				    &ftrace_subsystem_filter_fops);
	if (!entry) {
		kfree(system->filter);
		system->filter = NULL;
		pr_warn("Could not create tracefs '%s/filter' entry\n", name);
	}

	trace_create_file("enable", 0644, dir->entry, dir,
			  &ftrace_system_enable_fops);

	list_add(&dir->list, &tr->systems);

	return dir->entry;

 out_free:
	kfree(dir);
 out_fail:
	/* Only print this message if failed on memory allocation */
	if (!dir || !system)
		pr_warn("No memory to create event subsystem %s\n", name);
	return NULL;
}

static int
event_create_dir(struct dentry *parent, struct ftrace_event_file *file)
{
	struct ftrace_event_call *call = file->event_call;
	struct trace_array *tr = file->tr;
	struct list_head *head;
	struct dentry *d_events;
	const char *name;
	int ret;

	/*
	 * If the trace point header did not define TRACE_SYSTEM
	 * then the system would be called "TRACE_SYSTEM".
	 */
	if (strcmp(call->class->system, TRACE_SYSTEM) != 0) {
		d_events = event_subsystem_dir(tr, call->class->system, file, parent);
		if (!d_events)
			return -ENOMEM;
	} else
		d_events = parent;

	name = ftrace_event_name(call);
	file->dir = tracefs_create_dir(name, d_events);
	if (!file->dir) {
		pr_warn("Could not create tracefs '%s' directory\n", name);
		return -1;
	}

	if (call->class->reg && !(call->flags & TRACE_EVENT_FL_IGNORE_ENABLE))
		trace_create_file("enable", 0644, file->dir, file,
				  &ftrace_enable_fops);

#ifdef CONFIG_PERF_EVENTS
	if (call->event.type && call->class->reg)
		trace_create_file("id", 0444, file->dir,
				  (void *)(long)call->event.type,
				  &ftrace_event_id_fops);
#endif

	/*
	 * Other events may have the same class. Only update
	 * the fields if they are not already defined.
	 */
	head = trace_get_fields(call);
	if (list_empty(head)) {
		ret = call->class->define_fields(call);
		if (ret < 0) {
			pr_warn("Could not initialize trace point events/%s\n",
				name);
			return -1;
		}
	}
	trace_create_file("filter", 0644, file->dir, file,
			  &ftrace_event_filter_fops);

	trace_create_file("trigger", 0644, file->dir, file,
			  &event_trigger_fops);

	trace_create_file("format", 0444, file->dir, call,
			  &ftrace_event_format_fops);

	return 0;
}

static void remove_event_from_tracers(struct ftrace_event_call *call)
{
	struct ftrace_event_file *file;
	struct trace_array *tr;

	do_for_each_event_file_safe(tr, file) {
		if (file->event_call != call)
			continue;

		remove_event_file_dir(file);
		/*
		 * The do_for_each_event_file_safe() is
		 * a double loop. After finding the call for this
		 * trace_array, we use break to jump to the next
		 * trace_array.
		 */
		break;
	} while_for_each_event_file();
}

static void event_remove(struct ftrace_event_call *call)
{
	struct trace_array *tr;
	struct ftrace_event_file *file;

	do_for_each_event_file(tr, file) {
		if (file->event_call != call)
			continue;
		ftrace_event_enable_disable(file, 0);
		/*
		 * The do_for_each_event_file() is
		 * a double loop. After finding the call for this
		 * trace_array, we use break to jump to the next
		 * trace_array.
		 */
		break;
	} while_for_each_event_file();

	if (call->event.funcs)
		__unregister_ftrace_event(&call->event);
	remove_event_from_tracers(call);
	list_del(&call->list);
}

static int event_init(struct ftrace_event_call *call)
{
	int ret = 0;
	const char *name;

	name = ftrace_event_name(call);
	if (WARN_ON(!name))
		return -EINVAL;

	if (call->class->raw_init) {
		ret = call->class->raw_init(call);
		if (ret < 0 && ret != -ENOSYS)
			pr_warn("Could not initialize trace events/%s\n", name);
	}

	return ret;
}

static int
__register_event(struct ftrace_event_call *call, struct module *mod)
{
	int ret;

	ret = event_init(call);
	if (ret < 0)
		return ret;

	list_add(&call->list, &ftrace_events);
	call->mod = mod;

	return 0;
}

static char *enum_replace(char *ptr, struct trace_enum_map *map, int len)
{
	int rlen;
	int elen;

	/* Find the length of the enum value as a string */
	elen = snprintf(ptr, 0, "%ld", map->enum_value);
	/* Make sure there's enough room to replace the string with the value */
	if (len < elen)
		return NULL;

	snprintf(ptr, elen + 1, "%ld", map->enum_value);

	/* Get the rest of the string of ptr */
	rlen = strlen(ptr + len);
	memmove(ptr + elen, ptr + len, rlen);
	/* Make sure we end the new string */
	ptr[elen + rlen] = 0;

	return ptr + elen;
}

static void update_event_printk(struct ftrace_event_call *call,
				struct trace_enum_map *map)
{
	char *ptr;
	int quote = 0;
	int len = strlen(map->enum_string);

	for (ptr = call->print_fmt; *ptr; ptr++) {
		if (*ptr == '\\') {
			ptr++;
			/* paranoid */
			if (!*ptr)
				break;
			continue;
		}
		if (*ptr == '"') {
			quote ^= 1;
			continue;
		}
		if (quote)
			continue;
		if (isdigit(*ptr)) {
			/* skip numbers */
			do {
				ptr++;
				/* Check for alpha chars like ULL */
			} while (isalnum(*ptr));
			if (!*ptr)
				break;
			/*
			 * A number must have some kind of delimiter after
			 * it, and we can ignore that too.
			 */
			continue;
		}
		if (isalpha(*ptr) || *ptr == '_') {
			if (strncmp(map->enum_string, ptr, len) == 0 &&
			    !isalnum(ptr[len]) && ptr[len] != '_') {
				ptr = enum_replace(ptr, map, len);
				/* Hmm, enum string smaller than value */
				if (WARN_ON_ONCE(!ptr))
					return;
				/*
				 * No need to decrement here, as enum_replace()
				 * returns the pointer to the character passed
				 * the enum, and two enums can not be placed
				 * back to back without something in between.
				 * We can skip that something in between.
				 */
				continue;
			}
		skip_more:
			do {
				ptr++;
			} while (isalnum(*ptr) || *ptr == '_');
			if (!*ptr)
				break;
			/*
			 * If what comes after this variable is a '.' or
			 * '->' then we can continue to ignore that string.
			 */
			if (*ptr == '.' || (ptr[0] == '-' && ptr[1] == '>')) {
				ptr += *ptr == '.' ? 1 : 2;
				if (!*ptr)
					break;
				goto skip_more;
			}
			/*
			 * Once again, we can skip the delimiter that came
			 * after the string.
			 */
			continue;
		}
	}
}

void trace_event_enum_update(struct trace_enum_map **map, int len)
{
	struct ftrace_event_call *call, *p;
	const char *last_system = NULL;
	int last_i;
	int i;

	down_write(&trace_event_sem);
	list_for_each_entry_safe(call, p, &ftrace_events, list) {
		/* events are usually grouped together with systems */
		if (!last_system || call->class->system != last_system) {
			last_i = 0;
			last_system = call->class->system;
		}

		for (i = last_i; i < len; i++) {
			if (call->class->system == map[i]->system) {
				/* Save the first system if need be */
				if (!last_i)
					last_i = i;
				update_event_printk(call, map[i]);
			}
		}
	}
	up_write(&trace_event_sem);
}

static struct ftrace_event_file *
trace_create_new_event(struct ftrace_event_call *call,
		       struct trace_array *tr)
{
	struct ftrace_event_file *file;

	file = kmem_cache_alloc(file_cachep, GFP_TRACE);
	if (!file)
		return NULL;

	file->event_call = call;
	file->tr = tr;
	atomic_set(&file->sm_ref, 0);
	atomic_set(&file->tm_ref, 0);
	INIT_LIST_HEAD(&file->triggers);
	list_add(&file->list, &tr->events);

	return file;
}

/* Add an event to a trace directory */
static int
__trace_add_new_event(struct ftrace_event_call *call, struct trace_array *tr)
{
	struct ftrace_event_file *file;

	file = trace_create_new_event(call, tr);
	if (!file)
		return -ENOMEM;

	return event_create_dir(tr->event_dir, file);
}

/*
 * Just create a decriptor for early init. A descriptor is required
 * for enabling events at boot. We want to enable events before
 * the filesystem is initialized.
 */
static __init int
__trace_early_add_new_event(struct ftrace_event_call *call,
			    struct trace_array *tr)
{
	struct ftrace_event_file *file;

	file = trace_create_new_event(call, tr);
	if (!file)
		return -ENOMEM;

	return 0;
}

struct ftrace_module_file_ops;
static void __add_event_to_tracers(struct ftrace_event_call *call);

/* Add an additional event_call dynamically */
int trace_add_event_call(struct ftrace_event_call *call)
{
	int ret;
	mutex_lock(&trace_types_lock);
	mutex_lock(&event_mutex);

	ret = __register_event(call, NULL);
	if (ret >= 0)
		__add_event_to_tracers(call);

	mutex_unlock(&event_mutex);
	mutex_unlock(&trace_types_lock);
	return ret;
}

/*
 * Must be called under locking of trace_types_lock, event_mutex and
 * trace_event_sem.
 */
static void __trace_remove_event_call(struct ftrace_event_call *call)
{
	event_remove(call);
	trace_destroy_fields(call);
	free_event_filter(call->filter);
	call->filter = NULL;
}

static int probe_remove_event_call(struct ftrace_event_call *call)
{
	struct trace_array *tr;
	struct ftrace_event_file *file;

#ifdef CONFIG_PERF_EVENTS
	if (call->perf_refcount)
		return -EBUSY;
#endif
	do_for_each_event_file(tr, file) {
		if (file->event_call != call)
			continue;
		/*
		 * We can't rely on ftrace_event_enable_disable(enable => 0)
		 * we are going to do, FTRACE_EVENT_FL_SOFT_MODE can suppress
		 * TRACE_REG_UNREGISTER.
		 */
		if (file->flags & FTRACE_EVENT_FL_ENABLED)
			return -EBUSY;
		/*
		 * The do_for_each_event_file_safe() is
		 * a double loop. After finding the call for this
		 * trace_array, we use break to jump to the next
		 * trace_array.
		 */
		break;
	} while_for_each_event_file();

	__trace_remove_event_call(call);

	return 0;
}

/* Remove an event_call */
int trace_remove_event_call(struct ftrace_event_call *call)
{
	int ret;

	mutex_lock(&trace_types_lock);
	mutex_lock(&event_mutex);
	down_write(&trace_event_sem);
	ret = probe_remove_event_call(call);
	up_write(&trace_event_sem);
	mutex_unlock(&event_mutex);
	mutex_unlock(&trace_types_lock);

	return ret;
}

#define for_each_event(event, start, end)			\
	for (event = start;					\
	     (unsigned long)event < (unsigned long)end;		\
	     event++)

#ifdef CONFIG_MODULES

static void trace_module_add_events(struct module *mod)
{
	struct ftrace_event_call **call, **start, **end;

	if (!mod->num_trace_events)
		return;

	/* Don't add infrastructure for mods without tracepoints */
	if (trace_module_has_bad_taint(mod)) {
		pr_err("%s: module has bad taint, not creating trace events\n",
		       mod->name);
		return;
	}

	start = mod->trace_events;
	end = mod->trace_events + mod->num_trace_events;

	for_each_event(call, start, end) {
		__register_event(*call, mod);
		__add_event_to_tracers(*call);
	}
}

static void trace_module_remove_events(struct module *mod)
{
	struct ftrace_event_call *call, *p;
	bool clear_trace = false;

	down_write(&trace_event_sem);
	list_for_each_entry_safe(call, p, &ftrace_events, list) {
		if (call->mod == mod) {
			if (call->flags & TRACE_EVENT_FL_WAS_ENABLED)
				clear_trace = true;
			__trace_remove_event_call(call);
		}
	}
	up_write(&trace_event_sem);

	/*
	 * It is safest to reset the ring buffer if the module being unloaded
	 * registered any events that were used. The only worry is if
	 * a new module gets loaded, and takes on the same id as the events
	 * of this module. When printing out the buffer, traced events left
	 * over from this module may be passed to the new module events and
	 * unexpected results may occur.
	 */
	if (clear_trace)
		tracing_reset_all_online_cpus();
}

static int trace_module_notify(struct notifier_block *self,
			       unsigned long val, void *data)
{
	struct module *mod = data;

	mutex_lock(&trace_types_lock);
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
	mutex_unlock(&trace_types_lock);

	return 0;
}

static struct notifier_block trace_module_nb = {
	.notifier_call = trace_module_notify,
	.priority = 1, /* higher than trace.c module notify */
};
#endif /* CONFIG_MODULES */

/* Create a new event directory structure for a trace directory. */
static void
__trace_add_event_dirs(struct trace_array *tr)
{
	struct ftrace_event_call *call;
	int ret;

	list_for_each_entry(call, &ftrace_events, list) {
		ret = __trace_add_new_event(call, tr);
		if (ret < 0)
			pr_warn("Could not create directory for event %s\n",
				ftrace_event_name(call));
	}
}

struct ftrace_event_file *
find_event_file(struct trace_array *tr, const char *system,  const char *event)
{
	struct ftrace_event_file *file;
	struct ftrace_event_call *call;
	const char *name;

	list_for_each_entry(file, &tr->events, list) {

		call = file->event_call;
		name = ftrace_event_name(call);

		if (!name || !call->class || !call->class->reg)
			continue;

		if (call->flags & TRACE_EVENT_FL_IGNORE_ENABLE)
			continue;

		if (strcmp(event, name) == 0 &&
		    strcmp(system, call->class->system) == 0)
			return file;
	}
	return NULL;
}

#ifdef CONFIG_DYNAMIC_FTRACE

/* Avoid typos */
#define ENABLE_EVENT_STR	"enable_event"
#define DISABLE_EVENT_STR	"disable_event"

struct event_probe_data {
	struct ftrace_event_file	*file;
	unsigned long			count;
	int				ref;
	bool				enable;
};

static void
event_enable_probe(unsigned long ip, unsigned long parent_ip, void **_data)
{
	struct event_probe_data **pdata = (struct event_probe_data **)_data;
	struct event_probe_data *data = *pdata;

	if (!data)
		return;

	if (data->enable)
		clear_bit(FTRACE_EVENT_FL_SOFT_DISABLED_BIT, &data->file->flags);
	else
		set_bit(FTRACE_EVENT_FL_SOFT_DISABLED_BIT, &data->file->flags);
}

static void
event_enable_count_probe(unsigned long ip, unsigned long parent_ip, void **_data)
{
	struct event_probe_data **pdata = (struct event_probe_data **)_data;
	struct event_probe_data *data = *pdata;

	if (!data)
		return;

	if (!data->count)
		return;

	/* Skip if the event is in a state we want to switch to */
	if (data->enable == !(data->file->flags & FTRACE_EVENT_FL_SOFT_DISABLED))
		return;

	if (data->count != -1)
		(data->count)--;

	event_enable_probe(ip, parent_ip, _data);
}

static int
event_enable_print(struct seq_file *m, unsigned long ip,
		      struct ftrace_probe_ops *ops, void *_data)
{
	struct event_probe_data *data = _data;

	seq_printf(m, "%ps:", (void *)ip);

	seq_printf(m, "%s:%s:%s",
		   data->enable ? ENABLE_EVENT_STR : DISABLE_EVENT_STR,
		   data->file->event_call->class->system,
		   ftrace_event_name(data->file->event_call));

	if (data->count == -1)
		seq_puts(m, ":unlimited\n");
	else
		seq_printf(m, ":count=%ld\n", data->count);

	return 0;
}

static int
event_enable_init(struct ftrace_probe_ops *ops, unsigned long ip,
		  void **_data)
{
	struct event_probe_data **pdata = (struct event_probe_data **)_data;
	struct event_probe_data *data = *pdata;

	data->ref++;
	return 0;
}

static void
event_enable_free(struct ftrace_probe_ops *ops, unsigned long ip,
		  void **_data)
{
	struct event_probe_data **pdata = (struct event_probe_data **)_data;
	struct event_probe_data *data = *pdata;

	if (WARN_ON_ONCE(data->ref <= 0))
		return;

	data->ref--;
	if (!data->ref) {
		/* Remove the SOFT_MODE flag */
		__ftrace_event_enable_disable(data->file, 0, 1);
		module_put(data->file->event_call->mod);
		kfree(data);
	}
	*pdata = NULL;
}

static struct ftrace_probe_ops event_enable_probe_ops = {
	.func			= event_enable_probe,
	.print			= event_enable_print,
	.init			= event_enable_init,
	.free			= event_enable_free,
};

static struct ftrace_probe_ops event_enable_count_probe_ops = {
	.func			= event_enable_count_probe,
	.print			= event_enable_print,
	.init			= event_enable_init,
	.free			= event_enable_free,
};

static struct ftrace_probe_ops event_disable_probe_ops = {
	.func			= event_enable_probe,
	.print			= event_enable_print,
	.init			= event_enable_init,
	.free			= event_enable_free,
};

static struct ftrace_probe_ops event_disable_count_probe_ops = {
	.func			= event_enable_count_probe,
	.print			= event_enable_print,
	.init			= event_enable_init,
	.free			= event_enable_free,
};

static int
event_enable_func(struct ftrace_hash *hash,
		  char *glob, char *cmd, char *param, int enabled)
{
	struct trace_array *tr = top_trace_array();
	struct ftrace_event_file *file;
	struct ftrace_probe_ops *ops;
	struct event_probe_data *data;
	const char *system;
	const char *event;
	char *number;
	bool enable;
	int ret;

	if (!tr)
		return -ENODEV;

	/* hash funcs only work with set_ftrace_filter */
	if (!enabled || !param)
		return -EINVAL;

	system = strsep(&param, ":");
	if (!param)
		return -EINVAL;

	event = strsep(&param, ":");

	mutex_lock(&event_mutex);

	ret = -EINVAL;
	file = find_event_file(tr, system, event);
	if (!file)
		goto out;

	enable = strcmp(cmd, ENABLE_EVENT_STR) == 0;

	if (enable)
		ops = param ? &event_enable_count_probe_ops : &event_enable_probe_ops;
	else
		ops = param ? &event_disable_count_probe_ops : &event_disable_probe_ops;

	if (glob[0] == '!') {
		unregister_ftrace_function_probe_func(glob+1, ops);
		ret = 0;
		goto out;
	}

	ret = -ENOMEM;
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		goto out;

	data->enable = enable;
	data->count = -1;
	data->file = file;

	if (!param)
		goto out_reg;

	number = strsep(&param, ":");

	ret = -EINVAL;
	if (!strlen(number))
		goto out_free;

	/*
	 * We use the callback data field (which is a pointer)
	 * as our counter.
	 */
	ret = kstrtoul(number, 0, &data->count);
	if (ret)
		goto out_free;

 out_reg:
	/* Don't let event modules unload while probe registered */
	ret = try_module_get(file->event_call->mod);
	if (!ret) {
		ret = -EBUSY;
		goto out_free;
	}

	ret = __ftrace_event_enable_disable(file, 1, 1);
	if (ret < 0)
		goto out_put;
	ret = register_ftrace_function_probe(glob, ops, data);
	/*
	 * The above returns on success the # of functions enabled,
	 * but if it didn't find any functions it returns zero.
	 * Consider no functions a failure too.
	 */
	if (!ret) {
		ret = -ENOENT;
		goto out_disable;
	} else if (ret < 0)
		goto out_disable;
	/* Just return zero, not the number of enabled functions */
	ret = 0;
 out:
	mutex_unlock(&event_mutex);
	return ret;

 out_disable:
	__ftrace_event_enable_disable(file, 0, 1);
 out_put:
	module_put(file->event_call->mod);
 out_free:
	kfree(data);
	goto out;
}

static struct ftrace_func_command event_enable_cmd = {
	.name			= ENABLE_EVENT_STR,
	.func			= event_enable_func,
};

static struct ftrace_func_command event_disable_cmd = {
	.name			= DISABLE_EVENT_STR,
	.func			= event_enable_func,
};

static __init int register_event_cmds(void)
{
	int ret;

	ret = register_ftrace_command(&event_enable_cmd);
	if (WARN_ON(ret < 0))
		return ret;
	ret = register_ftrace_command(&event_disable_cmd);
	if (WARN_ON(ret < 0))
		unregister_ftrace_command(&event_enable_cmd);
	return ret;
}
#else
static inline int register_event_cmds(void) { return 0; }
#endif /* CONFIG_DYNAMIC_FTRACE */

/*
 * The top level array has already had its ftrace_event_file
 * descriptors created in order to allow for early events to
 * be recorded. This function is called after the tracefs has been
 * initialized, and we now have to create the files associated
 * to the events.
 */
static __init void
__trace_early_add_event_dirs(struct trace_array *tr)
{
	struct ftrace_event_file *file;
	int ret;


	list_for_each_entry(file, &tr->events, list) {
		ret = event_create_dir(tr->event_dir, file);
		if (ret < 0)
			pr_warn("Could not create directory for event %s\n",
				ftrace_event_name(file->event_call));
	}
}

/*
 * For early boot up, the top trace array requires to have
 * a list of events that can be enabled. This must be done before
 * the filesystem is set up in order to allow events to be traced
 * early.
 */
static __init void
__trace_early_add_events(struct trace_array *tr)
{
	struct ftrace_event_call *call;
	int ret;

	list_for_each_entry(call, &ftrace_events, list) {
		/* Early boot up should not have any modules loaded */
		if (WARN_ON_ONCE(call->mod))
			continue;

		ret = __trace_early_add_new_event(call, tr);
		if (ret < 0)
			pr_warn("Could not create early event %s\n",
				ftrace_event_name(call));
	}
}

/* Remove the event directory structure for a trace directory. */
static void
__trace_remove_event_dirs(struct trace_array *tr)
{
	struct ftrace_event_file *file, *next;

	list_for_each_entry_safe(file, next, &tr->events, list)
		remove_event_file_dir(file);
}

static void __add_event_to_tracers(struct ftrace_event_call *call)
{
	struct trace_array *tr;

	list_for_each_entry(tr, &ftrace_trace_arrays, list)
		__trace_add_new_event(call, tr);
}

extern struct ftrace_event_call *__start_ftrace_events[];
extern struct ftrace_event_call *__stop_ftrace_events[];

static char bootup_event_buf[COMMAND_LINE_SIZE] __initdata;

static __init int setup_trace_event(char *str)
{
	strlcpy(bootup_event_buf, str, COMMAND_LINE_SIZE);
	ring_buffer_expanded = true;
	tracing_selftest_disabled = true;

	return 1;
}
__setup("trace_event=", setup_trace_event);

/* Expects to have event_mutex held when called */
static int
create_event_toplevel_files(struct dentry *parent, struct trace_array *tr)
{
	struct dentry *d_events;
	struct dentry *entry;

	entry = tracefs_create_file("set_event", 0644, parent,
				    tr, &ftrace_set_event_fops);
	if (!entry) {
		pr_warn("Could not create tracefs 'set_event' entry\n");
		return -ENOMEM;
	}

	d_events = tracefs_create_dir("events", parent);
	if (!d_events) {
		pr_warn("Could not create tracefs 'events' directory\n");
		return -ENOMEM;
	}

	/* ring buffer internal formats */
	trace_create_file("header_page", 0444, d_events,
			  ring_buffer_print_page_header,
			  &ftrace_show_header_fops);

	trace_create_file("header_event", 0444, d_events,
			  ring_buffer_print_entry_header,
			  &ftrace_show_header_fops);

	trace_create_file("enable", 0644, d_events,
			  tr, &ftrace_tr_enable_fops);

	tr->event_dir = d_events;

	return 0;
}

/**
 * event_trace_add_tracer - add a instance of a trace_array to events
 * @parent: The parent dentry to place the files/directories for events in
 * @tr: The trace array associated with these events
 *
 * When a new instance is created, it needs to set up its events
 * directory, as well as other files associated with events. It also
 * creates the event hierachry in the @parent/events directory.
 *
 * Returns 0 on success.
 */
int event_trace_add_tracer(struct dentry *parent, struct trace_array *tr)
{
	int ret;

	mutex_lock(&event_mutex);

	ret = create_event_toplevel_files(parent, tr);
	if (ret)
		goto out_unlock;

	down_write(&trace_event_sem);
	__trace_add_event_dirs(tr);
	up_write(&trace_event_sem);

 out_unlock:
	mutex_unlock(&event_mutex);

	return ret;
}

/*
 * The top trace array already had its file descriptors created.
 * Now the files themselves need to be created.
 */
static __init int
early_event_add_tracer(struct dentry *parent, struct trace_array *tr)
{
	int ret;

	mutex_lock(&event_mutex);

	ret = create_event_toplevel_files(parent, tr);
	if (ret)
		goto out_unlock;

	down_write(&trace_event_sem);
	__trace_early_add_event_dirs(tr);
	up_write(&trace_event_sem);

 out_unlock:
	mutex_unlock(&event_mutex);

	return ret;
}

int event_trace_del_tracer(struct trace_array *tr)
{
	mutex_lock(&event_mutex);

	/* Disable any event triggers and associated soft-disabled events */
	clear_event_triggers(tr);

	/* Disable any running events */
	__ftrace_set_clr_event_nolock(tr, NULL, NULL, NULL, 0);

	/* Access to events are within rcu_read_lock_sched() */
	synchronize_sched();

	down_write(&trace_event_sem);
	__trace_remove_event_dirs(tr);
	tracefs_remove_recursive(tr->event_dir);
	up_write(&trace_event_sem);

	tr->event_dir = NULL;

	mutex_unlock(&event_mutex);

	return 0;
}

static __init int event_trace_memsetup(void)
{
	field_cachep = KMEM_CACHE(ftrace_event_field, SLAB_PANIC);
	file_cachep = KMEM_CACHE(ftrace_event_file, SLAB_PANIC);
	return 0;
}

static __init void
early_enable_events(struct trace_array *tr, bool disable_first)
{
	char *buf = bootup_event_buf;
	char *token;
	int ret;

	while (true) {
		token = strsep(&buf, ",");

		if (!token)
			break;
		if (!*token)
			continue;

		/* Restarting syscalls requires that we stop them first */
		if (disable_first)
			ftrace_set_clr_event(tr, token, 0);

		ret = ftrace_set_clr_event(tr, token, 1);
		if (ret)
			pr_warn("Failed to enable trace event: %s\n", token);

		/* Put back the comma to allow this to be called again */
		if (buf)
			*(buf - 1) = ',';
	}
}

static __init int event_trace_enable(void)
{
	struct trace_array *tr = top_trace_array();
	struct ftrace_event_call **iter, *call;
	int ret;

	if (!tr)
		return -ENODEV;

	for_each_event(iter, __start_ftrace_events, __stop_ftrace_events) {

		call = *iter;
		ret = event_init(call);
		if (!ret)
			list_add(&call->list, &ftrace_events);
	}

	/*
	 * We need the top trace array to have a working set of trace
	 * points at early init, before the debug files and directories
	 * are created. Create the file entries now, and attach them
	 * to the actual file dentries later.
	 */
	__trace_early_add_events(tr);

	early_enable_events(tr, false);

	trace_printk_start_comm();

	register_event_cmds();

	register_trigger_cmds();

	return 0;
}

/*
 * event_trace_enable() is called from trace_event_init() first to
 * initialize events and perhaps start any events that are on the
 * command line. Unfortunately, there are some events that will not
 * start this early, like the system call tracepoints that need
 * to set the TIF_SYSCALL_TRACEPOINT flag of pid 1. But event_trace_enable()
 * is called before pid 1 starts, and this flag is never set, making
 * the syscall tracepoint never get reached, but the event is enabled
 * regardless (and not doing anything).
 */
static __init int event_trace_enable_again(void)
{
	struct trace_array *tr;

	tr = top_trace_array();
	if (!tr)
		return -ENODEV;

	early_enable_events(tr, true);

	return 0;
}

early_initcall(event_trace_enable_again);

static __init int event_trace_init(void)
{
	struct trace_array *tr;
	struct dentry *d_tracer;
	struct dentry *entry;
	int ret;

	tr = top_trace_array();
	if (!tr)
		return -ENODEV;

	d_tracer = tracing_init_dentry();
	if (IS_ERR(d_tracer))
		return 0;

	entry = tracefs_create_file("available_events", 0444, d_tracer,
				    tr, &ftrace_avail_fops);
	if (!entry)
		pr_warn("Could not create tracefs 'available_events' entry\n");

	if (trace_define_common_fields())
		pr_warn("tracing: Failed to allocate common fields");

	ret = early_event_add_tracer(d_tracer, tr);
	if (ret)
		return ret;

#ifdef CONFIG_MODULES
	ret = register_module_notifier(&trace_module_nb);
	if (ret)
		pr_warn("Failed to register trace events module notifier\n");
#endif
	return 0;
}

void __init trace_event_init(void)
{
	event_trace_memsetup();
	init_ftrace_syscalls();
	event_trace_enable();
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
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);

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
	struct ftrace_subsystem_dir *dir;
	struct ftrace_event_file *file;
	struct ftrace_event_call *call;
	struct event_subsystem *system;
	struct trace_array *tr;
	int ret;

	tr = top_trace_array();
	if (!tr)
		return;

	pr_info("Running tests on trace events:\n");

	list_for_each_entry(file, &tr->events, list) {

		call = file->event_call;

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

		pr_info("Testing event %s: ", ftrace_event_name(call));

		/*
		 * If an event is already enabled, someone is using
		 * it and the self test should not be on.
		 */
		if (file->flags & FTRACE_EVENT_FL_ENABLED) {
			pr_warn("Enabled event during self test!\n");
			WARN_ON_ONCE(1);
			continue;
		}

		ftrace_event_enable_disable(file, 1);
		event_test_stuff();
		ftrace_event_enable_disable(file, 0);

		pr_cont("OK\n");
	}

	/* Now test at the sub system level */

	pr_info("Running tests on trace event systems:\n");

	list_for_each_entry(dir, &tr->systems, list) {

		system = dir->subsystem;

		/* the ftrace system is special, skip it */
		if (strcmp(system->name, "ftrace") == 0)
			continue;

		pr_info("Testing event system %s: ", system->name);

		ret = __ftrace_set_clr_event(tr, NULL, system->name, NULL, 1);
		if (WARN_ON_ONCE(ret)) {
			pr_warn("error enabling system %s\n",
				system->name);
			continue;
		}

		event_test_stuff();

		ret = __ftrace_set_clr_event(tr, NULL, system->name, NULL, 0);
		if (WARN_ON_ONCE(ret)) {
			pr_warn("error disabling system %s\n",
				system->name);
			continue;
		}

		pr_cont("OK\n");
	}

	/* Test with all events enabled */

	pr_info("Running tests on all trace events:\n");
	pr_info("Testing all events: ");

	ret = __ftrace_set_clr_event(tr, NULL, NULL, NULL, 1);
	if (WARN_ON_ONCE(ret)) {
		pr_warn("error enabling all events\n");
		return;
	}

	event_test_stuff();

	/* reset sysname */
	ret = __ftrace_set_clr_event(tr, NULL, NULL, NULL, 0);
	if (WARN_ON_ONCE(ret)) {
		pr_warn("error disabling all events\n");
		return;
	}

	pr_cont("OK\n");
}

#ifdef CONFIG_FUNCTION_TRACER

static DEFINE_PER_CPU(atomic_t, ftrace_test_event_disable);

static void
function_test_events_call(unsigned long ip, unsigned long parent_ip,
			  struct ftrace_ops *op, struct pt_regs *pt_regs)
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

	trace_buffer_unlock_commit(buffer, event, flags, pc);

 out:
	atomic_dec(&per_cpu(ftrace_test_event_disable, cpu));
	preempt_enable_notrace();
}

static struct ftrace_ops trace_ops __initdata  =
{
	.func = function_test_events_call,
	.flags = FTRACE_OPS_FL_RECURSION_SAFE,
};

static __init void event_trace_self_test_with_function(void)
{
	int ret;
	ret = register_ftrace_function(&trace_ops);
	if (WARN_ON(ret < 0)) {
		pr_info("Failed to enable function tracer for event tests\n");
		return;
	}
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
