/*
 * fs/f2fs/super.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/statfs.h>
#include <linux/buffer_head.h>
#include <linux/backing-dev.h>
#include <linux/kthread.h>
#include <linux/parser.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/exportfs.h>
#include <linux/blkdev.h>
#include <linux/f2fs_fs.h>
#include <linux/sysfs.h>

#include "f2fs.h"
#include "node.h"
#include "segment.h"
#include "xattr.h"
#include "gc.h"

#define CREATE_TRACE_POINTS
#include <trace/events/f2fs.h>

static struct proc_dir_entry *f2fs_proc_root;
static struct kmem_cache *f2fs_inode_cachep;
static struct kset *f2fs_kset;

enum {
	Opt_gc_background,
	Opt_disable_roll_forward,
	Opt_discard,
	Opt_noheap,
	Opt_user_xattr,
	Opt_nouser_xattr,
	Opt_acl,
	Opt_noacl,
	Opt_active_logs,
	Opt_disable_ext_identify,
	Opt_inline_xattr,
	Opt_err,
};

static match_table_t f2fs_tokens = {
	{Opt_gc_background, "background_gc=%s"},
	{Opt_disable_roll_forward, "disable_roll_forward"},
	{Opt_discard, "discard"},
	{Opt_noheap, "no_heap"},
	{Opt_user_xattr, "user_xattr"},
	{Opt_nouser_xattr, "nouser_xattr"},
	{Opt_acl, "acl"},
	{Opt_noacl, "noacl"},
	{Opt_active_logs, "active_logs=%u"},
	{Opt_disable_ext_identify, "disable_ext_identify"},
	{Opt_inline_xattr, "inline_xattr"},
	{Opt_err, NULL},
};

/* Sysfs support for f2fs */
struct f2fs_attr {
	struct attribute attr;
	ssize_t (*show)(struct f2fs_attr *, struct f2fs_sb_info *, char *);
	ssize_t (*store)(struct f2fs_attr *, struct f2fs_sb_info *,
			 const char *, size_t);
	int offset;
};

static ssize_t f2fs_sbi_show(struct f2fs_attr *a,
			struct f2fs_sb_info *sbi, char *buf)
{
	struct f2fs_gc_kthread *gc_kth = sbi->gc_thread;
	unsigned int *ui;

	if (!gc_kth)
		return -EINVAL;

	ui = (unsigned int *)(((char *)gc_kth) + a->offset);

	return snprintf(buf, PAGE_SIZE, "%u\n", *ui);
}

static ssize_t f2fs_sbi_store(struct f2fs_attr *a,
			struct f2fs_sb_info *sbi,
			const char *buf, size_t count)
{
	struct f2fs_gc_kthread *gc_kth = sbi->gc_thread;
	unsigned long t;
	unsigned int *ui;
	ssize_t ret;

	if (!gc_kth)
		return -EINVAL;

	ui = (unsigned int *)(((char *)gc_kth) + a->offset);

	ret = kstrtoul(skip_spaces(buf), 0, &t);
	if (ret < 0)
		return ret;
	*ui = t;
	return count;
}

static ssize_t f2fs_attr_show(struct kobject *kobj,
				struct attribute *attr, char *buf)
{
	struct f2fs_sb_info *sbi = container_of(kobj, struct f2fs_sb_info,
								s_kobj);
	struct f2fs_attr *a = container_of(attr, struct f2fs_attr, attr);

	return a->show ? a->show(a, sbi, buf) : 0;
}

static ssize_t f2fs_attr_store(struct kobject *kobj, struct attribute *attr,
						const char *buf, size_t len)
{
	struct f2fs_sb_info *sbi = container_of(kobj, struct f2fs_sb_info,
									s_kobj);
	struct f2fs_attr *a = container_of(attr, struct f2fs_attr, attr);

	return a->store ? a->store(a, sbi, buf, len) : 0;
}

static void f2fs_sb_release(struct kobject *kobj)
{
	struct f2fs_sb_info *sbi = container_of(kobj, struct f2fs_sb_info,
								s_kobj);
	complete(&sbi->s_kobj_unregister);
}

#define F2FS_ATTR_OFFSET(_name, _mode, _show, _store, _elname) \
static struct f2fs_attr f2fs_attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,					\
	.store	= _store,					\
	.offset = offsetof(struct f2fs_gc_kthread, _elname),	\
}

