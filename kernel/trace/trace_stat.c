/*
 * Infrastructure for statistic tracing (histogram output).
 *
 * Copyright (C) 2008 Frederic Weisbecker <fweisbec@gmail.com>
 *
 * Based on the code from trace_branch.c which is
 * Copyright (C) 2008 Steven Rostedt <srostedt@redhat.com>
 *
 */


#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include "trace.h"


/* List of stat entries from a tracer */
struct trace_stat_list {
	struct list_head list;
	void *stat;
};

static LIST_HEAD(stat_list);

/*
 * This is a copy of the current tracer to avoid racy
 * and dangerous output while the current tracer is
 * switched.
 */
static struct tracer current_tracer;

/*
 * Protect both the current tracer and the global
 * stat list.
 */
static DEFINE_MUTEX(stat_list_mutex);


static void reset_stat_list(void)
{
	struct trace_stat_list *node, *next;

	list_for_each_entry_safe(node, next, &stat_list, list)
		kfree(node);

	INIT_LIST_HEAD(&stat_list);
}

void init_tracer_stat(struct tracer *trace)
{
	mutex_lock(&stat_list_mutex);
	current_tracer = *trace;
	mutex_unlock(&stat_list_mutex);
}

/*
 * For tracers that don't provide a stat_cmp callback.
 * This one will force an immediate insertion on tail of
 * the list.
 */
static int dummy_cmp(void *p1, void *p2)
{
	return 1;
}

/*
 * Initialize the stat list at each trace_stat file opening.
 * All of these copies and sorting are required on all opening
 * since the stats could have changed between two file sessions.
 */
static int stat_seq_init(void)
{
	struct trace_stat_list *iter_entry, *new_entry;
	void *prev_stat;
	int ret = 0;
	int i;

	mutex_lock(&stat_list_mutex);
	reset_stat_list();

	if (!current_tracer.stat_start || !current_tracer.stat_next ||
					!current_tracer.stat_show)
		goto exit;

	if (!current_tracer.stat_cmp)
		current_tracer.stat_cmp = dummy_cmp;

	/*
	 * The first entry. Actually this is the second, but the first
	 * one (the stat_list head) is pointless.
	 */
	new_entry = kmalloc(sizeof(struct trace_stat_list), GFP_KERNEL);
	if (!new_entry) {
		ret = -ENOMEM;
		goto exit;
	}

	INIT_LIST_HEAD(&new_entry->list);
	list_add(&new_entry->list, &stat_list);
	new_entry->stat = current_tracer.stat_start();

	prev_stat = new_entry->stat;

	/*
	 * Iterate over the tracer stat entries and store them in a sorted
	 * list.
	 */
	for (i = 1; ; i++) {
		new_entry = kmalloc(sizeof(struct trace_stat_list), GFP_KERNEL);
		if (!new_entry) {
			ret = -ENOMEM;
			goto exit_free_list;
		}

		INIT_LIST_HEAD(&new_entry->list);
		new_entry->stat = current_tracer.stat_next(prev_stat, i);

		/* End of insertion */
		if (!new_entry->stat)
			break;

		list_for_each_entry(iter_entry, &stat_list, list) {
			/* Insertion with a descendent sorting */
			if (current_tracer.stat_cmp(new_entry->stat,
						iter_entry->stat) > 0) {

				list_add_tail(&new_entry->list,
						&iter_entry->list);
				break;

			/* The current smaller value */
			} else if (list_is_last(&iter_entry->list,
						&stat_list)) {
				list_add(&new_entry->list, &iter_entry->list);
				break;
			}
		}

		prev_stat = new_entry->stat;
	}
exit:
	mutex_unlock(&stat_list_mutex);
	return ret;

exit_free_list:
	reset_stat_list();
	mutex_unlock(&stat_list_mutex);
	return ret;
}


static void *stat_seq_start(struct seq_file *s, loff_t *pos)
{
	struct list_head *l = (struct list_head *)s->private;

	/* Prevent from tracer switch or stat_list modification */
	mutex_lock(&stat_list_mutex);

	/* If we are in the beginning of the file, print the headers */
	if (!*pos && current_tracer.stat_headers)
		current_tracer.stat_headers(s);

	return seq_list_start(l, *pos);
}

static void *stat_seq_next(struct seq_file *s, void *p, loff_t *pos)
{
	struct list_head *l = (struct list_head *)s->private;

	return seq_list_next(p, l, pos);
}

static void stat_seq_stop(struct seq_file *m, void *p)
{
	mutex_unlock(&stat_list_mutex);
}

static int stat_seq_show(struct seq_file *s, void *v)
{
	struct trace_stat_list *entry;

	entry =	list_entry(v, struct trace_stat_list, list);

	return current_tracer.stat_show(s, entry->stat);
}

static const struct seq_operations trace_stat_seq_ops = {
	.start = stat_seq_start,
	.next = stat_seq_next,
	.stop = stat_seq_stop,
	.show = stat_seq_show
};

static int tracing_stat_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = seq_open(file, &trace_stat_seq_ops);
	if (!ret) {
		struct seq_file *m = file->private_data;
		m->private = &stat_list;
		ret = stat_seq_init();
	}

	return ret;
}


/*
 * Avoid consuming memory with our now useless list.
 */
static int tracing_stat_release(struct inode *i, struct file *f)
{
	mutex_lock(&stat_list_mutex);
	reset_stat_list();
	mutex_unlock(&stat_list_mutex);
	return 0;
}

static const struct file_operations tracing_stat_fops = {
	.open		= tracing_stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= tracing_stat_release
};

static int __init tracing_stat_init(void)
{
	struct dentry *d_tracing;
	struct dentry *entry;

	d_tracing = tracing_init_dentry();

	entry = debugfs_create_file("trace_stat", 0444, d_tracing,
					NULL,
				    &tracing_stat_fops);
	if (!entry)
		pr_warning("Could not create debugfs "
			   "'trace_stat' entry\n");
	return 0;
}
fs_initcall(tracing_stat_init);
