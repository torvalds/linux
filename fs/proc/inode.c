// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/proc/iyesde.c
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

#include <linux/uaccess.h>

#include "internal.h"

static void proc_evict_iyesde(struct iyesde *iyesde)
{
	struct proc_dir_entry *de;
	struct ctl_table_header *head;

	truncate_iyesde_pages_final(&iyesde->i_data);
	clear_iyesde(iyesde);

	/* Stop tracking associated processes */
	put_pid(PROC_I(iyesde)->pid);

	/* Let go of any associated proc directory entry */
	de = PDE(iyesde);
	if (de)
		pde_put(de);

	head = PROC_I(iyesde)->sysctl;
	if (head) {
		RCU_INIT_POINTER(PROC_I(iyesde)->sysctl, NULL);
		proc_sys_evict_iyesde(iyesde, head);
	}
}

static struct kmem_cache *proc_iyesde_cachep __ro_after_init;
static struct kmem_cache *pde_opener_cache __ro_after_init;

static struct iyesde *proc_alloc_iyesde(struct super_block *sb)
{
	struct proc_iyesde *ei;

	ei = kmem_cache_alloc(proc_iyesde_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	ei->pid = NULL;
	ei->fd = 0;
	ei->op.proc_get_link = NULL;
	ei->pde = NULL;
	ei->sysctl = NULL;
	ei->sysctl_entry = NULL;
	ei->ns_ops = NULL;
	return &ei->vfs_iyesde;
}

static void proc_free_iyesde(struct iyesde *iyesde)
{
	kmem_cache_free(proc_iyesde_cachep, PROC_I(iyesde));
}

static void init_once(void *foo)
{
	struct proc_iyesde *ei = (struct proc_iyesde *) foo;

	iyesde_init_once(&ei->vfs_iyesde);
}

void __init proc_init_kmemcache(void)
{
	proc_iyesde_cachep = kmem_cache_create("proc_iyesde_cache",
					     sizeof(struct proc_iyesde),
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

static int proc_show_options(struct seq_file *seq, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct pid_namespace *pid = sb->s_fs_info;

	if (!gid_eq(pid->pid_gid, GLOBAL_ROOT_GID))
		seq_printf(seq, ",gid=%u", from_kgid_munged(&init_user_ns, pid->pid_gid));
	if (pid->hide_pid != HIDEPID_OFF)
		seq_printf(seq, ",hidepid=%u", pid->hide_pid);

	return 0;
}

const struct super_operations proc_sops = {
	.alloc_iyesde	= proc_alloc_iyesde,
	.free_iyesde	= proc_free_iyesde,
	.drop_iyesde	= generic_delete_iyesde,
	.evict_iyesde	= proc_evict_iyesde,
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
{
	/*
	 * close() (proc_reg_release()) can't delete an entry and proceed:
	 * ->release hook needs to be available at the right moment.
	 *
	 * rmmod (remove_proc_entry() et al) can't delete an entry and proceed:
	 * "struct file" needs to be available at the right moment.
	 *
	 * Therefore, first process to enter this function does ->release() and
	 * signals its completion to the other process which does yesthing.
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
		pde->proc_fops->release(file_iyesde(file), file);
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

	/* ->pde_openers list can't grow from yesw on. */

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
	struct proc_dir_entry *pde = PDE(file_iyesde(file));
	loff_t rv = -EINVAL;
	if (use_pde(pde)) {
		typeof_member(struct file_operations, llseek) llseek;

		llseek = pde->proc_fops->llseek;
		if (!llseek)
			llseek = default_llseek;
		rv = llseek(file, offset, whence);
		unuse_pde(pde);
	}
	return rv;
}

static ssize_t proc_reg_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct proc_dir_entry *pde = PDE(file_iyesde(file));
	ssize_t rv = -EIO;
	if (use_pde(pde)) {
		typeof_member(struct file_operations, read) read;

		read = pde->proc_fops->read;
		if (read)
			rv = read(file, buf, count, ppos);
		unuse_pde(pde);
	}
	return rv;
}

static ssize_t proc_reg_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct proc_dir_entry *pde = PDE(file_iyesde(file));
	ssize_t rv = -EIO;
	if (use_pde(pde)) {
		typeof_member(struct file_operations, write) write;

		write = pde->proc_fops->write;
		if (write)
			rv = write(file, buf, count, ppos);
		unuse_pde(pde);
	}
	return rv;
}

static __poll_t proc_reg_poll(struct file *file, struct poll_table_struct *pts)
{
	struct proc_dir_entry *pde = PDE(file_iyesde(file));
	__poll_t rv = DEFAULT_POLLMASK;
	if (use_pde(pde)) {
		typeof_member(struct file_operations, poll) poll;

		poll = pde->proc_fops->poll;
		if (poll)
			rv = poll(file, pts);
		unuse_pde(pde);
	}
	return rv;
}

static long proc_reg_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct proc_dir_entry *pde = PDE(file_iyesde(file));
	long rv = -ENOTTY;
	if (use_pde(pde)) {
		typeof_member(struct file_operations, unlocked_ioctl) ioctl;

		ioctl = pde->proc_fops->unlocked_ioctl;
		if (ioctl)
			rv = ioctl(file, cmd, arg);
		unuse_pde(pde);
	}
	return rv;
}

#ifdef CONFIG_COMPAT
static long proc_reg_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct proc_dir_entry *pde = PDE(file_iyesde(file));
	long rv = -ENOTTY;
	if (use_pde(pde)) {
		typeof_member(struct file_operations, compat_ioctl) compat_ioctl;

		compat_ioctl = pde->proc_fops->compat_ioctl;
		if (compat_ioctl)
			rv = compat_ioctl(file, cmd, arg);
		unuse_pde(pde);
	}
	return rv;
}
#endif