#define F2FS_RW_ATTR(name, elname)	\
	F2FS_ATTR_OFFSET(name, 0644, f2fs_sbi_show, f2fs_sbi_store, elname)

F2FS_RW_ATTR(gc_min_sleep_time, min_sleep_time);
F2FS_RW_ATTR(gc_max_sleep_time, max_sleep_time);
F2FS_RW_ATTR(gc_no_gc_sleep_time, no_gc_sleep_time);
F2FS_RW_ATTR(gc_idle, gc_idle);

#define ATTR_LIST(name) (&f2fs_attr_##name.attr)
static struct attribute *f2fs_attrs[] = {
	ATTR_LIST(gc_min_sleep_time),
	ATTR_LIST(gc_max_sleep_time),
	ATTR_LIST(gc_no_gc_sleep_time),
	ATTR_LIST(gc_idle),
	NULL,
};

static const struct sysfs_ops f2fs_attr_ops = {
	.show	= f2fs_attr_show,
	.store	= f2fs_attr_store,
};

static struct kobj_type f2fs_ktype = {
	.default_attrs	= f2fs_attrs,
	.sysfs_ops	= &f2fs_attr_ops,
	.release	= f2fs_sb_release,
};

void f2fs_msg(struct super_block *sb, const char *level, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	printk("%sF2FS-fs (%s): %pV\n", level, sb->s_id, &vaf);
	va_end(args);
}

static void init_once(void *foo)
{
	struct f2fs_inode_info *fi = (struct f2fs_inode_info *) foo;

	inode_init_once(&fi->vfs_inode);
}

static int parse_options(struct super_block *sb, char *options)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	substring_t args[MAX_OPT_ARGS];
	char *p, *name;
	int arg = 0;

	if (!options)
		return 0;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;
		/*
		 * Initialize args struct so we know whether arg was
		 * found; some options take optional arguments.
		 */
		args[0].to = args[0].from = NULL;
		token = match_token(p, f2fs_tokens, args);

		switch (token) {
		case Opt_gc_background:
			name = match_strdup(&args[0]);

			if (!name)
				return -ENOMEM;
			if (!strncmp(name, "on", 2))
				set_opt(sbi, BG_GC);
			else if (!strncmp(name, "off", 3))
				clear_opt(sbi, BG_GC);
			else {
				kfree(name);
				return -EINVAL;
			}
			kfree(name);
			break;
		case Opt_disable_roll_forward:
			set_opt(sbi, DISABLE_ROLL_FORWARD);
			break;
		case Opt_discard:
			set_opt(sbi, DISCARD);
			break;
		case Opt_noheap:
			set_opt(sbi, NOHEAP);
			break;
#ifdef CONFIG_F2FS_FS_XATTR
		case Opt_user_xattr:
			set_opt(sbi, XATTR_USER);
			break;
		case Opt_nouser_xattr:
			clear_opt(sbi, XATTR_USER);
			break;
		case Opt_inline_xattr:
			set_opt(sbi, INLINE_XATTR);
			break;
#else
		case Opt_user_xattr:
			f2fs_msg(sb, KERN_INFO,
				"user_xattr options not supported");
			break;
		case Opt_nouser_xattr:
			f2fs_msg(sb, KERN_INFO,
				"nouser_xattr options not supported");
			break;
		case Opt_inline_xattr:
			f2fs_msg(sb, KERN_INFO,
				"inline_xattr options not supported");
			break;
#endif
#ifdef CONFIG_F2FS_FS_POSIX_ACL
		case Opt_acl:
			set_opt(sbi, POSIX_ACL);
			break;
		case Opt_noacl:
			clear_opt(sbi, POSIX_ACL);
			break;
#else
		case Opt_acl:
			f2fs_msg(sb, KERN_INFO, "acl options not supported");
			break;
		case Opt_noacl:
			f2fs_msg(sb, KERN_INFO, "noacl options not supported");
			break;
#endif
		case Opt_active_logs:
			if (args->from && match_int(args, &arg))
				return -EINVAL;
			if (arg != 2 && arg != 4 && arg != NR_CURSEG_TYPE)
				return -EINVAL;
			sbi->active_logs = arg;
			break;
		case Opt_disable_ext_identify:
			set_opt(sbi, DISABLE_EXT_IDENTIFY);
			break;
		default:
			f2fs_msg(sb, KERN_ERR,
				"Unrecognized mount option \"%s\" or missing value",
				p);
			return -EINVAL;
		}
	}
	return 0;
}

