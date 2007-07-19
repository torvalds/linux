/*
 *  linux/fs/proc/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/completion.h>
#include <linux/file.h>
#include <linux/limits.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include "internal.h"

struct proc_dir_entry *de_get(struct proc_dir_entry *de)
{
	if (de)
		atomic_inc(&de->count);
	return de;
}

/*
 * Decrements the use count and checks for deferred deletion.
 */
void de_put(struct proc_dir_entry *de)
{
	if (de) {	
		lock_kernel();		
		if (!atomic_read(&de->count)) {
			printk("de_put: entry %s already free!\n", de->name);
			unlock_kernel();
			return;
		}

		if (atomic_dec_and_test(&de->count)) {
			if (de->deleted) {
				printk("de_put: deferred delete of %s\n",
					de->name);
				free_proc_entry(de);
			}
		}		
		unlock_kernel();
	}
}

/*
 * Decrement the use count of the proc_dir_entry.
 */
static void proc_delete_inode(struct inode *inode)
{
	struct proc_dir_entry *de;

	truncate_inode_pages(&inode->i_data, 0);

	/* Stop tracking associated processes */
	put_pid(PROC_I(inode)->pid);

	/* Let go of any associated proc directory entry */
	de = PROC_I(inode)->pde;
	if (de) {
		if (de->owner)
			module_put(de->owner);
		de_put(de);
	}
	clear_inode(inode);
}

struct vfsmount *proc_mnt;

static void proc_read_inode(struct inode * inode)
{
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
}

static struct kmem_cache * proc_inode_cachep;

static struct inode *proc_alloc_inode(struct super_block *sb)
{
	struct proc_inode *ei;
	struct inode *inode;

	ei = (struct proc_inode *)kmem_cache_alloc(proc_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	ei->pid = NULL;
	ei->fd = 0;
	ei->op.proc_get_link = NULL;
	ei->pde = NULL;
	inode = &ei->vfs_inode;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	return inode;
}

static void proc_destroy_inode(struct inode *inode)
{
	kmem_cache_free(proc_inode_cachep, PROC_I(inode));
}

static void init_once(void * foo, struct kmem_cache * cachep, unsigned long flags)
{
	struct proc_inode *ei = (struct proc_inode *) foo;

	inode_init_once(&ei->vfs_inode);
}
 
int __init proc_init_inodecache(void)
{
	proc_inode_cachep = kmem_cache_create("proc_inode_cache",
					     sizeof(struct proc_inode),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once, NULL);
	if (proc_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static int proc_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= MS_NODIRATIME;
	return 0;
}

static const struct super_operations proc_sops = {
	.alloc_inode	= proc_alloc_inode,
	.destroy_inode	= proc_destroy_inode,
	.read_inode	= proc_read_inode,
	.drop_inode	= generic_delete_inode,
	.delete_inode	= proc_delete_inode,
	.statfs		= simple_statfs,
	.remount_fs	= proc_remount,
};

static void pde_users_dec(struct proc_dir_entry *pde)
{
	spin_lock(&pde->pde_unload_lock);
	pde->pde_users--;
	if (pde->pde_unload_completion && pde->pde_users == 0)
		complete(pde->pde_unload_completion);
	spin_unlock(&pde->pde_unload_lock);
}

static loff_t proc_reg_llseek(struct file *file, loff_t offset, int whence)
{
	struct proc_dir_entry *pde = PDE(file->f_path.dentry->d_inode);
	loff_t rv = -EINVAL;
	loff_t (*llseek)(struct file *, loff_t, int);

	spin_lock(&pde->pde_unload_lock);
	/*
	 * remove_proc_entry() is going to delete PDE (as part of module
	 * cleanup sequence). No new callers into module allowed.
	 */
	if (!pde->proc_fops) {
		spin_unlock(&pde->pde_unload_lock);
		return rv;
	}
	/*
	 * Bump refcount so that remove_proc_entry will wail for ->llseek to
	 * complete.
	 */
	pde->pde_users++;
	/*
	 * Save function pointer under lock, to protect against ->proc_fops
	 * NULL'ifying right after ->pde_unload_lock is dropped.
	 */
	llseek = pde->proc_fops->llseek;
	spin_unlock(&pde->pde_unload_lock);

	if (!llseek)
		llseek = default_llseek;
	rv = llseek(file, offset, whence);

	pde_users_dec(pde);
	return rv;
}

static ssize_t proc_reg_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct proc_dir_entry *pde = PDE(file->f_path.dentry->d_inode);
	ssize_t rv = -EIO;
	ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);

