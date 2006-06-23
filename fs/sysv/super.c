/*
 *  linux/fs/sysv/inode.c
 *
 *  minix/inode.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  xenix/inode.c
 *  Copyright (C) 1992  Doug Evans
 *
 *  coh/inode.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/inode.c
 *  Copyright (C) 1993  Paul B. Monday
 *
 *  sysv/inode.c
 *  Copyright (C) 1993  Bruno Haible
 *  Copyright (C) 1997, 1998  Krzysztof G. Baranowski
 *
 *  This file contains code for read/parsing the superblock.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include "sysv.h"

/*
 * The following functions try to recognize specific filesystems.
 *
 * We recognize:
 * - Xenix FS by its magic number.
 * - SystemV FS by its magic number.
 * - Coherent FS by its funny fname/fpack field.
 * - SCO AFS by s_nfree == 0xffff
 * - V7 FS has no distinguishing features.
 *
 * We discriminate among SystemV4 and SystemV2 FS by the assumption that
 * the time stamp is not < 01-01-1980.
 */

enum {
	JAN_1_1980 = (10*365 + 2) * 24 * 60 * 60
};

static void detected_xenix(struct sysv_sb_info *sbi)
{
	struct buffer_head *bh1 = sbi->s_bh1;
	struct buffer_head *bh2 = sbi->s_bh2;
	struct xenix_super_block * sbd1;
	struct xenix_super_block * sbd2;

	if (bh1 != bh2)
		sbd1 = sbd2 = (struct xenix_super_block *) bh1->b_data;
	else {
		/* block size = 512, so bh1 != bh2 */
		sbd1 = (struct xenix_super_block *) bh1->b_data;
		sbd2 = (struct xenix_super_block *) (bh2->b_data - 512);
	}

	sbi->s_link_max = XENIX_LINK_MAX;
	sbi->s_fic_size = XENIX_NICINOD;
	sbi->s_flc_size = XENIX_NICFREE;
	sbi->s_sbd1 = (char *)sbd1;
	sbi->s_sbd2 = (char *)sbd2;
	sbi->s_sb_fic_count = &sbd1->s_ninode;
	sbi->s_sb_fic_inodes = &sbd1->s_inode[0];
	sbi->s_sb_total_free_inodes = &sbd2->s_tinode;
	sbi->s_bcache_count = &sbd1->s_nfree;
	sbi->s_bcache = &sbd1->s_free[0];
	sbi->s_free_blocks = &sbd2->s_tfree;
	sbi->s_sb_time = &sbd2->s_time;
	sbi->s_firstdatazone = fs16_to_cpu(sbi, sbd1->s_isize);
	sbi->s_nzones = fs32_to_cpu(sbi, sbd1->s_fsize);
}

static void detected_sysv4(struct sysv_sb_info *sbi)
{
	struct sysv4_super_block * sbd;
	struct buffer_head *bh1 = sbi->s_bh1;
	struct buffer_head *bh2 = sbi->s_bh2;

	if (bh1 == bh2)
		sbd = (struct sysv4_super_block *) (bh1->b_data + BLOCK_SIZE/2);
	else
		sbd = (struct sysv4_super_block *) bh2->b_data;

	sbi->s_link_max = SYSV_LINK_MAX;
	sbi->s_fic_size = SYSV_NICINOD;
	sbi->s_flc_size = SYSV_NICFREE;
	sbi->s_sbd1 = (char *)sbd;
	sbi->s_sbd2 = (char *)sbd;
	sbi->s_sb_fic_count = &sbd->s_ninode;
	sbi->s_sb_fic_inodes = &sbd->s_inode[0];
	sbi->s_sb_total_free_inodes = &sbd->s_tinode;
	sbi->s_bcache_count = &sbd->s_nfree;
	sbi->s_bcache = &sbd->s_free[0];
	sbi->s_free_blocks = &sbd->s_tfree;
	sbi->s_sb_time = &sbd->s_time;
	sbi->s_sb_state = &sbd->s_state;
	sbi->s_firstdatazone = fs16_to_cpu(sbi, sbd->s_isize);
	sbi->s_nzones = fs32_to_cpu(sbi, sbd->s_fsize);
}