static struct inode *f2fs_alloc_inode(struct super_block *sb)
{
	struct f2fs_inode_info *fi;

	fi = kmem_cache_alloc(f2fs_inode_cachep, GFP_NOFS | __GFP_ZERO);
	if (!fi)
		return NULL;

	init_once((void *) fi);

	/* Initialize f2fs-specific inode info */
	fi->vfs_inode.i_version = 1;
	atomic_set(&fi->dirty_dents, 0);
	fi->i_current_depth = 1;
	fi->i_advise = 0;
	rwlock_init(&fi->ext.ext_lock);

	set_inode_flag(fi, FI_NEW_INODE);

	if (test_opt(F2FS_SB(sb), INLINE_XATTR))
		set_inode_flag(fi, FI_INLINE_XATTR);

	return &fi->vfs_inode;
}

static int f2fs_drop_inode(struct inode *inode)
{
	/*
	 * This is to avoid a deadlock condition like below.
	 * writeback_single_inode(inode)
	 *  - f2fs_write_data_page
	 *    - f2fs_gc -> iput -> evict
	 *       - inode_wait_for_writeback(inode)
	 */
	if (!inode_unhashed(inode) && inode->i_state & I_SYNC)
		return 0;
	return generic_drop_inode(inode);
}

/*
 * f2fs_dirty_inode() is called from __mark_inode_dirty()
 *
 * We should call set_dirty_inode to write the dirty inode through write_inode.
 */
static void f2fs_dirty_inode(struct inode *inode, int flags)
{
	set_inode_flag(F2FS_I(inode), FI_DIRTY_INODE);
}

static void f2fs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(f2fs_inode_cachep, F2FS_I(inode));
}

static void f2fs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, f2fs_i_callback);
}

static void f2fs_put_super(struct super_block *sb)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	if (sbi->s_proc) {
		remove_proc_entry("segment_info", sbi->s_proc);
		remove_proc_entry(sb->s_id, f2fs_proc_root);
	}
	kobject_del(&sbi->s_kobj);

	f2fs_destroy_stats(sbi);
	stop_gc_thread(sbi);

	/* We don't need to do checkpoint when it's clean */
	if (sbi->s_dirty && get_pages(sbi, F2FS_DIRTY_NODES))
		write_checkpoint(sbi, true);

	iput(sbi->node_inode);
	iput(sbi->meta_inode);

	/* destroy f2fs internal modules */
	destroy_node_manager(sbi);
	destroy_segment_manager(sbi);

	kfree(sbi->ckpt);
	kobject_put(&sbi->s_kobj);
	wait_for_completion(&sbi->s_kobj_unregister);

	sb->s_fs_info = NULL;
	brelse(sbi->raw_super_buf);
	kfree(sbi);
}

int f2fs_sync_fs(struct super_block *sb, int sync)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);

	trace_f2fs_sync_fs(sb, sync);

	if (!sbi->s_dirty && !get_pages(sbi, F2FS_DIRTY_NODES))
		return 0;

	if (sync) {
		mutex_lock(&sbi->gc_mutex);
		write_checkpoint(sbi, false);
		mutex_unlock(&sbi->gc_mutex);
	} else {
		f2fs_balance_fs(sbi);
	}

	return 0;
}

static int f2fs_freeze(struct super_block *sb)
{
	int err;

	if (f2fs_readonly(sb))
		return 0;

	err = f2fs_sync_fs(sb, 1);
	return err;
}

static int f2fs_unfreeze(struct super_block *sb)
{
	return 0;
}

static int f2fs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
	block_t total_count, user_block_count, start_count, ovp_count;

	total_count = le64_to_cpu(sbi->raw_super->block_count);
	user_block_count = sbi->user_block_count;
	start_count = le32_to_cpu(sbi->raw_super->segment0_blkaddr);
	ovp_count = SM_I(sbi)->ovp_segments << sbi->log_blocks_per_seg;
	buf->f_type = F2FS_SUPER_MAGIC;
	buf->f_bsize = sbi->blocksize;

	buf->f_blocks = total_count - start_count;
	buf->f_bfree = buf->f_blocks - valid_user_blocks(sbi) - ovp_count;
	buf->f_bavail = user_block_count - valid_user_blocks(sbi);

	buf->f_files = sbi->total_node_count;
	buf->f_ffree = sbi->total_node_count - valid_inode_count(sbi);

	buf->f_namelen = F2FS_NAME_LEN;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);

	return 0;
}

