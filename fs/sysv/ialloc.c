/*
 *  linux/fs/sysv/ialloc.c
 *
 *  minix/bitmap.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext/freelists.c
 *  Copyright (C) 1992  Remy Card (card@masi.ibp.fr)
 *
 *  xenix/alloc.c
 *  Copyright (C) 1992  Doug Evans
 *
 *  coh/alloc.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/ialloc.c
 *  Copyright (C) 1993  Bruno Haible
 *
 *  This file contains code for allocating/freeing inodes.
 */

#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include "sysv.h"

/* We don't trust the value of
   sb->sv_sbd2->s_tinode = *sb->sv_sb_total_free_inodes
   but we nevertheless keep it up to date. */

/* An inode on disk is considered free if both i_mode == 0 and i_nlink == 0. */

/* return &sb->sv_sb_fic_inodes[i] = &sbd->s_inode[i]; */
static inline sysv_ino_t *
sv_sb_fic_inode(struct super_block * sb, unsigned int i)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);

	if (sbi->s_bh1 == sbi->s_bh2)
		return &sbi->s_sb_fic_inodes[i];
	else {
		/* 512 byte Xenix FS */
		unsigned int offset = offsetof(struct xenix_super_block, s_inode[i]);
		if (offset < 512)
			return (sysv_ino_t*)(sbi->s_sbd1 + offset);
		else
			return (sysv_ino_t*)(sbi->s_sbd2 + offset);
	}
}

struct sysv_inode *
sysv_raw_inode(struct super_block *sb, unsigned ino, struct buffer_head **bh)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct sysv_inode *res;
	int block = sbi->s_firstinodezone + sbi->s_block_base;

	block += (ino-1) >> sbi->s_inodes_per_block_bits;
	*bh = sb_bread(sb, block);
	if (!*bh)
		return NULL;
	res = (struct sysv_inode *)(*bh)->b_data;
	return res + ((ino-1) & sbi->s_inodes_per_block_1);
}

static int refill_free_cache(struct super_block *sb)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct buffer_head * bh;
	struct sysv_inode * raw_inode;
	int i = 0, ino;

	ino = SYSV_ROOT_INO+1;
	raw_inode = sysv_raw_inode(sb, ino, &bh);
	if (!raw_inode)
		goto out;
	while (ino <= sbi->s_ninodes) {
		if (raw_inode->i_mode == 0 && raw_inode->i_nlink == 0) {
			*sv_sb_fic_inode(sb,i++) = cpu_to_fs16(SYSV_SB(sb), ino);
			if (i == sbi->s_fic_size)
				break;
		}
		if ((ino++ & sbi->s_inodes_per_block_1) == 0) {
			brelse(bh);
			raw_inode = sysv_raw_inode(sb, ino, &bh);
			if (!raw_inode)
				goto out;
		} else
			raw_inode++;
	}
	brelse(bh);
out:
	return i;
}

void sysv_free_inode(struct inode * inode)
{
	struct super_block *sb = inode->i_sb;
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	unsigned int ino;
	struct buffer_head * bh;
	struct sysv_inode * raw_inode;
	unsigned count;

	sb = inode->i_sb;
	ino = inode->i_ino;
	if (ino <= SYSV_ROOT_INO || ino > sbi->s_ninodes) {
		printk("sysv_free_inode: inode 0,1,2 or nonexistent inode\n");
		return;
	}
	raw_inode = sysv_raw_inode(sb, ino, &bh);
	clear_inode(inode);
	if (!raw_inode) {
		printk("sysv_free_inode: unable to read inode block on device "
		       "%s\n", inode->i_sb->s_id);
		return;
	}
	lock_super(sb);
	count = fs16_to_cpu(sbi, *sbi->s_sb_fic_count);
	if (count < sbi->s_fic_size) {
		*sv_sb_fic_inode(sb,count++) = cpu_to_fs16(sbi, ino);
		*sbi->s_sb_fic_count = cpu_to_fs16(sbi, count);
	}
	fs16_add(sbi, sbi->s_sb_total_free_inodes, 1);
	dirty_sb(sb);
	memset(raw_inode, 0, sizeof(struct sysv_inode));
	mark_buffer_dirty(bh);
	unlock_super(sb);
	brelse(bh);
}

struct inode * sysv_new_inode(const struct inode * dir, mode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct inode *inode;
	sysv_ino_t ino;
	unsigned count;

	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	lock_super(sb);
	count = fs16_to_cpu(sbi, *sbi->s_sb_fic_count);
	if (count == 0 || (*sv_sb_fic_inode(sb,count-1) == 0)) {
		count = refill_free_cache(sb);
		if (count == 0) {
			iput(inode);
			unlock_super(sb);
			return ERR_PTR(-ENOSPC);
		}
	}
	/* Now count > 0. */
	ino = *sv_sb_fic_inode(sb,--count);
	*sbi->s_sb_fic_count = cpu_to_fs16(sbi, count);
	fs16_add(sbi, sbi->s_sb_total_free_inodes, -1);
	dirty_sb(sb);
	
	if (dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		inode->i_gid = current->fsgid;

	inode->i_uid = current->fsuid;
	inode->i_ino = fs16_to_cpu(sbi, ino);
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	inode->i_blocks = inode->i_blksize = 0;
	memset(SYSV_I(inode)->i_data, 0, sizeof(SYSV_I(inode)->i_data));
	SYSV_I(inode)->i_dir_start_lookup = 0;
	insert_inode_hash(inode);
	mark_inode_dirty(inode);

	inode->i_mode = mode;		/* for sysv_write_inode() */
	sysv_write_inode(inode, 0);	/* ensure inode not allocated again */
	mark_inode_dirty(inode);	/* cleared by sysv_write_inode() */
	/* That's it. */
	unlock_super(sb);
	return inode;
}

unsigned long sysv_count_free_inodes(struct super_block * sb)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct buffer_head * bh;
	struct sysv_inode * raw_inode;
	int ino, count, sb_count;

	lock_super(sb);

	sb_count = fs16_to_cpu(sbi, *sbi->s_sb_total_free_inodes);

	if (0)
		goto trust_sb;

	/* this causes a lot of disk traffic ... */
	count = 0;
	ino = SYSV_ROOT_INO+1;
	raw_inode = sysv_raw_inode(sb, ino, &bh);
	if (!raw_inode)
		goto Eio;
	while (ino <= sbi->s_ninodes) {
		if (raw_inode->i_mode == 0 && raw_inode->i_nlink == 0)
			count++;
		if ((ino++ & sbi->s_inodes_per_block_1) == 0) {
			brelse(bh);
			raw_inode = sysv_raw_inode(sb, ino, &bh);
			if (!raw_inode)
				goto Eio;
		} else
			raw_inode++;
	}
	brelse(bh);
	if (count != sb_count)
		goto Einval;
out:
	unlock_super(sb);
	return count;

Einval:
	printk("sysv_count_free_inodes: "
		"free inode count was %d, correcting to %d\n",
		sb_count, count);
	if (!(sb->s_flags & MS_RDONLY)) {
		*sbi->s_sb_total_free_inodes = cpu_to_fs16(SYSV_SB(sb), count);
		dirty_sb(sb);
	}
	goto out;

Eio:
	printk("sysv_count_free_inodes: unable to read inode table\n");
trust_sb:
	count = sb_count;
	goto out;
}