static void detected_sysv2(struct sysv_sb_info *sbi)
{
	struct sysv2_super_block *sbd;
	struct buffer_head *bh1 = sbi->s_bh1;
	struct buffer_head *bh2 = sbi->s_bh2;

	if (bh1 == bh2)
		sbd = (struct sysv2_super_block *) (bh1->b_data + BLOCK_SIZE/2);
	else
		sbd = (struct sysv2_super_block *) bh2->b_data;

	sbi->s_link_max = SYSV_LINK_MAX;
	sbi->s_fic_size = SYSV_NICINOD;
	sbi->s_flc_size = SYSV_NICFREE;
	sbi->s_sbd1 = (char *)sbd;
	sbi->s_sbd2 = (char *)sbd;
	sbi->s_sb_fic_count = &sbd->s_ninode;
	sbi->s_sb_fic_inodes = &sbd->s_inode[0];
	sbi->s_sb_total_free_inodes = &sbd->s_tinode;
	sbi->s_bcache_count = &sbd->s_nfree;
	sbi->s_bcache = &sbd->s_free[0];
	sbi->s_free_blocks = &sbd->s_tfree;
	sbi->s_sb_time = &sbd->s_time;
	sbi->s_sb_state = &sbd->s_state;
	sbi->s_firstdatazone = fs16_to_cpu(sbi, sbd->s_isize);
	sbi->s_nzones = fs32_to_cpu(sbi, sbd->s_fsize);
}

static void detected_coherent(struct sysv_sb_info *sbi)
{
	struct coh_super_block * sbd;
	struct buffer_head *bh1 = sbi->s_bh1;

	sbd = (struct coh_super_block *) bh1->b_data;

	sbi->s_link_max = COH_LINK_MAX;
	sbi->s_fic_size = COH_NICINOD;
	sbi->s_flc_size = COH_NICFREE;
	sbi->s_sbd1 = (char *)sbd;
	sbi->s_sbd2 = (char *)sbd;
	sbi->s_sb_fic_count = &sbd->s_ninode;
	sbi->s_sb_fic_inodes = &sbd->s_inode[0];
	sbi->s_sb_total_free_inodes = &sbd->s_tinode;
	sbi->s_bcache_count = &sbd->s_nfree;
	sbi->s_bcache = &sbd->s_free[0];
	sbi->s_free_blocks = &sbd->s_tfree;
	sbi->s_sb_time = &sbd->s_time;
	sbi->s_firstdatazone = fs16_to_cpu(sbi, sbd->s_isize);
	sbi->s_nzones = fs32_to_cpu(sbi, sbd->s_fsize);
}

static void detected_v7(struct sysv_sb_info *sbi)
{
	struct buffer_head *bh2 = sbi->s_bh2;
	struct v7_super_block *sbd = (struct v7_super_block *)bh2->b_data;

	sbi->s_link_max = V7_LINK_MAX;
	sbi->s_fic_size = V7_NICINOD;
	sbi->s_flc_size = V7_NICFREE;
	sbi->s_sbd1 = (char *)sbd;
	sbi->s_sbd2 = (char *)sbd;
	sbi->s_sb_fic_count = &sbd->s_ninode;
	sbi->s_sb_fic_inodes = &sbd->s_inode[0];
	sbi->s_sb_total_free_inodes = &sbd->s_tinode;
	sbi->s_bcache_count = &sbd->s_nfree;
	sbi->s_bcache = &sbd->s_free[0];
	sbi->s_free_blocks = &sbd->s_tfree;
	sbi->s_sb_time = &sbd->s_time;
	sbi->s_firstdatazone = fs16_to_cpu(sbi, sbd->s_isize);
	sbi->s_nzones = fs32_to_cpu(sbi, sbd->s_fsize);
}

static int detect_xenix(struct sysv_sb_info *sbi, struct buffer_head *bh)
{
	struct xenix_super_block *sbd = (struct xenix_super_block *)bh->b_data;
	if (*(__le32 *)&sbd->s_magic == cpu_to_le32(0x2b5544))
		sbi->s_bytesex = BYTESEX_LE;
	else if (*(__be32 *)&sbd->s_magic == cpu_to_be32(0x2b5544))
		sbi->s_bytesex = BYTESEX_BE;
	else
		return 0;
	switch (fs32_to_cpu(sbi, sbd->s_type)) {
	case 1:
		sbi->s_type = FSTYPE_XENIX;
		return 1;
	case 2:
		sbi->s_type = FSTYPE_XENIX;
		return 2;
	default:
		return 0;
	}
}