static int f2fs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct f2fs_sb_info *sbi = F2FS_SB(root->d_sb);

	if (!(root->d_sb->s_flags & MS_RDONLY) && test_opt(sbi, BG_GC))
		seq_printf(seq, ",background_gc=%s", "on");
	else
		seq_printf(seq, ",background_gc=%s", "off");
	if (test_opt(sbi, DISABLE_ROLL_FORWARD))
		seq_puts(seq, ",disable_roll_forward");
	if (test_opt(sbi, DISCARD))
		seq_puts(seq, ",discard");
	if (test_opt(sbi, NOHEAP))
		seq_puts(seq, ",no_heap_alloc");
#ifdef CONFIG_F2FS_FS_XATTR
	if (test_opt(sbi, XATTR_USER))
		seq_puts(seq, ",user_xattr");
	else
		seq_puts(seq, ",nouser_xattr");
	if (test_opt(sbi, INLINE_XATTR))
		seq_puts(seq, ",inline_xattr");
#endif
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	if (test_opt(sbi, POSIX_ACL))
		seq_puts(seq, ",acl");
	else
		seq_puts(seq, ",noacl");
#endif
	if (test_opt(sbi, DISABLE_EXT_IDENTIFY))
		seq_puts(seq, ",disable_ext_identify");

	seq_printf(seq, ",active_logs=%u", sbi->active_logs);

	return 0;
}

static int segment_info_seq_show(struct seq_file *seq, void *offset)
{
	struct super_block *sb = seq->private;
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	unsigned int total_segs = le32_to_cpu(sbi->raw_super->segment_count_main);
	int i;

	for (i = 0; i < total_segs; i++) {
		seq_printf(seq, "%u", get_valid_blocks(sbi, i, 1));
		if (i != 0 && (i % 10) == 0)
			seq_puts(seq, "\n");
		else
			seq_puts(seq, " ");
	}
	return 0;
}

static int segment_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, segment_info_seq_show, PDE_DATA(inode));
}

static const struct file_operations f2fs_seq_segment_info_fops = {
	.owner = THIS_MODULE,
	.open = segment_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int f2fs_remount(struct super_block *sb, int *flags, char *data)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct f2fs_mount_info org_mount_opt;
	int err, active_logs;

	/*
	 * Save the old mount options in case we
	 * need to restore them.
	 */
	org_mount_opt = sbi->mount_opt;
	active_logs = sbi->active_logs;

	/* parse mount options */
	err = parse_options(sb, data);
	if (err)
		goto restore_opts;

	/*
	 * Previous and new state of filesystem is RO,
	 * so no point in checking GC conditions.
	 */
	if ((sb->s_flags & MS_RDONLY) && (*flags & MS_RDONLY))
		goto skip;

	/*
	 * We stop the GC thread if FS is mounted as RO
	 * or if background_gc = off is passed in mount
	 * option. Also sync the filesystem.
	 */
	if ((*flags & MS_RDONLY) || !test_opt(sbi, BG_GC)) {
		if (sbi->gc_thread) {
			stop_gc_thread(sbi);
			f2fs_sync_fs(sb, 1);
		}
	} else if (test_opt(sbi, BG_GC) && !sbi->gc_thread) {
		err = start_gc_thread(sbi);
		if (err)
			goto restore_opts;
	}
skip:
	/* Update the POSIXACL Flag */
	 sb->s_flags = (sb->s_flags & ~MS_POSIXACL) |
		(test_opt(sbi, POSIX_ACL) ? MS_POSIXACL : 0);
	return 0;

restore_opts:
	sbi->mount_opt = org_mount_opt;
	sbi->active_logs = active_logs;
	return err;
}

