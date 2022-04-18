// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/proc/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/cache.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/pid_namespace.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/completion.h>
#include <linux/poll.h>
#include <linux/printk.h>
#include <linux/file.h>
#include <linux/limits.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/bug.h>

#include <linux/uaccess.h>

#include "internal.h"

static void proc_evict_inode(struct inode *inode)
{
	struct proc_dir_entry *de;
	struct ctl_table_header *head;
	struct proc_inode *ei = PROC_I(inode);

	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);

	/* Stop tracking associated processes */
	if (ei->pid) {
		proc_pid_evict_inode(ei);
		ei->pid = NULL;
	}

	/* Let go of any associated proc directory entry */
	de = ei->pde;
	if (de) {
		pde_put(de);
		ei->pde = NULL;
	}

	head = ei->sysctl;
	if (head) {
		RCU_INIT_POINTER(ei->sysctl, NULL);
		proc_sys_evict_inode(inode, head);
	}
}

static struct kmem_cache *proc_inode_cachep __ro_after_init;
static struct kmem_cache *pde_opener_cache __ro_after_init;

static struct inode *proc_alloc_inode(struct super_block *sb)
{
	struct proc_inode *ei;

	ei = alloc_inode_sb(sb, proc_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	ei->pid = NULL;
	ei->fd = 0;
	ei->op.proc_get_link = NULL;
	ei->pde = NULL;
	ei->sysctl = NULL;
	ei->sysctl_entry = NULL;
	INIT_HLIST_NODE(&ei->sibling_inodes);
	ei->ns_ops = NULL;
	return &ei->vfs_inode;
}

static void proc_free_inode(struct inode *inode)
{
	kmem_cache_free(proc_inode_cachep, PROC_I(inode));
}

static void init_once(void *foo)
{
	struct proc_inode *ei = (struct proc_inode *) foo;

	inode_init_once(&ei->vfs_inode);
}

void __init proc_init_kmemcache(void)
{
	proc_inode_cachep = kmem_cache_create("proc_inode_cache",
					     sizeof(struct proc_inode),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT|
						SLAB_PANIC),
					     init_once);
	pde_opener_cache =
		kmem_cache_create("pde_opener", sizeof(struct pde_opener), 0,
				  SLAB_ACCOUNT|SLAB_PANIC, NULL);
	proc_dir_entry_cache = kmem_cache_create_usercopy(
		"proc_dir_entry", SIZEOF_PDE, 0, SLAB_PANIC,
		offsetof(struct proc_dir_entry, inline_name),
		SIZEOF_PDE_INLINE_NAME, NULL);
	BUILD_BUG_ON(sizeof(struct proc_dir_entry) >= SIZEOF_PDE);
}

void proc_invalidate_siblings_dcache(struct hlist_head *inodes, spinlock_t *lock)
{
	struct inode *inode;
	struct proc_inode *ei;
	struct hlist_node *node;
	struct super_block *old_sb = NULL;

	rcu_read_lock();
	for (;;) {
		struct super_block *sb;
		node = hlist_first_rcu(inodes);
		if (!node)
			break;
		ei = hlist_entry(node, struct proc_inode, sibling_inodes);
		spin_lock(lock);
		hlist_del_init_rcu(&ei->sibling_inodes);
		spin_unlock(lock);

		inode = &ei->vfs_inode;
		sb = inode->i_sb;
		if ((sb != old_sb) && !atomic_inc_not_zero(&sb->s_active))
			continue;
		inode = igrab(inode);
		rcu_read_unlock();
		if (sb != old_sb) {
			if (old_sb)
				deactivate_super(old_sb);
			old_sb = sb;
		}
		if (unlikely(!inode)) {
			rcu_read_lock();
			continue;
		}

		if (S_ISDIR(inode->i_mode)) {
			struct dentry *dir = d_find_any_alias(inode);
			if (dir) {
				d_invalidate(dir);
				dput(dir);
			}
		} else {
			struct dentry *dentry;
			while ((dentry = d_find_alias(inode))) {
				d_invalidate(dentry);
				dput(dentry);
			}
		}
		iput(inode);

		rcu_read_lock();
	}
	rcu_read_unlock();
	if (old_sb)
		deactivate_super(old_sb);
}

