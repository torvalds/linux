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

#include "internal.h"


struct proc_dir_entry *proc_net_fops_create(struct net *net,
	const char *name, mode_t mode, const struct file_operations *fops)
{
	struct proc_dir_entry *res;

	res = create_proc_entry(name, mode, net->proc_net);
	if (res)
		res->proc_fops = fops;
	return res;
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

static __net_init int proc_net_ns_init(struct net *net)
{
	struct proc_dir_entry *root, *netd, *net_statd;
	int err;

	err = -ENOMEM;
	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		goto out;

	err = -EEXIST;
	netd = proc_mkdir("net", root);
	if (!netd)
		goto free_root;

	err = -EEXIST;
	net_statd = proc_mkdir("stat", netd);
	if (!net_statd)
		goto free_net;

	root->data = net;
	netd->data = net;
	net_statd->data = net;

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
