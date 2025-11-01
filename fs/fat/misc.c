// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/fat/misc.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  22/11/2000 - Fixed fat_date_unix2dos for dates earlier than 01/01/1980
 *		 and date_dos2unix for date==0 by Igor Zhbanov(bsg@uniyar.ac.ru)
 */

#include "fat.h"
#include <linux/iversion.h>

/*
 * fat_fs_error reports a file system problem that might indicate fa data
 * corruption/inconsistency. Depending on 'errors' mount option the
 * panic() is called, or error message is printed FAT and nothing is done,
 * or filesystem is remounted read-only (default behavior).
 * In case the file system is remounted read-only, it can be made writable
 * again by remounting it.
 */
void __fat_fs_error(struct super_block *sb, int report, const char *fmt, ...)
{
	struct fat_mount_options *opts = &MSDOS_SB(sb)->options;
	va_list args;
	struct va_format vaf;

	if (report) {
		va_start(args, fmt);
		vaf.fmt = fmt;
		vaf.va = &args;
		fat_msg(sb, KERN_ERR, "error, %pV", &vaf);
		va_end(args);
	}

	if (opts->errors == FAT_ERRORS_PANIC)
		panic("FAT-fs (%s): fs panic from previous error\n", sb->s_id);
	else if (opts->errors == FAT_ERRORS_RO && !sb_rdonly(sb)) {
		sb->s_flags |= SB_RDONLY;
		fat_msg(sb, KERN_ERR, "Filesystem has been set read-only");
	}
}
EXPORT_SYMBOL_GPL(__fat_fs_error);

/**
 * _fat_msg() - Print a preformatted FAT message based on a superblock.
 * @sb: A pointer to a &struct super_block
 * @level: A Kernel printk level constant
 * @fmt: The printf-style format string to print.
 *
 * Everything that is not fat_fs_error() should be fat_msg().
 *
 * fat_msg() wraps _fat_msg() for printk indexing.
 */
void _fat_msg(struct super_block *sb, const char *level, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	_printk(FAT_PRINTK_PREFIX "%pV\n", level, sb->s_id, &vaf);
	va_end(args);
}

/* Flushes the number of free clusters on FAT32 */
/* XXX: Need to write one per FSINFO block.  Currently only writes 1 */
int fat_clusters_flush(struct super_block *sb)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bh;
	struct fat_boot_fsinfo *fsinfo;

	if (!is_fat32(sbi))
		return 0;

	bh = sb_bread(sb, sbi->fsinfo_sector);
	if (bh == NULL) {
		fat_msg(sb, KERN_ERR, "bread failed in fat_clusters_flush");
		return -EIO;
	}

	fsinfo = (struct fat_boot_fsinfo *)bh->b_data;
	/* Sanity check */
	if (!IS_FSINFO(fsinfo)) {
		fat_msg(sb, KERN_ERR, "Invalid FSINFO signature: "
		       "0x%08x, 0x%08x (sector = %lu)",
		       le32_to_cpu(fsinfo->signature1),
		       le32_to_cpu(fsinfo->signature2),
		       sbi->fsinfo_sector);
	} else {
		if (sbi->free_clusters != -1)
			fsinfo->free_clusters = cpu_to_le32(sbi->free_clusters);
		if (sbi->prev_free != -1)
			fsinfo->next_cluster = cpu_to_le32(sbi->prev_free);
		mark_buffer_dirty(bh);
	}
	brelse(bh);

	return 0;
}

/*
 * fat_chain_add() adds a new cluster to the chain of clusters represented
 * by inode.
 */
