// SPDX-License-Identifier: GPL-2.0
/*
 * Generic dynamic event control interface
 *
 * Copyright (C) 2018 Masami Hiramatsu <mhiramat@kernel.org>
 */

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/tracefs.h>

#include "trace.h"
#include "trace_dynevent.h"

static DEFINE_MUTEX(dyn_event_ops_mutex);
static LIST_HEAD(dyn_event_ops_list);

int dyn_event_register(struct dyn_event_operations *ops)
{
	if (!ops || !ops->create || !ops->show || !ops->is_busy ||
	    !ops->free || !ops->match)
		return -EINVAL;

	INIT_LIST_HEAD(&ops->list);
	mutex_lock(&dyn_event_ops_mutex);
	list_add_tail(&ops->list, &dyn_event_ops_list);
	mutex_unlock(&dyn_event_ops_mutex);
	return 0;
}

int dyn_event_release(int argc, char **argv, struct dyn_event_operations *type)
{
	struct dyn_event *pos, *n;
	char *system = NULL, *event, *p;
	int ret = -ENOENT;

	if (argv[0][0] == '-') {
		if (argv[0][1] != ':')
			return -EINVAL;
		event = &argv[0][2];
	} else {
		event = strchr(argv[0], ':');
		if (!event)
			return -EINVAL;
		event++;
	}
	argc--; argv++;

	p = strchr(event, '/');
	if (p) {
		system = event;
		event = p + 1;
		*p = '\0';
	}
	if (event[0] == '\0')
		return -EINVAL;

	mutex_lock(&event_mutex);
	for_each_dyn_event_safe(pos, n) {
		if (type && type != pos->ops)
			continue;
		if (!pos->ops->match(system, event,
				argc, (const char **)argv, pos))
			continue;

		ret = pos->ops->free(pos);
		if (ret)
			break;
	}
	mutex_unlock(&event_mutex);

	return ret;
}

static int create_dyn_event(int argc, char **argv)
{
	struct dyn_event_operations *ops;
	int ret = -ENODEV;

	if (argv[0][0] == '-' || argv[0][0] == '!')
		return dyn_event_release(argc, argv, NULL);

	mutex_lock(&dyn_event_ops_mutex);
	list_for_each_entry(ops, &dyn_event_ops_list, list) {
		ret = ops->create(argc, (const char **)argv);
		if (!ret || ret != -ECANCELED)
			break;
	}
	mutex_unlock(&dyn_event_ops_mutex);
	if (ret == -ECANCELED)
		ret = -EINVAL;

	return ret;
}

/* Protected by event_mutex */
LIST_HEAD(dyn_event_list);

void *dyn_event_seq_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&event_mutex);
	return seq_list_start(&dyn_event_list, *pos);
}

void *dyn_event_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &dyn_event_list, pos);
}

void dyn_event_seq_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&event_mutex);
}

static int dyn_event_seq_show(struct seq_file *m, void *v)
{
	struct dyn_event *ev = v;

	if (ev && ev->ops)
		return ev->ops->show(m, ev);

	return 0;
}

static const struct seq_operations dyn_event_seq_op = {
	.start	= dyn_event_seq_start,
	.next	= dyn_event_seq_next,
	.stop	= dyn_event_seq_stop,
	.show	= dyn_event_seq_show
};

/*
 * dyn_events_release_all - Release all specific events
 * @type:	the dyn_event_operations * which filters releasing events
 *
 * This releases all events which ->ops matches @type. If @type is NULL,
 * all events are released.
 * Return -EBUSY if any of them are in use, and return other errors when
 * it failed to free the given event. Except for -EBUSY, event releasing
 * process will be aborted at that point and there may be some other
 * releasable events on the list.
 */
int dyn_events_release_all(struct dyn_event_operations *type)
{
	struct dyn_event *ev, *tmp;
	int ret = 0;

	mutex_lock(&event_mutex);
	for_each_dyn_event(ev) {
		if (type && ev->ops != type)
			continue;
		if (ev->ops->is_busy(ev)) {
			ret = -EBUSY;
			goto out;
		}
	}
	for_each_dyn_event_safe(ev, tmp) {
		if (type && ev->ops != type)
			continue;
		ret = ev->ops->free(ev);
		if (ret)
			break;
	}
out:
	mutex_unlock(&event_mutex);

	return ret;
}

static int dyn_event_open(struct inode *inode, struct file *file)
{
	int ret;

	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC)) {
		ret = dyn_events_release_all(NULL);
		if (ret < 0)
			return ret;
	}

	return seq_open(file, &dyn_event_seq_op);
}

static ssize_t dyn_event_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *ppos)
{
	return trace_parse_run_command(file, buffer, count, ppos,
				       create_dyn_event);
}

static const struct file_operations dynamic_events_ops = {
	.owner          = THIS_MODULE,
	.open           = dyn_event_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
	.write		= dyn_event_write,
};

/* Make a tracefs interface for controlling dynamic events */
static __init int init_dynamic_event(void)
{
	struct dentry *d_tracer;
	struct dentry *entry;

	d_tracer = tracing_init_dentry();
	if (IS_ERR(d_tracer))
		return 0;

	entry = tracefs_create_file("dynamic_events", 0644, d_tracer,
				    NULL, &dynamic_events_ops);

	/* Event list interface */
	if (!entry)
		pr_warn("Could not create tracefs 'dynamic_events' entry\n");

	return 0;
}
fs_initcall(init_dynamic_event);
