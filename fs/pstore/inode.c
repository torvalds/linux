// SPDX-License-Identifier: GPL-2.0-only
/*
 * Persistent Storage - ramfs parts.
 *
 * Copyright (C) 2010 Intel Corporation <tony.luck@intel.com>
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
#include <linux/seq_file.h>
#include <linux/ramfs.h>
#include <linux/fs_parser.h>
#include <linux/fs_context.h>
#include <linux/sched.h>
#include <linux/magic.h>
#include <linux/pstore.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/cleanup.h>

#include "internal.h"

#define	PSTORE_NAMELEN	64

static DEFINE_MUTEX(records_list_lock);
static LIST_HEAD(records_list);

static DEFINE_MUTEX(pstore_sb_lock);
static struct super_block *pstore_sb;

DEFINE_FREE(pstore_iput, struct inode *, if (_T) iput(_T))

struct pstore_private {
	struct list_head list;
	struct dentry *dentry;
	struct pstore_record *record;
	size_t total_size;
};

struct pstore_ftrace_seq_data {
	const void *ptr;
	size_t off;
	size_t size;
};

#define REC_SIZE sizeof(struct pstore_ftrace_record)

static void free_pstore_private(struct pstore_private *private)
{
	if (!private)
		return;
	if (private->record) {
		kvfree(private->record->buf);
		kfree(private->record->priv);
		kfree(private->record);
	}
	kfree(private);
}
DEFINE_FREE(pstore_private, struct pstore_private *, free_pstore_private(_T));

static void *pstore_ftrace_seq_start(struct seq_file *s, loff_t *pos)
{
	struct pstore_private *ps = s->private;
	struct pstore_ftrace_seq_data *data __free(kfree) = NULL;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->off = ps->total_size % REC_SIZE;
	data->off += *pos * REC_SIZE;
	if (data->off + REC_SIZE > ps->total_size)
		return NULL;

	return_ptr(data);
}

static void pstore_ftrace_seq_stop(struct seq_file *s, void *v)
{
	kfree(v);
}

static void *pstore_ftrace_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct pstore_private *ps = s->private;
	struct pstore_ftrace_seq_data *data = v;

	(*pos)++;
	data->off += REC_SIZE;
	if (data->off + REC_SIZE > ps->total_size)
		return NULL;

	return data;
}

static int pstore_ftrace_seq_show(struct seq_file *s, void *v)
{
	struct pstore_private *ps = s->private;
	struct pstore_ftrace_seq_data *data = v;
	struct pstore_ftrace_record *rec;

	if (!data)
		return 0;

	rec = (struct pstore_ftrace_record *)(ps->record->buf + data->off);

	seq_printf(s, "CPU:%d ts:%llu %08lx  %08lx  %ps <- %pS\n",
		   pstore_ftrace_decode_cpu(rec),
		   pstore_ftrace_read_timestamp(rec),
		   rec->ip, rec->parent_ip, (void *)rec->ip,
		   (void *)rec->parent_ip);

	return 0;
}

static const struct seq_operations pstore_ftrace_seq_ops = {
	.start	= pstore_ftrace_seq_start,
	.next	= pstore_ftrace_seq_next,
	.stop	= pstore_ftrace_seq_stop,
	.show	= pstore_ftrace_seq_show,
};

static ssize_t pstore_file_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct seq_file *sf = file->private_data;
	struct pstore_private *ps = sf->private;

	if (ps->record->type == PSTORE_TYPE_FTRACE)
		return seq_read(file, userbuf, count, ppos);
	return simple_read_from_buffer(userbuf, count, ppos,
				       ps->record->buf, ps->total_size);
}

static int pstore_file_open(struct inode *inode, struct file *file)
{
	struct pstore_private *ps = inode->i_private;
	struct seq_file *sf;
	int err;
	const struct seq_operations *sops = NULL;

	if (ps->record->type == PSTORE_TYPE_FTRACE)
		sops = &pstore_ftrace_seq_ops;

	err = seq_open(file, sops);
	if (err < 0)
		return err;

	sf = file->private_data;
	sf->private = ps;

	return 0;
}

static loff_t pstore_file_llseek(struct file *file, loff_t off, int whence)
{
	struct seq_file *sf = file->private_data;

	if (sf->op)
		return seq_lseek(file, off, whence);
	return default_llseek(file, off, whence);
}

static const struct file_operations pstore_file_operations = {
	.open		= pstore_file_open,
	.read		= pstore_file_read,
	.llseek		= pstore_file_llseek,
	.release	= seq_release,
};

/*
 * When a file is unlinked from our file system we call the
 * platform driver to erase the record from persistent store.
 */
