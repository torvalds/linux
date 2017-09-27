/*
 * Infrastructure for statistic tracing (histogram output).
 *
 * Copyright (C) 2008-2009 Frederic Weisbecker <fweisbec@gmail.com>
 *
 * Based on the code from trace_branch.c which is
 * Copyright (C) 2008 Steven Rostedt <srostedt@redhat.com>
 *
 */


#include <linux/list.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/tracefs.h>
#include "trace_stat.h"
#include "trace.h"


/*
 * List of stat red-black nodes from a tracer
 * We use a such tree to sort quickly the stat
 * entries from the tracer.
 */
struct stat_node {
	struct rb_node		node;
	void			*stat;
};

/* A stat session is the stats output in one file */
struct stat_session {
	struct list_head	session_list;
	struct tracer_stat	*ts;
	struct rb_root		stat_root;
	struct mutex		stat_mutex;
	struct dentry		*file;
};

/* All of the sessions currently in use. Each stat file embed one session */
static LIST_HEAD(all_stat_sessions);
static DEFINE_MUTEX(all_stat_sessions_mutex);

/* The root directory for all stat files */
static struct dentry		*stat_dir;

static void __reset_stat_session(struct stat_session *session)
{
	struct stat_node *snode, *n;

	rbtree_postorder_for_each_entry_safe(snode, n, &session->stat_root, node) {
		if (session->ts->stat_release)
			session->ts->stat_release(snode->stat);
		kfree(snode);
	}

	session->stat_root = RB_ROOT;
}

static void reset_stat_session(struct stat_session *session)
{
	mutex_lock(&session->stat_mutex);
	__reset_stat_session(session);
	mutex_unlock(&session->stat_mutex);
}

static void destroy_session(struct stat_session *session)
{
	tracefs_remove(session->file);
	__reset_stat_session(session);
	mutex_destroy(&session->stat_mutex);
	kfree(session);
}

typedef int (*cmp_stat_t)(void *, void *);

static int insert_stat(struct rb_root *root, void *stat, cmp_stat_t cmp)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct stat_node *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->stat = stat;

	/*
	 * Figure out where to put new node
	 * This is a descendent sorting
	 */
	while (*new) {
		struct stat_node *this;
		int result;

		this = container_of(*new, struct stat_node, node);
		result = cmp(data->stat, this->stat);

		parent = *new;
		if (result >= 0)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
	return 0;
}

/*
 * For tracers that don't provide a stat_cmp callback.
 * This one will force an insertion as right-most node
 * in the rbtree.
 */
static int dummy_cmp(void *p1, void *p2)
{
	return -1;
}

/*
 * Initialize the stat rbtree at each trace_stat file opening.
 * All of these copies and sorting are required on all opening
 * since the stats could have changed between two file sessions.
 */
static int stat_seq_init(struct stat_session *session)
{
	struct tracer_stat *ts = session->ts;
	struct rb_root *root = &session->stat_root;
	void *stat;
	int ret = 0;
	int i;

	mutex_lock(&session->stat_mutex);
	__reset_stat_session(session);

	if (!ts->stat_cmp)
		ts->stat_cmp = dummy_cmp;

	stat = ts->stat_start(ts);
	if (!stat)
		goto exit;

	ret = insert_stat(root, stat, ts->stat_cmp);
	if (ret)
		goto exit;

	/*
	 * Iterate over the tracer stat entries and store them in an rbtree.
	 */
	for (i = 1; ; i++) {
		stat = ts->stat_next(stat, i);

		/* End of insertion */
		if (!stat)
			break;

		ret = insert_stat(root, stat, ts->stat_cmp);
		if (ret)
			goto exit_free_rbtree;
	}

exit:
	mutex_unlock(&session->stat_mutex);
	return ret;

exit_free_rbtree:
	__reset_stat_session(session);
	mutex_unlock(&session->stat_mutex);
	return ret;
}


static void *stat_seq_start(struct seq_file *s, loff_t *pos)
{
	struct stat_session *session = s->private;
	struct rb_node *node;
	int n = *pos;
	int i;

	/* Prevent from tracer switch or rbtree modification */
	mutex_lock(&session->stat_mutex);

	/* If we are in the beginning of the file, print the headers */
	if (session->ts->stat_headers) {
		if (n == 0)
			return SEQ_START_TOKEN;
		n--;
	}

	node = rb_first(&session->stat_root);
	for (i = 0; node && i < n; i++)
		node = rb_next(node);

	return node;
}

static void *stat_seq_next(struct seq_file *s, void *p, loff_t *pos)
{
	struct stat_session *session = s->private;
	struct rb_node *node = p;

	(*pos)++;

	if (p == SEQ_START_TOKEN)
		return rb_first(&session->stat_root);

	return rb_next(node);
}

static void stat_seq_stop(struct seq_file *s, void *p)
{
	struct stat_session *session = s->private;
	mutex_unlock(&session->stat_mutex);
}

static int stat_seq_show(struct seq_file *s, void *v)
{
	struct stat_session *session = s->private;
	struct stat_node *l = container_of(v, struct stat_node, node);

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
	struct seq_file *m;
	struct stat_session *session = inode->i_private;

	ret = stat_seq_init(session);
	if (ret)
		return ret;

	ret = seq_open(file, &trace_stat_seq_ops);
	if (ret) {
		reset_stat_session(session);
		return ret;
	}

	m = file->private_data;
	m->private = session;
	return ret;
}

/*
 * Avoid consuming memory with our now useless rbtree.
 */
static int tracing_stat_release(struct inode *i, struct file *f)
{
	struct stat_session *session = i->i_private;

	reset_stat_session(session);

	return seq_release(i, f);
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
	if (IS_ERR(d_tracing))
		return 0;

	stat_dir = tracefs_create_dir("trace_stat", d_tracing);
	if (!stat_dir)
		pr_warn("Could not create tracefs 'trace_stat' entry\n");
	return 0;
}

static int init_stat_file(struct stat_session *session)
{
	if (!stat_dir && tracing_stat_init())
		return -ENODEV;

	session->file = tracefs_create_file(session->ts->name, 0644,
					    stat_dir,
					    session, &tracing_stat_fops);
	if (!session->file)
		return -ENOMEM;
	return 0;
}

int register_stat_tracer(struct tracer_stat *trace)
{
	struct stat_session *session, *node;
	int ret;

	if (!trace)
		return -EINVAL;

	if (!trace->stat_start || !trace->stat_next || !trace->stat_show)
		return -EINVAL;

	/* Already registered? */
	mutex_lock(&all_stat_sessions_mutex);
	list_for_each_entry(node, &all_stat_sessions, session_list) {
		if (node->ts == trace) {
			mutex_unlock(&all_stat_sessions_mutex);
			return -EINVAL;
		}
	}
	mutex_unlock(&all_stat_sessions_mutex);

	/* Init the session */
	session = kzalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		return -ENOMEM;

	session->ts = trace;
	INIT_LIST_HEAD(&session->session_list);
	mutex_init(&session->stat_mutex);

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
	struct stat_session *node, *tmp;

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