int fat_chain_add(struct inode *inode, int new_dclus, int nr_cluster)
{
	struct super_block *sb = inode->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	int ret, new_fclus, last;

	/*
	 * We must locate the last cluster of the file to add this new
	 * one (new_dclus) to the end of the link list (the FAT).
	 */
	last = new_fclus = 0;
	if (MSDOS_I(inode)->i_start) {
		int fclus, dclus;

		ret = fat_get_cluster(inode, FAT_ENT_EOF, &fclus, &dclus);
		if (ret < 0)
			return ret;
		new_fclus = fclus + 1;
		last = dclus;
	}

	/* add new one to the last of the cluster chain */
	if (last) {
		struct fat_entry fatent;

		fatent_init(&fatent);
		ret = fat_ent_read(inode, &fatent, last);
		if (ret >= 0) {
			int wait = inode_needs_sync(inode);
			ret = fat_ent_write(inode, &fatent, new_dclus, wait);
			fatent_brelse(&fatent);
		}
		if (ret < 0)
			return ret;
		/*
		 * FIXME:Although we can add this cache, fat_cache_add() is
		 * assuming to be called after linear search with fat_cache_id.
		 */
//		fat_cache_add(inode, new_fclus, new_dclus);
	} else {
		MSDOS_I(inode)->i_start = new_dclus;
		MSDOS_I(inode)->i_logstart = new_dclus;
		/*
		 * Since generic_write_sync() synchronizes regular files later,
		 * we sync here only directories.
		 */
		if (S_ISDIR(inode->i_mode) && IS_DIRSYNC(inode)) {
			ret = fat_sync_inode(inode);
			if (ret)
				return ret;
		} else
			mark_inode_dirty(inode);
	}
	if (new_fclus != (inode->i_blocks >> (sbi->cluster_bits - 9))) {
		fat_fs_error_ratelimit(
			sb, "clusters badly computed (%d != %llu)", new_fclus,
			(llu)(inode->i_blocks >> (sbi->cluster_bits - 9)));
		fat_cache_inval_inode(inode);
	}
	inode->i_blocks += nr_cluster << (sbi->cluster_bits - 9);

	return 0;
}

/*
 * The epoch of FAT timestamp is 1980.
 *     :  bits :     value
 * date:  0 -  4: day	(1 -  31)
 * date:  5 -  8: month	(1 -  12)
 * date:  9 - 15: year	(0 - 127) from 1980
 * time:  0 -  4: sec	(0 -  29) 2sec counts
 * time:  5 - 10: min	(0 -  59)
 * time: 11 - 15: hour	(0 -  23)
 */
#define SECS_PER_MIN	60
#define SECS_PER_HOUR	(60 * 60)
#define SECS_PER_DAY	(SECS_PER_HOUR * 24)
/* days between 1.1.70 and 1.1.80 (2 leap days) */
#define DAYS_DELTA	(365 * 10 + 2)
/* 120 (2100 - 1980) isn't leap year */
#define YEAR_2100	120
#define IS_LEAP_YEAR(y)	(!((y) & 3) && (y) != YEAR_2100)

/* Linear day numbers of the respective 1sts in non-leap years. */
static long days_in_year[] = {
	/* Jan  Feb  Mar  Apr  May  Jun  Jul  Aug  Sep  Oct  Nov  Dec */
	0,   0,  31,  59,  90, 120, 151, 181, 212, 243, 273, 304, 334, 0, 0, 0,
};

static inline int fat_tz_offset(const struct msdos_sb_info *sbi)
{
	return (sbi->options.tz_set ?
	       -sbi->options.time_offset :
	       sys_tz.tz_minuteswest) * SECS_PER_MIN;
}

/* Convert a FAT time/date pair to a UNIX date (seconds since 1 1 70). */
void fat_time_fat2unix(struct msdos_sb_info *sbi, struct timespec64 *ts,
		       __le16 __time, __le16 __date, u8 time_cs)
{
	u16 time = le16_to_cpu(__time), date = le16_to_cpu(__date);
	time64_t second;
	long day, leap_day, month, year;

	year  = date >> 9;
	month = max(1, (date >> 5) & 0xf);
	day   = max(1, date & 0x1f) - 1;

	leap_day = (year + 3) / 4;
	if (year > YEAR_2100)		/* 2100 isn't leap year */
		leap_day--;
	if (IS_LEAP_YEAR(year) && month > 2)
		leap_day++;

	second =  (time & 0x1f) << 1;
	second += ((time >> 5) & 0x3f) * SECS_PER_MIN;
	second += (time >> 11) * SECS_PER_HOUR;
	second += (time64_t)(year * 365 + leap_day
		   + days_in_year[month] + day
		   + DAYS_DELTA) * SECS_PER_DAY;

	second += fat_tz_offset(sbi);

	if (time_cs) {
		ts->tv_sec = second + (time_cs / 100);
		ts->tv_nsec = (time_cs % 100) * 10000000;
	} else {
		ts->tv_sec = second;
		ts->tv_nsec = 0;
	}
}

/* Export fat_time_fat2unix() for the fat_test KUnit tests. */
EXPORT_SYMBOL_GPL(fat_time_fat2unix);

