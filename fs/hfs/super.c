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
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/nls.h>
#include <linux/parser.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vfs.h>

#include "hfs_fs.h"
#include "btree.h"

static struct kmem_cache *hfs_inode_cachep;

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
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	buf->f_namelen = HFS_NAMELEN;

	return 0;
}

static int hfs_remount(struct super_block *sb, int *flags, char *data)
{
	sync_filesystem(sb);
	*flags |= SB_NODIRATIME;
	if ((bool)(*flags & SB_RDONLY) == sb_rdonly(sb))
		return 0;
	if (!(*flags & SB_RDONLY)) {
		if (!(HFS_SB(sb)->mdb->drAtrb & cpu_to_be16(HFS_SB_ATTRIB_UNMNT))) {
			pr_warn("filesystem was not cleanly unmounted, running fsck.hfs is recommended.  leaving read-only.\n");
			sb->s_flags |= SB_RDONLY;
			*flags |= SB_RDONLY;
		} else if (HFS_SB(sb)->mdb->drAtrb & cpu_to_be16(HFS_SB_ATTRIB_SLOCK)) {
			pr_warn("filesystem is marked locked, leaving read-only.\n");
			sb->s_flags |= SB_RDONLY;
			*flags |= SB_RDONLY;
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

	i = kmem_cache_alloc(hfs_inode_cachep, GFP_KERNEL);
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
	.remount_fs     = hfs_remount,
	.show_options	= hfs_show_options,
};

enum {
	opt_uid, opt_gid, opt_umask, opt_file_umask, opt_dir_umask,
	opt_part, opt_session, opt_type, opt_creator, opt_quiet,
	opt_codepage, opt_iocharset,
	opt_err
};

static const match_table_t tokens = {
	{ opt_uid, "uid=%u" },
	{ opt_gid, "gid=%u" },
	{ opt_umask, "umask=%o" },
	{ opt_file_umask, "file_umask=%o" },
	{ opt_dir_umask, "dir_umask=%o" },
	{ opt_part, "part=%u" },
	{ opt_session, "session=%u" },
	{ opt_type, "type=%s" },
	{ opt_creator, "creator=%s" },
	{ opt_quiet, "quiet" },
	{ opt_codepage, "codepage=%s" },
	{ opt_iocharset, "iocharset=%s" },
	{ opt_err, NULL }
};

static inline int match_fourchar(substring_t *arg, u32 *result)
{
	if (arg->to - arg->from != 4)
		return -EINVAL;
	memcpy(result, arg->from, 4);
	return 0;
}

/*
 * parse_options()
 *
 * adapted from linux/fs/msdos/inode.c written 1992,93 by Werner Almesberger
 * This function is called by hfs_read_super() to parse the mount options.
 */
static int parse_options(char *options, struct hfs_sb_info *hsb)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int tmp, token;

	/* initialize the sb with defaults */
	hsb->s_uid = current_uid();
	hsb->s_gid = current_gid();
	hsb->s_file_umask = 0133;
	hsb->s_dir_umask = 0022;
	hsb->s_type = hsb->s_creator = cpu_to_be32(0x3f3f3f3f);	/* == '????' */
	hsb->s_quiet = 0;
	hsb->part = -1;
	hsb->session = -1;

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case opt_uid:
			if (match_int(&args[0], &tmp)) {
				pr_err("uid requires an argument\n");
				return 0;
			}
			hsb->s_uid = make_kuid(current_user_ns(), (uid_t)tmp);
			if (!uid_valid(hsb->s_uid)) {
				pr_err("invalid uid %d\n", tmp);
				return 0;
			}
			break;
		case opt_gid:
			if (match_int(&args[0], &tmp)) {
				pr_err("gid requires an argument\n");
				return 0;
			}
			hsb->s_gid = make_kgid(current_user_ns(), (gid_t)tmp);
			if (!gid_valid(hsb->s_gid)) {
				pr_err("invalid gid %d\n", tmp);
				return 0;
			}
			break;
		case opt_umask:
			if (match_octal(&args[0], &tmp)) {
				pr_err("umask requires a value\n");
				return 0;
			}
			hsb->s_file_umask = (umode_t)tmp;
			hsb->s_dir_umask = (umode_t)tmp;
			break;
		case opt_file_umask:
			if (match_octal(&args[0], &tmp)) {
				pr_err("file_umask requires a value\n");
				return 0;
			}
			hsb->s_file_umask = (umode_t)tmp;
			break;
		case opt_dir_umask:
			if (match_octal(&args[0], &tmp)) {
				pr_err("dir_umask requires a value\n");
				return 0;
			}
			hsb->s_dir_umask = (umode_t)tmp;
			break;
		case opt_part:
			if (match_int(&args[0], &hsb->part)) {
				pr_err("part requires an argument\n");
				return 0;
			}
			break;
		case opt_session:
			if (match_int(&args[0], &hsb->session)) {
				pr_err("session requires an argument\n");
				return 0;
			}
			break;
		case opt_type:
			if (match_fourchar(&args[0], &hsb->s_type)) {
				pr_err("type requires a 4 character value\n");
				return 0;
			}
			break;
		case opt_creator:
			if (match_fourchar(&args[0], &hsb->s_creator)) {
				pr_err("creator requires a 4 character value\n");
				return 0;
			}
			break;
		case opt_quiet:
			hsb->s_quiet = 1;
			break;
		case opt_codepage:
			if (hsb->nls_disk) {
				pr_err("unable to change codepage\n");
				return 0;
			}
			p = match_strdup(&args[0]);
			if (p)
				hsb->nls_disk = load_nls(p);
			if (!hsb->nls_disk) {
				pr_err("unable to load codepage \"%s\"\n", p);
				kfree(p);
				return 0;
			}
			kfree(p);
			break;
		case opt_iocharset:
			if (hsb->nls_io) {
				pr_err("unable to change iocharset\n");
				return 0;
			}
			p = match_strdup(&args[0]);
			if (p)
				hsb->nls_io = load_nls(p);
			if (!hsb->nls_io) {
				pr_err("unable to load iocharset \"%s\"\n", p);
				kfree(p);
				return 0;
			}
			kfree(p);
			break;
		default:
			return 0;
		}
	}

	if (hsb->nls_disk && !hsb->nls_io) {
		hsb->nls_io = load_nls_default();
		if (!hsb->nls_io) {
			pr_err("unable to load default iocharset\n");
			return 0;
		}
	}
	hsb->s_dir_umask &= 0777;
	hsb->s_file_umask &= 0577;

	return 1;
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
static int hfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct hfs_sb_info *sbi;
	struct hfs_find_data fd;
	hfs_cat_rec rec;
	struct inode *root_inode;
	int res;

	sbi = kzalloc(sizeof(struct hfs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sbi->sb = sb;
	sb->s_fs_info = sbi;
	spin_lock_init(&sbi->work_lock);
	INIT_DELAYED_WORK(&sbi->mdb_work, flush_mdb);

	res = -EINVAL;
	if (!parse_options((char *)data, sbi)) {
		pr_err("unable to parse mount options\n");
		goto bail;
	}

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
		if (fd.entrylength > sizeof(rec) || fd.entrylength < 0) {
			res =  -EIO;
			goto bail;
		}
		hfs_bnode_read(fd.bnode, &rec, fd.entryoffset, fd.entrylength);
	}
	if (res) {
		hfs_find_exit(&fd);
		goto bail_no_root;
	}
	res = -EINVAL;
	root_inode = hfs_iget(sb, &fd.search_key->cat, &rec);
	hfs_find_exit(&fd);
	if (!root_inode)
		goto bail_no_root;

	sb->s_d_op = &hfs_dentry_operations;
	res = -ENOMEM;
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root)
		goto bail_no_root;

	/* everything's okay */
	return 0;

bail_no_root:
	pr_err("get root inode failed\n");
bail:
	hfs_mdb_put(sb);
	return res;
}

static struct dentry *hfs_mount(struct file_system_type *fs_type,
		      int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, hfs_fill_super);
}

static struct file_system_type hfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "hfs",
	.mount		= hfs_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
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
