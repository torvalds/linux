// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/proc/net.c
 *
 *  Copyright (C) 2007
 *
 *  Author: Eric Biederman <ebiederm@xmission.com>
 *
 *  proc net directory handling functions
 */

#include <linux/uaccess.h>

#include <linux/errno.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/mount.h>
#include <linux/nsproxy.h>
#include <linux/uidgid.h>
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

static int seq_open_net(struct inode *inode, struct file *file)
{
	unsigned int state_size = PDE(inode)->state_size;
	struct seq_net_private *p;
	struct net *net;

	WARN_ON_ONCE(state_size < sizeof(*p));

	if (file->f_mode & FMODE_WRITE && !PDE(inode)->write)
		return -EACCES;

	net = get_proc_net(inode);
	if (!net)
		return -ENXIO;

	p = __seq_open_private(file, PDE(inode)->seq_ops, state_size);
	if (!p) {
		put_net(net);
		return -ENOMEM;
	}
#ifdef CONFIG_NET_NS
	p->net = net;
#endif
	return 0;
}

static int seq_release_net(struct inode *ino, struct file *f)
{
	struct seq_file *seq = f->private_data;

	put_net(seq_file_net(seq));
	seq_release_private(ino, f);
	return 0;
}

static const struct proc_ops proc_net_seq_ops = {
	.proc_open	= seq_open_net,
	.proc_read	= seq_read,
	.proc_write	= proc_simple_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release_net,
};

int bpf_iter_init_seq_net(void *priv_data, struct bpf_iter_aux_info *aux)
{
#ifdef CONFIG_NET_NS
	struct seq_net_private *p = priv_data;

	p->net = get_net(current->nsproxy->net_ns);
#endif
	return 0;
}

void bpf_iter_fini_seq_net(void *priv_data)
{
#ifdef CONFIG_NET_NS
	struct seq_net_private *p = priv_data;

	put_net(p->net);
#endif
}

struct proc_dir_entry *proc_create_net_data(const char *name, umode_t mode,
		struct proc_dir_entry *parent, const struct seq_operations *ops,
		unsigned int state_size, void *data)
{
	struct proc_dir_entry *p;

	p = proc_create_reg(name, mode, &parent, data);
	if (!p)
		return NULL;
	pde_force_lookup(p);
	p->proc_ops = &proc_net_seq_ops;
	p->seq_ops = ops;
	p->state_size = state_size;
	return proc_register(parent, p);
}
EXPORT_SYMBOL_GPL(proc_create_net_data);

/**
 * proc_create_net_data_write - Create a writable net_ns-specific proc file
 * @name: The name of the file.
 * @mode: The file's access mode.
 * @parent: The parent directory in which to create.
 * @ops: The seq_file ops with which to read the file.
 * @write: The write method with which to 'modify' the file.
 * @data: Data for retrieval by PDE_DATA().
 *
 * Create a network namespaced proc file in the @parent directory with the
 * specified @name and @mode that allows reading of a file that displays a
 * series of elements and also provides for the file accepting writes that have
 * some arbitrary effect.
 *
 * The functions in the @ops table are used to iterate over items to be
 * presented and extract the readable content using the seq_file interface.
 *
 * The @write function is called with the data copied into a kernel space
 * scratch buffer and has a NUL appended for convenience.  The buffer may be
 * modified by the @write function.  @write should return 0 on success.
 *
 * The @data value is accessible from the @show and @write functions by calling
 * PDE_DATA() on the file inode.  The network namespace must be accessed by
 * calling seq_file_net() on the seq_file struct.
 */
struct proc_dir_entry *proc_create_net_data_write(const char *name, umode_t mode,
						  struct proc_dir_entry *parent,
						  const struct seq_operations *ops,
						  proc_write_t write,
						  unsigned int state_size, void *data)
{
	struct proc_dir_entry *p;

	p = proc_create_reg(name, mode, &parent, data);
	if (!p)
		return NULL;
	pde_force_lookup(p);
	p->proc_ops = &proc_net_seq_ops;
	p->seq_ops = ops;
	p->state_size = state_size;
	p->write = write;
	return proc_register(parent, p);
}
EXPORT_SYMBOL_GPL(proc_create_net_data_write);

static int single_open_net(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *de = PDE(inode);
	struct net *net;
	int err;

	net = get_proc_net(inode);
	if (!net)
		return -ENXIO;

	err = single_open(file, de->single_show, net);
	if (err)
		put_net(net);
	return err;
}

static int single_release_net(struct inode *ino, struct file *f)
{
	struct seq_file *seq = f->private_data;
	put_net(seq->private);
	return single_release(ino, f);
}

