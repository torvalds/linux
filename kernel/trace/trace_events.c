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
#include <linux/delay.h>

#include "trace_output.h"

#define TRACE_SYSTEM "TRACE_SYSTEM"

static DEFINE_MUTEX(event_mutex);

LIST_HEAD(ftrace_events);

int trace_define_field(struct ftrace_event_call *call, char *type,
		       char *name, int offset, int size)
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

	field->offset = offset;
	field->size = size;
	list_add(&field->link, &call->fields);

	return 0;

err:
	if (field) {
		kfree(field->name);
		kfree(field->type);
	}
	kfree(field);

	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(trace_define_field);

static void ftrace_clear_events(void)
{
	struct ftrace_event_call *call;

	list_for_each_entry(call, &ftrace_events, list) {

		if (call->enabled) {
			call->enabled = 0;
			call->unregfunc();
		}
	}
}

static void ftrace_event_enable_disable(struct ftrace_event_call *call,
					int enable)
{

	switch (enable) {
	case 0:
		if (call->enabled) {
			call->enabled = 0;
			call->unregfunc();
		}
		break;
	case 1:
		if (!call->enabled) {
			call->enabled = 1;
			call->regfunc();
		}
		break;
	}
}

static int ftrace_set_clr_event(char *buf, int set)
{
	struct ftrace_event_call *call;
	char *event = NULL, *sub = NULL, *match;
	int ret = -EINVAL;

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

	mutex_lock(&event_mutex);
	list_for_each_entry(call, &ftrace_events, list) {

		if (!call->name || !call->regfunc)
			continue;

		if (match &&
		    strcmp(match, call->name) != 0 &&
		    strcmp(match, call->system) != 0)
			continue;

		if (sub && strcmp(sub, call->system) != 0)
			continue;

		if (event && strcmp(event, call->name) != 0)
			continue;

		ftrace_event_enable_disable(call, set);

		ret = 0;
	}
	mutex_unlock(&event_mutex);

	return ret;
}

/* 128 should be much more than enough */
#define EVENT_BUF_SIZE		127

static ssize_t
ftrace_event_write(struct file *file, const char __user *ubuf,
		   size_t cnt, loff_t *ppos)
{
	size_t read = 0;
	int i, set = 1;
	ssize_t ret;
	char *buf;
	char ch;

	if (!cnt || cnt < 0)
		return 0;

	ret = tracing_update_buffers();
	if (ret < 0)
		return ret;

	ret = get_user(ch, ubuf++);
	if (ret)
		return ret;
	read++;
	cnt--;

	/* skip white space */
	while (cnt && isspace(ch)) {
		ret = get_user(ch, ubuf++);
		if (ret)
			return ret;
		read++;
		cnt--;
	}

	/* Only white space found? */
	if (isspace(ch)) {
		file->f_pos += read;
		ret = read;
		return ret;
	}

	buf = kmalloc(EVENT_BUF_SIZE+1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (cnt > EVENT_BUF_SIZE)
		cnt = EVENT_BUF_SIZE;

	i = 0;
	while (cnt && !isspace(ch)) {
		if (!i && ch == '!')
			set = 0;
		else
			buf[i++] = ch;

		ret = get_user(ch, ubuf++);
		if (ret)
			goto out_free;
		read++;
		cnt--;
	}
	buf[i] = 0;

	file->f_pos += read;

	ret = ftrace_set_clr_event(buf, set);
	if (ret)
		goto out_free;

	ret = read;

 out_free:
	kfree(buf);

	return ret;
}

static void *
t_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct list_head *list = m->private;
	struct ftrace_event_call *call;

	(*pos)++;

	for (;;) {
		if (list == &ftrace_events)
			return NULL;

		call = list_entry(list, struct ftrace_event_call, list);

		/*
		 * The ftrace subsystem is for showing formats only.
		 * They can not be enabled or disabled via the event files.
		 */
		if (call->regfunc)
			break;

		list = list->next;
	}

	m->private = list->next;

	return call;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	return t_next(m, NULL, pos);
}

static void *
s_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct list_head *list = m->private;
	struct ftrace_event_call *call;

	(*pos)++;

 retry:
	if (list == &ftrace_events)
		return NULL;

	call = list_entry(list, struct ftrace_event_call, list);

	if (!call->enabled) {
		list = list->next;
		goto retry;
	}

	m->private = list->next;

	return call;
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	return s_next(m, NULL, pos);
}

