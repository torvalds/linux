/*
 * event tracer
 *
 * Copyright (C) 2008 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ctype.h>

#include "trace_events.h"

void event_trace_printk(unsigned long ip, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	tracing_record_cmdline(current);
	trace_vprintk(ip, task_curr_ret_stack(current), fmt, ap);
	va_end(ap);
}

static void ftrace_clear_events(void)
{
	struct ftrace_event_call *call = (void *)__start_ftrace_events;


	while ((unsigned long)call < (unsigned long)__stop_ftrace_events) {

		if (call->enabled) {
			call->enabled = 0;
			call->unregfunc();
		}
		call++;
	}
}

static int ftrace_set_clr_event(char *buf, int set)
{
	struct ftrace_event_call *call = (void *)__start_ftrace_events;


	while ((unsigned long)call < (unsigned long)__stop_ftrace_events) {

		if (strcmp(buf, call->name) != 0) {
			call++;
			continue;
		}

		if (set) {
			/* Already set? */
			if (call->enabled)
				return 0;
			call->enabled = 1;
			call->regfunc();
		} else {
			/* Already cleared? */
			if (!call->enabled)
				return 0;
			call->enabled = 0;
			call->unregfunc();
		}
		return 0;
	}
	return -EINVAL;
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
	struct ftrace_event_call *call = m->private;
	struct ftrace_event_call *next = call;

	(*pos)++;

	if ((unsigned long)call >= (unsigned long)__stop_ftrace_events)
		return NULL;

	m->private = ++next;

	return call;
}

static void *t_start(struct seq_file *m, loff_t *pos)
{
	return t_next(m, NULL, pos);
}

static void *
s_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct ftrace_event_call *call = m->private;
	struct ftrace_event_call *next;

	(*pos)++;

 retry:
	if ((unsigned long)call >= (unsigned long)__stop_ftrace_events)
		return NULL;

	if (!call->enabled) {
		call++;
		goto retry;
	}

	next = call;
	m->private = ++next;

	return call;
}

static void *s_start(struct seq_file *m, loff_t *pos)
{
	return s_next(m, NULL, pos);
}

static int t_show(struct seq_file *m, void *v)
{
	struct ftrace_event_call *call = v;

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

		m->private = __start_ftrace_events;
	}
	return ret;
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

static __init int event_trace_init(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;

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

	return 0;
}
fs_initcall(event_trace_init);