/* Convert linear UNIX date to a FAT time/date pair. */
void fat_time_unix2fat(struct msdos_sb_info *sbi, struct timespec64 *ts,
		       __le16 *time, __le16 *date, u8 *time_cs)
{
	struct tm tm;
	time64_to_tm(ts->tv_sec, -fat_tz_offset(sbi), &tm);

	/*  FAT can only support year between 1980 to 2107 */
	if (tm.tm_year < 1980 - 1900) {
		*time = 0;
		*date = cpu_to_le16((0 << 9) | (1 << 5) | 1);
		if (time_cs)
			*time_cs = 0;
		return;
	}
	if (tm.tm_year > 2107 - 1900) {
		*time = cpu_to_le16((23 << 11) | (59 << 5) | 29);
		*date = cpu_to_le16((127 << 9) | (12 << 5) | 31);
		if (time_cs)
			*time_cs = 199;
		return;
	}

	/* from 1900 -> from 1980 */
	tm.tm_year -= 80;
	/* 0~11 -> 1~12 */
	tm.tm_mon++;
	/* 0~59 -> 0~29(2sec counts) */
	tm.tm_sec >>= 1;

	*time = cpu_to_le16(tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec);
	*date = cpu_to_le16(tm.tm_year << 9 | tm.tm_mon << 5 | tm.tm_mday);
	if (time_cs)
		*time_cs = (ts->tv_sec & 1) * 100 + ts->tv_nsec / 10000000;
}
EXPORT_SYMBOL_GPL(fat_time_unix2fat);

static inline struct timespec64 fat_timespec64_trunc_2secs(struct timespec64 ts)
{
	return (struct timespec64){ ts.tv_sec & ~1ULL, 0 };
}

/*
 * truncate atime to 24 hour granularity (00:00:00 in local timezone)
 */
struct timespec64 fat_truncate_atime(const struct msdos_sb_info *sbi,
				     const struct timespec64 *ts)
{
	/* to localtime */
	time64_t seconds = ts->tv_sec - fat_tz_offset(sbi);
	s32 remainder;

	div_s64_rem(seconds, SECS_PER_DAY, &remainder);
	/* to day boundary, and back to unix time */
	seconds = seconds + fat_tz_offset(sbi) - remainder;

	return (struct timespec64){ seconds, 0 };
}

/*
 * truncate mtime to 2 second granularity
 */
struct timespec64 fat_truncate_mtime(const struct msdos_sb_info *sbi,
				     const struct timespec64 *ts)
{
	return fat_timespec64_trunc_2secs(*ts);
}

/*
 * truncate the various times with appropriate granularity:
 *   all times in root node are always 0
 */
int fat_truncate_time(struct inode *inode, struct timespec64 *now, int flags)
{
	struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
	struct timespec64 ts;

	if (inode->i_ino == MSDOS_ROOT_INO)
		return 0;

	if (now == NULL) {
		now = &ts;
		ts = current_time(inode);
	}

	if (flags & S_ATIME)
		inode_set_atime_to_ts(inode, fat_truncate_atime(sbi, now));
	/*
	 * ctime and mtime share the same on-disk field, and should be
	 * identical in memory. all mtime updates will be applied to ctime,
	 * but ctime updates are ignored.
	 */
	if (flags & S_MTIME)
		inode_set_mtime_to_ts(inode,
				      inode_set_ctime_to_ts(inode, fat_truncate_mtime(sbi, now)));

	return 0;
}
EXPORT_SYMBOL_GPL(fat_truncate_time);

int fat_update_time(struct inode *inode, int flags)
{
	int dirty_flags = 0;

	if (inode->i_ino == MSDOS_ROOT_INO)
		return 0;

	if (flags & (S_ATIME | S_CTIME | S_MTIME)) {
		fat_truncate_time(inode, NULL, flags);
		if (inode->i_sb->s_flags & SB_LAZYTIME)
			dirty_flags |= I_DIRTY_TIME;
		else
			dirty_flags |= I_DIRTY_SYNC;
	}

	__mark_inode_dirty(inode, dirty_flags);
	return 0;
}
EXPORT_SYMBOL_GPL(fat_update_time);

int fat_sync_bhs(struct buffer_head **bhs, int nr_bhs)
{
	int i, err = 0;

	for (i = 0; i < nr_bhs; i++)
		write_dirty_buffer(bhs[i], 0);

	for (i = 0; i < nr_bhs; i++) {
		wait_on_buffer(bhs[i]);
		if (!err && !buffer_uptodate(bhs[i]))
			err = -EIO;
	}
	return err;
}
