/**
 * debugfs interface for sunrpc
 *
 * (c) 2014 Jeff Layton <jlayton@primarydata.com>
 */

#include <linux/debugfs.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>
#include "netns.h"

static struct dentry *topdir;
static struct dentry *rpc_clnt_dir;
static struct dentry *rpc_xprt_dir;

struct rpc_clnt_iter {
	struct rpc_clnt	*clnt;
	loff_t		pos;
};

static int
tasks_show(struct seq_file *f, void *v)
{
	u32 xid = 0;
	struct rpc_task *task = v;
	struct rpc_clnt *clnt = task->tk_client;
	const char *rpc_waitq = "none";

	if (RPC_IS_QUEUED(task))
		rpc_waitq = rpc_qname(task->tk_waitqueue);

	if (task->tk_rqstp)
		xid = be32_to_cpu(task->tk_rqstp->rq_xid);

	seq_printf(f, "%5u %04x %6d 0x%x 0x%x %8ld %ps %sv%u %s a:%ps q:%s\n",
		task->tk_pid, task->tk_flags, task->tk_status,
		clnt->cl_clid, xid, task->tk_timeout, task->tk_ops,
		clnt->cl_program->name, clnt->cl_vers, rpc_proc_name(task),
		task->tk_action, rpc_waitq);
	return 0;
}

static void *
tasks_start(struct seq_file *f, loff_t *ppos)
	__acquires(&clnt->cl_lock)
{
	struct rpc_clnt_iter *iter = f->private;
	loff_t pos = *ppos;
	struct rpc_clnt *clnt = iter->clnt;
	struct rpc_task *task;

	iter->pos = pos + 1;
	spin_lock(&clnt->cl_lock);
	list_for_each_entry(task, &clnt->cl_tasks, tk_task)
		if (pos-- == 0)
			return task;
	return NULL;
}

static void *
tasks_next(struct seq_file *f, void *v, loff_t *pos)
{
	struct rpc_clnt_iter *iter = f->private;
	struct rpc_clnt *clnt = iter->clnt;
	struct rpc_task *task = v;
	struct list_head *next = task->tk_task.next;

	++iter->pos;
	++*pos;

	/* If there's another task on list, return it */
	if (next == &clnt->cl_tasks)
		return NULL;
	return list_entry(next, struct rpc_task, tk_task);
}

static void
tasks_stop(struct seq_file *f, void *v)
	__releases(&clnt->cl_lock)
{
	struct rpc_clnt_iter *iter = f->private;
	struct rpc_clnt *clnt = iter->clnt;

	spin_unlock(&clnt->cl_lock);
}

static const struct seq_operations tasks_seq_operations = {
	.start	= tasks_start,
	.next	= tasks_next,
	.stop	= tasks_stop,
	.show	= tasks_show,
};

static int tasks_open(struct inode *inode, struct file *filp)
{
	int ret = seq_open_private(filp, &tasks_seq_operations,
					sizeof(struct rpc_clnt_iter));

	if (!ret) {
		struct seq_file *seq = filp->private_data;
		struct rpc_clnt_iter *iter = seq->private;

		iter->clnt = inode->i_private;

		if (!atomic_inc_not_zero(&iter->clnt->cl_count)) {
			seq_release_private(inode, filp);
			ret = -EINVAL;
		}
	}

	return ret;
}

static int
tasks_release(struct inode *inode, struct file *filp)
{
	struct seq_file *seq = filp->private_data;
	struct rpc_clnt_iter *iter = seq->private;

	rpc_release_client(iter->clnt);
	return seq_release_private(inode, filp);
}

static const struct file_operations tasks_fops = {
	.owner		= THIS_MODULE,
	.open		= tasks_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= tasks_release,
};

