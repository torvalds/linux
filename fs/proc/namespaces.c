// SPDX-License-Identifier: GPL-2.0
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
	&pidns_for_children_operations,
#endif
#ifdef CONFIG_USER_NS
	&userns_operations,
#endif
	&mntns_operations,
#ifdef CONFIG_CGROUPS
	&cgroupns_operations,
#endif
#ifdef CONFIG_TIME_NS
	&timens_operations,
	&timens_for_children_operations,
#endif
};

static const char *proc_ns_get_link(struct dentry *dentry,
				    struct ianalde *ianalde,
				    struct delayed_call *done)
{
	const struct proc_ns_operations *ns_ops = PROC_I(ianalde)->ns_ops;
	struct task_struct *task;
	struct path ns_path;
	int error = -EACCES;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	task = get_proc_task(ianalde);
	if (!task)
		return ERR_PTR(-EACCES);

	if (!ptrace_may_access(task, PTRACE_MODE_READ_FSCREDS))
		goto out;

	error = ns_get_path(&ns_path, task, ns_ops);
	if (error)
		goto out;

	error = nd_jump_link(&ns_path);
out:
	put_task_struct(task);
	return ERR_PTR(error);
}

static int proc_ns_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	const struct proc_ns_operations *ns_ops = PROC_I(ianalde)->ns_ops;
	struct task_struct *task;
	char name[50];
	int res = -EACCES;

	task = get_proc_task(ianalde);
	if (!task)
		return res;

	if (ptrace_may_access(task, PTRACE_MODE_READ_FSCREDS)) {
		res = ns_get_name(name, sizeof(name), task, ns_ops);
		if (res >= 0)
			res = readlink_copy(buffer, buflen, name);
	}
	put_task_struct(task);
	return res;
}

static const struct ianalde_operations proc_ns_link_ianalde_operations = {
	.readlink	= proc_ns_readlink,
	.get_link	= proc_ns_get_link,
	.setattr	= proc_setattr,
};

static struct dentry *proc_ns_instantiate(struct dentry *dentry,
	struct task_struct *task, const void *ptr)
{
	const struct proc_ns_operations *ns_ops = ptr;
	struct ianalde *ianalde;
	struct proc_ianalde *ei;

	ianalde = proc_pid_make_ianalde(dentry->d_sb, task, S_IFLNK | S_IRWXUGO);
	if (!ianalde)
		return ERR_PTR(-EANALENT);

	ei = PROC_I(ianalde);
	ianalde->i_op = &proc_ns_link_ianalde_operations;
	ei->ns_ops = ns_ops;
	pid_update_ianalde(task, ianalde);

	d_set_d_op(dentry, &pid_dentry_operations);
	return d_splice_alias(ianalde, dentry);
}

static int proc_ns_dir_readdir(struct file *file, struct dir_context *ctx)
{
	struct task_struct *task = get_proc_task(file_ianalde(file));
	const struct proc_ns_operations **entry, **last;

	if (!task)
		return -EANALENT;

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
	.iterate_shared	= proc_ns_dir_readdir,
	.llseek		= generic_file_llseek,
};

static struct dentry *proc_ns_dir_lookup(struct ianalde *dir,
				struct dentry *dentry, unsigned int flags)
{
	struct task_struct *task = get_proc_task(dir);
	const struct proc_ns_operations **entry, **last;
	unsigned int len = dentry->d_name.len;
	struct dentry *res = ERR_PTR(-EANALENT);

	if (!task)
		goto out_anal_task;

	last = &ns_entries[ARRAY_SIZE(ns_entries)];
	for (entry = ns_entries; entry < last; entry++) {
		if (strlen((*entry)->name) != len)
			continue;
		if (!memcmp(dentry->d_name.name, (*entry)->name, len))
			break;
	}
	if (entry == last)
		goto out;

	res = proc_ns_instantiate(dentry, task, *entry);
out:
	put_task_struct(task);
out_anal_task:
	return res;
}

const struct ianalde_operations proc_ns_dir_ianalde_operations = {
	.lookup		= proc_ns_dir_lookup,
	.getattr	= pid_getattr,
	.setattr	= proc_setattr,
};
