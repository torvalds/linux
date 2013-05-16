/*
 *  linux/fs/proc/net.c
 *
 *  Copyright (C) 2007
 *
 *  Author: Eric Biederman <ebiederm@xmission.com>
 *
 *  proc net directory handling functions
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/mount.h>
#include <linux/nsproxy.h>
#include <net/net_namespace.h>
#include <linux/seq_file.h>

#include "internal.h"

static inline struct net *PDE_NET(struct proc_dir_entry *pde)
{
	return pde->parent->data;
}

static struct net *get_proc_net(const struct inode *inode)
{
	return maybe_get_net(PDE_NET(PDE(inode)));
}

int seq_open_net(struct inode *ino, struct file *f,
		 const struct seq_operations *ops, int size)
{
	struct net *net;
	struct seq_net_private *p;

	BUG_ON(size < sizeof(*p));

	net = get_proc_net(ino);
	if (net == NULL)
		return -ENXIO;

	p = __seq_open_private(f, ops, size);
	if (p == NULL) {
		put_net(net);
		return -ENOMEM;
	}
#ifdef CONFIG_NET_NS
	p->net = net;
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(seq_open_net);

int single_open_net(struct inode *inode, struct file *file,
		int (*show)(struct seq_file *, void *))
{
	int err;
	struct net *net;

	err = -ENXIO;
	net = get_proc_net(inode);
	if (net == NULL)
		goto err_net;

	err = single_open(file, show, net);
	if (err < 0)
		goto err_open;

	return 0;

err_open:
	put_net(net);
err_net:
	return err;
}
EXPORT_SYMBOL_GPL(single_open_net);

int seq_release_net(struct inode *ino, struct file *f)
{
	struct seq_file *seq;

	seq = f->private_data;

	put_net(seq_file_net(seq));
	seq_release_private(ino, f);
	return 0;
}
EXPORT_SYMBOL_GPL(seq_release_net);

int single_release_net(struct inode *ino, struct file *f)
{
	struct seq_file *seq = f->private_data;
	put_net(seq->private);
	return single_release(ino, f);
}
EXPORT_SYMBOL_GPL(single_release_net);

static struct net *get_proc_task_net(struct inode *dir)
{
	struct task_struct *task;
	struct nsproxy *ns;
	struct net *net = NULL;

	rcu_read_lock();
	task = pid_task(proc_pid(dir), PIDTYPE_PID);
	if (task != NULL) {
		ns = task_nsproxy(task);
		if (ns != NULL)
			net = get_net(ns->net_ns);
	}
	rcu_read_unlock();

	return net;
}

static struct dentry *proc_tgid_net_lookup(struct inode *dir,
		struct dentry *dentry, unsigned int flags)
{
	struct dentry *de;
	struct net *net;

	de = ERR_PTR(-ENOENT);
	net = get_proc_task_net(dir);
	if (net != NULL) {
		de = proc_lookup_de(net->proc_net, dir, dentry);
		put_net(net);
	}
	return de;
}

static int proc_tgid_net_getattr(struct vfsmount *mnt, struct dentry *dentry,
		struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct net *net;

	net = get_proc_task_net(inode);

	generic_fillattr(inode, stat);

	if (net != NULL) {
		stat->nlink = net->proc_net->nlink;
		put_net(net);
	}

	return 0;
}

const struct inode_operations proc_net_inode_operations = {
	.lookup		= proc_tgid_net_lookup,
	.getattr	= proc_tgid_net_getattr,
};

static int proc_tgid_net_readdir(struct file *file, struct dir_context *ctx)
{
	int ret;
	struct net *net;

	ret = -EINVAL;
	net = get_proc_task_net(file_inode(file));
	if (net != NULL) {
		ret = proc_readdir_de(net->proc_net, file, ctx);
		put_net(net);
	}
	return ret;
}

const struct file_operations proc_net_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate	= proc_tgid_net_readdir,
};

static __net_init int proc_net_ns_init(struct net *net)
{
	struct proc_dir_entry *netd, *net_statd;
	int err;

	err = -ENOMEM;
	netd = kzalloc(sizeof(*netd) + 4, GFP_KERNEL);
	if (!netd)
		goto out;

	netd->data = net;
	netd->nlink = 2;
	netd->namelen = 3;
	netd->parent = &proc_root;
	memcpy(netd->name, "net", 4);

	err = -EEXIST;
	net_statd = proc_net_mkdir(net, "stat", netd);
	if (!net_statd)
		goto free_net;

	net->proc_net = netd;
	net->proc_net_stat = net_statd;
	return 0;

free_net:
	kfree(netd);
out:
	return err;
}

static __net_exit void proc_net_ns_exit(struct net *net)
{
	remove_proc_entry("stat", net->proc_net);
	kfree(net->proc_net);
}

static struct pernet_operations __net_initdata proc_net_ns_ops = {
	.init = proc_net_ns_init,
	.exit = proc_net_ns_exit,
};

int __init proc_net_init(void)
{
	proc_symlink("net", NULL, "self/net");

	return register_pernet_subsys(&proc_net_ns_ops);
}