static int t_show(struct seq_file *m, void *v)
{
	struct ftrace_event_call *call = v;

	if (strcmp(call->system, TRACE_SYSTEM) != 0)
		seq_printf(m, "%s:", call->system);
	seq_printf(m, "%s\n", call->name);

	return 0;
}

static void t_stop(struct seq_file *m, void *p)
{
}

static int
ftrace_event_seq_open(struct inode *inode, struct file *file)
{
	int ret;
	const struct seq_operations *seq_ops;

	if ((file->f_mode & FMODE_WRITE) &&
	    !(file->f_flags & O_APPEND))
		ftrace_clear_events();

	seq_ops = inode->i_private;
	ret = seq_open(file, seq_ops);
	if (!ret) {
		struct seq_file *m = file->private_data;

		m->private = ftrace_events.next;
	}
	return ret;
}

static ssize_t
event_enable_read(struct file *filp, char __user *ubuf, size_t cnt,
		  loff_t *ppos)
{
	struct ftrace_event_call *call = filp->private_data;
	char *buf;

	if (call->enabled)
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
		ftrace_event_enable_disable(call, val);
		mutex_unlock(&event_mutex);
		break;

	default:
		return -EINVAL;
	}

	*ppos += cnt;

	return cnt;
}

#undef FIELD
#define FIELD(type, name)						\
	#type, "common_" #name, offsetof(typeof(field), name),		\
		sizeof(field.name)

static int trace_write_header(struct trace_seq *s)
{
	struct trace_entry field;

	/* struct trace_entry */
	return trace_seq_printf(s,
				"\tfield:%s %s;\toffset:%zu;\tsize:%zu;\n"
				"\tfield:%s %s;\toffset:%zu;\tsize:%zu;\n"
				"\tfield:%s %s;\toffset:%zu;\tsize:%zu;\n"
				"\tfield:%s %s;\toffset:%zu;\tsize:%zu;\n"
				"\tfield:%s %s;\toffset:%zu;\tsize:%zu;\n"
				"\n",
				FIELD(unsigned char, type),
				FIELD(unsigned char, flags),
				FIELD(unsigned char, preempt_count),
				FIELD(int, pid),
				FIELD(int, tgid));
}

static ssize_t
event_format_read(struct file *filp, char __user *ubuf, size_t cnt,
		  loff_t *ppos)
{
	struct ftrace_event_call *call = filp->private_data;
	struct trace_seq *s;
	char *buf;
	int r;

	if (*ppos)
		return 0;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	trace_seq_init(s);

	/* If any of the first writes fail, so will the show_format. */

	trace_seq_printf(s, "name: %s\n", call->name);
	trace_seq_printf(s, "ID: %d\n", call->id);
	trace_seq_printf(s, "format:\n");
	trace_write_header(s);

	r = call->show_format(s);
	if (!r) {
		/*
		 * ug!  The format output is bigger than a PAGE!!
		 */
		buf = "FORMAT TOO BIG\n";
		r = simple_read_from_buffer(ubuf, cnt, ppos,
					      buf, strlen(buf));
		goto out;
	}

	r = simple_read_from_buffer(ubuf, cnt, ppos,
				    s->buffer, s->len);
 out:
	kfree(s);
	return r;
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
	trace_seq_printf(s, "%d\n", call->id);

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

	filter_print_preds(call->preds, call->n_preds, s);
	r = simple_read_from_buffer(ubuf, cnt, ppos, s->buffer, s->len);

	kfree(s);

	return r;
}

static ssize_t
event_filter_write(struct file *filp, const char __user *ubuf, size_t cnt,
		   loff_t *ppos)
{
	struct ftrace_event_call *call = filp->private_data;
	char buf[64], *pbuf = buf;
	struct filter_pred *pred;
	int err;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = '\0';

	pred = kzalloc(sizeof(*pred), GFP_KERNEL);
	if (!pred)
		return -ENOMEM;

	err = filter_parse(&pbuf, pred);
	if (err < 0) {
		filter_free_pred(pred);
		return err;
	}

	if (pred->clear) {
		filter_disable_preds(call);
		filter_free_pred(pred);
		return cnt;
	}

	err = filter_add_pred(call, pred);
	if (err < 0) {
		filter_free_pred(pred);
		return err;
	}

	filter_free_pred(pred);

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

	filter_print_preds(system->preds, system->n_preds, s);
	r = simple_read_from_buffer(ubuf, cnt, ppos, s->buffer, s->len);

	kfree(s);

	return r;
}