static int detect_sysv(struct sysv_sb_info *sbi, struct buffer_head *bh)
{
	struct super_block *sb = sbi->s_sb;
	/* All relevant fields are at the same offsets in R2 and R4 */
	struct sysv4_super_block * sbd;
	u32 type;

	sbd = (struct sysv4_super_block *) (bh->b_data + BLOCK_SIZE/2);
	if (*(__le32 *)&sbd->s_magic == cpu_to_le32(0xfd187e20))
		sbi->s_bytesex = BYTESEX_LE;
	else if (*(__be32 *)&sbd->s_magic == cpu_to_be32(0xfd187e20))
		sbi->s_bytesex = BYTESEX_BE;
	else
		return 0;

	type = fs32_to_cpu(sbi, sbd->s_type);
 
 	if (fs16_to_cpu(sbi, sbd->s_nfree) == 0xffff) {
 		sbi->s_type = FSTYPE_AFS;
		sbi->s_forced_ro = 1;
 		if (!(sb->s_flags & MS_RDONLY)) {
 			printk("SysV FS: SCO EAFS on %s detected, " 
 				"forcing read-only mode.\n", 
 				sb->s_id);
 		}
 		return type;
 	}
 
	if (fs32_to_cpu(sbi, sbd->s_time) < JAN_1_1980) {
		/* this is likely to happen on SystemV2 FS */
		if (type > 3 || type < 1)
			return 0;
		sbi->s_type = FSTYPE_SYSV2;
		return type;
	}
	if ((type > 3 || type < 1) && (type > 0x30 || type < 0x10))
		return 0;

	/* On Interactive Unix (ISC) Version 4.0/3.x s_type field = 0x10,
	   0x20 or 0x30 indicates that symbolic links and the 14-character
	   filename limit is gone. Due to lack of information about this
           feature read-only mode seems to be a reasonable approach... -KGB */

	if (type >= 0x10) {
		printk("SysV FS: can't handle long file names on %s, "
		       "forcing read-only mode.\n", sb->s_id);
		sbi->s_forced_ro = 1;
	}

	sbi->s_type = FSTYPE_SYSV4;
	return type >= 0x10 ? type >> 4 : type;
}

static int detect_coherent(struct sysv_sb_info *sbi, struct buffer_head *bh)
{
	struct coh_super_block * sbd;

	sbd = (struct coh_super_block *) (bh->b_data + BLOCK_SIZE/2);
	if ((memcmp(sbd->s_fname,"noname",6) && memcmp(sbd->s_fname,"xxxxx ",6))
	    || (memcmp(sbd->s_fpack,"nopack",6) && memcmp(sbd->s_fpack,"xxxxx\n",6)))
		return 0;
	sbi->s_bytesex = BYTESEX_PDP;
	sbi->s_type = FSTYPE_COH;
	return 1;
}

static int detect_sysv_odd(struct sysv_sb_info *sbi, struct buffer_head *bh)
{
	int size = detect_sysv(sbi, bh);

	return size>2 ? 0 : size;
}

static struct {
	int block;
	int (*test)(struct sysv_sb_info *, struct buffer_head *);
} flavours[] = {
	{1, detect_xenix},
	{0, detect_sysv},
	{0, detect_coherent},
	{9, detect_sysv_odd},
	{15,detect_sysv_odd},
	{18,detect_sysv},
};

static char *flavour_names[] = {
	[FSTYPE_XENIX]	= "Xenix",
	[FSTYPE_SYSV4]	= "SystemV",
	[FSTYPE_SYSV2]	= "SystemV Release 2",
	[FSTYPE_COH]	= "Coherent",
	[FSTYPE_V7]	= "V7",
	[FSTYPE_AFS]	= "AFS",
};