static inline const char *hidepid2str(enum proc_hidepid v)
{
	switch (v) {
		case HIDEPID_OFF: return "off";
		case HIDEPID_NO_ACCESS: return "noaccess";
		case HIDEPID_INVISIBLE: return "invisible";
		case HIDEPID_NOT_PTRACEABLE: return "ptraceable";
	}
	WARN_ONCE(1, "bad hide_pid value: %d\n", v);
	return "unknown";
}

static int proc_show_options(struct seq_file *seq, struct dentry *root)
{
	struct proc_fs_info *fs_info = proc_sb_info(root->d_sb);

	if (!gid_eq(fs_info->pid_gid, GLOBAL_ROOT_GID))
		seq_printf(seq, ",gid=%u", from_kgid_munged(&init_user_ns, fs_info->pid_gid));
	if (fs_info->hide_pid != HIDEPID_OFF)
		seq_printf(seq, ",hidepid=%s", hidepid2str(fs_info->hide_pid));
	if (fs_info->pidonly != PROC_PIDONLY_OFF)
		seq_printf(seq, ",subset=pid");

	return 0;
}

const struct super_operations proc_sops = {
	.alloc_inode	= proc_alloc_inode,
	.free_inode	= proc_free_inode,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= proc_evict_inode,
	.statfs		= simple_statfs,
	.show_options	= proc_show_options,
};

enum {BIAS = -1U<<31};

static inline int use_pde(struct proc_dir_entry *pde)
{
	return likely(atomic_inc_unless_negative(&pde->in_use));
}

static void unuse_pde(struct proc_dir_entry *pde)
{
	if (unlikely(atomic_dec_return(&pde->in_use) == BIAS))
		complete(pde->pde_unload_completion);
}

/* pde is locked on entry, unlocked on exit */
static void close_pdeo(struct proc_dir_entry *pde, struct pde_opener *pdeo)
	__releases(&pde->pde_unload_lock)
{
	/*
	 * close() (proc_reg_release()) can't delete an entry and proceed:
	 * ->release hook needs to be available at the right moment.
	 *
	 * rmmod (remove_proc_entry() et al) can't delete an entry and proceed:
	 * "struct file" needs to be available at the right moment.
	 *
	 * Therefore, first process to enter this function does ->release() and
	 * signals its completion to the other process which does nothing.
	 */
	if (pdeo->closing) {
		/* somebody else is doing that, just wait */
		DECLARE_COMPLETION_ONSTACK(c);
		pdeo->c = &c;
		spin_unlock(&pde->pde_unload_lock);
		wait_for_completion(&c);
	} else {
		struct file *file;
		struct completion *c;

		pdeo->closing = true;
		spin_unlock(&pde->pde_unload_lock);
		file = pdeo->file;
		pde->proc_ops->proc_release(file_inode(file), file);
		spin_lock(&pde->pde_unload_lock);
		/* After ->release. */
		list_del(&pdeo->lh);
		c = pdeo->c;
		spin_unlock(&pde->pde_unload_lock);
		if (unlikely(c))
			complete(c);
		kmem_cache_free(pde_opener_cache, pdeo);
	}
}

void proc_entry_rundown(struct proc_dir_entry *de)
{
	DECLARE_COMPLETION_ONSTACK(c);
	/* Wait until all existing callers into module are done. */
	de->pde_unload_completion = &c;
	if (atomic_add_return(BIAS, &de->in_use) != BIAS)
		wait_for_completion(&c);

	/* ->pde_openers list can't grow from now on. */

	spin_lock(&de->pde_unload_lock);
	while (!list_empty(&de->pde_openers)) {
		struct pde_opener *pdeo;
		pdeo = list_first_entry(&de->pde_openers, struct pde_opener, lh);
		close_pdeo(de, pdeo);
		spin_lock(&de->pde_unload_lock);
	}
	spin_unlock(&de->pde_unload_lock);
}

static loff_t proc_reg_llseek(struct file *file, loff_t offset, int whence)
{
	struct proc_dir_entry *pde = PDE(file_inode(file));
	loff_t rv = -EINVAL;

	if (pde_is_permanent(pde)) {
		return pde->proc_ops->proc_lseek(file, offset, whence);
	} else if (use_pde(pde)) {
		rv = pde->proc_ops->proc_lseek(file, offset, whence);
		unuse_pde(pde);
	}
	return rv;
}