static ssize_t
subsystem_filter_write(struct file *filp, const char __user *ubuf, size_t cnt,
		       loff_t *ppos)
{
	struct event_subsystem *system = filp->private_data;
	char buf[64], *pbuf = buf;
	struct filter_pred *pred;
	int err;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = '\0';

	pred = kzalloc(sizeof(*pred), GFP_KERNEL);
	if (!pred)
		return -ENOMEM;

	err = filter_parse(&pbuf, pred);
	if (err < 0) {
		filter_free_pred(pred);
		return err;
	}

	if (pred->clear) {
		filter_free_subsystem_preds(system);
		filter_free_pred(pred);
		return cnt;
	}

	err = filter_add_subsystem_pred(system, pred);
	if (err < 0) {
		filter_free_subsystem_preds(system);
		filter_free_pred(pred);
		return err;
	}

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
};

static const struct file_operations ftrace_event_format_fops = {
	.open = tracing_open_generic,
	.read = event_format_read,
};

static const struct file_operations ftrace_event_id_fops = {
	.open = tracing_open_generic,
	.read = event_id_read,
};

static const struct file_operations ftrace_event_filter_fops = {
	.open = tracing_open_generic,
	.read = event_filter_read,
	.write = event_filter_write,
};

static const struct file_operations ftrace_subsystem_filter_fops = {
	.open = tracing_open_generic,
	.read = subsystem_filter_read,
	.write = subsystem_filter_write,
};

