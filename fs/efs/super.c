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

#include "efs.h"
#include <linux/efs_vh.h>
#include <linux/efs_fs_sb.h>

static int efs_statfs(struct dentry *dentry, struct kstatfs *buf);
static int efs_fill_super(struct super_block *s, void *d, int silent);

static int efs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, efs_fill_super, mnt);
}

static struct file_system_type efs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "efs",
	.get_sb		= efs_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

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


static struct kmem_cache * efs_inode_cachep;

static struct inode *efs_alloc_inode(struct super_block *sb)
{
	struct efs_inode_info *ei;
	ei = (struct efs_inode_info *)kmem_cache_alloc(efs_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}

static void efs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(efs_inode_cachep, INODE_INFO(inode));
}

static void init_once(void *foo)
{
	struct efs_inode_info *ei = (struct efs_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	efs_inode_cachep = kmem_cache_create("efs_inode_cache",
				sizeof(struct efs_inode_info),
				0, SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
				init_once);
	if (efs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	kmem_cache_destroy(efs_inode_cachep);
}

static void efs_put_super(struct super_block *s)
{
	kfree(s->s_fs_info);
	s->s_fs_info = NULL;
}

static int efs_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= MS_RDONLY;
	return 0;
}

static const struct super_operations efs_superblock_operations = {
	.alloc_inode	= efs_alloc_inode,
	.destroy_inode	= efs_destroy_inode,
	.put_super	= efs_put_super,
	.statfs		= efs_statfs,
	.remount_fs	= efs_remount,
};

static const struct export_operations efs_export_ops = {
	.fh_to_dentry	= efs_fh_to_dentry,
	.fh_to_parent	= efs_fh_to_parent,
	.get_parent	= efs_get_parent,
};

static int __init init_efs_fs(void) {
	int err;
	printk("EFS: "EFS_VERSION" - http://aeschi.ch.eu.org/efs/\n");
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
		printk(KERN_INFO "EFS: SGI disklabel: checksum bad, label corrupted\n");
		return 0;
	}

#ifdef DEBUG
	printk(KERN_DEBUG "EFS: bf: \"%16s\"\n", vh->vh_bootfile);

	for(i = 0; i < NVDIR; i++) {
		int	j;
		char	name[VDNAMESIZE+1];

		for(j = 0; j < VDNAMESIZE; j++) {
			name[j] = vh->vh_vd[i].vd_name[j];
		}
		name[j] = (char) 0;

		if (name[0]) {
			printk(KERN_DEBUG "EFS: vh: %8s block: 0x%08x size: 0x%08x\n",
				name,
				(int) be32_to_cpu(vh->vh_vd[i].vd_lbn),
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
			printk(KERN_DEBUG "EFS: pt %2d: start: %08d size: %08d type: 0x%02x (%s)\n",
				i,
				(int) be32_to_cpu(vh->vh_pt[i].pt_firstlbn),
				(int) be32_to_cpu(vh->vh_pt[i].pt_nblks),
				pt_type,
				(pt_entry->pt_name) ? pt_entry->pt_name : "unknown");
		}
#endif
		if (IS_EFS(pt_type)) {
			sblock = be32_to_cpu(vh->vh_pt[i].pt_firstlbn);
			slice = i;
		}
	}

	if (slice == -1) {
		printk(KERN_NOTICE "EFS: partition table contained no EFS partitions\n");
#ifdef DEBUG
	} else {
		printk(KERN_INFO "EFS: using slice %d (type %s, offset 0x%x)\n",
			slice,
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

static int efs_fill_super(struct super_block *s, void *d, int silent)
{
	struct efs_sb_info *sb;
	struct buffer_head *bh;
	struct inode *root;
	int ret = -EINVAL;

 	sb = kzalloc(sizeof(struct efs_sb_info), GFP_KERNEL);
	if (!sb)
		return -ENOMEM;
	s->s_fs_info = sb;
 
	s->s_magic		= EFS_SUPER_MAGIC;
	if (!sb_set_blocksize(s, EFS_BLOCKSIZE)) {
		printk(KERN_ERR "EFS: device does not support %d byte blocks\n",
			EFS_BLOCKSIZE);
		goto out_no_fs_ul;
	}
  
	/* read the vh (volume header) block */
	bh = sb_bread(s, 0);

	if (!bh) {
		printk(KERN_ERR "EFS: cannot read volume header\n");
		goto out_no_fs_ul;
	}

	/*
	 * if this returns zero then we didn't find any partition table.
	 * this isn't (yet) an error - just assume for the moment that
	 * the device is valid and go on to search for a superblock.
	 */
	sb->fs_start = efs_validate_vh((struct volume_header *) bh->b_data);
	brelse(bh);

	if (sb->fs_start == -1) {
		goto out_no_fs_ul;
	}

	bh = sb_bread(s, sb->fs_start + EFS_SUPER);
	if (!bh) {
		printk(KERN_ERR "EFS: cannot read superblock\n");
		goto out_no_fs_ul;
	}
		
	if (efs_validate_super(sb, (struct efs_super *) bh->b_data)) {
#ifdef DEBUG
		printk(KERN_WARNING "EFS: invalid superblock at block %u\n", sb->fs_start + EFS_SUPER);
#endif
		brelse(bh);
		goto out_no_fs_ul;
	}
	brelse(bh);

	if (!(s->s_flags & MS_RDONLY)) {
#ifdef DEBUG
		printk(KERN_INFO "EFS: forcing read-only mode\n");
#endif
		s->s_flags |= MS_RDONLY;
	}
	s->s_op   = &efs_superblock_operations;
	s->s_export_op = &efs_export_ops;
	root = efs_iget(s, EFS_ROOTINODE);
	if (IS_ERR(root)) {
		printk(KERN_ERR "EFS: get root inode failed\n");
		ret = PTR_ERR(root);
		goto out_no_fs;
	}

	s->s_root = d_alloc_root(root);
	if (!(s->s_root)) {
		printk(KERN_ERR "EFS: get root dentry failed\n");
		iput(root);
		ret = -ENOMEM;
		goto out_no_fs;
	}

	return 0;

out_no_fs_ul:
out_no_fs:
	s->s_fs_info = NULL;
	kfree(sb);
	return ret;
}

static int efs_statfs(struct dentry *dentry, struct kstatfs *buf) {
	struct efs_sb_info *sb = SUPER_INFO(dentry->d_sb);

	buf->f_type    = EFS_SUPER_MAGIC;	/* efs magic number */
	buf->f_bsize   = EFS_BLOCKSIZE;		/* blocksize */
	buf->f_blocks  = sb->total_groups *	/* total data blocks */
			(sb->group_size - sb->inode_blocks);
	buf->f_bfree   = sb->data_free;		/* free data blocks */
	buf->f_bavail  = sb->data_free;		/* free blocks for non-root */
	buf->f_files   = sb->total_groups *	/* total inodes */
			sb->inode_blocks *
			(EFS_BLOCKSIZE / sizeof(struct efs_dinode));
	buf->f_ffree   = sb->inode_free;	/* free inodes */
	buf->f_fsid.val[0] = (sb->fs_magic >> 16) & 0xffff; /* fs ID */
	buf->f_fsid.val[1] =  sb->fs_magic        & 0xffff; /* fs ID */
	buf->f_namelen = EFS_MAXNAMELEN;	/* max filename length */

	return 0;
}

