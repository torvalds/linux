// SPDX-License-Identifier: GPL-2.0
/*
 * super.c
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from work (c) 1995,1996 Christian Vogelgsang.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/exportfs.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/vfs.h>
#include <linux/blkdev.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include "efs.h"
#include <linux/efs_vh.h>
#include <linux/efs_fs_sb.h>

static int efs_statfs(struct dentry *dentry, struct kstatfs *buf);
static int efs_init_fs_context(struct fs_context *fc);

static void efs_kill_sb(struct super_block *s)
{
	struct efs_sb_info *sbi = SUPER_INFO(s);
	kill_block_super(s);
	kfree(sbi);
}

static struct pt_types sgi_pt_types[] = {
	{0x00,		"SGI vh"},
	{0x01,		"SGI trkrepl"},
	{0x02,		"SGI secrepl"},
	{0x03,		"SGI raw"},
	{0x04,		"SGI bsd"},
	{SGI_SYSV,	"SGI sysv"},
	{0x06,		"SGI vol"},
	{SGI_EFS,	"SGI efs"},
	{0x08,		"SGI lv"},
	{0x09,		"SGI rlv"},
	{0x0A,		"SGI xfs"},
	{0x0B,		"SGI xfslog"},
	{0x0C,		"SGI xlv"},
	{0x82,		"Linux swap"},
	{0x83,		"Linux native"},
	{0,		NULL}
};

enum {
	Opt_explicit_open,
};

static const struct fs_parameter_spec efs_param_spec[] = {
	fsparam_flag    ("explicit-open",       Opt_explicit_open),
	{}
};

/*
 * File system definition and registration.
 */
static struct file_system_type efs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "efs",
	.kill_sb		= efs_kill_sb,
	.fs_flags		= FS_REQUIRES_DEV,
	.init_fs_context	= efs_init_fs_context,
	.parameters		= efs_param_spec,
};
MODULE_ALIAS_FS("efs");

static struct kmem_cache * efs_inode_cachep;

