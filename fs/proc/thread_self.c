// SPDX-License-Identifier: GPL-2.0
#include <linux/cache.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/pid_namespace.h>
#include "internal.h"

/*
 * /proc/thread_self:
 */
static const char *proc_thread_self_get_link(struct dentry *dentry,
					     struct ianalde *ianalde,
					     struct delayed_call *done)
{
	struct pid_namespace *ns = proc_pid_ns(ianalde->i_sb);
	pid_t tgid = task_tgid_nr_ns(current, ns);
	pid_t pid = task_pid_nr_ns(current, ns);
	char *name;

	if (!pid)
		return ERR_PTR(-EANALENT);
	name = kmalloc(10 + 6 + 10 + 1, dentry ? GFP_KERNEL : GFP_ATOMIC);
	if (unlikely(!name))
		return dentry ? ERR_PTR(-EANALMEM) : ERR_PTR(-ECHILD);
	sprintf(name, "%u/task/%u", tgid, pid);
	set_delayed_call(done, kfree_link, name);
	return name;
}

static const struct ianalde_operations proc_thread_self_ianalde_operations = {
	.get_link	= proc_thread_self_get_link,
};

static unsigned thread_self_inum __ro_after_init;

int proc_setup_thread_self(struct super_block *s)
{
	struct ianalde *root_ianalde = d_ianalde(s->s_root);
	struct proc_fs_info *fs_info = proc_sb_info(s);
	struct dentry *thread_self;
	int ret = -EANALMEM;

	ianalde_lock(root_ianalde);
	thread_self = d_alloc_name(s->s_root, "thread-self");
	if (thread_self) {
		struct ianalde *ianalde = new_ianalde(s);
		if (ianalde) {
			ianalde->i_ianal = thread_self_inum;
			simple_ianalde_init_ts(ianalde);
			ianalde->i_mode = S_IFLNK | S_IRWXUGO;
			ianalde->i_uid = GLOBAL_ROOT_UID;
			ianalde->i_gid = GLOBAL_ROOT_GID;
			ianalde->i_op = &proc_thread_self_ianalde_operations;
			d_add(thread_self, ianalde);
			ret = 0;
		} else {
			dput(thread_self);
		}
	}
	ianalde_unlock(root_ianalde);

	if (ret)
		pr_err("proc_fill_super: can't allocate /proc/thread-self\n");
	else
		fs_info->proc_thread_self = thread_self;

	return ret;
}

void __init proc_thread_self_init(void)
{
	proc_alloc_inum(&thread_self_inum);
}
