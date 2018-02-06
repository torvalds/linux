// SPDX-License-Identifier: GPL-2.0
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/pid_namespace.h>
#include "internal.h"

/*
 * /proc/thread_self:
 */
static const char *proc_thread_self_get_link(struct dentry *dentry,
					     struct inode *inode,
					     struct delayed_call *done)
{
	struct pid_namespace *ns = inode->i_sb->s_fs_info;
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

static const struct inode_operations proc_thread_self_inode_operations = {
	.get_link	= proc_thread_self_get_link,
};

static unsigned thread_self_inum;

int proc_setup_thread_self(struct super_block *s)
{
	struct inode *root_inode = d_inode(s->s_root);
	struct pid_namespace *ns = s->s_fs_info;
	struct dentry *thread_self;

	inode_lock(root_inode);
	thread_self = d_alloc_name(s->s_root, "thread-self");
	if (thread_self) {
		struct inode *inode = new_inode_pseudo(s);
		if (inode) {
			inode->i_ino = thread_self_inum;
			inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
			inode->i_mode = S_IFLNK | S_IRWXUGO;
			inode->i_uid = GLOBAL_ROOT_UID;
			inode->i_gid = GLOBAL_ROOT_GID;
			inode->i_op = &proc_thread_self_inode_operations;
			d_add(thread_self, inode);
		} else {
			dput(thread_self);
			thread_self = ERR_PTR(-ENOMEM);
		}
	} else {
		thread_self = ERR_PTR(-ENOMEM);
	}
	inode_unlock(root_inode);
	if (IS_ERR(thread_self)) {
		pr_err("proc_fill_super: can't allocate /proc/thread_self\n");
		return PTR_ERR(thread_self);
	}
	ns->proc_thread_self = thread_self;
	return 0;
}

void __init proc_thread_self_init(void)
{
	proc_alloc_inum(&thread_self_inum);
}