static ssize_t proc_reg_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct proc_dir_entry *pde = PDE(file_inode(iocb->ki_filp));
	ssize_t ret;

	if (pde_is_permanent(pde))
		return pde->proc_ops->proc_read_iter(iocb, iter);

	if (!use_pde(pde))
		return -EIO;
	ret = pde->proc_ops->proc_read_iter(iocb, iter);
	unuse_pde(pde);
	return ret;
}

static ssize_t pde_read(struct proc_dir_entry *pde, struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	typeof_member(struct proc_ops, proc_read) read;

	read = pde->proc_ops->proc_read;
	if (read)
		return read(file, buf, count, ppos);
	return -EIO;
}

static ssize_t proc_reg_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct proc_dir_entry *pde = PDE(file_inode(file));
	ssize_t rv = -EIO;

	if (pde_is_permanent(pde)) {
		return pde_read(pde, file, buf, count, ppos);
	} else if (use_pde(pde)) {
		rv = pde_read(pde, file, buf, count, ppos);
		unuse_pde(pde);
	}
	return rv;
}

static ssize_t pde_write(struct proc_dir_entry *pde, struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	typeof_member(struct proc_ops, proc_write) write;

	write = pde->proc_ops->proc_write;
	if (write)
		return write(file, buf, count, ppos);
	return -EIO;
}

static ssize_t proc_reg_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct proc_dir_entry *pde = PDE(file_inode(file));
	ssize_t rv = -EIO;

	if (pde_is_permanent(pde)) {
		return pde_write(pde, file, buf, count, ppos);
	} else if (use_pde(pde)) {
		rv = pde_write(pde, file, buf, count, ppos);
		unuse_pde(pde);
	}
	return rv;
}

static __poll_t pde_poll(struct proc_dir_entry *pde, struct file *file, struct poll_table_struct *pts)
{
	typeof_member(struct proc_ops, proc_poll) poll;

	poll = pde->proc_ops->proc_poll;
	if (poll)
		return poll(file, pts);
	return DEFAULT_POLLMASK;
}

static __poll_t proc_reg_poll(struct file *file, struct poll_table_struct *pts)
{
	struct proc_dir_entry *pde = PDE(file_inode(file));
	__poll_t rv = DEFAULT_POLLMASK;

	if (pde_is_permanent(pde)) {
		return pde_poll(pde, file, pts);
	} else if (use_pde(pde)) {
		rv = pde_poll(pde, file, pts);
		unuse_pde(pde);
	}
	return rv;
}

static long pde_ioctl(struct proc_dir_entry *pde, struct file *file, unsigned int cmd, unsigned long arg)
{
	typeof_member(struct proc_ops, proc_ioctl) ioctl;

	ioctl = pde->proc_ops->proc_ioctl;
	if (ioctl)
		return ioctl(file, cmd, arg);
	return -ENOTTY;
}

static long proc_reg_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct proc_dir_entry *pde = PDE(file_inode(file));
	long rv = -ENOTTY;

	if (pde_is_permanent(pde)) {
		return pde_ioctl(pde, file, cmd, arg);
	} else if (use_pde(pde)) {
		rv = pde_ioctl(pde, file, cmd, arg);
		unuse_pde(pde);
	}
	return rv;
}

#ifdef CONFIG_COMPAT
static long pde_compat_ioctl(struct proc_dir_entry *pde, struct file *file, unsigned int cmd, unsigned long arg)
{
	typeof_member(struct proc_ops, proc_compat_ioctl) compat_ioctl;

	compat_ioctl = pde->proc_ops->proc_compat_ioctl;
	if (compat_ioctl)
		return compat_ioctl(file, cmd, arg);
	return -ENOTTY;
}

static long proc_reg_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct proc_dir_entry *pde = PDE(file_inode(file));
	long rv = -ENOTTY;
	if (pde_is_permanent(pde)) {
		return pde_compat_ioctl(pde, file, cmd, arg);
	} else if (use_pde(pde)) {
		rv = pde_compat_ioctl(pde, file, cmd, arg);
		unuse_pde(pde);
	}
	return rv;
}
#endif