static const struct file_operations ftrace_show_header_fops = {
	.open = tracing_open_generic,
	.read = show_header,
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
		if (strcmp(system->name, name) == 0)
			return system->entry;
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

	system->name = kstrdup(name, GFP_KERNEL);
	if (!system->name) {
		debugfs_remove(system->entry);
		kfree(system);
		return d_events;
	}

	list_add(&system->list, &event_subsystems);

	system->preds = NULL;
	system->n_preds = 0;

	entry = debugfs_create_file("filter", 0644, system->entry, system,
				    &ftrace_subsystem_filter_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'%s/filter' entry\n", name);

	return system->entry;
}

static int
event_create_dir(struct ftrace_event_call *call, struct dentry *d_events)
{
	struct dentry *entry;
	int ret;

	/*
	 * If the trace point header did not define TRACE_SYSTEM
	 * then the system would be called "TRACE_SYSTEM".
	 */
	if (strcmp(call->system, TRACE_SYSTEM) != 0)
		d_events = event_subsystem_dir(call->system, d_events);

	if (call->raw_init) {
		ret = call->raw_init();
		if (ret < 0) {
			pr_warning("Could not initialize trace point"
				   " events/%s\n", call->name);
			return ret;
		}
	}

	call->dir = debugfs_create_dir(call->name, d_events);
	if (!call->dir) {
		pr_warning("Could not create debugfs "
			   "'%s' directory\n", call->name);
		return -1;
	}

	if (call->regfunc)
		entry = trace_create_file("enable", 0644, call->dir, call,
					  &ftrace_enable_fops);

	if (call->id)
		entry = trace_create_file("id", 0444, call->dir, call,
					  &ftrace_event_id_fops);

	if (call->define_fields) {
		ret = call->define_fields();
		if (ret < 0) {
			pr_warning("Could not initialize trace point"
				   " events/%s\n", call->name);
			return ret;
		}
		entry = trace_create_file("filter", 0644, call->dir, call,
					  &ftrace_event_filter_fops);
	}

	/* A trace may not want to export its format */
	if (!call->show_format)
		return 0;

	entry = trace_create_file("format", 0444, call->dir, call,
				  &ftrace_event_format_fops);

	return 0;
}

#define for_each_event(event, start, end)			\
	for (event = start;					\
	     (unsigned long)event < (unsigned long)end;		\
	     event++)

#ifdef CONFIG_MODULES
static void trace_module_add_events(struct module *mod)
{
	struct ftrace_event_call *call, *start, *end;
	struct dentry *d_events;

	start = mod->trace_events;
	end = mod->trace_events + mod->num_trace_events;

	if (start == end)
		return;

	d_events = event_trace_events_dir();
	if (!d_events)
		return;

	for_each_event(call, start, end) {
		/* The linker may leave blanks */
		if (!call->name)
			continue;
		call->mod = mod;
		list_add(&call->list, &ftrace_events);
		event_create_dir(call, d_events);
	}
}

static void trace_module_remove_events(struct module *mod)
{
	struct ftrace_event_call *call, *p;

	list_for_each_entry_safe(call, p, &ftrace_events, list) {
		if (call->mod == mod) {
			if (call->enabled) {
				call->enabled = 0;
				call->unregfunc();
			}
			if (call->event)
				unregister_ftrace_event(call->event);
			debugfs_remove_recursive(call->dir);
			list_del(&call->list);
		}
	}
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

struct notifier_block trace_module_nb = {
	.notifier_call = trace_module_notify,
	.priority = 0,
};

extern struct ftrace_event_call __start_ftrace_events[];
extern struct ftrace_event_call __stop_ftrace_events[];

static __init int event_trace_init(void)
{
	struct ftrace_event_call *call;
	struct dentry *d_tracer;
	struct dentry *entry;
	struct dentry *d_events;
	int ret;

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

	for_each_event(call, __start_ftrace_events, __stop_ftrace_events) {
		/* The linker may leave blanks */
		if (!call->name)
			continue;
		list_add(&call->list, &ftrace_events);
		event_create_dir(call, d_events);
	}

	ret = register_module_notifier(&trace_module_nb);
	if (!ret)
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
static __init int event_trace_self_tests(void)
{
	struct ftrace_event_call *call;
	struct event_subsystem *system;
	char *sysname;
	int ret;

	pr_info("Running tests on trace events:\n");

	list_for_each_entry(call, &ftrace_events, list) {

		/* Only test those that have a regfunc */
		if (!call->regfunc)
			continue;

		pr_info("Testing event %s: ", call->name);

		/*
		 * If an event is already enabled, someone is using
		 * it and the self test should not be on.
		 */
		if (call->enabled) {
			pr_warning("Enabled event during self test!\n");
			WARN_ON_ONCE(1);
			continue;
		}

		call->enabled = 1;
		call->regfunc();

		event_test_stuff();

		call->unregfunc();
		call->enabled = 0;

		pr_cont("OK\n");
	}

	/* Now test at the sub system level */

	pr_info("Running tests on trace event systems:\n");

	list_for_each_entry(system, &event_subsystems, list) {

		/* the ftrace system is special, skip it */
		if (strcmp(system->name, "ftrace") == 0)
			continue;

		pr_info("Testing event system %s: ", system->name);

		/* ftrace_set_clr_event can modify the name passed in. */
		sysname = kstrdup(system->name, GFP_KERNEL);
		if (WARN_ON(!sysname)) {
			pr_warning("Can't allocate memory, giving up!\n");
			return 0;
		}
		ret = ftrace_set_clr_event(sysname, 1);
		kfree(sysname);
		if (WARN_ON_ONCE(ret)) {
			pr_warning("error enabling system %s\n",
				   system->name);
			continue;
		}

		event_test_stuff();

		sysname = kstrdup(system->name, GFP_KERNEL);
		if (WARN_ON(!sysname)) {
			pr_warning("Can't allocate memory, giving up!\n");
			return 0;
		}
		ret = ftrace_set_clr_event(sysname, 0);
		kfree(sysname);

		if (WARN_ON_ONCE(ret))
			pr_warning("error disabling system %s\n",
				   system->name);

		pr_cont("OK\n");
	}

	/* Test with all events enabled */

	pr_info("Running tests on all trace events:\n");
	pr_info("Testing all events: ");

	sysname = kmalloc(4, GFP_KERNEL);
	if (WARN_ON(!sysname)) {
		pr_warning("Can't allocate memory, giving up!\n");
		return 0;
	}
	memcpy(sysname, "*:*", 4);
	ret = ftrace_set_clr_event(sysname, 1);
	if (WARN_ON_ONCE(ret)) {
		kfree(sysname);
		pr_warning("error enabling all events\n");
		return 0;
	}

	event_test_stuff();

	/* reset sysname */
	memcpy(sysname, "*:*", 4);
	ret = ftrace_set_clr_event(sysname, 0);
	kfree(sysname);

	if (WARN_ON_ONCE(ret)) {
		pr_warning("error disabling all events\n");
		return 0;
	}

	pr_cont("OK\n");

	return 0;
}

late_initcall(event_trace_self_tests);

#endif
