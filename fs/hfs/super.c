/*
 *  linux/fs/hfs/super.c
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains hfs_read_super(), some of the super_ops and
 * init_hfs_fs() and exit_hfs_fs().  The remaining super_ops are in
 * inode.c since they deal with inodes.
 *
 * Based on the minix file system code, (C) 1991, 1992 by Linus Torvalds
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/nls.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vfs.h>

#include "hfs_fs.h"
#include "btree.h"

static struct kmem_cache *hfs_inode_cachep;

MODULE_DESCRIPTION("Apple Macintosh file system support");
MODULE_LICENSE("GPL");

static int hfs_sync_fs(struct super_block *sb, int wait)
{
	hfs_mdb_commit(sb);
	return 0;
}

/*
 * hfs_put_super()
 *
 * This is the put_super() entry in the super_operations structure for
 * HFS filesystems.  The purpose is to release the resources
 * associated with the superblock sb.
 */
static void hfs_put_super(struct super_block *sb)
{
	cancel_delayed_work_sync(&HFS_SB(sb)->mdb_work);
	hfs_mdb_close(sb);
	/* release the MDB's resources */
	hfs_mdb_put(sb);
}

static void flush_mdb(struct work_struct *work)
{
	struct hfs_sb_info *sbi;
	struct super_block *sb;

	sbi = container_of(work, struct hfs_sb_info, mdb_work.work);
	sb = sbi->sb;

	spin_lock(&sbi->work_lock);
	sbi->work_queued = 0;
	spin_unlock(&sbi->work_lock);

	hfs_mdb_commit(sb);
}

void hfs_mark_mdb_dirty(struct super_block *sb)
{
	struct hfs_sb_info *sbi = HFS_SB(sb);
	unsigned long delay;

	if (sb_rdonly(sb))
		return;

	spin_lock(&sbi->work_lock);
	if (!sbi->work_queued) {
		delay = msecs_to_jiffies(dirty_writeback_interval * 10);
		queue_delayed_work(system_long_wq, &sbi->mdb_work, delay);
		sbi->work_queued = 1;
	}
	spin_unlock(&sbi->work_lock);
}

/*
 * hfs_statfs()
 *
 * This is the statfs() entry in the super_operations structure for
 * HFS filesystems.  The purpose is to return various data about the
 * filesystem.
 *
 * changed f_files/f_ffree to reflect the fs_ablock/free_ablocks.
 */
static int hfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	buf->f_type = HFS_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = (u32)HFS_SB(sb)->fs_ablocks * HFS_SB(sb)->fs_div;
	buf->f_bfree = (u32)HFS_SB(sb)->free_ablocks * HFS_SB(sb)->fs_div;
	buf->f_bavail = buf->f_bfree;
	buf->f_files = HFS_SB(sb)->fs_ablocks;
	buf->f_ffree = HFS_SB(sb)->free_ablocks;
	buf->f_fsid = u64_to_fsid(id);
	buf->f_namelen = HFS_NAMELEN;

	return 0;
}

static int hfs_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;

	sync_filesystem(sb);
	fc->sb_flags |= SB_NODIRATIME;
	if ((bool)(fc->sb_flags & SB_RDONLY) == sb_rdonly(sb))
		return 0;

	if (!(fc->sb_flags & SB_RDONLY)) {
		if (!(HFS_SB(sb)->mdb->drAtrb & cpu_to_be16(HFS_SB_ATTRIB_UNMNT))) {
			pr_warn("filesystem was not cleanly unmounted, running fsck.hfs is recommended.  leaving read-only.\n");
			sb->s_flags |= SB_RDONLY;
			fc->sb_flags |= SB_RDONLY;
		} else if (HFS_SB(sb)->mdb->drAtrb & cpu_to_be16(HFS_SB_ATTRIB_SLOCK)) {
			pr_warn("filesystem is marked locked, leaving read-only.\n");
			sb->s_flags |= SB_RDONLY;
			fc->sb_flags |= SB_RDONLY;
		}
	}
	return 0;
}

static int hfs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct hfs_sb_info *sbi = HFS_SB(root->d_sb);

	if (sbi->s_creator != cpu_to_be32(0x3f3f3f3f))
		seq_show_option_n(seq, "creator", (char *)&sbi->s_creator, 4);
	if (sbi->s_type != cpu_to_be32(0x3f3f3f3f))
		seq_show_option_n(seq, "type", (char *)&sbi->s_type, 4);
	seq_printf(seq, ",uid=%u,gid=%u",
			from_kuid_munged(&init_user_ns, sbi->s_uid),
			from_kgid_munged(&init_user_ns, sbi->s_gid));
	if (sbi->s_file_umask != 0133)
		seq_printf(seq, ",file_umask=%o", sbi->s_file_umask);
	if (sbi->s_dir_umask != 0022)
		seq_printf(seq, ",dir_umask=%o", sbi->s_dir_umask);
	if (sbi->part >= 0)
		seq_printf(seq, ",part=%u", sbi->part);
	if (sbi->session >= 0)
		seq_printf(seq, ",session=%u", sbi->session);
	if (sbi->nls_disk)
		seq_printf(seq, ",codepage=%s", sbi->nls_disk->charset);
	if (sbi->nls_io)
		seq_printf(seq, ",iocharset=%s", sbi->nls_io->charset);
	if (sbi->s_quiet)
		seq_printf(seq, ",quiet");
	return 0;
}