static int pstore_unlink(struct inode *dir, struct dentry *dentry)
{
	struct pstore_private *p = d_inode(dentry)->i_private;
	struct pstore_record *record = p->record;

	if (!record->psi->erase)
		return -EPERM;

	/* Make sure we can't race while removing this file. */
	scoped_guard(mutex, &records_list_lock) {
		if (!list_empty(&p->list))
			list_del_init(&p->list);
		else
			return -ENOENT;
		p->dentry = NULL;
	}

	scoped_guard(mutex, &record->psi->read_mutex)
		record->psi->erase(record);

	return simple_unlink(dir, dentry);
}

static void pstore_evict_inode(struct inode *inode)
{
	struct pstore_private	*p = inode->i_private;

	clear_inode(inode);
	free_pstore_private(p);
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
		simple_inode_init_ts(inode);
	}
	return inode;
}

enum {
	Opt_kmsg_bytes
};

static const struct fs_parameter_spec pstore_param_spec[] = {
	fsparam_u32	("kmsg_bytes",	Opt_kmsg_bytes),
	{}
};

struct pstore_context {
	unsigned int kmsg_bytes;
};

static int pstore_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct pstore_context *ctx = fc->fs_private;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, pstore_param_spec, param, &result);
	/* pstore has historically ignored invalid kmsg_bytes param */
	if (opt < 0)
		return 0;

	switch (opt) {
	case Opt_kmsg_bytes:
		ctx->kmsg_bytes = result.uint_32;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Display the mount options in /proc/mounts.
 */
static int pstore_show_options(struct seq_file *m, struct dentry *root)
{
	if (kmsg_bytes != CONFIG_PSTORE_DEFAULT_KMSG_BYTES)
		seq_printf(m, ",kmsg_bytes=%u", kmsg_bytes);
	return 0;
}

static int pstore_reconfigure(struct fs_context *fc)
{
	struct pstore_context *ctx = fc->fs_private;

	sync_filesystem(fc->root->d_sb);
	pstore_set_kmsg_bytes(ctx->kmsg_bytes);

	return 0;
}

static const struct super_operations pstore_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= pstore_evict_inode,
	.show_options	= pstore_show_options,
};

static struct dentry *psinfo_lock_root(void)
{
	struct dentry *root;

	guard(mutex)(&pstore_sb_lock);
	/*
	 * Having no backend is fine -- no records appear.
	 * Not being mounted is fine -- nothing to do.
	 */
	if (!psinfo || !pstore_sb)
		return NULL;

	root = pstore_sb->s_root;
	inode_lock(d_inode(root));

	return root;
}

int pstore_put_backend_records(struct pstore_info *psi)
{
	struct pstore_private *pos, *tmp;
	struct dentry *root;

	root = psinfo_lock_root();
	if (!root)
		return 0;

	scoped_guard(mutex, &records_list_lock) {
		list_for_each_entry_safe(pos, tmp, &records_list, list) {
			if (pos->record->psi == psi) {
				list_del_init(&pos->list);
				d_invalidate(pos->dentry);
				simple_unlink(d_inode(root), pos->dentry);
				pos->dentry = NULL;
			}
		}
	}

	inode_unlock(d_inode(root));

	return 0;
}

/*
 * Make a regular file in the root directory of our file system.
 * Load it up with "size" bytes of data from "buf".
 * Set the mtime & ctime to the date that this record was originally stored.
 */
