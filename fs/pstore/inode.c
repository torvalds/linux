/*
 * Persistent Storage - ramfs parts.
 *
 * Copyright (C) 2010 Intel Corporation <tony.luck@intel.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/mount.h>
#include <linux/ramfs.h>
#include <linux/parser.h>
#include <linux/sched.h>
#include <linux/magic.h>
#include <linux/pstore.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

#include "internal.h"

#define	PSTORE_NAMELEN	64

static DEFINE_SPINLOCK(allpstore_lock);
static LIST_HEAD(allpstore);

struct pstore_private {
	struct list_head list;
	struct pstore_info *psi;
	enum pstore_type_id type;
	u64	id;
	ssize_t	size;
	char	data[];
};

static ssize_t pstore_file_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct pstore_private *ps = file->private_data;

	return simple_read_from_buffer(userbuf, count, ppos, ps->data, ps->size);
}

static const struct file_operations pstore_file_operations = {
	.open	= simple_open,
	.read	= pstore_file_read,
	.llseek	= default_llseek,
};

/*
 * When a file is unlinked from our file system we call the
 * platform driver to erase the record from persistent store.
 */
static int pstore_unlink(struct inode *dir, struct dentry *dentry)
{
	struct pstore_private *p = dentry->d_inode->i_private;

	if (p->psi->erase)
		p->psi->erase(p->type, p->id, p->psi);

	return simple_unlink(dir, dentry);
}

static void pstore_evict_inode(struct inode *inode)
{
	struct pstore_private	*p = inode->i_private;
	unsigned long		flags;

	end_writeback(inode);
	if (p) {
		spin_lock_irqsave(&allpstore_lock, flags);
		list_del(&p->list);
		spin_unlock_irqrestore(&allpstore_lock, flags);
		kfree(p);
	}
}

static const struct inode_operations pstore_dir_inode_operations = {
	.lookup		= simple_lookup,
	.unlink		= pstore_unlink,
};

static struct inode *pstore_get_inode(struct super_block *sb)
{
	struct inode *inode = new_inode(sb);
	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	}
	return inode;
}

enum {
	Opt_kmsg_bytes, Opt_err
};

static const match_table_t tokens = {
	{Opt_kmsg_bytes, "kmsg_bytes=%u"},
	{Opt_err, NULL}
};

static void parse_options(char *options)
{
	char		*p;
	substring_t	args[MAX_OPT_ARGS];
	int		option;

	if (!options)
		return;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_kmsg_bytes:
			if (!match_int(&args[0], &option))
				pstore_set_kmsg_bytes(option);
			break;
		}
	}
}

static int pstore_remount(struct super_block *sb, int *flags, char *data)
{
	parse_options(data);

	return 0;
}

static const struct super_operations pstore_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= pstore_evict_inode,
	.remount_fs	= pstore_remount,
	.show_options	= generic_show_options,
};

static struct super_block *pstore_sb;

int pstore_is_mounted(void)
{
	return pstore_sb != NULL;
}

/*
 * Make a regular file in the root directory of our file system.
 * Load it up with "size" bytes of data from "buf".
 * Set the mtime & ctime to the date that this record was originally stored.
 */
int pstore_mkfile(enum pstore_type_id type, char *psname, u64 id,
		  char *data, size_t size, struct timespec time,
		  struct pstore_info *psi)
{
	struct dentry		*root = pstore_sb->s_root;
	struct dentry		*dentry;
	struct inode		*inode;
	int			rc = 0;
	char			name[PSTORE_NAMELEN];
	struct pstore_private	*private, *pos;
	unsigned long		flags;

	spin_lock_irqsave(&allpstore_lock, flags);
	list_for_each_entry(pos, &allpstore, list) {
		if (pos->type == type &&
		    pos->id == id &&
		    pos->psi == psi) {
			rc = -EEXIST;
			break;
		}
	}
	spin_unlock_irqrestore(&allpstore_lock, flags);
	if (rc)
		return rc;

	rc = -ENOMEM;
	inode = pstore_get_inode(pstore_sb);
	if (!inode)
		goto fail;
	inode->i_mode = S_IFREG | 0444;
	inode->i_fop = &pstore_file_operations;
	private = kmalloc(sizeof *private + size, GFP_KERNEL);
	if (!private)
		goto fail_alloc;
	private->type = type;
	private->id = id;
	private->psi = psi;

	switch (type) {
	case PSTORE_TYPE_DMESG:
		sprintf(name, "dmesg-%s-%lld", psname, id);
		break;
	case PSTORE_TYPE_MCE:
		sprintf(name, "mce-%s-%lld", psname, id);
		break;
	case PSTORE_TYPE_UNKNOWN:
		sprintf(name, "unknown-%s-%lld", psname, id);
		break;
	default:
		sprintf(name, "type%d-%s-%lld", type, psname, id);
		break;
	}

	mutex_lock(&root->d_inode->i_mutex);

	rc = -ENOSPC;
	dentry = d_alloc_name(root, name);
	if (IS_ERR(dentry))
		goto fail_lockedalloc;

	memcpy(private->data, data, size);
	inode->i_size = private->size = size;

	inode->i_private = private;

	if (time.tv_sec)
		inode->i_mtime = inode->i_ctime = time;

	d_add(dentry, inode);

	spin_lock_irqsave(&allpstore_lock, flags);
	list_add(&private->list, &allpstore);
	spin_unlock_irqrestore(&allpstore_lock, flags);

	mutex_unlock(&root->d_inode->i_mutex);

	return 0;

fail_lockedalloc:
	mutex_unlock(&root->d_inode->i_mutex);
	kfree(private);
fail_alloc:
	iput(inode);

fail:
	return rc;
}

int pstore_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;

	save_mount_options(sb, data);

	pstore_sb = sb;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_CACHE_SIZE;
	sb->s_blocksize_bits	= PAGE_CACHE_SHIFT;
	sb->s_magic		= PSTOREFS_MAGIC;
	sb->s_op		= &pstore_ops;
	sb->s_time_gran		= 1;

	parse_options(data);

	inode = pstore_get_inode(sb);
	if (inode) {
		inode->i_mode = S_IFDIR | 0755;
		inode->i_op = &pstore_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
		inc_nlink(inode);
	}
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	pstore_get_records(0);

	return 0;
}

static struct dentry *pstore_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_single(fs_type, flags, data, pstore_fill_super);
}

static void pstore_kill_sb(struct super_block *sb)
{
	kill_litter_super(sb);
	pstore_sb = NULL;
}

static struct file_system_type pstore_fs_type = {
	.name		= "pstore",
	.mount		= pstore_mount,
	.kill_sb	= pstore_kill_sb,
};

static int __init init_pstore_fs(void)
{
	return register_filesystem(&pstore_fs_type);
}
module_init(init_pstore_fs)

MODULE_AUTHOR("Tony Luck <tony.luck@intel.com>");
MODULE_LICENSE("GPL");