static struct inode *hfs_alloc_inode(struct super_block *sb)
{
	struct hfs_inode_info *i;

	i = alloc_inode_sb(sb, hfs_inode_cachep, GFP_KERNEL);
	return i ? &i->vfs_inode : NULL;
}

static void hfs_free_inode(struct inode *inode)
{
	kmem_cache_free(hfs_inode_cachep, HFS_I(inode));
}

static const struct super_operations hfs_super_operations = {
	.alloc_inode	= hfs_alloc_inode,
	.free_inode	= hfs_free_inode,
	.write_inode	= hfs_write_inode,
	.evict_inode	= hfs_evict_inode,
	.put_super	= hfs_put_super,
	.sync_fs	= hfs_sync_fs,
	.statfs		= hfs_statfs,
	.show_options	= hfs_show_options,
};

enum {
	opt_uid, opt_gid, opt_umask, opt_file_umask, opt_dir_umask,
	opt_part, opt_session, opt_type, opt_creator, opt_quiet,
	opt_codepage, opt_iocharset,
};

static const struct fs_parameter_spec hfs_param_spec[] = {
	fsparam_u32	("uid",		opt_uid),
	fsparam_u32	("gid",		opt_gid),
	fsparam_u32oct	("umask",	opt_umask),
	fsparam_u32oct	("file_umask",	opt_file_umask),
	fsparam_u32oct	("dir_umask",	opt_dir_umask),
	fsparam_u32	("part",	opt_part),
	fsparam_u32	("session",	opt_session),
	fsparam_string	("type",	opt_type),
	fsparam_string	("creator",	opt_creator),
	fsparam_flag	("quiet",	opt_quiet),
	fsparam_string	("codepage",	opt_codepage),
	fsparam_string	("iocharset",	opt_iocharset),
	{}
};

/*
 * hfs_parse_param()
 *
 * This function is called by the vfs to parse the mount options.
 */
