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

static struct proc_dir_entry *shadow_pde;

static struct proc_dir_entry *proc_net_shadow(struct task_struct *task,
						struct proc_dir_entry *de)
{
	return task->nsproxy->net_ns->proc_net;
}

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
	struct proc_dir_entry *root, *netd, *net_statd;
	int err;

	err = -ENOMEM;
	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		goto out;

	err = -EEXIST;
	netd = proc_net_mkdir(net, "net", root);
	if (!netd)
		goto free_root;

	err = -EEXIST;
	net_statd = proc_net_mkdir(net, "stat", netd);
	if (!net_statd)
		goto free_net;

	root->data = net;

	net->proc_net_root = root;
	net->proc_net = netd;
	net->proc_net_stat = net_statd;
	err = 0;

out:
	return err;
free_net:
	remove_proc_entry("net", root);
free_root:
	kfree(root);
	goto out;
}

static __net_exit void proc_net_ns_exit(struct net *net)
{
	remove_proc_entry("stat", net->proc_net);
	remove_proc_entry("net", net->proc_net_root);
	kfree(net->proc_net_root);
}

static struct pernet_operations __net_initdata proc_net_ns_ops = {
	.init = proc_net_ns_init,
	.exit = proc_net_ns_exit,
};

int __init proc_net_init(void)
{
	shadow_pde = proc_mkdir("net", NULL);
	shadow_pde->shadow_proc = proc_net_shadow;

	return register_pernet_subsys(&proc_net_ns_ops);
}