static struct super_operations f2fs_sops = {
	.alloc_inode	= f2fs_alloc_inode,
	.drop_inode	= f2fs_drop_inode,
	.destroy_inode	= f2fs_destroy_inode,
	.write_inode	= f2fs_write_inode,
	.dirty_inode	= f2fs_dirty_inode,
	.show_options	= f2fs_show_options,
	.evict_inode	= f2fs_evict_inode,
	.put_super	= f2fs_put_super,
	.sync_fs	= f2fs_sync_fs,
	.freeze_fs	= f2fs_freeze,
	.unfreeze_fs	= f2fs_unfreeze,
	.statfs		= f2fs_statfs,
	.remount_fs	= f2fs_remount,
};

static struct inode *f2fs_nfs_get_inode(struct super_block *sb,
		u64 ino, u32 generation)
{
	struct f2fs_sb_info *sbi = F2FS_SB(sb);
	struct inode *inode;

	if (ino < F2FS_ROOT_INO(sbi))
		return ERR_PTR(-ESTALE);

	/*
	 * f2fs_iget isn't quite right if the inode is currently unallocated!
	 * However f2fs_iget currently does appropriate checks to handle stale
	 * inodes so everything is OK.
	 */
	inode = f2fs_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (generation && inode->i_generation != generation) {
		/* we didn't find the right inode.. */
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return inode;
}

static struct dentry *f2fs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    f2fs_nfs_get_inode);
}

static struct dentry *f2fs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    f2fs_nfs_get_inode);
}

static const struct export_operations f2fs_export_ops = {
	.fh_to_dentry = f2fs_fh_to_dentry,
	.fh_to_parent = f2fs_fh_to_parent,
	.get_parent = f2fs_get_parent,
};

static loff_t max_file_size(unsigned bits)
{
	loff_t result = (DEF_ADDRS_PER_INODE - F2FS_INLINE_XATTR_ADDRS);
	loff_t leaf_count = ADDRS_PER_BLOCK;

	/* two direct node blocks */
	result += (leaf_count * 2);

	/* two indirect node blocks */
	leaf_count *= NIDS_PER_BLOCK;
	result += (leaf_count * 2);

	/* one double indirect node block */
	leaf_count *= NIDS_PER_BLOCK;
	result += leaf_count;

	result <<= bits;
	return result;
}

static int sanity_check_raw_super(struct super_block *sb,
			struct f2fs_super_block *raw_super)
{
	unsigned int blocksize;

	if (F2FS_SUPER_MAGIC != le32_to_cpu(raw_super->magic)) {
		f2fs_msg(sb, KERN_INFO,
			"Magic Mismatch, valid(0x%x) - read(0x%x)",
			F2FS_SUPER_MAGIC, le32_to_cpu(raw_super->magic));
		return 1;
	}

	/* Currently, support only 4KB page cache size */
	if (F2FS_BLKSIZE != PAGE_CACHE_SIZE) {
		f2fs_msg(sb, KERN_INFO,
			"Invalid page_cache_size (%lu), supports only 4KB\n",
			PAGE_CACHE_SIZE);
		return 1;
	}

	/* Currently, support only 4KB block size */
	blocksize = 1 << le32_to_cpu(raw_super->log_blocksize);
	if (blocksize != F2FS_BLKSIZE) {
		f2fs_msg(sb, KERN_INFO,
			"Invalid blocksize (%u), supports only 4KB\n",
			blocksize);
		return 1;
	}

	if (le32_to_cpu(raw_super->log_sectorsize) !=
					F2FS_LOG_SECTOR_SIZE) {
		f2fs_msg(sb, KERN_INFO, "Invalid log sectorsize");
		return 1;
	}
	if (le32_to_cpu(raw_super->log_sectors_per_block) !=
					F2FS_LOG_SECTORS_PER_BLOCK) {
		f2fs_msg(sb, KERN_INFO, "Invalid log sectors per block");
		return 1;
	}
	return 0;
}

static int sanity_check_ckpt(struct f2fs_sb_info *sbi)
{
	unsigned int total, fsmeta;
	struct f2fs_super_block *raw_super = F2FS_RAW_SUPER(sbi);
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);

	total = le32_to_cpu(raw_super->segment_count);
	fsmeta = le32_to_cpu(raw_super->segment_count_ckpt);
	fsmeta += le32_to_cpu(raw_super->segment_count_sit);
	fsmeta += le32_to_cpu(raw_super->segment_count_nat);
	fsmeta += le32_to_cpu(ckpt->rsvd_segment_count);
	fsmeta += le32_to_cpu(raw_super->segment_count_ssa);

	if (fsmeta >= total)
		return 1;

	if (is_set_ckpt_flags(ckpt, CP_ERROR_FLAG)) {
		f2fs_msg(sbi->sb, KERN_ERR, "A bug case: need to run fsck");
		return 1;
	}
	return 0;
}

