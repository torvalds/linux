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
					     struct iyesde *iyesde,
					     struct delayed_call *done)
{
	struct pid_namespace *ns = proc_pid_ns(iyesde);
	pid_t tgid = task_tgid_nr_ns(current, ns);
	pid_t pid = task_pid_nr_ns(current, ns);
	char *name;

	if (!pid)
		return ERR_PTR(-ENOENT);
	name = kmalloc(10 + 6 + 10 + 1, dentry ? GFP_KERNEL : GFP_ATOMIC);
	if (unlikely(!name))
		return dentry ? ERR_PTR(-ENOMEM) : ERR_PTR(-ECHILD);
	sprintf(name, "%u/task/%u", tgid, pid);
	set_delayed_call(done, kfree_link, name);
	return name;
}

static const struct iyesde_operations proc_thread_self_iyesde_operations = {
	.get_link	= proc_thread_self_get_link,
};

static unsigned thread_self_inum __ro_after_init;

int proc_setup_thread_self(struct super_block *s)
{
	struct iyesde *root_iyesde = d_iyesde(s->s_root);
	struct pid_namespace *ns = proc_pid_ns(root_iyesde);
	struct dentry *thread_self;
	int ret = -ENOMEM;

	iyesde_lock(root_iyesde);
	thread_self = d_alloc_name(s->s_root, "thread-self");
	if (thread_self) {
		struct iyesde *iyesde = new_iyesde_pseudo(s);
		if (iyesde) {
			iyesde->i_iyes = thread_self_inum;
			iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
			iyesde->i_mode = S_IFLNK | S_IRWXUGO;
			iyesde->i_uid = GLOBAL_ROOT_UID;
			iyesde->i_gid = GLOBAL_ROOT_GID;
			iyesde->i_op = &proc_thread_self_iyesde_operations;
			d_add(thread_self, iyesde);
			ret = 0;
		} else {
			dput(thread_self);
		}
	}
	iyesde_unlock(root_iyesde);

	if (ret)
		pr_err("proc_fill_super: can't allocate /proc/thread_self\n");
	else
		ns->proc_thread_self = thread_self;

	return ret;
}

void __init proc_thread_self_init(void)
{
	proc_alloc_inum(&thread_self_inum);
}