static const struct proc_ops proc_net_single_ops = {
	.proc_open	= single_open_net,
	.proc_read	= seq_read,
	.proc_write	= proc_simple_write,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release_net,
};

struct proc_dir_entry *proc_create_net_single(const char *name, umode_t mode,
		struct proc_dir_entry *parent,
		int (*show)(struct seq_file *, void *), void *data)
{
	struct proc_dir_entry *p;

	p = proc_create_reg(name, mode, &parent, data);
	if (!p)
		return NULL;
	pde_force_lookup(p);
	p->proc_ops = &proc_net_single_ops;
	p->single_show = show;
	return proc_register(parent, p);
}
EXPORT_SYMBOL_GPL(proc_create_net_single);

/**
 * proc_create_net_single_write - Create a writable net_ns-specific proc file
 * @name: The name of the file.
 * @mode: The file's access mode.
 * @parent: The parent directory in which to create.
 * @show: The seqfile show method with which to read the file.
 * @write: The write method with which to 'modify' the file.
 * @data: Data for retrieval by PDE_DATA().
 *
 * Create a network-namespaced proc file in the @parent directory with the
 * specified @name and @mode that allows reading of a file that displays a
 * single element rather than a series and also provides for the file accepting
 * writes that have some arbitrary effect.
 *
 * The @show function is called to extract the readable content via the
 * seq_file interface.
 *
 * The @write function is called with the data copied into a kernel space
 * scratch buffer and has a NUL appended for convenience.  The buffer may be
 * modified by the @write function.  @write should return 0 on success.
 *
 * The @data value is accessible from the @show and @write functions by calling
 * PDE_DATA() on the file inode.  The network namespace must be accessed by
 * calling seq_file_single_net() on the seq_file struct.
 */
struct proc_dir_entry *proc_create_net_single_write(const char *name, umode_t mode,
						    struct proc_dir_entry *parent,
						    int (*show)(struct seq_file *, void *),
						    proc_write_t write,
						    void *data)
{
	struct proc_dir_entry *p;

	p = proc_create_reg(name, mode, &parent, data);
	if (!p)
		return NULL;
	pde_force_lookup(p);
	p->proc_ops = &proc_net_single_ops;
	p->single_show = show;
	p->write = write;
	return proc_register(parent, p);
}
EXPORT_SYMBOL_GPL(proc_create_net_single_write);

static struct net *get_proc_task_net(struct inode *dir)
{
	struct task_struct *task;
	struct nsproxy *ns;
	struct net *net = NULL;

	rcu_read_lock();
	task = pid_task(proc_pid(dir), PIDTYPE_PID);
	if (task != NULL) {
		task_lock(task);
		ns = task->nsproxy;
		if (ns != NULL)
			net = get_net(ns->net_ns);
		task_unlock(task);
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
		de = proc_lookup_de(dir, dentry, net->proc_net);
		put_net(net);
	}
	return de;
}

static int proc_tgid_net_getattr(struct user_namespace *mnt_userns,
				 const struct path *path, struct kstat *stat,
				 u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct net *net;

	net = get_proc_task_net(inode);

	generic_fillattr(&init_user_ns, inode, stat);

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
		ret = proc_readdir_de(file, ctx, net->proc_net);
		put_net(net);
	}
	return ret;
}

const struct file_operations proc_net_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= proc_tgid_net_readdir,
};

static __net_init int proc_net_ns_init(struct net *net)
{
	struct proc_dir_entry *netd, *net_statd;
	kuid_t uid;
	kgid_t gid;
	int err;

	err = -ENOMEM;
	netd = kmem_cache_zalloc(proc_dir_entry_cache, GFP_KERNEL);
	if (!netd)
		goto out;

	netd->subdir = RB_ROOT;
	netd->data = net;
	netd->nlink = 2;
	netd->namelen = 3;
	netd->parent = &proc_root;
	netd->name = netd->inline_name;
	memcpy(netd->name, "net", 4);

	uid = make_kuid(net->user_ns, 0);
	if (!uid_valid(uid))
		uid = netd->uid;

	gid = make_kgid(net->user_ns, 0);
	if (!gid_valid(gid))
		gid = netd->gid;

	proc_set_user(netd, uid, gid);

	err = -EEXIST;
	net_statd = proc_net_mkdir(net, "stat", netd);
	if (!net_statd)
		goto free_net;

	net->proc_net = netd;
	net->proc_net_stat = net_statd;
	return 0;

free_net:
	pde_free(netd);
out:
	return err;
}

static __net_exit void proc_net_ns_exit(struct net *net)
{
	remove_proc_entry("stat", net->proc_net);
	pde_free(net->proc_net);
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