static int pde_mmap(struct proc_dir_entry *pde, struct file *file, struct vm_area_struct *vma)
{
	typeof_member(struct proc_ops, proc_mmap) mmap;

	mmap = pde->proc_ops->proc_mmap;
	if (mmap)
		return mmap(file, vma);
	return -EIO;
}

static int proc_reg_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct proc_dir_entry *pde = PDE(file_inode(file));
	int rv = -EIO;

	if (pde_is_permanent(pde)) {
		return pde_mmap(pde, file, vma);
	} else if (use_pde(pde)) {
		rv = pde_mmap(pde, file, vma);
		unuse_pde(pde);
	}
	return rv;
}

static unsigned long
pde_get_unmapped_area(struct proc_dir_entry *pde, struct file *file, unsigned long orig_addr,
			   unsigned long len, unsigned long pgoff,
			   unsigned long flags)
{
	typeof_member(struct proc_ops, proc_get_unmapped_area) get_area;

	get_area = pde->proc_ops->proc_get_unmapped_area;
#ifdef CONFIG_MMU
	if (!get_area)
		get_area = current->mm->get_unmapped_area;
#endif
	if (get_area)
		return get_area(file, orig_addr, len, pgoff, flags);
	return orig_addr;
}

static unsigned long
proc_reg_get_unmapped_area(struct file *file, unsigned long orig_addr,
			   unsigned long len, unsigned long pgoff,
			   unsigned long flags)
{
	struct proc_dir_entry *pde = PDE(file_inode(file));
	unsigned long rv = -EIO;

	if (pde_is_permanent(pde)) {
		return pde_get_unmapped_area(pde, file, orig_addr, len, pgoff, flags);
	} else if (use_pde(pde)) {
		rv = pde_get_unmapped_area(pde, file, orig_addr, len, pgoff, flags);
		unuse_pde(pde);
	}
	return rv;
}

static int proc_reg_open(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *pde = PDE(inode);
	int rv = 0;
	typeof_member(struct proc_ops, proc_open) open;
	typeof_member(struct proc_ops, proc_release) release;
	struct pde_opener *pdeo;

	if (pde_is_permanent(pde)) {
		open = pde->proc_ops->proc_open;
		if (open)
			rv = open(inode, file);
		return rv;
	}

	/*
	 * Ensure that
	 * 1) PDE's ->release hook will be called no matter what
	 *    either normally by close()/->release, or forcefully by
	 *    rmmod/remove_proc_entry.
	 *
	 * 2) rmmod isn't blocked by opening file in /proc and sitting on
	 *    the descriptor (including "rmmod foo </proc/foo" scenario).
	 *
	 * Save every "struct file" with custom ->release hook.
	 */
	if (!use_pde(pde))
		return -ENOENT;

	release = pde->proc_ops->proc_release;
	if (release) {
		pdeo = kmem_cache_alloc(pde_opener_cache, GFP_KERNEL);
		if (!pdeo) {
			rv = -ENOMEM;
			goto out_unuse;
		}
	}

	open = pde->proc_ops->proc_open;
	if (open)
		rv = open(inode, file);

	if (release) {
		if (rv == 0) {
			/* To know what to release. */
			pdeo->file = file;
			pdeo->closing = false;
			pdeo->c = NULL;
			spin_lock(&pde->pde_unload_lock);
			list_add(&pdeo->lh, &pde->pde_openers);
			spin_unlock(&pde->pde_unload_lock);
		} else
			kmem_cache_free(pde_opener_cache, pdeo);
	}

out_unuse:
	unuse_pde(pde);
	return rv;
}

static int proc_reg_release(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *pde = PDE(inode);
	struct pde_opener *pdeo;

	if (pde_is_permanent(pde)) {
		typeof_member(struct proc_ops, proc_release) release;

		release = pde->proc_ops->proc_release;
		if (release) {
			return release(inode, file);
		}
		return 0;
	}

	spin_lock(&pde->pde_unload_lock);
	list_for_each_entry(pdeo, &pde->pde_openers, lh) {
		if (pdeo->file == file) {
			close_pdeo(pde, pdeo);
			return 0;
		}
	}
	spin_unlock(&pde->pde_unload_lock);
	return 0;
}

