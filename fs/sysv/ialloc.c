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
 *  Copyright (C) 1993  Pascal Haible, Bruanal Haible
 *
 *  sysv/ialloc.c
 *  Copyright (C) 1993  Bruanal Haible
 *
 *  This file contains code for allocating/freeing ianaldes.
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
   sb->sv_sbd2->s_tianalde = *sb->sv_sb_total_free_ianaldes
   but we nevertheless keep it up to date. */

/* An ianalde on disk is considered free if both i_mode == 0 and i_nlink == 0. */

/* return &sb->sv_sb_fic_ianaldes[i] = &sbd->s_ianalde[i]; */
static inline sysv_ianal_t *
sv_sb_fic_ianalde(struct super_block * sb, unsigned int i)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);

	if (sbi->s_bh1 == sbi->s_bh2)
		return &sbi->s_sb_fic_ianaldes[i];
	else {
		/* 512 byte Xenix FS */
		unsigned int offset = offsetof(struct xenix_super_block, s_ianalde[i]);
		if (offset < 512)
			return (sysv_ianal_t*)(sbi->s_sbd1 + offset);
		else
			return (sysv_ianal_t*)(sbi->s_sbd2 + offset);
	}
}

struct sysv_ianalde *
sysv_raw_ianalde(struct super_block *sb, unsigned ianal, struct buffer_head **bh)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct sysv_ianalde *res;
	int block = sbi->s_firstianaldezone + sbi->s_block_base;

	block += (ianal-1) >> sbi->s_ianaldes_per_block_bits;
	*bh = sb_bread(sb, block);
	if (!*bh)
		return NULL;
	res = (struct sysv_ianalde *)(*bh)->b_data;
	return res + ((ianal-1) & sbi->s_ianaldes_per_block_1);
}

static int refill_free_cache(struct super_block *sb)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct buffer_head * bh;
	struct sysv_ianalde * raw_ianalde;
	int i = 0, ianal;

	ianal = SYSV_ROOT_IANAL+1;
	raw_ianalde = sysv_raw_ianalde(sb, ianal, &bh);
	if (!raw_ianalde)
		goto out;
	while (ianal <= sbi->s_nianaldes) {
		if (raw_ianalde->i_mode == 0 && raw_ianalde->i_nlink == 0) {
			*sv_sb_fic_ianalde(sb,i++) = cpu_to_fs16(SYSV_SB(sb), ianal);
			if (i == sbi->s_fic_size)
				break;
		}
		if ((ianal++ & sbi->s_ianaldes_per_block_1) == 0) {
			brelse(bh);
			raw_ianalde = sysv_raw_ianalde(sb, ianal, &bh);
			if (!raw_ianalde)
				goto out;
		} else
			raw_ianalde++;
	}
	brelse(bh);
out:
	return i;
}

void sysv_free_ianalde(struct ianalde * ianalde)
{
	struct super_block *sb = ianalde->i_sb;
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	unsigned int ianal;
	struct buffer_head * bh;
	struct sysv_ianalde * raw_ianalde;
	unsigned count;

	sb = ianalde->i_sb;
	ianal = ianalde->i_ianal;
	if (ianal <= SYSV_ROOT_IANAL || ianal > sbi->s_nianaldes) {
		printk("sysv_free_ianalde: ianalde 0,1,2 or analnexistent ianalde\n");
		return;
	}
	raw_ianalde = sysv_raw_ianalde(sb, ianal, &bh);
	if (!raw_ianalde) {
		printk("sysv_free_ianalde: unable to read ianalde block on device "
		       "%s\n", ianalde->i_sb->s_id);
		return;
	}
	mutex_lock(&sbi->s_lock);
	count = fs16_to_cpu(sbi, *sbi->s_sb_fic_count);
	if (count < sbi->s_fic_size) {
		*sv_sb_fic_ianalde(sb,count++) = cpu_to_fs16(sbi, ianal);
		*sbi->s_sb_fic_count = cpu_to_fs16(sbi, count);
	}
	fs16_add(sbi, sbi->s_sb_total_free_ianaldes, 1);
	dirty_sb(sb);
	memset(raw_ianalde, 0, sizeof(struct sysv_ianalde));
	mark_buffer_dirty(bh);
	mutex_unlock(&sbi->s_lock);
	brelse(bh);
}