	spin_lock(&pde->pde_unload_lock);
	if (!pde->proc_fops) {
		spin_unlock(&pde->pde_unload_lock);
		return rv;
	}
	pde->pde_users++;
	read = pde->proc_fops->read;
	spin_unlock(&pde->pde_unload_lock);

	if (read)
		rv = read(file, buf, count, ppos);

	pde_users_dec(pde);
	return rv;
}

static ssize_t proc_reg_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct proc_dir_entry *pde = PDE(file->f_path.dentry->d_inode);
	ssize_t rv = -EIO;
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);

	spin_lock(&pde->pde_unload_lock);
	if (!pde->proc_fops) {
		spin_unlock(&pde->pde_unload_lock);
		return rv;
	}
	pde->pde_users++;
	write = pde->proc_fops->write;
	spin_unlock(&pde->pde_unload_lock);

	if (write)
		rv = write(file, buf, count, ppos);

	pde_users_dec(pde);
	return rv;
}

static unsigned int proc_reg_poll(struct file *file, struct poll_table_struct *pts)
{
	struct proc_dir_entry *pde = PDE(file->f_path.dentry->d_inode);
	unsigned int rv = 0;
	unsigned int (*poll)(struct file *, struct poll_table_struct *);

	spin_lock(&pde->pde_unload_lock);
	if (!pde->proc_fops) {
		spin_unlock(&pde->pde_unload_lock);
		return rv;
	}
	pde->pde_users++;
	poll = pde->proc_fops->poll;
	spin_unlock(&pde->pde_unload_lock);

	if (poll)
		rv = poll(file, pts);

	pde_users_dec(pde);
	return rv;
}

static long proc_reg_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct proc_dir_entry *pde = PDE(file->f_path.dentry->d_inode);
	long rv = -ENOTTY;
	long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
	int (*ioctl)(struct inode *, struct file *, unsigned int, unsigned long);

	spin_lock(&pde->pde_unload_lock);
	if (!pde->proc_fops) {
		spin_unlock(&pde->pde_unload_lock);
		return rv;
	}
	pde->pde_users++;
	unlocked_ioctl = pde->proc_fops->unlocked_ioctl;
	ioctl = pde->proc_fops->ioctl;
	spin_unlock(&pde->pde_unload_lock);

	if (unlocked_ioctl) {
		rv = unlocked_ioctl(file, cmd, arg);
		if (rv == -ENOIOCTLCMD)
			rv = -EINVAL;
	} else if (ioctl) {
		lock_kernel();
		rv = ioctl(file->f_path.dentry->d_inode, file, cmd, arg);
		unlock_kernel();
	}

	pde_users_dec(pde);
	return rv;
}

#ifdef CONFIG_COMPAT
static long proc_reg_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct proc_dir_entry *pde = PDE(file->f_path.dentry->d_inode);
	long rv = -ENOTTY;
	long (*compat_ioctl)(struct file *, unsigned int, unsigned long);

	spin_lock(&pde->pde_unload_lock);
	if (!pde->proc_fops) {
		spin_unlock(&pde->pde_unload_lock);
		return rv;
	}
	pde->pde_users++;
	compat_ioctl = pde->proc_fops->compat_ioctl;
	spin_unlock(&pde->pde_unload_lock);

	if (compat_ioctl)
		rv = compat_ioctl(file, cmd, arg);

	pde_users_dec(pde);
	return rv;
}
#endif