static void (*flavour_setup[])(struct sysv_sb_info *) = {
	[FSTYPE_XENIX]	= detected_xenix,
	[FSTYPE_SYSV4]	= detected_sysv4,
	[FSTYPE_SYSV2]	= detected_sysv2,
	[FSTYPE_COH]	= detected_coherent,
	[FSTYPE_V7]	= detected_v7,
	[FSTYPE_AFS]	= detected_sysv4,
};

static int complete_read_super(struct super_block *sb, int silent, int size)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct inode *root_inode;
	char *found = flavour_names[sbi->s_type];
	u_char n_bits = size+8;
	int bsize = 1 << n_bits;
	int bsize_4 = bsize >> 2;

	sbi->s_firstinodezone = 2;

	flavour_setup[sbi->s_type](sbi);
	
	sbi->s_truncate = 1;
	sbi->s_ndatazones = sbi->s_nzones - sbi->s_firstdatazone;
	sbi->s_inodes_per_block = bsize >> 6;
	sbi->s_inodes_per_block_1 = (bsize >> 6)-1;
	sbi->s_inodes_per_block_bits = n_bits-6;
	sbi->s_ind_per_block = bsize_4;
	sbi->s_ind_per_block_2 = bsize_4*bsize_4;
	sbi->s_toobig_block = 10 + bsize_4 * (1 + bsize_4 * (1 + bsize_4));
	sbi->s_ind_per_block_bits = n_bits-2;

	sbi->s_ninodes = (sbi->s_firstdatazone - sbi->s_firstinodezone)
		<< sbi->s_inodes_per_block_bits;

	if (!silent)
		printk("VFS: Found a %s FS (block size = %ld) on device %s\n",
		       found, sb->s_blocksize, sb->s_id);

	sb->s_magic = SYSV_MAGIC_BASE + sbi->s_type;
	/* set up enough so that it can read an inode */
	sb->s_op = &sysv_sops;
	root_inode = iget(sb,SYSV_ROOT_INO);
	if (!root_inode || is_bad_inode(root_inode)) {
		printk("SysV FS: get root inode failed\n");
		return 0;
	}
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root) {
		iput(root_inode);
		printk("SysV FS: get root dentry failed\n");
		return 0;
	}
	if (sbi->s_forced_ro)
		sb->s_flags |= MS_RDONLY;
	if (sbi->s_truncate)
		sb->s_root->d_op = &sysv_dentry_operations;
	sb->s_dirt = 1;
	return 1;
}

