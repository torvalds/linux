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
				      struct iyesde *iyesde,
				      struct delayed_call *done)
{
	struct pid_namespace *ns = proc_pid_ns(iyesde);
	pid_t tgid = task_tgid_nr_ns(current, ns);
	char *name;

	if (!tgid)
		return ERR_PTR(-ENOENT);
	/* max length of unsigned int in decimal + NULL term */
	name = kmalloc(10 + 1, dentry ? GFP_KERNEL : GFP_ATOMIC);
	if (unlikely(!name))
		return dentry ? ERR_PTR(-ENOMEM) : ERR_PTR(-ECHILD);
	sprintf(name, "%u", tgid);
	set_delayed_call(done, kfree_link, name);
	return name;
}

static const struct iyesde_operations proc_self_iyesde_operations = {
	.get_link	= proc_self_get_link,
};

static unsigned self_inum __ro_after_init;

int proc_setup_self(struct super_block *s)
{
	struct iyesde *root_iyesde = d_iyesde(s->s_root);
	struct pid_namespace *ns = proc_pid_ns(root_iyesde);
	struct dentry *self;
	int ret = -ENOMEM;
	
	iyesde_lock(root_iyesde);
	self = d_alloc_name(s->s_root, "self");
	if (self) {
		struct iyesde *iyesde = new_iyesde_pseudo(s);
		if (iyesde) {
			iyesde->i_iyes = self_inum;
			iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
			iyesde->i_mode = S_IFLNK | S_IRWXUGO;
			iyesde->i_uid = GLOBAL_ROOT_UID;
			iyesde->i_gid = GLOBAL_ROOT_GID;
			iyesde->i_op = &proc_self_iyesde_operations;
			d_add(self, iyesde);
			ret = 0;
		} else {
			dput(self);
		}
	}
	iyesde_unlock(root_iyesde);

	if (ret)
		pr_err("proc_fill_super: can't allocate /proc/self\n");
	else
		ns->proc_self = self;

	return ret;
}

void __init proc_self_init(void)
{
	proc_alloc_inum(&self_inum);
}
