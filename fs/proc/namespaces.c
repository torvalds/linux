#include <linux/proc_fs.h>
#include <linux/nsproxy.h>
#include <linux/ptrace.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <net/net_namespace.h>
#include <linux/ipc_namespace.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include "internal.h"


static const struct proc_ns_operations *ns_entries[] = {
#ifdef CONFIG_NET_NS
	&netns_operations,
#endif
#ifdef CONFIG_UTS_NS
	&utsns_operations,
#endif
#ifdef CONFIG_IPC_NS
	&ipcns_operations,
#endif
#ifdef CONFIG_PID_NS
	&pidns_operations,
#endif
#ifdef CONFIG_USER_NS
	&userns_operations,
#endif
	&mntns_operations,
};

static void *proc_ns_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	const struct proc_ns_operations *ns_ops = PROC_I(inode)->ns.ns_ops;
	struct task_struct *task;
	struct path ns_path;
	void *error = ERR_PTR(-EACCES);

	task = get_proc_task(inode);
	if (!task)
		return error;

	if (ptrace_may_access(task, PTRACE_MODE_READ)) {
		error = ns_get_path(&ns_path, task, ns_ops);
		if (!error)
			nd_jump_link(nd, &ns_path);
	}
	put_task_struct(task);
	return error;
}

static int proc_ns_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	const struct proc_ns_operations *ns_ops = PROC_I(inode)->ns.ns_ops;
	struct task_struct *task;
	char name[50];
	int res = -EACCES;

	task = get_proc_task(inode);
	if (!task)
		return res;

	if (ptrace_may_access(task, PTRACE_MODE_READ)) {
		res = ns_get_name(name, sizeof(name), task, ns_ops);
		if (res >= 0)
			res = readlink_copy(buffer, buflen, name);
	}
	put_task_struct(task);
	return res;
}

static const struct inode_operations proc_ns_link_inode_operations = {
	.readlink	= proc_ns_readlink,
	.follow_link	= proc_ns_follow_link,
	.setattr	= proc_setattr,
};

static int proc_ns_instantiate(struct inode *dir,
	struct dentry *dentry, struct task_struct *task, const void *ptr)
{
	const struct proc_ns_operations *ns_ops = ptr;
	struct inode *inode;
	struct proc_inode *ei;

	inode = proc_pid_make_inode(dir->i_sb, task);
	if (!inode)
		goto out;

	ei = PROC_I(inode);
	inode->i_mode = S_IFLNK|S_IRWXUGO;
	inode->i_op = &proc_ns_link_inode_operations;
	ei->ns.ns_ops = ns_ops;

	d_set_d_op(dentry, &pid_dentry_operations);
	d_add(dentry, inode);
	/* Close the race of the process dying before we return the dentry */
	if (pid_revalidate(dentry, 0))
		return 0;
out:
	return -ENOENT;
}

static int proc_ns_dir_readdir(struct file *file, struct dir_context *ctx)
{
	struct task_struct *task = get_proc_task(file_inode(file));
	const struct proc_ns_operations **entry, **last;

	if (!task)
		return -ENOENT;

	if (!dir_emit_dots(file, ctx))
		goto out;
	if (ctx->pos >= 2 + ARRAY_SIZE(ns_entries))
		goto out;
	entry = ns_entries + (ctx->pos - 2);
	last = &ns_entries[ARRAY_SIZE(ns_entries) - 1];
	while (entry <= last) {
		const struct proc_ns_operations *ops = *entry;
		if (!proc_fill_cache(file, ctx, ops->name, strlen(ops->name),
				     proc_ns_instantiate, task, ops))
			break;
		ctx->pos++;
		entry++;
	}
out:
	put_task_struct(task);
	return 0;
}

const struct file_operations proc_ns_dir_operations = {
	.read		= generic_read_dir,
	.iterate	= proc_ns_dir_readdir,
};

static struct dentry *proc_ns_dir_lookup(struct inode *dir,
				struct dentry *dentry, unsigned int flags)
{
	int error;
	struct task_struct *task = get_proc_task(dir);
	const struct proc_ns_operations **entry, **last;
	unsigned int len = dentry->d_name.len;

	error = -ENOENT;

	if (!task)
		goto out_no_task;

	last = &ns_entries[ARRAY_SIZE(ns_entries)];
	for (entry = ns_entries; entry < last; entry++) {
		if (strlen((*entry)->name) != len)
			continue;
		if (!memcmp(dentry->d_name.name, (*entry)->name, len))
			break;
	}
	if (entry == last)
		goto out;

	error = proc_ns_instantiate(dir, dentry, task, *entry);
out:
	put_task_struct(task);
out_no_task:
	return ERR_PTR(error);
}

const struct inode_operations proc_ns_dir_inode_operations = {
	.lookup		= proc_ns_dir_lookup,
	.getattr	= pid_getattr,
	.setattr	= proc_setattr,
};