static int proc_reg_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct proc_dir_entry *pde = PDE(file->f_path.dentry->d_inode);
	int rv = -EIO;
	int (*mmap)(struct file *, struct vm_area_struct *);

	spin_lock(&pde->pde_unload_lock);
	if (!pde->proc_fops) {
		spin_unlock(&pde->pde_unload_lock);
		return rv;
	}
	pde->pde_users++;
	mmap = pde->proc_fops->mmap;
	spin_unlock(&pde->pde_unload_lock);

	if (mmap)
		rv = mmap(file, vma);

	pde_users_dec(pde);
	return rv;
}

static int proc_reg_open(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *pde = PDE(inode);
	int rv = 0;
	int (*open)(struct inode *, struct file *);

	spin_lock(&pde->pde_unload_lock);
	if (!pde->proc_fops) {
		spin_unlock(&pde->pde_unload_lock);
		return rv;
	}
	pde->pde_users++;
	open = pde->proc_fops->open;
	spin_unlock(&pde->pde_unload_lock);

	if (open)
		rv = open(inode, file);

	pde_users_dec(pde);
	return rv;
}

static int proc_reg_release(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *pde = PDE(inode);
	int rv = 0;
	int (*release)(struct inode *, struct file *);

	spin_lock(&pde->pde_unload_lock);
	if (!pde->proc_fops) {
		spin_unlock(&pde->pde_unload_lock);
		return rv;
	}
	pde->pde_users++;
	release = pde->proc_fops->release;
	spin_unlock(&pde->pde_unload_lock);

	if (release)
		rv = release(inode, file);

	pde_users_dec(pde);
	return rv;
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
	.open		= proc_reg_open,
	.release	= proc_reg_release,
};

struct inode *proc_get_inode(struct super_block *sb, unsigned int ino,
				struct proc_dir_entry *de)
{
	struct inode * inode;

	if (de != NULL && !try_module_get(de->owner))
		goto out_mod;

	inode = iget(sb, ino);
	if (!inode)
		goto out_ino;

	PROC_I(inode)->fd = 0;
	PROC_I(inode)->pde = de;
	if (de) {
		if (de->mode) {
			inode->i_mode = de->mode;
			inode->i_uid = de->uid;
			inode->i_gid = de->gid;
		}
		if (de->size)
			inode->i_size = de->size;
		if (de->nlink)
			inode->i_nlink = de->nlink;
		if (de->proc_iops)
			inode->i_op = de->proc_iops;
		if (de->proc_fops) {
			if (S_ISREG(inode->i_mode))
				inode->i_fop = &proc_reg_file_ops;
			else
				inode->i_fop = de->proc_fops;
		}
	}

	return inode;

out_ino:
	if (de != NULL)
		module_put(de->owner);
out_mod:
	return NULL;
}			

int proc_fill_super(struct super_block *s, void *data, int silent)
{
	struct inode * root_inode;

	s->s_flags |= MS_NODIRATIME | MS_NOSUID | MS_NOEXEC;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = PROC_SUPER_MAGIC;
	s->s_op = &proc_sops;
	s->s_time_gran = 1;
	
	de_get(&proc_root);
	root_inode = proc_get_inode(s, PROC_ROOT_INO, &proc_root);
	if (!root_inode)
		goto out_no_root;
	root_inode->i_uid = 0;
	root_inode->i_gid = 0;
	s->s_root = d_alloc_root(root_inode);
	if (!s->s_root)
		goto out_no_root;
	return 0;

out_no_root:
	printk("proc_read_super: get root inode failed\n");
	iput(root_inode);
	de_put(&proc_root);
	return -ENOMEM;
}
MODULE_LICENSE("GPL");
