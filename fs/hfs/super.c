/*
 *  linux/fs/hfs/super.c
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains hfs_read_super(), some of the super_ops and
 * init_module() and cleanup_module().	The remaining super_ops are in
 * inode.c since they deal with inodes.
 *
 * Based on the minix file system code, (C) 1991, 1992 by Linus Torvalds
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/nls.h>
#include <linux/parser.h>
#include <linux/seq_file.h>
#include <linux/vfs.h>

#include "hfs_fs.h"
#include "btree.h"

static struct kmem_cache *hfs_inode_cachep;

MODULE_LICENSE("GPL");

/*
 * hfs_write_super()
 *
 * Description:
 *   This function is called by the VFS only. When the filesystem
 *   is mounted r/w it updates the MDB on disk.
 * Input Variable(s):
 *   struct super_block *sb: Pointer to the hfs superblock
 * Output Variable(s):
 *   NONE
 * Returns:
 *   void
 * Preconditions:
 *   'sb' points to a "valid" (struct super_block).
 * Postconditions:
 *   The MDB is marked 'unsuccessfully unmounted' by clearing bit 8 of drAtrb
 *   (hfs_put_super() must set this flag!). Some MDB fields are updated
 *   and the MDB buffer is written to disk by calling hfs_mdb_commit().
 */
static void hfs_write_super(struct super_block *sb)
{
	sb->s_dirt = 0;
	if (sb->s_flags & MS_RDONLY)
		return;
	/* sync everything to the buffers */
	hfs_mdb_commit(sb);
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
	hfs_mdb_close(sb);
	/* release the MDB's resources */
	hfs_mdb_put(sb);
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

	buf->f_type = HFS_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = (u32)HFS_SB(sb)->fs_ablocks * HFS_SB(sb)->fs_div;
	buf->f_bfree = (u32)HFS_SB(sb)->free_ablocks * HFS_SB(sb)->fs_div;
	buf->f_bavail = buf->f_bfree;
	buf->f_files = HFS_SB(sb)->fs_ablocks;
	buf->f_ffree = HFS_SB(sb)->free_ablocks;
	buf->f_namelen = HFS_NAMELEN;

	return 0;
}

static int hfs_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= MS_NODIRATIME;
	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		return 0;
	if (!(*flags & MS_RDONLY)) {
		if (!(HFS_SB(sb)->mdb->drAtrb & cpu_to_be16(HFS_SB_ATTRIB_UNMNT))) {
			printk(KERN_WARNING "hfs: filesystem was not cleanly unmounted, "
			       "running fsck.hfs is recommended.  leaving read-only.\n");
			sb->s_flags |= MS_RDONLY;
			*flags |= MS_RDONLY;
		} else if (HFS_SB(sb)->mdb->drAtrb & cpu_to_be16(HFS_SB_ATTRIB_SLOCK)) {
			printk(KERN_WARNING "hfs: filesystem is marked locked, leaving read-only.\n");
			sb->s_flags |= MS_RDONLY;
			*flags |= MS_RDONLY;
		}
	}
	return 0;
}

static int hfs_show_options(struct seq_file *seq, struct vfsmount *mnt)
{
	struct hfs_sb_info *sbi = HFS_SB(mnt->mnt_sb);

	if (sbi->s_creator != cpu_to_be32(0x3f3f3f3f))
		seq_printf(seq, ",creator=%.4s", (char *)&sbi->s_creator);
	if (sbi->s_type != cpu_to_be32(0x3f3f3f3f))
		seq_printf(seq, ",type=%.4s", (char *)&sbi->s_type);
	seq_printf(seq, ",uid=%u,gid=%u", sbi->s_uid, sbi->s_gid);
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

static void hfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(hfs_inode_cachep, HFS_I(inode));
}

