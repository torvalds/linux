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

static struct proc_dir_entry *proc_net_shadow;

static struct dentry *proc_net_shadow_dentry(struct dentry *parent,
						struct proc_dir_entry *de)
{
	struct dentry *shadow = NULL;
	struct inode *inode;
	if (!de)
		goto out;
	de_get(de);
	inode = proc_get_inode(parent->d_inode->i_sb, de->low_ino, de);
	if (!inode)
		goto out_de_put;
	shadow = d_alloc_name(parent, de->name);
	if (!shadow)
		goto out_iput;
	shadow->d_op = parent->d_op; /* proc_dentry_operations */
	d_instantiate(shadow, inode);
out:
	return shadow;
out_iput:
	iput(inode);
out_de_put:
	de_put(de);
	goto out;
}

static void *proc_net_follow_link(struct dentry *parent, struct nameidata *nd)
{
	struct net *net = current->nsproxy->net_ns;
	struct dentry *shadow;
	shadow = proc_net_shadow_dentry(parent, net->proc_net);
	if (!shadow)
		return ERR_PTR(-ENOENT);

	dput(nd->dentry);
	/* My dentry count is 1 and that should be enough as the
	 * shadow dentry is thrown away immediately.
	 */
	nd->dentry = shadow;
	return NULL;
}

static struct dentry *proc_net_lookup(struct inode *dir, struct dentry *dentry,
				      struct nameidata *nd)
{
	struct net *net = current->nsproxy->net_ns;
	struct dentry *shadow;

	shadow = proc_net_shadow_dentry(nd->dentry, net->proc_net);
	if (!shadow)
		return ERR_PTR(-ENOENT);

	dput(nd->dentry);
	nd->dentry = shadow;

	return shadow->d_inode->i_op->lookup(shadow->d_inode, dentry, nd);
}

static int proc_net_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct net *net = current->nsproxy->net_ns;
	struct dentry *shadow;
	int ret;

	shadow = proc_net_shadow_dentry(dentry->d_parent, net->proc_net);
	if (!shadow)
		return -ENOENT;
	ret = shadow->d_inode->i_op->setattr(shadow, iattr);
	dput(shadow);
	return ret;
}

static const struct file_operations proc_net_dir_operations = {
	.read			= generic_read_dir,
};

static struct inode_operations proc_net_dir_inode_operations = {
	.follow_link	= proc_net_follow_link,
	.lookup		= proc_net_lookup,
	.setattr	= proc_net_setattr,
};

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
	proc_net_shadow = proc_mkdir("net", NULL);
	proc_net_shadow->proc_iops = &proc_net_dir_inode_operations;
	proc_net_shadow->proc_fops = &proc_net_dir_operations;

	return register_pernet_subsys(&proc_net_ns_ops);
}
