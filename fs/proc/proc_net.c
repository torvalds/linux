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
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/smp_lock.h>
#include <linux/mount.h>
#include <linux/nsproxy.h>
#include <net/net_namespace.h>
#include <linux/seq_file.h>

#include "internal.h"


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
	p->net = net;
	return 0;
}
EXPORT_SYMBOL_GPL(seq_open_net);

int seq_release_net(struct inode *ino, struct file *f)
{
	struct seq_file *seq;
	struct seq_net_private *p;

	seq = f->private_data;
	p = seq->private;

	put_net(p->net);
	seq_release_private(ino, f);
	return 0;
}
EXPORT_SYMBOL_GPL(seq_release_net);

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
		struct dentry *dentry, struct nameidata *nd)
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

static int proc_tgid_net_readdir(struct file *filp, void *dirent,
		filldir_t filldir)
{
	int ret;
	struct net *net;

	ret = -EINVAL;
	net = get_proc_task_net(filp->f_path.dentry->d_inode);
	if (net != NULL) {
		ret = proc_readdir_de(net->proc_net, filp, dirent, filldir);
		put_net(net);
	}
	return ret;
}

const struct file_operations proc_net_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_tgid_net_readdir,
};


struct proc_dir_entry *proc_net_fops_create(struct net *net,
	const char *name, mode_t mode, const struct file_operations *fops)
{
	return proc_create(name, mode, net->proc_net, fops);
}
EXPORT_SYMBOL_GPL(proc_net_fops_create);

void proc_net_remove(struct net *net, const char *name)
{
	remove_proc_entry(name, net->proc_net);
}
EXPORT_SYMBOL_GPL(proc_net_remove);

struct net *get_proc_net(const struct inode *inode)
{
	return maybe_get_net(PDE_NET(PDE(inode)));
}
EXPORT_SYMBOL_GPL(get_proc_net);

struct proc_dir_entry *proc_net_mkdir(struct net *net, const char *name,
		struct proc_dir_entry *parent)
{
	struct proc_dir_entry *pde;
	pde = proc_mkdir_mode(name, S_IRUGO | S_IXUGO, parent);
	if (pde != NULL)
		pde->data = net;
	return pde;
}
EXPORT_SYMBOL_GPL(proc_net_mkdir);

static __net_init int proc_net_ns_init(struct net *net)
{
	struct proc_dir_entry *netd, *net_statd;
	int err;

	err = -ENOMEM;
	netd = kzalloc(sizeof(*netd), GFP_KERNEL);
	if (!netd)
		goto out;

	netd->data = net;
	netd->nlink = 2;
	netd->name = "net";
	netd->namelen = 3;
	netd->parent = &proc_root;

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