static const struct file_operations proc_reg_file_ops = {
	.llseek		= proc_reg_llseek,
	.read		= proc_reg_read,
	.write		= proc_reg_write,
	.poll		= proc_reg_poll,
	.unlocked_ioctl	= proc_reg_unlocked_ioctl,
	.mmap		= proc_reg_mmap,
	.get_unmapped_area = proc_reg_get_unmapped_area,
	.open		= proc_reg_open,
	.release	= proc_reg_release,
};

static const struct file_operations proc_iter_file_ops = {
	.llseek		= proc_reg_llseek,
	.read_iter	= proc_reg_read_iter,
	.write		= proc_reg_write,
	.splice_read	= generic_file_splice_read,
	.poll		= proc_reg_poll,
	.unlocked_ioctl	= proc_reg_unlocked_ioctl,
	.mmap		= proc_reg_mmap,
	.get_unmapped_area = proc_reg_get_unmapped_area,
	.open		= proc_reg_open,
	.release	= proc_reg_release,
};

#ifdef CONFIG_COMPAT
static const struct file_operations proc_reg_file_ops_compat = {
	.llseek		= proc_reg_llseek,
	.read		= proc_reg_read,
	.write		= proc_reg_write,
	.poll		= proc_reg_poll,
	.unlocked_ioctl	= proc_reg_unlocked_ioctl,
	.compat_ioctl	= proc_reg_compat_ioctl,
	.mmap		= proc_reg_mmap,
	.get_unmapped_area = proc_reg_get_unmapped_area,
	.open		= proc_reg_open,
	.release	= proc_reg_release,
};

static const struct file_operations proc_iter_file_ops_compat = {
	.llseek		= proc_reg_llseek,
	.read_iter	= proc_reg_read_iter,
	.splice_read	= generic_file_splice_read,
	.write		= proc_reg_write,
	.poll		= proc_reg_poll,
	.unlocked_ioctl	= proc_reg_unlocked_ioctl,
	.compat_ioctl	= proc_reg_compat_ioctl,
	.mmap		= proc_reg_mmap,
	.get_unmapped_area = proc_reg_get_unmapped_area,
	.open		= proc_reg_open,
	.release	= proc_reg_release,
};
#endif

static void proc_put_link(void *p)
{
	unuse_pde(p);
}

static const char *proc_get_link(struct dentry *dentry,
				 struct inode *inode,
				 struct delayed_call *done)
{
	struct proc_dir_entry *pde = PDE(inode);
	if (!use_pde(pde))
		return ERR_PTR(-EINVAL);
	set_delayed_call(done, proc_put_link, pde);
	return pde->data;
}

const struct inode_operations proc_link_inode_operations = {
	.get_link	= proc_get_link,
};

struct inode *proc_get_inode(struct super_block *sb, struct proc_dir_entry *de)
{
	struct inode *inode = new_inode(sb);

	if (!inode) {
		pde_put(de);
		return NULL;
	}

	inode->i_private = de->data;
	inode->i_ino = de->low_ino;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	PROC_I(inode)->pde = de;
	if (is_empty_pde(de)) {
		make_empty_dir_inode(inode);
		return inode;
	}

	if (de->mode) {
		inode->i_mode = de->mode;
		inode->i_uid = de->uid;
		inode->i_gid = de->gid;
	}
	if (de->size)
		inode->i_size = de->size;
	if (de->nlink)
		set_nlink(inode, de->nlink);

	if (S_ISREG(inode->i_mode)) {
		inode->i_op = de->proc_iops;
		if (de->proc_ops->proc_read_iter)
			inode->i_fop = &proc_iter_file_ops;
		else
			inode->i_fop = &proc_reg_file_ops;
#ifdef CONFIG_COMPAT
		if (de->proc_ops->proc_compat_ioctl) {
			if (de->proc_ops->proc_read_iter)
				inode->i_fop = &proc_iter_file_ops_compat;
			else
				inode->i_fop = &proc_reg_file_ops_compat;
		}
#endif
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = de->proc_iops;
		inode->i_fop = de->proc_dir_ops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = de->proc_iops;
		inode->i_fop = NULL;
	} else {
		BUG();
	}
	return inode;
}