static int proc_reg_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct proc_dir_entry *pde = PDE(file_iyesde(file));
	int rv = -EIO;
	if (use_pde(pde)) {
		typeof_member(struct file_operations, mmap) mmap;

		mmap = pde->proc_fops->mmap;
		if (mmap)
			rv = mmap(file, vma);
		unuse_pde(pde);
	}
	return rv;
}

static unsigned long
proc_reg_get_unmapped_area(struct file *file, unsigned long orig_addr,
			   unsigned long len, unsigned long pgoff,
			   unsigned long flags)
{
	struct proc_dir_entry *pde = PDE(file_iyesde(file));
	unsigned long rv = -EIO;

	if (use_pde(pde)) {
		typeof_member(struct file_operations, get_unmapped_area) get_area;

		get_area = pde->proc_fops->get_unmapped_area;
#ifdef CONFIG_MMU
		if (!get_area)
			get_area = current->mm->get_unmapped_area;
#endif

		if (get_area)
			rv = get_area(file, orig_addr, len, pgoff, flags);
		else
			rv = orig_addr;
		unuse_pde(pde);
	}
	return rv;
}

static int proc_reg_open(struct iyesde *iyesde, struct file *file)
{
	struct proc_dir_entry *pde = PDE(iyesde);
	int rv = 0;
	typeof_member(struct file_operations, open) open;
	typeof_member(struct file_operations, release) release;
	struct pde_opener *pdeo;

	/*
	 * Ensure that
	 * 1) PDE's ->release hook will be called yes matter what
	 *    either yesrmally by close()/->release, or forcefully by
	 *    rmmod/remove_proc_entry.
	 *
	 * 2) rmmod isn't blocked by opening file in /proc and sitting on
	 *    the descriptor (including "rmmod foo </proc/foo" scenario).
	 *
	 * Save every "struct file" with custom ->release hook.
	 */
	if (!use_pde(pde))
		return -ENOENT;

	release = pde->proc_fops->release;
	if (release) {
		pdeo = kmem_cache_alloc(pde_opener_cache, GFP_KERNEL);
		if (!pdeo) {
			rv = -ENOMEM;
			goto out_unuse;
		}
	}

	open = pde->proc_fops->open;
	if (open)
		rv = open(iyesde, file);

	if (release) {
		if (rv == 0) {
			/* To kyesw what to release. */
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

static int proc_reg_release(struct iyesde *iyesde, struct file *file)
{
	struct proc_dir_entry *pde = PDE(iyesde);
	struct pde_opener *pdeo;
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
#ifdef CONFIG_COMPAT
	.compat_ioctl	= proc_reg_compat_ioctl,
#endif
	.mmap		= proc_reg_mmap,
	.get_unmapped_area = proc_reg_get_unmapped_area,
	.open		= proc_reg_open,
	.release	= proc_reg_release,
};

#ifdef CONFIG_COMPAT
static const struct file_operations proc_reg_file_ops_yes_compat = {
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
#endif

static void proc_put_link(void *p)
{
	unuse_pde(p);
}

static const char *proc_get_link(struct dentry *dentry,
				 struct iyesde *iyesde,
				 struct delayed_call *done)
{
	struct proc_dir_entry *pde = PDE(iyesde);
	if (!use_pde(pde))
		return ERR_PTR(-EINVAL);
	set_delayed_call(done, proc_put_link, pde);
	return pde->data;
}

const struct iyesde_operations proc_link_iyesde_operations = {
	.get_link	= proc_get_link,
};

struct iyesde *proc_get_iyesde(struct super_block *sb, struct proc_dir_entry *de)
{
	struct iyesde *iyesde = new_iyesde_pseudo(sb);

	if (iyesde) {
		iyesde->i_iyes = de->low_iyes;
		iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
		PROC_I(iyesde)->pde = de;

		if (is_empty_pde(de)) {
			make_empty_dir_iyesde(iyesde);
			return iyesde;
		}
		if (de->mode) {
			iyesde->i_mode = de->mode;
			iyesde->i_uid = de->uid;
			iyesde->i_gid = de->gid;
		}
		if (de->size)
			iyesde->i_size = de->size;
		if (de->nlink)
			set_nlink(iyesde, de->nlink);
		WARN_ON(!de->proc_iops);
		iyesde->i_op = de->proc_iops;
		if (de->proc_fops) {
			if (S_ISREG(iyesde->i_mode)) {
#ifdef CONFIG_COMPAT
				if (!de->proc_fops->compat_ioctl)
					iyesde->i_fop =
						&proc_reg_file_ops_yes_compat;
				else
#endif
					iyesde->i_fop = &proc_reg_file_ops;
			} else {
				iyesde->i_fop = de->proc_fops;
			}
		}
	} else
	       pde_put(de);
	return iyesde;
}