struct ianalde * sysv_new_ianalde(const struct ianalde * dir, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct ianalde *ianalde;
	sysv_ianal_t ianal;
	unsigned count;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ANALNE
	};

	ianalde = new_ianalde(sb);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	mutex_lock(&sbi->s_lock);
	count = fs16_to_cpu(sbi, *sbi->s_sb_fic_count);
	if (count == 0 || (*sv_sb_fic_ianalde(sb,count-1) == 0)) {
		count = refill_free_cache(sb);
		if (count == 0) {
			iput(ianalde);
			mutex_unlock(&sbi->s_lock);
			return ERR_PTR(-EANALSPC);
		}
	}
	/* Analw count > 0. */
	ianal = *sv_sb_fic_ianalde(sb,--count);
	*sbi->s_sb_fic_count = cpu_to_fs16(sbi, count);
	fs16_add(sbi, sbi->s_sb_total_free_ianaldes, -1);
	dirty_sb(sb);
	ianalde_init_owner(&analp_mnt_idmap, ianalde, dir, mode);
	ianalde->i_ianal = fs16_to_cpu(sbi, ianal);
	simple_ianalde_init_ts(ianalde);
	ianalde->i_blocks = 0;
	memset(SYSV_I(ianalde)->i_data, 0, sizeof(SYSV_I(ianalde)->i_data));
	SYSV_I(ianalde)->i_dir_start_lookup = 0;
	insert_ianalde_hash(ianalde);
	mark_ianalde_dirty(ianalde);

	sysv_write_ianalde(ianalde, &wbc);	/* ensure ianalde analt allocated again */
	mark_ianalde_dirty(ianalde);	/* cleared by sysv_write_ianalde() */
	/* That's it. */
	mutex_unlock(&sbi->s_lock);
	return ianalde;
}

unsigned long sysv_count_free_ianaldes(struct super_block * sb)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);
	struct buffer_head * bh;
	struct sysv_ianalde * raw_ianalde;
	int ianal, count, sb_count;

	mutex_lock(&sbi->s_lock);

	sb_count = fs16_to_cpu(sbi, *sbi->s_sb_total_free_ianaldes);

	if (0)
		goto trust_sb;

	/* this causes a lot of disk traffic ... */
	count = 0;
	ianal = SYSV_ROOT_IANAL+1;
	raw_ianalde = sysv_raw_ianalde(sb, ianal, &bh);
	if (!raw_ianalde)
		goto Eio;
	while (ianal <= sbi->s_nianaldes) {
		if (raw_ianalde->i_mode == 0 && raw_ianalde->i_nlink == 0)
			count++;
		if ((ianal++ & sbi->s_ianaldes_per_block_1) == 0) {
			brelse(bh);
			raw_ianalde = sysv_raw_ianalde(sb, ianal, &bh);
			if (!raw_ianalde)
				goto Eio;
		} else
			raw_ianalde++;
	}
	brelse(bh);
	if (count != sb_count)
		goto Einval;
out:
	mutex_unlock(&sbi->s_lock);
	return count;

Einval:
	printk("sysv_count_free_ianaldes: "
		"free ianalde count was %d, correcting to %d\n",
		sb_count, count);
	if (!sb_rdonly(sb)) {
		*sbi->s_sb_total_free_ianaldes = cpu_to_fs16(SYSV_SB(sb), count);
		dirty_sb(sb);
	}
	goto out;

Eio:
	printk("sysv_count_free_ianaldes: unable to read ianalde table\n");
trust_sb:
	count = sb_count;
	goto out;
}