static void init_sb_info(struct f2fs_sb_info *sbi)
{
	struct f2fs_super_block *raw_super = sbi->raw_super;
	int i;

	sbi->log_sectors_per_block =
		le32_to_cpu(raw_super->log_sectors_per_block);
	sbi->log_blocksize = le32_to_cpu(raw_super->log_blocksize);
	sbi->blocksize = 1 << sbi->log_blocksize;
	sbi->log_blocks_per_seg = le32_to_cpu(raw_super->log_blocks_per_seg);
	sbi->blocks_per_seg = 1 << sbi->log_blocks_per_seg;
	sbi->segs_per_sec = le32_to_cpu(raw_super->segs_per_sec);
	sbi->secs_per_zone = le32_to_cpu(raw_super->secs_per_zone);
	sbi->total_sections = le32_to_cpu(raw_super->section_count);
	sbi->total_node_count =
		(le32_to_cpu(raw_super->segment_count_nat) / 2)
			* sbi->blocks_per_seg * NAT_ENTRY_PER_BLOCK;
	sbi->root_ino_num = le32_to_cpu(raw_super->root_ino);
	sbi->node_ino_num = le32_to_cpu(raw_super->node_ino);
	sbi->meta_ino_num = le32_to_cpu(raw_super->meta_ino);
	sbi->cur_victim_sec = NULL_SECNO;

	for (i = 0; i < NR_COUNT_TYPE; i++)
		atomic_set(&sbi->nr_pages[i], 0);
}

static int validate_superblock(struct super_block *sb,
		struct f2fs_super_block **raw_super,
		struct buffer_head **raw_super_buf, sector_t block)
{
	const char *super = (block == 0 ? "first" : "second");

	/* read f2fs raw super block */
	*raw_super_buf = sb_bread(sb, block);
	if (!*raw_super_buf) {
		f2fs_msg(sb, KERN_ERR, "unable to read %s superblock",
				super);
		return -EIO;
	}

	*raw_super = (struct f2fs_super_block *)
		((char *)(*raw_super_buf)->b_data + F2FS_SUPER_OFFSET);

	/* sanity checking of raw super */
	if (!sanity_check_raw_super(sb, *raw_super))
		return 0;

	f2fs_msg(sb, KERN_ERR, "Can't find a valid F2FS filesystem "
				"in %s superblock", super);
	return -EINVAL;
}

