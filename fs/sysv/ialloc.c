// SPDX-License-Identifier: GPL-2.0
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
 *  Copyright (C) 1993  Pascal Haible, Bruyes Haible
 *
 *  sysv/ialloc.c
 *  Copyright (C) 1993  Bruyes Haible
 *
 *  This file contains code for allocating/freeing iyesdes.
 */

#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include "sysv.h"

/* We don't trust the value of
   sb->sv_sbd2->s_tiyesde = *sb->sv_sb_total_free_iyesdes
   but we nevertheless keep it up to date. */

/* An iyesde on disk is considered free if both i_mode == 0 and i_nlink == 0. */

/* return &sb->sv_sb_fic_iyesdes[i] = &sbd->s_iyesde[i]; */
static inline sysv_iyes_t *
sv_sb_fic_iyesde(struct super_block * sb, unsigned int i)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);

	if (sbi->s_bh1 == sbi->s_bh2)
		return &sbi->s_sb_fic_iyesdes[i];
	else {
		/* 512 byte Xenix FS */
		unsigned int offset = offsetof(struct xenix_super_block, s_iyesde[i]);
		if (offset < 512)
			return (sysv_iyes_t*)(sbi->s_sbd1 + offset);
		else
			return (sysv_iyes_t*)(sbi->s_sbd2 + offset);
	}
}

struct sysv_iyesde *
sysv_raw_iyesde(struct super_block *sb, unsigned iyes, struct buffer_head **bh)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct sysv_iyesde *res;
	int block = sbi->s_firstiyesdezone + sbi->s_block_base;

	block += (iyes-1) >> sbi->s_iyesdes_per_block_bits;
	*bh = sb_bread(sb, block);
	if (!*bh)
		return NULL;
	res = (struct sysv_iyesde *)(*bh)->b_data;
	return res + ((iyes-1) & sbi->s_iyesdes_per_block_1);
}

static int refill_free_cache(struct super_block *sb)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct buffer_head * bh;
	struct sysv_iyesde * raw_iyesde;
	int i = 0, iyes;

	iyes = SYSV_ROOT_INO+1;
	raw_iyesde = sysv_raw_iyesde(sb, iyes, &bh);
	if (!raw_iyesde)
		goto out;
	while (iyes <= sbi->s_niyesdes) {
		if (raw_iyesde->i_mode == 0 && raw_iyesde->i_nlink == 0) {
			*sv_sb_fic_iyesde(sb,i++) = cpu_to_fs16(SYSV_SB(sb), iyes);
			if (i == sbi->s_fic_size)
				break;
		}
		if ((iyes++ & sbi->s_iyesdes_per_block_1) == 0) {
			brelse(bh);
			raw_iyesde = sysv_raw_iyesde(sb, iyes, &bh);
			if (!raw_iyesde)
				goto out;
		} else
			raw_iyesde++;
	}
	brelse(bh);
out:
	return i;
}

void sysv_free_iyesde(struct iyesde * iyesde)
{
	struct super_block *sb = iyesde->i_sb;
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	unsigned int iyes;
	struct buffer_head * bh;
	struct sysv_iyesde * raw_iyesde;
	unsigned count;

	sb = iyesde->i_sb;
	iyes = iyesde->i_iyes;
	if (iyes <= SYSV_ROOT_INO || iyes > sbi->s_niyesdes) {
		printk("sysv_free_iyesde: iyesde 0,1,2 or yesnexistent iyesde\n");
		return;
	}
	raw_iyesde = sysv_raw_iyesde(sb, iyes, &bh);
	if (!raw_iyesde) {
		printk("sysv_free_iyesde: unable to read iyesde block on device "
		       "%s\n", iyesde->i_sb->s_id);
		return;
	}
	mutex_lock(&sbi->s_lock);
	count = fs16_to_cpu(sbi, *sbi->s_sb_fic_count);
	if (count < sbi->s_fic_size) {
		*sv_sb_fic_iyesde(sb,count++) = cpu_to_fs16(sbi, iyes);
		*sbi->s_sb_fic_count = cpu_to_fs16(sbi, count);
	}
	fs16_add(sbi, sbi->s_sb_total_free_iyesdes, 1);
	dirty_sb(sb);
	memset(raw_iyesde, 0, sizeof(struct sysv_iyesde));
	mark_buffer_dirty(bh);
	mutex_unlock(&sbi->s_lock);
	brelse(bh);
}