int pstore_mkfile(struct dentry *root, struct pstore_record *record)
{
	struct dentry		*dentry;
	struct inode		*inode __free(pstore_iput) = NULL;
	char			name[PSTORE_NAMELEN];
	struct pstore_private	*private __free(pstore_private) = NULL, *pos;
	size_t			size = record->size + record->ecc_notice_size;

	if (WARN_ON(!inode_is_locked(d_inode(root))))
		return -EINVAL;

	guard(mutex)(&records_list_lock);

	/* Skip records that are already present in the filesystem. */
	list_for_each_entry(pos, &records_list, list) {
		if (pos->record->type == record->type &&
		    pos->record->id == record->id &&
		    pos->record->psi == record->psi)
			return -EEXIST;
	}

	inode = pstore_get_inode(root->d_sb);
	if (!inode)
		return -ENOMEM;
	inode->i_mode = S_IFREG | 0444;
	inode->i_fop = &pstore_file_operations;
	scnprintf(name, sizeof(name), "%s-%s-%llu%s",
			pstore_type_to_name(record->type),
			record->psi->name, record->id,
			record->compressed ? ".enc.z" : "");

	private = kzalloc(sizeof(*private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	dentry = d_alloc_name(root, name);
	if (!dentry)
		return -ENOMEM;

	private->dentry = dentry;
	private->record = record;
	inode->i_size = private->total_size = size;
	inode->i_private = private;

	if (record->time.tv_sec)
		inode_set_mtime_to_ts(inode,
				      inode_set_ctime_to_ts(inode, record->time));

	d_add(dentry, no_free_ptr(inode));

	list_add(&(no_free_ptr(private))->list, &records_list);

	return 0;
}

/*
 * Read all the records from the persistent store. Create
 * files in our filesystem.  Don't warn about -EEXIST errors
 * when we are re-scanning the backing store looking to add new
 * error records.
 */
void pstore_get_records(int quiet)
{
	struct dentry *root;

	root = psinfo_lock_root();
	if (!root)
		return;

	pstore_get_backend_records(psinfo, root, quiet);
	inode_unlock(d_inode(root));
}

static int pstore_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct pstore_context *ctx = fc->fs_private;
	struct inode *inode;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= PSTOREFS_MAGIC;
	sb->s_op		= &pstore_ops;
	sb->s_time_gran		= 1;

	pstore_set_kmsg_bytes(ctx->kmsg_bytes);

	inode = pstore_get_inode(sb);
	if (inode) {
		inode->i_mode = S_IFDIR | 0750;
		inode->i_op = &pstore_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
		inc_nlink(inode);
	}
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	scoped_guard(mutex, &pstore_sb_lock)
		pstore_sb = sb;

	pstore_get_records(0);

	return 0;
}

static int pstore_get_tree(struct fs_context *fc)
{
	if (fc->root)
		return pstore_reconfigure(fc);

	return get_tree_single(fc, pstore_fill_super);
}

static void pstore_free_fc(struct fs_context *fc)
{
	kfree(fc->fs_private);
}

static const struct fs_context_operations pstore_context_ops = {
	.parse_param	= pstore_parse_param,
	.get_tree	= pstore_get_tree,
	.reconfigure	= pstore_reconfigure,
	.free		= pstore_free_fc,
};

static void pstore_kill_sb(struct super_block *sb)
{
	guard(mutex)(&pstore_sb_lock);
	WARN_ON(pstore_sb && pstore_sb != sb);

	kill_litter_super(sb);
	pstore_sb = NULL;

	guard(mutex)(&records_list_lock);
	INIT_LIST_HEAD(&records_list);
}

static int pstore_init_fs_context(struct fs_context *fc)
{
	struct pstore_context *ctx;

	ctx = kzalloc(sizeof(struct pstore_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	/*
	 * Global kmsg_bytes is initialized to default, and updated
	 * every time we (re)mount the single-sb filesystem with the
	 * option specified.
	 */
	ctx->kmsg_bytes = kmsg_bytes;

	fc->fs_private = ctx;
	fc->ops = &pstore_context_ops;

	return 0;
}

static struct file_system_type pstore_fs_type = {
	.owner          = THIS_MODULE,
	.name		= "pstore",
	.kill_sb	= pstore_kill_sb,
	.init_fs_context = pstore_init_fs_context,
	.parameters	= pstore_param_spec,
};

int __init pstore_init_fs(void)
{
	int err;

	/* Create a convenient mount point for people to access pstore */
	err = sysfs_create_mount_point(fs_kobj, "pstore");
	if (err)
		goto out;

	err = register_filesystem(&pstore_fs_type);
	if (err < 0)
		sysfs_remove_mount_point(fs_kobj, "pstore");

out:
	return err;
}

void __exit pstore_exit_fs(void)
{
	unregister_filesystem(&pstore_fs_type);
	sysfs_remove_mount_point(fs_kobj, "pstore");
}