static int hfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct hfs_sb_info *hsb = fc->s_fs_info;
	struct fs_parse_result result;
	int opt;

	/* hfs does not honor any fs-specific options on remount */
	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE)
		return 0;

	opt = fs_parse(fc, hfs_param_spec, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case opt_uid:
		hsb->s_uid = result.uid;
		break;
	case opt_gid:
		hsb->s_gid = result.gid;
		break;
	case opt_umask:
		hsb->s_file_umask = (umode_t)result.uint_32;
		hsb->s_dir_umask = (umode_t)result.uint_32;
		break;
	case opt_file_umask:
		hsb->s_file_umask = (umode_t)result.uint_32;
		break;
	case opt_dir_umask:
		hsb->s_dir_umask = (umode_t)result.uint_32;
		break;
	case opt_part:
		hsb->part = result.uint_32;
		break;
	case opt_session:
		hsb->session = result.uint_32;
		break;
	case opt_type:
		if (strlen(param->string) != 4) {
			pr_err("type requires a 4 character value\n");
			return -EINVAL;
		}
		memcpy(&hsb->s_type, param->string, 4);
		break;
	case opt_creator:
		if (strlen(param->string) != 4) {
			pr_err("creator requires a 4 character value\n");
			return -EINVAL;
		}
		memcpy(&hsb->s_creator, param->string, 4);
		break;
	case opt_quiet:
		hsb->s_quiet = 1;
		break;
	case opt_codepage:
		if (hsb->nls_disk) {
			pr_err("unable to change codepage\n");
			return -EINVAL;
		}
		hsb->nls_disk = load_nls(param->string);
		if (!hsb->nls_disk) {
			pr_err("unable to load codepage \"%s\"\n",
					param->string);
			return -EINVAL;
		}
		break;
	case opt_iocharset:
		if (hsb->nls_io) {
			pr_err("unable to change iocharset\n");
			return -EINVAL;
		}
		hsb->nls_io = load_nls(param->string);
		if (!hsb->nls_io) {
			pr_err("unable to load iocharset \"%s\"\n",
					param->string);
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * hfs_read_super()
 *
 * This is the function that is responsible for mounting an HFS
 * filesystem.	It performs all the tasks necessary to get enough data
 * from the disk to read the root inode.  This includes parsing the
 * mount options, dealing with Macintosh partitions, reading the
 * superblock and the allocation bitmap blocks, calling
 * hfs_btree_init() to get the necessary data about the extents and
 * catalog B-trees and, finally, reading the root inode into memory.
 */
static int hfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct hfs_sb_info *sbi = HFS_SB(sb);
	struct hfs_find_data fd;
	hfs_cat_rec rec;
	struct inode *root_inode;
	int silent = fc->sb_flags & SB_SILENT;
	int res;

	atomic64_set(&sbi->file_count, 0);
	atomic64_set(&sbi->folder_count, 0);
	atomic64_set(&sbi->next_id, 0);

	/* load_nls_default does not fail */
	if (sbi->nls_disk && !sbi->nls_io)
		sbi->nls_io = load_nls_default();
	sbi->s_dir_umask &= 0777;
	sbi->s_file_umask &= 0577;

	spin_lock_init(&sbi->work_lock);
	INIT_DELAYED_WORK(&sbi->mdb_work, flush_mdb);

	sbi->sb = sb;
	sb->s_op = &hfs_super_operations;
	sb->s_xattr = hfs_xattr_handlers;
	sb->s_flags |= SB_NODIRATIME;
	mutex_init(&sbi->bitmap_lock);

	res = hfs_mdb_get(sb);
	if (res) {
		if (!silent)
			pr_warn("can't find a HFS filesystem on dev %s\n",
				hfs_mdb_name(sb));
		res = -EINVAL;
		goto bail;
	}

	/* try to get the root inode */
	res = hfs_find_init(HFS_SB(sb)->cat_tree, &fd);
	if (res)
		goto bail_no_root;
	res = hfs_cat_find_brec(sb, HFS_ROOT_CNID, &fd);
	if (!res) {
		if (fd.entrylength != sizeof(rec.dir)) {
			res =  -EIO;
			goto bail_hfs_find;
		}
		hfs_bnode_read(fd.bnode, &rec, fd.entryoffset, fd.entrylength);
		if (rec.type != HFS_CDR_DIR)
			res = -EIO;
	}
	if (res)
		goto bail_hfs_find;
	res = -EINVAL;
	root_inode = hfs_iget(sb, &fd.search_key->cat, &rec);
	hfs_find_exit(&fd);
	if (!root_inode)
		goto bail_no_root;

	set_default_d_op(sb, &hfs_dentry_operations);
	res = -ENOMEM;
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root)
		goto bail_no_root;

	/* everything's okay */
	return 0;

bail_hfs_find:
	hfs_find_exit(&fd);
bail_no_root:
	pr_err("get root inode failed\n");
bail:
	hfs_mdb_put(sb);
	return res;
}

static int hfs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, hfs_fill_super);
}

static void hfs_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations hfs_context_ops = {
	.parse_param	= hfs_parse_param,
	.get_tree	= hfs_get_tree,
	.reconfigure	= hfs_reconfigure,
	.free		= hfs_free_fc,
};

static int hfs_init_fs_context(struct fs_context *fc)
{
	struct hfs_sb_info *hsb;

	hsb = kzalloc(sizeof(struct hfs_sb_info), GFP_KERNEL);
	if (!hsb)
		return -ENOMEM;

	fc->s_fs_info = hsb;
	fc->ops = &hfs_context_ops;

	if (fc->purpose != FS_CONTEXT_FOR_RECONFIGURE) {
		/* initialize options with defaults */
		hsb->s_uid = current_uid();
		hsb->s_gid = current_gid();
		hsb->s_file_umask = 0133;
		hsb->s_dir_umask = 0022;
		hsb->s_type = cpu_to_be32(0x3f3f3f3f); /* == '????' */
		hsb->s_creator = cpu_to_be32(0x3f3f3f3f); /* == '????' */
		hsb->s_quiet = 0;
		hsb->part = -1;
		hsb->session = -1;
	}

	return 0;
}

static struct file_system_type hfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "hfs",
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
	.init_fs_context = hfs_init_fs_context,
};
MODULE_ALIAS_FS("hfs");

static void hfs_init_once(void *p)
{
	struct hfs_inode_info *i = p;

	inode_init_once(&i->vfs_inode);
}

static int __init init_hfs_fs(void)
{
	int err;

	hfs_inode_cachep = kmem_cache_create("hfs_inode_cache",
		sizeof(struct hfs_inode_info), 0,
		SLAB_HWCACHE_ALIGN|SLAB_ACCOUNT, hfs_init_once);
	if (!hfs_inode_cachep)
		return -ENOMEM;
	err = register_filesystem(&hfs_fs_type);
	if (err)
		kmem_cache_destroy(hfs_inode_cachep);
	return err;
}

static void __exit exit_hfs_fs(void)
{
	unregister_filesystem(&hfs_fs_type);

	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(hfs_inode_cachep);
}

module_init(init_hfs_fs)
module_exit(exit_hfs_fs)