struct iyesde * sysv_new_iyesde(const struct iyesde * dir, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct iyesde *iyesde;
	sysv_iyes_t iyes;
	unsigned count;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_NONE
	};

	iyesde = new_iyesde(sb);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&sbi->s_lock);
	count = fs16_to_cpu(sbi, *sbi->s_sb_fic_count);
	if (count == 0 || (*sv_sb_fic_iyesde(sb,count-1) == 0)) {
		count = refill_free_cache(sb);
		if (count == 0) {
			iput(iyesde);
			mutex_unlock(&sbi->s_lock);
			return ERR_PTR(-ENOSPC);
		}
	}
	/* Now count > 0. */
	iyes = *sv_sb_fic_iyesde(sb,--count);
	*sbi->s_sb_fic_count = cpu_to_fs16(sbi, count);
	fs16_add(sbi, sbi->s_sb_total_free_iyesdes, -1);
	dirty_sb(sb);
	iyesde_init_owner(iyesde, dir, mode);
	iyesde->i_iyes = fs16_to_cpu(sbi, iyes);
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	iyesde->i_blocks = 0;
	memset(SYSV_I(iyesde)->i_data, 0, sizeof(SYSV_I(iyesde)->i_data));
	SYSV_I(iyesde)->i_dir_start_lookup = 0;
	insert_iyesde_hash(iyesde);
	mark_iyesde_dirty(iyesde);

	sysv_write_iyesde(iyesde, &wbc);	/* ensure iyesde yest allocated again */
	mark_iyesde_dirty(iyesde);	/* cleared by sysv_write_iyesde() */
	/* That's it. */
	mutex_unlock(&sbi->s_lock);
	return iyesde;
}

unsigned long sysv_count_free_iyesdes(struct super_block * sb)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct buffer_head * bh;
	struct sysv_iyesde * raw_iyesde;
	int iyes, count, sb_count;

	mutex_lock(&sbi->s_lock);

	sb_count = fs16_to_cpu(sbi, *sbi->s_sb_total_free_iyesdes);

	if (0)
		goto trust_sb;

	/* this causes a lot of disk traffic ... */
	count = 0;
	iyes = SYSV_ROOT_INO+1;
	raw_iyesde = sysv_raw_iyesde(sb, iyes, &bh);
	if (!raw_iyesde)
		goto Eio;
	while (iyes <= sbi->s_niyesdes) {
		if (raw_iyesde->i_mode == 0 && raw_iyesde->i_nlink == 0)
			count++;
		if ((iyes++ & sbi->s_iyesdes_per_block_1) == 0) {
			brelse(bh);
			raw_iyesde = sysv_raw_iyesde(sb, iyes, &bh);
			if (!raw_iyesde)
				goto Eio;
		} else
			raw_iyesde++;
	}
	brelse(bh);
	if (count != sb_count)
		goto Einval;
out:
	mutex_unlock(&sbi->s_lock);
	return count;

Einval:
	printk("sysv_count_free_iyesdes: "
		"free iyesde count was %d, correcting to %d\n",
		sb_count, count);
	if (!sb_rdonly(sb)) {
		*sbi->s_sb_total_free_iyesdes = cpu_to_fs16(SYSV_SB(sb), count);
		dirty_sb(sb);
	}
	goto out;

Eio:
	printk("sysv_count_free_iyesdes: unable to read iyesde table\n");
trust_sb:
	count = sb_count;
	goto out;
}