static int f2fs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct f2fs_sb_info *sbi;
	struct f2fs_super_block *raw_super;
	struct buffer_head *raw_super_buf;
	struct inode *root;
	long err = -EINVAL;

	/* allocate memory for f2fs-specific super block info */
	sbi = kzalloc(sizeof(struct f2fs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	/* set a block size */
	if (!sb_set_blocksize(sb, F2FS_BLKSIZE)) {
		f2fs_msg(sb, KERN_ERR, "unable to set blocksize");
		goto free_sbi;
	}

	err = validate_superblock(sb, &raw_super, &raw_super_buf, 0);
	if (err) {
		brelse(raw_super_buf);
		/* check secondary superblock when primary failed */
		err = validate_superblock(sb, &raw_super, &raw_super_buf, 1);
		if (err)
			goto free_sb_buf;
	}
	sb->s_fs_info = sbi;
	/* init some FS parameters */
	sbi->active_logs = NR_CURSEG_TYPE;

	set_opt(sbi, BG_GC);

#ifdef CONFIG_F2FS_FS_XATTR
	set_opt(sbi, XATTR_USER);
#endif
#ifdef CONFIG_F2FS_FS_POSIX_ACL
	set_opt(sbi, POSIX_ACL);
#endif
	/* parse mount options */
	err = parse_options(sb, (char *)data);
	if (err)
		goto free_sb_buf;

	sb->s_maxbytes = max_file_size(le32_to_cpu(raw_super->log_blocksize));
	sb->s_max_links = F2FS_LINK_MAX;
	get_random_bytes(&sbi->s_next_generation, sizeof(u32));

	sb->s_op = &f2fs_sops;
	sb->s_xattr = f2fs_xattr_handlers;
	sb->s_export_op = &f2fs_export_ops;
	sb->s_magic = F2FS_SUPER_MAGIC;
	sb->s_time_gran = 1;
	sb->s_flags = (sb->s_flags & ~MS_POSIXACL) |
		(test_opt(sbi, POSIX_ACL) ? MS_POSIXACL : 0);
	memcpy(sb->s_uuid, raw_super->uuid, sizeof(raw_super->uuid));

	/* init f2fs-specific super block info */
	sbi->sb = sb;
	sbi->raw_super = raw_super;
	sbi->raw_super_buf = raw_super_buf;
	mutex_init(&sbi->gc_mutex);
	mutex_init(&sbi->writepages);
	mutex_init(&sbi->cp_mutex);
	mutex_init(&sbi->node_write);
	sbi->por_doing = 0;
	spin_lock_init(&sbi->stat_lock);
	init_rwsem(&sbi->bio_sem);
	init_rwsem(&sbi->cp_rwsem);
	init_waitqueue_head(&sbi->cp_wait);
	init_sb_info(sbi);

	/* get an inode for meta space */
	sbi->meta_inode = f2fs_iget(sb, F2FS_META_INO(sbi));
	if (IS_ERR(sbi->meta_inode)) {
		f2fs_msg(sb, KERN_ERR, "Failed to read F2FS meta data inode");
		err = PTR_ERR(sbi->meta_inode);
		goto free_sb_buf;
	}

	err = get_valid_checkpoint(sbi);
	if (err) {
		f2fs_msg(sb, KERN_ERR, "Failed to get valid F2FS checkpoint");
		goto free_meta_inode;
	}

	/* sanity checking of checkpoint */
	err = -EINVAL;
	if (sanity_check_ckpt(sbi)) {
		f2fs_msg(sb, KERN_ERR, "Invalid F2FS checkpoint");
		goto free_cp;
	}

	sbi->total_valid_node_count =
				le32_to_cpu(sbi->ckpt->valid_node_count);
	sbi->total_valid_inode_count =
				le32_to_cpu(sbi->ckpt->valid_inode_count);
	sbi->user_block_count = le64_to_cpu(sbi->ckpt->user_block_count);
	sbi->total_valid_block_count =
				le64_to_cpu(sbi->ckpt->valid_block_count);
	sbi->last_valid_block_count = sbi->total_valid_block_count;
	sbi->alloc_valid_block_count = 0;
	INIT_LIST_HEAD(&sbi->dir_inode_list);
	spin_lock_init(&sbi->dir_inode_lock);

	init_orphan_info(sbi);

	/* setup f2fs internal modules */
	err = build_segment_manager(sbi);
	if (err) {
		f2fs_msg(sb, KERN_ERR,
			"Failed to initialize F2FS segment manager");
		goto free_sm;
	}
	err = build_node_manager(sbi);
	if (err) {
		f2fs_msg(sb, KERN_ERR,
			"Failed to initialize F2FS node manager");
		goto free_nm;
	}

	build_gc_manager(sbi);

	/* get an inode for node space */
	sbi->node_inode = f2fs_iget(sb, F2FS_NODE_INO(sbi));
	if (IS_ERR(sbi->node_inode)) {
		f2fs_msg(sb, KERN_ERR, "Failed to read node inode");
		err = PTR_ERR(sbi->node_inode);
		goto free_nm;
	}

	/* if there are nt orphan nodes free them */
	err = -EINVAL;
	if (recover_orphan_inodes(sbi))
		goto free_node_inode;

	/* read root inode and dentry */
	root = f2fs_iget(sb, F2FS_ROOT_INO(sbi));
	if (IS_ERR(root)) {
		f2fs_msg(sb, KERN_ERR, "Failed to read root inode");
		err = PTR_ERR(root);
		goto free_node_inode;
	}
	if (!S_ISDIR(root->i_mode) || !root->i_blocks || !root->i_size)
		goto free_root_inode;

	sb->s_root = d_make_root(root); /* allocate root dentry */
	if (!sb->s_root) {
		err = -ENOMEM;
		goto free_root_inode;
	}

	/* recover fsynced data */
	if (!test_opt(sbi, DISABLE_ROLL_FORWARD)) {
		err = recover_fsync_data(sbi);
		if (err)
			f2fs_msg(sb, KERN_ERR,
				"Cannot recover all fsync data errno=%ld", err);
	}

	/*
	 * If filesystem is not mounted as read-only then
	 * do start the gc_thread.
	 */
	if (!(sb->s_flags & MS_RDONLY)) {
		/* After POR, we can run background GC thread.*/
		err = start_gc_thread(sbi);
		if (err)
			goto fail;
	}

	err = f2fs_build_stats(sbi);
	if (err)
		goto fail;

	if (f2fs_proc_root)
		sbi->s_proc = proc_mkdir(sb->s_id, f2fs_proc_root);

	if (sbi->s_proc)
		proc_create_data("segment_info", S_IRUGO, sbi->s_proc,
				 &f2fs_seq_segment_info_fops, sb);

	if (test_opt(sbi, DISCARD)) {
		struct request_queue *q = bdev_get_queue(sb->s_bdev);
		if (!blk_queue_discard(q))
			f2fs_msg(sb, KERN_WARNING,
					"mounting with \"discard\" option, but "
					"the device does not support discard");
	}

	sbi->s_kobj.kset = f2fs_kset;
	init_completion(&sbi->s_kobj_unregister);
	err = kobject_init_and_add(&sbi->s_kobj, &f2fs_ktype, NULL,
							"%s", sb->s_id);
	if (err)
		goto fail;

	return 0;
fail:
	stop_gc_thread(sbi);
free_root_inode:
	dput(sb->s_root);
	sb->s_root = NULL;
free_node_inode:
	iput(sbi->node_inode);
free_nm:
	destroy_node_manager(sbi);
free_sm:
	destroy_segment_manager(sbi);
free_cp:
	kfree(sbi->ckpt);
free_meta_inode:
	make_bad_inode(sbi->meta_inode);
	iput(sbi->meta_inode);
free_sb_buf:
	brelse(raw_super_buf);
free_sbi:
	kfree(sbi);
	return err;
}

static struct dentry *f2fs_mount(struct file_system_type *fs_type, int flags,
			const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, f2fs_fill_super);
}