int
rpc_clnt_debugfs_register(struct rpc_clnt *clnt)
{
	int len, err;
	char name[24]; /* enough for "../../rpc_xprt/ + 8 hex digits + NULL */

	/* Already registered? */
	if (clnt->cl_debugfs)
		return 0;

	len = snprintf(name, sizeof(name), "%x", clnt->cl_clid);
	if (len >= sizeof(name))
		return -EINVAL;

	/* make the per-client dir */
	clnt->cl_debugfs = debugfs_create_dir(name, rpc_clnt_dir);
	if (!clnt->cl_debugfs)
		return -ENOMEM;

	/* make tasks file */
	err = -ENOMEM;
	if (!debugfs_create_file("tasks", S_IFREG | S_IRUSR, clnt->cl_debugfs,
				 clnt, &tasks_fops))
		goto out_err;

	err = -EINVAL;
	rcu_read_lock();
	len = snprintf(name, sizeof(name), "../../rpc_xprt/%s",
			rcu_dereference(clnt->cl_xprt)->debugfs->d_name.name);
	rcu_read_unlock();
	if (len >= sizeof(name))
		goto out_err;

	err = -ENOMEM;
	if (!debugfs_create_symlink("xprt", clnt->cl_debugfs, name))
		goto out_err;

	return 0;
out_err:
	debugfs_remove_recursive(clnt->cl_debugfs);
	clnt->cl_debugfs = NULL;
	return err;
}

void
rpc_clnt_debugfs_unregister(struct rpc_clnt *clnt)
{
	debugfs_remove_recursive(clnt->cl_debugfs);
	clnt->cl_debugfs = NULL;
}

static int
xprt_info_show(struct seq_file *f, void *v)
{
	struct rpc_xprt *xprt = f->private;

	seq_printf(f, "netid: %s\n", xprt->address_strings[RPC_DISPLAY_NETID]);
	seq_printf(f, "addr:  %s\n", xprt->address_strings[RPC_DISPLAY_ADDR]);
	seq_printf(f, "port:  %s\n", xprt->address_strings[RPC_DISPLAY_PORT]);
	seq_printf(f, "state: 0x%lx\n", xprt->state);
	return 0;
}

static int
xprt_info_open(struct inode *inode, struct file *filp)
{
	int ret;
	struct rpc_xprt *xprt = inode->i_private;

	ret = single_open(filp, xprt_info_show, xprt);

	if (!ret) {
		if (!xprt_get(xprt)) {
			single_release(inode, filp);
			ret = -EINVAL;
		}
	}
	return ret;
}

static int
xprt_info_release(struct inode *inode, struct file *filp)
{
	struct rpc_xprt *xprt = inode->i_private;

	xprt_put(xprt);
	return single_release(inode, filp);
}

static const struct file_operations xprt_info_fops = {
	.owner		= THIS_MODULE,
	.open		= xprt_info_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= xprt_info_release,
};

int
rpc_xprt_debugfs_register(struct rpc_xprt *xprt)
{
	int len, id;
	static atomic_t	cur_id;
	char		name[9]; /* 8 hex digits + NULL term */

	id = (unsigned int)atomic_inc_return(&cur_id);

	len = snprintf(name, sizeof(name), "%x", id);
	if (len >= sizeof(name))
		return -EINVAL;

	/* make the per-client dir */
	xprt->debugfs = debugfs_create_dir(name, rpc_xprt_dir);
	if (!xprt->debugfs)
		return -ENOMEM;

	/* make tasks file */
	if (!debugfs_create_file("info", S_IFREG | S_IRUSR, xprt->debugfs,
				 xprt, &xprt_info_fops)) {
		debugfs_remove_recursive(xprt->debugfs);
		xprt->debugfs = NULL;
		return -ENOMEM;
	}

	return 0;
}

void
rpc_xprt_debugfs_unregister(struct rpc_xprt *xprt)
{
	debugfs_remove_recursive(xprt->debugfs);
	xprt->debugfs = NULL;
}

void __exit
sunrpc_debugfs_exit(void)
{
	debugfs_remove_recursive(topdir);
}

int __init
sunrpc_debugfs_init(void)
{
	topdir = debugfs_create_dir("sunrpc", NULL);
	if (!topdir)
		goto out;

	rpc_clnt_dir = debugfs_create_dir("rpc_clnt", topdir);
	if (!rpc_clnt_dir)
		goto out_remove;

	rpc_xprt_dir = debugfs_create_dir("rpc_xprt", topdir);
	if (!rpc_xprt_dir)
		goto out_remove;

	return 0;
out_remove:
	debugfs_remove_recursive(topdir);
	topdir = NULL;
out:
	return -ENOMEM;
}
