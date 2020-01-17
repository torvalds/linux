// SPDX-License-Identifier: GPL-2.0-only
/*
 * Persistent Storage - ramfs parts.
 *
 * Copyright (C) 2010 Intel Corporation <tony.luck@intel.com>
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fsyestify.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
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
		kfree(private->record->buf);
		kfree(private->record);
	}
	kfree(private);
}

static void *pstore_ftrace_seq_start(struct seq_file *s, loff_t *pos)
{
	struct pstore_private *ps = s->private;
	struct pstore_ftrace_seq_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->off = ps->total_size % REC_SIZE;
	data->off += *pos * REC_SIZE;
	if (data->off + REC_SIZE > ps->total_size) {
		kfree(data);
		return NULL;
	}

	return data;

}

static void pstore_ftrace_seq_stop(struct seq_file *s, void *v)
{
	kfree(v);
}

static void *pstore_ftrace_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct pstore_private *ps = s->private;
	struct pstore_ftrace_seq_data *data = v;

	data->off += REC_SIZE;
	if (data->off + REC_SIZE > ps->total_size)
		return NULL;

	(*pos)++;
	return data;
}

static int pstore_ftrace_seq_show(struct seq_file *s, void *v)
{
	struct pstore_private *ps = s->private;
	struct pstore_ftrace_seq_data *data = v;
	struct pstore_ftrace_record *rec;

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

static int pstore_file_open(struct iyesde *iyesde, struct file *file)
{
	struct pstore_private *ps = iyesde->i_private;
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
static int pstore_unlink(struct iyesde *dir, struct dentry *dentry)
{
	struct pstore_private *p = d_iyesde(dentry)->i_private;
	struct pstore_record *record = p->record;

	if (!record->psi->erase)
		return -EPERM;

	mutex_lock(&record->psi->read_mutex);
	record->psi->erase(record);
	mutex_unlock(&record->psi->read_mutex);

	return simple_unlink(dir, dentry);
}

static void pstore_evict_iyesde(struct iyesde *iyesde)
{
	struct pstore_private	*p = iyesde->i_private;
	unsigned long		flags;

	clear_iyesde(iyesde);
	if (p) {
		spin_lock_irqsave(&allpstore_lock, flags);
		list_del(&p->list);
		spin_unlock_irqrestore(&allpstore_lock, flags);
		free_pstore_private(p);
	}
}

static const struct iyesde_operations pstore_dir_iyesde_operations = {
	.lookup		= simple_lookup,
	.unlink		= pstore_unlink,
};

static struct iyesde *pstore_get_iyesde(struct super_block *sb)
{
	struct iyesde *iyesde = new_iyesde(sb);
	if (iyesde) {
		iyesde->i_iyes = get_next_iyes();
		iyesde->i_atime = iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
	}
	return iyesde;
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

/*
 * Display the mount options in /proc/mounts.
 */
static int pstore_show_options(struct seq_file *m, struct dentry *root)
{
	if (kmsg_bytes != PSTORE_DEFAULT_KMSG_BYTES)
		seq_printf(m, ",kmsg_bytes=%lu", kmsg_bytes);
	return 0;
}

static int pstore_remount(struct super_block *sb, int *flags, char *data)
{
	sync_filesystem(sb);
	parse_options(data);

	return 0;
}

static const struct super_operations pstore_ops = {
	.statfs		= simple_statfs,
	.drop_iyesde	= generic_delete_iyesde,
	.evict_iyesde	= pstore_evict_iyesde,
	.remount_fs	= pstore_remount,
	.show_options	= pstore_show_options,
};

static struct super_block *pstore_sb;

bool pstore_is_mounted(void)
{
	return pstore_sb != NULL;
}

/*
 * Make a regular file in the root directory of our file system.
 * Load it up with "size" bytes of data from "buf".
 * Set the mtime & ctime to the date that this record was originally stored.
 */
int pstore_mkfile(struct dentry *root, struct pstore_record *record)
{
	struct dentry		*dentry;
	struct iyesde		*iyesde;
	int			rc = 0;
	char			name[PSTORE_NAMELEN];
	struct pstore_private	*private, *pos;
	unsigned long		flags;
	size_t			size = record->size + record->ecc_yestice_size;

	WARN_ON(!iyesde_is_locked(d_iyesde(root)));

	spin_lock_irqsave(&allpstore_lock, flags);
	list_for_each_entry(pos, &allpstore, list) {
		if (pos->record->type == record->type &&
		    pos->record->id == record->id &&
		    pos->record->psi == record->psi) {
			rc = -EEXIST;
			break;
		}
	}
	spin_unlock_irqrestore(&allpstore_lock, flags);
	if (rc)
		return rc;

	rc = -ENOMEM;
	iyesde = pstore_get_iyesde(root->d_sb);
	if (!iyesde)
		goto fail;
	iyesde->i_mode = S_IFREG | 0444;
	iyesde->i_fop = &pstore_file_operations;
	scnprintf(name, sizeof(name), "%s-%s-%llu%s",
			pstore_type_to_name(record->type),
			record->psi->name, record->id,
			record->compressed ? ".enc.z" : "");

	private = kzalloc(sizeof(*private), GFP_KERNEL);
	if (!private)
		goto fail_iyesde;

	dentry = d_alloc_name(root, name);
	if (!dentry)
		goto fail_private;

	private->record = record;
	iyesde->i_size = private->total_size = size;
	iyesde->i_private = private;

	if (record->time.tv_sec)
		iyesde->i_mtime = iyesde->i_ctime = record->time;

	d_add(dentry, iyesde);

	spin_lock_irqsave(&allpstore_lock, flags);
	list_add(&private->list, &allpstore);
	spin_unlock_irqrestore(&allpstore_lock, flags);

	return 0;

fail_private:
	free_pstore_private(private);
fail_iyesde:
	iput(iyesde);

fail:
	return rc;
}

/*
 * Read all the records from the persistent store. Create
 * files in our filesystem.  Don't warn about -EEXIST errors
 * when we are re-scanning the backing store looking to add new
 * error records.
 */
void pstore_get_records(int quiet)
{
	struct pstore_info *psi = psinfo;
	struct dentry *root;

	if (!psi || !pstore_sb)
		return;

	root = pstore_sb->s_root;

	iyesde_lock(d_iyesde(root));
	pstore_get_backend_records(psi, root, quiet);
	iyesde_unlock(d_iyesde(root));
}

static int pstore_fill_super(struct super_block *sb, void *data, int silent)
{
	struct iyesde *iyesde;

	pstore_sb = sb;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= PSTOREFS_MAGIC;
	sb->s_op		= &pstore_ops;
	sb->s_time_gran		= 1;

	parse_options(data);

	iyesde = pstore_get_iyesde(sb);
	if (iyesde) {
		iyesde->i_mode = S_IFDIR | 0750;
		iyesde->i_op = &pstore_dir_iyesde_operations;
		iyesde->i_fop = &simple_dir_operations;
		inc_nlink(iyesde);
	}
	sb->s_root = d_make_root(iyesde);
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
	.owner          = THIS_MODULE,
	.name		= "pstore",
	.mount		= pstore_mount,
	.kill_sb	= pstore_kill_sb,
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
