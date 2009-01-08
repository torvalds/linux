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

/* A stat session is the stats output in one file */
struct tracer_stat_session {
	struct tracer_stat *ts;
	struct list_head stat_list;
	struct mutex stat_mutex;
};

/* All of the sessions currently in use. Each stat file embeed one session */
static struct tracer_stat_session **all_stat_sessions;
static int nb_sessions;
static struct dentry *stat_dir, **stat_files;


static void reset_stat_session(struct tracer_stat_session *session)
{
	struct trace_stat_list *node, *next;

	list_for_each_entry_safe(node, next, &session->stat_list, list)
		kfree(node);

	INIT_LIST_HEAD(&session->stat_list);
}

/* Called when a tracer is initialized */
static int init_all_sessions(int nb, struct tracer_stat *ts)
{
	int i, j;
	struct tracer_stat_session *session;

	nb_sessions = 0;

	if (all_stat_sessions) {
		for (i = 0; i < nb_sessions; i++) {
			session = all_stat_sessions[i];
			reset_stat_session(session);
			mutex_destroy(&session->stat_mutex);
			kfree(session);
		}
	}
	all_stat_sessions = kmalloc(sizeof(struct tracer_stat_session *) * nb,
				    GFP_KERNEL);
	if (!all_stat_sessions)
		return -ENOMEM;

	for (i = 0; i < nb; i++) {
		session = kmalloc(sizeof(struct tracer_stat_session) * nb,
				  GFP_KERNEL);
		if (!session)
			goto free_sessions;

		INIT_LIST_HEAD(&session->stat_list);
		mutex_init(&session->stat_mutex);
		session->ts = &ts[i];
		all_stat_sessions[i] = session;
	}
	nb_sessions = nb;
	return 0;

free_sessions:

	for (j = 0; j < i; j++)
		kfree(all_stat_sessions[i]);

	kfree(all_stat_sessions);
	all_stat_sessions = NULL;

	return -ENOMEM;
}

static int basic_tracer_stat_checks(struct tracer_stat *ts)
{
	int i;

	if (!ts)
		return 0;

	for (i = 0; ts[i].name; i++) {
		if (!ts[i].stat_start || !ts[i].stat_next || !ts[i].stat_show)
			return -EBUSY;
	}
	return i;
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
static int stat_seq_init(struct tracer_stat_session *session)
{
	struct trace_stat_list *iter_entry, *new_entry;
	struct tracer_stat *ts = session->ts;
	void *prev_stat;
	int ret = 0;
	int i;

	mutex_lock(&session->stat_mutex);
	reset_stat_session(session);

	if (!ts->stat_cmp)
		ts->stat_cmp = dummy_cmp;

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

	list_add(&new_entry->list, &session->stat_list);

	new_entry->stat = ts->stat_start();
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
		new_entry->stat = ts->stat_next(prev_stat, i);

		/* End of insertion */
		if (!new_entry->stat)
			break;

		list_for_each_entry(iter_entry, &session->stat_list, list) {

			/* Insertion with a descendent sorting */
			if (ts->stat_cmp(new_entry->stat,
						iter_entry->stat) > 0) {

				list_add_tail(&new_entry->list,
						&iter_entry->list);
				break;

			/* The current smaller value */
			} else if (list_is_last(&iter_entry->list,
						&session->stat_list)) {
				list_add(&new_entry->list, &iter_entry->list);
				break;
			}
		}

		prev_stat = new_entry->stat;
	}
exit:
	mutex_unlock(&session->stat_mutex);
	return ret;

exit_free_list:
	reset_stat_session(session);
	mutex_unlock(&session->stat_mutex);
	return ret;
}


static void *stat_seq_start(struct seq_file *s, loff_t *pos)
{
	struct tracer_stat_session *session = s->private;

	/* Prevent from tracer switch or stat_list modification */
	mutex_lock(&session->stat_mutex);

	/* If we are in the beginning of the file, print the headers */
	if (!*pos && session->ts->stat_headers)
		session->ts->stat_headers(s);

	return seq_list_start(&session->stat_list, *pos);
}

static void *stat_seq_next(struct seq_file *s, void *p, loff_t *pos)
{
	struct tracer_stat_session *session = s->private;

	return seq_list_next(p, &session->stat_list, pos);
}

static void stat_seq_stop(struct seq_file *s, void *p)
{
	struct tracer_stat_session *session = s->private;
	mutex_unlock(&session->stat_mutex);
}

static int stat_seq_show(struct seq_file *s, void *v)
{
	struct tracer_stat_session *session = s->private;
	struct trace_stat_list *l = list_entry(v, struct trace_stat_list, list);

	return session->ts->stat_show(s, l->stat);
}

static const struct seq_operations trace_stat_seq_ops = {
	.start = stat_seq_start,
	.next = stat_seq_next,
	.stop = stat_seq_stop,
	.show = stat_seq_show
};

/* The session stat is refilled and resorted at each stat file opening */
static int tracing_stat_open(struct inode *inode, struct file *file)
{
	int ret;

	struct tracer_stat_session *session = inode->i_private;

	ret = seq_open(file, &trace_stat_seq_ops);
	if (!ret) {
		struct seq_file *m = file->private_data;
		m->private = session;
		ret = stat_seq_init(session);
	}

	return ret;
}


/*
 * Avoid consuming memory with our now useless list.
 */
static int tracing_stat_release(struct inode *i, struct file *f)
{
	struct tracer_stat_session *session = i->i_private;

	mutex_lock(&session->stat_mutex);
	reset_stat_session(session);
	mutex_unlock(&session->stat_mutex);

	return 0;
}

static const struct file_operations tracing_stat_fops = {
	.open		= tracing_stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= tracing_stat_release
};


static void destroy_trace_stat_files(void)
{
	int i;

	if (stat_files) {
		for (i = 0; i < nb_sessions; i++)
			debugfs_remove(stat_files[i]);
		kfree(stat_files);
		stat_files = NULL;
	}
}

static void init_trace_stat_files(void)
{
	int i;

	if (!stat_dir || !nb_sessions)
		return;

	stat_files = kmalloc(sizeof(struct dentry *) * nb_sessions, GFP_KERNEL);

	if (!stat_files) {
		pr_warning("trace stat: not enough memory\n");
		return;
	}

	for (i = 0; i < nb_sessions; i++) {
		struct tracer_stat_session *session = all_stat_sessions[i];
		stat_files[i] = debugfs_create_file(session->ts->name, 0644,
						stat_dir,
						session, &tracing_stat_fops);
		if (!stat_files[i])
			pr_warning("cannot create %s entry\n",
				   session->ts->name);
	}
}

void init_tracer_stat(struct tracer *trace)
{
	int nb = basic_tracer_stat_checks(trace->stats);

	destroy_trace_stat_files();

	if (nb < 0) {
		pr_warning("stat tracing: missing stat callback on %s\n",
			   trace->name);
		return;
	}
	if (!nb)
		return;

	init_all_sessions(nb, trace->stats);
	init_trace_stat_files();
}

static int __init tracing_stat_init(void)
{
	struct dentry *d_tracing;

	d_tracing = tracing_init_dentry();

	stat_dir = debugfs_create_dir("trace_stat", d_tracing);
	if (!stat_dir)
		pr_warning("Could not create debugfs "
			   "'trace_stat' entry\n");
	return 0;
}
fs_initcall(tracing_stat_init);