static const struct super_operations hfs_super_operations = {
	.alloc_inode	= hfs_alloc_inode,
	.destroy_inode	= hfs_destroy_inode,
	.write_inode	= hfs_write_inode,
	.clear_inode	= hfs_clear_inode,
	.put_super	= hfs_put_super,
	.write_super	= hfs_write_super,
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

static match_table_t tokens = {
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
	hsb->s_uid = current->uid;
	hsb->s_gid = current->gid;
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
				printk(KERN_ERR "hfs: uid requires an argument\n");
				return 0;
			}
			hsb->s_uid = (uid_t)tmp;
			break;
		case opt_gid:
			if (match_int(&args[0], &tmp)) {
				printk(KERN_ERR "hfs: gid requires an argument\n");
				return 0;
			}
			hsb->s_gid = (gid_t)tmp;
			break;
		case opt_umask:
			if (match_octal(&args[0], &tmp)) {
				printk(KERN_ERR "hfs: umask requires a value\n");
				return 0;
			}
			hsb->s_file_umask = (umode_t)tmp;
			hsb->s_dir_umask = (umode_t)tmp;
			break;
		case opt_file_umask:
			if (match_octal(&args[0], &tmp)) {
				printk(KERN_ERR "hfs: file_umask requires a value\n");
				return 0;
			}
			hsb->s_file_umask = (umode_t)tmp;
			break;
		case opt_dir_umask:
			if (match_octal(&args[0], &tmp)) {
				printk(KERN_ERR "hfs: dir_umask requires a value\n");
				return 0;
			}
			hsb->s_dir_umask = (umode_t)tmp;
			break;
		case opt_part:
			if (match_int(&args[0], &hsb->part)) {
				printk(KERN_ERR "hfs: part requires an argument\n");
				return 0;
			}
			break;
		case opt_session:
			if (match_int(&args[0], &hsb->session)) {
				printk(KERN_ERR "hfs: session requires an argument\n");
				return 0;
			}
			break;
		case opt_type:
			if (match_fourchar(&args[0], &hsb->s_type)) {
				printk(KERN_ERR "hfs: type requires a 4 character value\n");
				return 0;
			}
			break;
		case opt_creator:
			if (match_fourchar(&args[0], &hsb->s_creator)) {
				printk(KERN_ERR "hfs: creator requires a 4 character value\n");
				return 0;
			}
			break;
		case opt_quiet:
			hsb->s_quiet = 1;
			break;
		case opt_codepage:
			if (hsb->nls_disk) {
				printk(KERN_ERR "hfs: unable to change codepage\n");
				return 0;
			}
			p = match_strdup(&args[0]);
			hsb->nls_disk = load_nls(p);
			if (!hsb->nls_disk) {
				printk(KERN_ERR "hfs: unable to load codepage \"%s\"\n", p);
				kfree(p);
				return 0;
			}
			kfree(p);
			break;
		case opt_iocharset:
			if (hsb->nls_io) {
				printk(KERN_ERR "hfs: unable to change iocharset\n");
				return 0;
			}
			p = match_strdup(&args[0]);
			hsb->nls_io = load_nls(p);
			if (!hsb->nls_io) {
				printk(KERN_ERR "hfs: unable to load iocharset \"%s\"\n", p);
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
			printk(KERN_ERR "hfs: unable to load default iocharset\n");
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
	sb->s_fs_info = sbi;
	INIT_HLIST_HEAD(&sbi->rsrc_inodes);

	res = -EINVAL;
	if (!parse_options((char *)data, sbi)) {
		printk(KERN_ERR "hfs: unable to parse mount options.\n");
		goto bail;
	}

	sb->s_op = &hfs_super_operations;
	sb->s_flags |= MS_NODIRATIME;
	init_MUTEX(&sbi->bitmap_lock);

	res = hfs_mdb_get(sb);
	if (res) {
		if (!silent)
			printk(KERN_WARNING "hfs: can't find a HFS filesystem on dev %s.\n",
				hfs_mdb_name(sb));
		res = -EINVAL;
		goto bail;
	}

	/* try to get the root inode */
	hfs_find_init(HFS_SB(sb)->cat_tree, &fd);
	res = hfs_cat_find_brec(sb, HFS_ROOT_CNID, &fd);
	if (!res)
		hfs_bnode_read(fd.bnode, &rec, fd.entryoffset, fd.entrylength);
	if (res) {
		hfs_find_exit(&fd);
		goto bail_no_root;
	}
	res = -EINVAL;
	root_inode = hfs_iget(sb, &fd.search_key->cat, &rec);
	hfs_find_exit(&fd);
	if (!root_inode)
		goto bail_no_root;

	res = -ENOMEM;
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root)
		goto bail_iput;

	sb->s_root->d_op = &hfs_dentry_operations;

	/* everything's okay */
	return 0;

bail_iput:
	iput(root_inode);
bail_no_root:
	printk(KERN_ERR "hfs: get root inode failed.\n");
bail:
	hfs_mdb_put(sb);
	return res;
}

static int hfs_get_sb(struct file_system_type *fs_type,
		      int flags, const char *dev_name, void *data,
		      struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, hfs_fill_super, mnt);
}

static struct file_system_type hfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "hfs",
	.get_sb		= hfs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static void hfs_init_once(void *p, struct kmem_cache *cachep, unsigned long flags)
{
	struct hfs_inode_info *i = p;

	if (flags & SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(&i->vfs_inode);
}

static int __init init_hfs_fs(void)
{
	int err;

	hfs_inode_cachep = kmem_cache_create("hfs_inode_cache",
		sizeof(struct hfs_inode_info), 0, SLAB_HWCACHE_ALIGN,
		hfs_init_once, NULL);
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
	kmem_cache_destroy(hfs_inode_cachep);
}

module_init(init_hfs_fs)
module_exit(exit_hfs_fs)