static int sysv_fill_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *bh1, *bh = NULL;
	struct sysv_sb_info *sbi;
	unsigned long blocknr;
	int size = 0, i;
	
	if (1024 != sizeof (struct xenix_super_block))
		panic("Xenix FS: bad superblock size");
	if (512 != sizeof (struct sysv4_super_block))
		panic("SystemV FS: bad superblock size");
	if (512 != sizeof (struct sysv2_super_block))
		panic("SystemV FS: bad superblock size");
	if (500 != sizeof (struct coh_super_block))
		panic("Coherent FS: bad superblock size");
	if (64 != sizeof (struct sysv_inode))
		panic("sysv fs: bad inode size");

	sbi = kmalloc(sizeof(struct sysv_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	memset(sbi, 0, sizeof(struct sysv_sb_info));

	sbi->s_sb = sb;
	sbi->s_block_base = 0;
	sb->s_fs_info = sbi;

	sb_set_blocksize(sb, BLOCK_SIZE);

	for (i = 0; i < ARRAY_SIZE(flavours) && !size; i++) {
		brelse(bh);
		bh = sb_bread(sb, flavours[i].block);
		if (!bh)
			continue;
		size = flavours[i].test(SYSV_SB(sb), bh);
	}

	if (!size)
		goto Eunknown;

	switch (size) {
		case 1:
			blocknr = bh->b_blocknr << 1;
			brelse(bh);
			sb_set_blocksize(sb, 512);
			bh1 = sb_bread(sb, blocknr);
			bh = sb_bread(sb, blocknr + 1);
			break;
		case 2:
			bh1 = bh;
			break;
		case 3:
			blocknr = bh->b_blocknr >> 1;
			brelse(bh);
			sb_set_blocksize(sb, 2048);
			bh1 = bh = sb_bread(sb, blocknr);
			break;
		default:
			goto Ebadsize;
	}

	if (bh && bh1) {
		sbi->s_bh1 = bh1;
		sbi->s_bh2 = bh;
		if (complete_read_super(sb, silent, size))
			return 0;
	}

	brelse(bh1);
	brelse(bh);
	sb_set_blocksize(sb, BLOCK_SIZE);
	printk("oldfs: cannot read superblock\n");
failed:
	kfree(sbi);
	return -EINVAL;

Eunknown:
	brelse(bh);
	if (!silent)
		printk("VFS: unable to find oldfs superblock on device %s\n",
			sb->s_id);
	goto failed;
Ebadsize:
	brelse(bh);
	if (!silent)
		printk("VFS: oldfs: unsupported block size (%dKb)\n",
			1<<(size-2));
	goto failed;
}

static int v7_fill_super(struct super_block *sb, void *data, int silent)
{
	struct sysv_sb_info *sbi;
	struct buffer_head *bh, *bh2 = NULL;
	struct v7_super_block *v7sb;
	struct sysv_inode *v7i;

	if (440 != sizeof (struct v7_super_block))
		panic("V7 FS: bad super-block size");
	if (64 != sizeof (struct sysv_inode))
		panic("sysv fs: bad i-node size");

	sbi = kmalloc(sizeof(struct sysv_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	memset(sbi, 0, sizeof(struct sysv_sb_info));

	sbi->s_sb = sb;
	sbi->s_block_base = 0;
	sbi->s_type = FSTYPE_V7;
	sbi->s_bytesex = BYTESEX_PDP;
	sb->s_fs_info = sbi;
	
	sb_set_blocksize(sb, 512);

	if ((bh = sb_bread(sb, 1)) == NULL) {
		if (!silent)
			printk("VFS: unable to read V7 FS superblock on "
			       "device %s.\n", sb->s_id);
		goto failed;
	}

	/* plausibility check on superblock */
	v7sb = (struct v7_super_block *) bh->b_data;
	if (fs16_to_cpu(sbi, v7sb->s_nfree) > V7_NICFREE ||
	    fs16_to_cpu(sbi, v7sb->s_ninode) > V7_NICINOD ||
	    fs32_to_cpu(sbi, v7sb->s_time) == 0)
		goto failed;

	/* plausibility check on root inode: it is a directory,
	   with a nonzero size that is a multiple of 16 */
	if ((bh2 = sb_bread(sb, 2)) == NULL)
		goto failed;
	v7i = (struct sysv_inode *)(bh2->b_data + 64);
	if ((fs16_to_cpu(sbi, v7i->i_mode) & ~0777) != S_IFDIR ||
	    (fs32_to_cpu(sbi, v7i->i_size) == 0) ||
	    (fs32_to_cpu(sbi, v7i->i_size) & 017) != 0)
		goto failed;
	brelse(bh2);
	bh2 = NULL;

	sbi->s_bh1 = bh;
	sbi->s_bh2 = bh;
	if (complete_read_super(sb, silent, 1))
		return 0;

failed:
	brelse(bh2);
	brelse(bh);
	kfree(sbi);
	return -EINVAL;
}

/* Every kernel module contains stuff like this. */

static int sysv_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, sysv_fill_super,
			   mnt);
}

static int v7_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, v7_fill_super, mnt);
}

static struct file_system_type sysv_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "sysv",
	.get_sb		= sysv_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static struct file_system_type v7_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "v7",
	.get_sb		= v7_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

extern int sysv_init_icache(void) __init;
extern void sysv_destroy_icache(void);

static int __init init_sysv_fs(void)
{
	int error;

	error = sysv_init_icache();
	if (error)
		goto out;
	error = register_filesystem(&sysv_fs_type);
	if (error)
		goto destroy_icache;
	error = register_filesystem(&v7_fs_type);
	if (error)
		goto unregister;
	return 0;

unregister:
	unregister_filesystem(&sysv_fs_type);
destroy_icache:
	sysv_destroy_icache();
out:
	return error;
}

static void __exit exit_sysv_fs(void)
{
	unregister_filesystem(&sysv_fs_type);
	unregister_filesystem(&v7_fs_type);
	sysv_destroy_icache();
}

module_init(init_sysv_fs)
module_exit(exit_sysv_fs)
MODULE_LICENSE("GPL");
