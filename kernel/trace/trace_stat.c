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
#include <linux/debugfs.h>
#include "trace_stat.h"
#include "trace.h"


/* List of stat entries from a tracer */
struct trace_stat_list {
	struct list_head	list;
	void			*stat;
};

/* A stat session is the stats output in one file */
struct tracer_stat_session {
	struct list_head	session_list;
	struct tracer_stat	*ts;
	struct list_head	stat_list;
	struct mutex		stat_mutex;
	struct dentry		*file;
};

/* All of the sessions currently in use. Each stat file embed one session */
static LIST_HEAD(all_stat_sessions);
static DEFINE_MUTEX(all_stat_sessions_mutex);

/* The root directory for all stat files */
static struct dentry		*stat_dir;


static void reset_stat_session(struct tracer_stat_session *session)
{
	struct trace_stat_list *node, *next;

	list_for_each_entry_safe(node, next, &session->stat_list, list)
		kfree(node);

	INIT_LIST_HEAD(&session->stat_list);
}

static void destroy_session(struct tracer_stat_session *session)
{
	debugfs_remove(session->file);
	reset_stat_session(session);
	mutex_destroy(&session->stat_mutex);
	kfree(session);
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
	void *stat;
	int ret = 0;
	int i;

	mutex_lock(&session->stat_mutex);
	reset_stat_session(session);

	if (!ts->stat_cmp)
		ts->stat_cmp = dummy_cmp;

	stat = ts->stat_start();
	if (!stat)
		goto exit;

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

	new_entry->stat = stat;

	/*
	 * Iterate over the tracer stat entries and store them in a sorted
	 * list.
	 */
	for (i = 1; ; i++) {
		stat = ts->stat_next(stat, i);

		/* End of insertion */
		if (!stat)
			break;

		new_entry = kmalloc(sizeof(struct trace_stat_list), GFP_KERNEL);
		if (!new_entry) {
			ret = -ENOMEM;
			goto exit_free_list;
		}

		INIT_LIST_HEAD(&new_entry->list);
		new_entry->stat = stat;

		list_for_each_entry_reverse(iter_entry, &session->stat_list,
				list) {

			/* Insertion with a descendent sorting */
			if (ts->stat_cmp(iter_entry->stat,
					new_entry->stat) >= 0) {

				list_add(&new_entry->list, &iter_entry->list);
				break;
			}
		}

		/* The current larger value */
		if (list_empty(&new_entry->list))
			list_add(&new_entry->list, &session->stat_list);
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
		return SEQ_START_TOKEN;

	return seq_list_start(&session->stat_list, *pos);
}

static void *stat_seq_next(struct seq_file *s, void *p, loff_t *pos)
{
	struct tracer_stat_session *session = s->private;

	if (p == SEQ_START_TOKEN)
		return seq_list_start(&session->stat_list, *pos);

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

	if (v == SEQ_START_TOKEN)
		return session->ts->stat_headers(s);

	return session->ts->stat_show(s, l->stat);
}

static const struct seq_operations trace_stat_seq_ops = {
	.start		= stat_seq_start,
	.next		= stat_seq_next,
	.stop		= stat_seq_stop,
	.show		= stat_seq_show
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

static int tracing_stat_init(void)
{
	struct dentry *d_tracing;

	d_tracing = tracing_init_dentry();

	stat_dir = debugfs_create_dir("trace_stat", d_tracing);
	if (!stat_dir)
		pr_warning("Could not create debugfs "
			   "'trace_stat' entry\n");
	return 0;
}

static int init_stat_file(struct tracer_stat_session *session)
{
	if (!stat_dir && tracing_stat_init())
		return -ENODEV;

	session->file = debugfs_create_file(session->ts->name, 0644,
					    stat_dir,
					    session, &tracing_stat_fops);
	if (!session->file)
		return -ENOMEM;
	return 0;
}

int register_stat_tracer(struct tracer_stat *trace)
{
	struct tracer_stat_session *session, *node, *tmp;
	int ret;

	if (!trace)
		return -EINVAL;

	if (!trace->stat_start || !trace->stat_next || !trace->stat_show)
		return -EINVAL;

	/* Already registered? */
	mutex_lock(&all_stat_sessions_mutex);
	list_for_each_entry_safe(node, tmp, &all_stat_sessions, session_list) {
		if (node->ts == trace) {
			mutex_unlock(&all_stat_sessions_mutex);
			return -EINVAL;
		}
	}
	mutex_unlock(&all_stat_sessions_mutex);

	/* Init the session */
	session = kmalloc(sizeof(struct tracer_stat_session), GFP_KERNEL);
	if (!session)
		return -ENOMEM;

	session->ts = trace;
	INIT_LIST_HEAD(&session->session_list);
	INIT_LIST_HEAD(&session->stat_list);
	mutex_init(&session->stat_mutex);
	session->file = NULL;

	ret = init_stat_file(session);
	if (ret) {
		destroy_session(session);
		return ret;
	}

	/* Register */
	mutex_lock(&all_stat_sessions_mutex);
	list_add_tail(&session->session_list, &all_stat_sessions);
	mutex_unlock(&all_stat_sessions_mutex);

	return 0;
}

void unregister_stat_tracer(struct tracer_stat *trace)
{
	struct tracer_stat_session *node, *tmp;

	mutex_lock(&all_stat_sessions_mutex);
	list_for_each_entry_safe(node, tmp, &all_stat_sessions, session_list) {
		if (node->ts == trace) {
			list_del(&node->session_list);
			destroy_session(node);
			break;
		}
	}
	mutex_unlock(&all_stat_sessions_mutex);
}