static struct inode *efs_alloc_inode(struct super_block *sb)
{
	struct efs_inode_info *ei;
	ei = alloc_inode_sb(sb, efs_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void efs_free_inode(struct inode *inode)
{
	kmem_cache_free(efs_inode_cachep, INODE_INFO(inode));
}

static void init_once(void *foo)
{
	struct efs_inode_info *ei = (struct efs_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

static int __init init_inodecache(void)
{
	efs_inode_cachep = kmem_cache_create("efs_inode_cache",
				sizeof(struct efs_inode_info), 0,
				SLAB_RECLAIM_ACCOUNT|SLAB_ACCOUNT,
				init_once);
	if (efs_inode_cachep == NULL)
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
	kmem_cache_destroy(efs_inode_cachep);
}

static const struct super_operations efs_superblock_operations = {
	.alloc_inode	= efs_alloc_inode,
	.free_inode	= efs_free_inode,
	.statfs		= efs_statfs,
};

static const struct export_operations efs_export_ops = {
	.encode_fh	= generic_encode_ino32_fh,
	.fh_to_dentry	= efs_fh_to_dentry,
	.fh_to_parent	= efs_fh_to_parent,
	.get_parent	= efs_get_parent,
};

static int __init init_efs_fs(void) {
	int err;
	pr_info(EFS_VERSION" - http://aeschi.ch.eu.org/efs/\n");
	err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&efs_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_efs_fs(void) {
	unregister_filesystem(&efs_fs_type);
	destroy_inodecache();
}

module_init(init_efs_fs)
module_exit(exit_efs_fs)

static efs_block_t efs_validate_vh(struct volume_header *vh) {
	int		i;
	__be32		cs, *ui;
	int		csum;
	efs_block_t	sblock = 0; /* shuts up gcc */
	struct pt_types	*pt_entry;
	int		pt_type, slice = -1;

	if (be32_to_cpu(vh->vh_magic) != VHMAGIC) {
		/*
		 * assume that we're dealing with a partition and allow
		 * read_super() to try and detect a valid superblock
		 * on the next block.
		 */
		return 0;
	}

	ui = ((__be32 *) (vh + 1)) - 1;
	for(csum = 0; ui >= ((__be32 *) vh);) {
		cs = *ui--;
		csum += be32_to_cpu(cs);
	}
	if (csum) {
		pr_warn("SGI disklabel: checksum bad, label corrupted\n");
		return 0;
	}

#ifdef DEBUG
	pr_debug("bf: \"%16s\"\n", vh->vh_bootfile);

	for(i = 0; i < NVDIR; i++) {
		int	j;
		char	name[VDNAMESIZE+1];

		for(j = 0; j < VDNAMESIZE; j++) {
			name[j] = vh->vh_vd[i].vd_name[j];
		}
		name[j] = (char) 0;

		if (name[0]) {
			pr_debug("vh: %8s block: 0x%08x size: 0x%08x\n",
				name, (int) be32_to_cpu(vh->vh_vd[i].vd_lbn),
				(int) be32_to_cpu(vh->vh_vd[i].vd_nbytes));
		}
	}
#endif

	for(i = 0; i < NPARTAB; i++) {
		pt_type = (int) be32_to_cpu(vh->vh_pt[i].pt_type);
		for(pt_entry = sgi_pt_types; pt_entry->pt_name; pt_entry++) {
			if (pt_type == pt_entry->pt_type) break;
		}
#ifdef DEBUG
		if (be32_to_cpu(vh->vh_pt[i].pt_nblks)) {
			pr_debug("pt %2d: start: %08d size: %08d type: 0x%02x (%s)\n",
				 i, (int)be32_to_cpu(vh->vh_pt[i].pt_firstlbn),
				 (int)be32_to_cpu(vh->vh_pt[i].pt_nblks),
				 pt_type, (pt_entry->pt_name) ?
				 pt_entry->pt_name : "unknown");
		}
#endif
		if (IS_EFS(pt_type)) {
			sblock = be32_to_cpu(vh->vh_pt[i].pt_firstlbn);
			slice = i;
		}
	}

	if (slice == -1) {
		pr_notice("partition table contained no EFS partitions\n");
#ifdef DEBUG
	} else {
		pr_info("using slice %d (type %s, offset 0x%x)\n", slice,
			(pt_entry->pt_name) ? pt_entry->pt_name : "unknown",
			sblock);
#endif
	}
	return sblock;
}

static int efs_validate_super(struct efs_sb_info *sb, struct efs_super *super) {

	if (!IS_EFS_MAGIC(be32_to_cpu(super->fs_magic)))
		return -1;

	sb->fs_magic     = be32_to_cpu(super->fs_magic);
	sb->total_blocks = be32_to_cpu(super->fs_size);
	sb->first_block  = be32_to_cpu(super->fs_firstcg);
	sb->group_size   = be32_to_cpu(super->fs_cgfsize);
	sb->data_free    = be32_to_cpu(super->fs_tfree);
	sb->inode_free   = be32_to_cpu(super->fs_tinode);
	sb->inode_blocks = be16_to_cpu(super->fs_cgisize);
	sb->total_groups = be16_to_cpu(super->fs_ncg);
    
	return 0;    
}

static int efs_fill_super(struct super_block *s, struct fs_context *fc)
{
	struct efs_sb_info *sb;
	struct buffer_head *bh;
	struct inode *root;

	sb = kzalloc(sizeof(struct efs_sb_info), GFP_KERNEL);
	if (!sb)
		return -ENOMEM;
	s->s_fs_info = sb;
	s->s_time_min = 0;
	s->s_time_max = U32_MAX;

	s->s_magic		= EFS_SUPER_MAGIC;
	if (!sb_set_blocksize(s, EFS_BLOCKSIZE)) {
		pr_err("device does not support %d byte blocks\n",
			EFS_BLOCKSIZE);
		return -EINVAL;
	}

	/* read the vh (volume header) block */
	bh = sb_bread(s, 0);

	if (!bh) {
		pr_err("cannot read volume header\n");
		return -EIO;
	}

	/*
	 * if this returns zero then we didn't find any partition table.
	 * this isn't (yet) an error - just assume for the moment that
	 * the device is valid and go on to search for a superblock.
	 */
	sb->fs_start = efs_validate_vh((struct volume_header *) bh->b_data);
	brelse(bh);

	if (sb->fs_start == -1) {
		return -EINVAL;
	}

	bh = sb_bread(s, sb->fs_start + EFS_SUPER);
	if (!bh) {
		pr_err("cannot read superblock\n");
		return -EIO;
	}

	if (efs_validate_super(sb, (struct efs_super *) bh->b_data)) {
#ifdef DEBUG
		pr_warn("invalid superblock at block %u\n",
			sb->fs_start + EFS_SUPER);
#endif
		brelse(bh);
		return -EINVAL;
	}
	brelse(bh);

	if (!sb_rdonly(s)) {
#ifdef DEBUG
		pr_info("forcing read-only mode\n");
#endif
		s->s_flags |= SB_RDONLY;
	}
	s->s_op   = &efs_superblock_operations;
	s->s_export_op = &efs_export_ops;
	root = efs_iget(s, EFS_ROOTINODE);
	if (IS_ERR(root)) {
		pr_err("get root inode failed\n");
		return PTR_ERR(root);
	}

	s->s_root = d_make_root(root);
	if (!(s->s_root)) {
		pr_err("get root dentry failed\n");
		return -ENOMEM;
	}

	return 0;
}

static void efs_free_fc(struct fs_context *fc)
{
	kfree(fc->fs_private);
}

static int efs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, efs_fill_super);
}

static int efs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	int token;
	struct fs_parse_result result;

	token = fs_parse(fc, efs_param_spec, param, &result);
	if (token < 0)
		return token;
	return 0;
}

static int efs_reconfigure(struct fs_context *fc)
{
	sync_filesystem(fc->root->d_sb);

	return 0;
}

struct efs_context {
	unsigned long s_mount_opts;
};

static const struct fs_context_operations efs_context_opts = {
	.parse_param	= efs_parse_param,
	.get_tree	= efs_get_tree,
	.reconfigure	= efs_reconfigure,
	.free		= efs_free_fc,
};

/*
 * Set up the filesystem mount context.
 */
static int efs_init_fs_context(struct fs_context *fc)
{
	struct efs_context *ctx;

	ctx = kzalloc(sizeof(struct efs_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	fc->fs_private = ctx;
	fc->ops = &efs_context_opts;

	return 0;
}

static int efs_statfs(struct dentry *dentry, struct kstatfs *buf) {
	struct super_block *sb = dentry->d_sb;
	struct efs_sb_info *sbi = SUPER_INFO(sb);
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);

	buf->f_type    = EFS_SUPER_MAGIC;	/* efs magic number */
	buf->f_bsize   = EFS_BLOCKSIZE;		/* blocksize */
	buf->f_blocks  = sbi->total_groups *	/* total data blocks */
			(sbi->group_size - sbi->inode_blocks);
	buf->f_bfree   = sbi->data_free;	/* free data blocks */
	buf->f_bavail  = sbi->data_free;	/* free blocks for non-root */
	buf->f_files   = sbi->total_groups *	/* total inodes */
			sbi->inode_blocks *
			(EFS_BLOCKSIZE / sizeof(struct efs_dinode));
	buf->f_ffree   = sbi->inode_free;	/* free inodes */
	buf->f_fsid    = u64_to_fsid(id);
	buf->f_namelen = EFS_MAXNAMELEN;	/* max filename length */

	return 0;
}

