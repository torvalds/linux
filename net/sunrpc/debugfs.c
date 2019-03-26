// SPDX-License-Identifier: GPL-2.0
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
static struct dentry *rpc_fault_dir;
static struct dentry *rpc_clnt_dir;
static struct dentry *rpc_xprt_dir;

unsigned int rpc_inject_disconnect;

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
	struct rpc_clnt *clnt = f->private;
	loff_t pos = *ppos;
	struct rpc_task *task;

	spin_lock(&clnt->cl_lock);
	list_for_each_entry(task, &clnt->cl_tasks, tk_task)
		if (pos-- == 0)
			return task;
	return NULL;
}

static void *
tasks_next(struct seq_file *f, void *v, loff_t *pos)
{
	struct rpc_clnt *clnt = f->private;
	struct rpc_task *task = v;
	struct list_head *next = task->tk_task.next;

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
	struct rpc_clnt *clnt = f->private;
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
	int ret = seq_open(filp, &tasks_seq_operations);
	if (!ret) {
		struct seq_file *seq = filp->private_data;
		struct rpc_clnt *clnt = seq->private = inode->i_private;

		if (!atomic_inc_not_zero(&clnt->cl_count)) {
			seq_release(inode, filp);
			ret = -EINVAL;
		}
	}

	return ret;
}

static int
tasks_release(struct inode *inode, struct file *filp)
{
	struct seq_file *seq = filp->private_data;
	struct rpc_clnt *clnt = seq->private;

	rpc_release_client(clnt);
	return seq_release(inode, filp);
}

static const struct file_operations tasks_fops = {
	.owner		= THIS_MODULE,
	.open		= tasks_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= tasks_release,
};

void
rpc_clnt_debugfs_register(struct rpc_clnt *clnt)
{
	int len;
	char name[24]; /* enough for "../../rpc_xprt/ + 8 hex digits + NULL */
	struct rpc_xprt *xprt;

	/* Already registered? */
	if (clnt->cl_debugfs || !rpc_clnt_dir)
		return;

	len = snprintf(name, sizeof(name), "%x", clnt->cl_clid);
	if (len >= sizeof(name))
		return;

	/* make the per-client dir */
	clnt->cl_debugfs = debugfs_create_dir(name, rpc_clnt_dir);
	if (!clnt->cl_debugfs)
		return;

	/* make tasks file */
	if (!debugfs_create_file("tasks", S_IFREG | 0400, clnt->cl_debugfs,
				 clnt, &tasks_fops))
		goto out_err;

	rcu_read_lock();
	xprt = rcu_dereference(clnt->cl_xprt);
	/* no "debugfs" dentry? Don't bother with the symlink. */
	if (IS_ERR_OR_NULL(xprt->debugfs)) {
		rcu_read_unlock();
		return;
	}
	len = snprintf(name, sizeof(name), "../../rpc_xprt/%s",
			xprt->debugfs->d_name.name);
	rcu_read_unlock();

	if (len >= sizeof(name))
		goto out_err;

	if (!debugfs_create_symlink("xprt", clnt->cl_debugfs, name))
		goto out_err;

	return;
out_err:
	debugfs_remove_recursive(clnt->cl_debugfs);
	clnt->cl_debugfs = NULL;
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

void
rpc_xprt_debugfs_register(struct rpc_xprt *xprt)
{
	int len, id;
	static atomic_t	cur_id;
	char		name[9]; /* 8 hex digits + NULL term */

	if (!rpc_xprt_dir)
		return;

	id = (unsigned int)atomic_inc_return(&cur_id);

	len = snprintf(name, sizeof(name), "%x", id);
	if (len >= sizeof(name))
		return;

	/* make the per-client dir */
	xprt->debugfs = debugfs_create_dir(name, rpc_xprt_dir);
	if (!xprt->debugfs)
		return;

	/* make tasks file */
	if (!debugfs_create_file("info", S_IFREG | 0400, xprt->debugfs,
				 xprt, &xprt_info_fops)) {
		debugfs_remove_recursive(xprt->debugfs);
		xprt->debugfs = NULL;
	}

	atomic_set(&xprt->inject_disconnect, rpc_inject_disconnect);
}

void
rpc_xprt_debugfs_unregister(struct rpc_xprt *xprt)
{
	debugfs_remove_recursive(xprt->debugfs);
	xprt->debugfs = NULL;
}

static int
fault_open(struct inode *inode, struct file *filp)
{
	filp->private_data = kmalloc(128, GFP_KERNEL);
	if (!filp->private_data)
		return -ENOMEM;
	return 0;
}

static int
fault_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	return 0;
}

static ssize_t
fault_disconnect_read(struct file *filp, char __user *user_buf,
		      size_t len, loff_t *offset)
{
	char *buffer = (char *)filp->private_data;
	size_t size;

	size = sprintf(buffer, "%u\n", rpc_inject_disconnect);
	return simple_read_from_buffer(user_buf, len, offset, buffer, size);
}

static ssize_t
fault_disconnect_write(struct file *filp, const char __user *user_buf,
		       size_t len, loff_t *offset)
{
	char buffer[16];

	if (len >= sizeof(buffer))
		len = sizeof(buffer) - 1;
	if (copy_from_user(buffer, user_buf, len))
		return -EFAULT;
	buffer[len] = '\0';
	if (kstrtouint(buffer, 10, &rpc_inject_disconnect))
		return -EINVAL;
	return len;
}

static const struct file_operations fault_disconnect_fops = {
	.owner		= THIS_MODULE,
	.open		= fault_open,
	.read		= fault_disconnect_read,
	.write		= fault_disconnect_write,
	.release	= fault_release,
};

static struct dentry *
inject_fault_dir(struct dentry *topdir)
{
	struct dentry *faultdir;

	faultdir = debugfs_create_dir("inject_fault", topdir);
	if (!faultdir)
		return NULL;

	if (!debugfs_create_file("disconnect", S_IFREG | 0400, faultdir,
				 NULL, &fault_disconnect_fops))
		return NULL;

	return faultdir;
}

void __exit
sunrpc_debugfs_exit(void)
{
	debugfs_remove_recursive(topdir);
	topdir = NULL;
	rpc_fault_dir = NULL;
	rpc_clnt_dir = NULL;
	rpc_xprt_dir = NULL;
}

void __init
sunrpc_debugfs_init(void)
{
	topdir = debugfs_create_dir("sunrpc", NULL);
	if (!topdir)
		return;

	rpc_fault_dir = inject_fault_dir(topdir);
	if (!rpc_fault_dir)
		goto out_remove;

	rpc_clnt_dir = debugfs_create_dir("rpc_clnt", topdir);
	if (!rpc_clnt_dir)
		goto out_remove;

	rpc_xprt_dir = debugfs_create_dir("rpc_xprt", topdir);
	if (!rpc_xprt_dir)
		goto out_remove;

	return;
out_remove:
	debugfs_remove_recursive(topdir);
	topdir = NULL;
	rpc_fault_dir = NULL;
	rpc_clnt_dir = NULL;
}
