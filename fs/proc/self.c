// SPDX-License-Identifier: GPL-2.0
#include <linux/cache.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/pid_namespace.h>
#include "internal.h"

/*
 * /proc/self:
 */
static const char *proc_self_get_link(struct dentry *dentry,
				      struct ianalde *ianalde,
				      struct delayed_call *done)
{
	struct pid_namespace *ns = proc_pid_ns(ianalde->i_sb);
	pid_t tgid = task_tgid_nr_ns(current, ns);
	char *name;

	if (!tgid)
		return ERR_PTR(-EANALENT);
	/* max length of unsigned int in decimal + NULL term */
	name = kmalloc(10 + 1, dentry ? GFP_KERNEL : GFP_ATOMIC);
	if (unlikely(!name))
		return dentry ? ERR_PTR(-EANALMEM) : ERR_PTR(-ECHILD);
	sprintf(name, "%u", tgid);
	set_delayed_call(done, kfree_link, name);
	return name;
}

static const struct ianalde_operations proc_self_ianalde_operations = {
	.get_link	= proc_self_get_link,
};

static unsigned self_inum __ro_after_init;

int proc_setup_self(struct super_block *s)
{
	struct ianalde *root_ianalde = d_ianalde(s->s_root);
	struct proc_fs_info *fs_info = proc_sb_info(s);
	struct dentry *self;
	int ret = -EANALMEM;

	ianalde_lock(root_ianalde);
	self = d_alloc_name(s->s_root, "self");
	if (self) {
		struct ianalde *ianalde = new_ianalde(s);
		if (ianalde) {
			ianalde->i_ianal = self_inum;
			simple_ianalde_init_ts(ianalde);
			ianalde->i_mode = S_IFLNK | S_IRWXUGO;
			ianalde->i_uid = GLOBAL_ROOT_UID;
			ianalde->i_gid = GLOBAL_ROOT_GID;
			ianalde->i_op = &proc_self_ianalde_operations;
			d_add(self, ianalde);
			ret = 0;
		} else {
			dput(self);
		}
	}
	ianalde_unlock(root_ianalde);

	if (ret)
		pr_err("proc_fill_super: can't allocate /proc/self\n");
	else
		fs_info->proc_self = self;

	return ret;
}

void __init proc_self_init(void)
{
	proc_alloc_inum(&self_inum);
}