static struct file_system_type f2fs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "f2fs",
	.mount		= f2fs_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("f2fs");

static int __init init_inodecache(void)
{
	f2fs_inode_cachep = f2fs_kmem_cache_create("f2fs_inode_cache",
			sizeof(struct f2fs_inode_info), NULL);
	if (f2fs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(f2fs_inode_cachep);
}

static int __init init_f2fs_fs(void)
{
	int err;

	err = init_inodecache();
	if (err)
		goto fail;
	err = create_node_manager_caches();
	if (err)
		goto free_inodecache;
	err = create_gc_caches();
	if (err)
		goto free_node_manager_caches;
	err = create_checkpoint_caches();
	if (err)
		goto free_gc_caches;
	f2fs_kset = kset_create_and_add("f2fs", NULL, fs_kobj);
	if (!f2fs_kset) {
		err = -ENOMEM;
		goto free_checkpoint_caches;
	}
	err = register_filesystem(&f2fs_fs_type);
	if (err)
		goto free_kset;
	f2fs_create_root_stats();
	f2fs_proc_root = proc_mkdir("fs/f2fs", NULL);
	return 0;

free_kset:
	kset_unregister(f2fs_kset);
free_checkpoint_caches:
	destroy_checkpoint_caches();
free_gc_caches:
	destroy_gc_caches();
free_node_manager_caches:
	destroy_node_manager_caches();
free_inodecache:
	destroy_inodecache();
fail:
	return err;
}

static void __exit exit_f2fs_fs(void)
{
	remove_proc_entry("fs/f2fs", NULL);
	f2fs_destroy_root_stats();
	unregister_filesystem(&f2fs_fs_type);
	destroy_checkpoint_caches();
	destroy_gc_caches();
	destroy_node_manager_caches();
	destroy_inodecache();
	kset_unregister(f2fs_kset);
}

module_init(init_f2fs_fs)
module_exit(exit_f2fs_fs)

MODULE_AUTHOR("Samsung Electronics's Praesto Team");
MODULE_DESCRIPTION("Flash Friendly File System");
MODULE_LICENSE("GPL");
