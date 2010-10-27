/*
 *  linux/fs/fat/misc.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  22/11/2000 - Fixed fat_date_unix2dos for dates earlier than 01/01/1980
 *		 and date_dos2unix for date==0 by Igor Zhbanov(bsg@uniyar.ac.ru)
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/time.h>
#include "fat.h"

/*
 * fat_fs_error reports a file system problem that might indicate fa data
 * corruption/inconsistency. Depending on 'errors' mount option the
 * panic() is called, or error message is printed FAT and nothing is done,
 * or filesystem is remounted read-only (default behavior).
 * In case the file system is remounted read-only, it can be made writable
 * again by remounting it.
 */
void __fat_fs_error(struct super_block *s, int report, const char *fmt, ...)
{
	struct fat_mount_options *opts = &MSDOS_SB(s)->options;
	va_list args;

	if (report) {
		printk(KERN_ERR "FAT: Filesystem error (dev %s)\n", s->s_id);

		printk(KERN_ERR "    ");
		va_start(args, fmt);
		vprintk(fmt, args);
		va_end(args);
		printk("\n");
	}

	if (opts->errors == FAT_ERRORS_PANIC)
		panic("FAT: fs panic from previous error\n");
	else if (opts->errors == FAT_ERRORS_RO && !(s->s_flags & MS_RDONLY)) {
		s->s_flags |= MS_RDONLY;
		printk(KERN_ERR "FAT: Filesystem has been set read-only\n");
	}
}
EXPORT_SYMBOL_GPL(__fat_fs_error);

/* Flushes the number of free clusters on FAT32 */
/* XXX: Need to write one per FSINFO block.  Currently only writes 1 */
int fat_clusters_flush(struct super_block *sb)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bh;
	struct fat_boot_fsinfo *fsinfo;

	if (sbi->fat_bits != 32)
		return 0;

	bh = sb_bread(sb, sbi->fsinfo_sector);
	if (bh == NULL) {
		printk(KERN_ERR "FAT: bread failed in fat_clusters_flush\n");
		return -EIO;
	}

	fsinfo = (struct fat_boot_fsinfo *)bh->b_data;
	/* Sanity check */
	if (!IS_FSINFO(fsinfo)) {
		printk(KERN_ERR "FAT: Invalid FSINFO signature: "
		       "0x%08x, 0x%08x (sector = %lu)\n",
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
		fat_fs_error(sb, "clusters badly computed (%d != %llu)",
			     new_fclus,
			     (llu)(inode->i_blocks >> (sbi->cluster_bits - 9)));
		fat_cache_inval_inode(inode);
	}
	inode->i_blocks += nr_cluster << (sbi->cluster_bits - 9);

	return 0;
}

extern struct timezone sys_tz;

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
static time_t days_in_year[] = {
	/* Jan  Feb  Mar  Apr  May  Jun  Jul  Aug  Sep  Oct  Nov  Dec */
	0,   0,  31,  59,  90, 120, 151, 181, 212, 243, 273, 304, 334, 0, 0, 0,
};

/* Convert a FAT time/date pair to a UNIX date (seconds since 1 1 70). */
void fat_time_fat2unix(struct msdos_sb_info *sbi, struct timespec *ts,
		       __le16 __time, __le16 __date, u8 time_cs)
{
	u16 time = le16_to_cpu(__time), date = le16_to_cpu(__date);
	time_t second, day, leap_day, month, year;

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
	second += (year * 365 + leap_day
		   + days_in_year[month] + day
		   + DAYS_DELTA) * SECS_PER_DAY;

	if (!sbi->options.tz_utc)
		second += sys_tz.tz_minuteswest * SECS_PER_MIN;

	if (time_cs) {
		ts->tv_sec = second + (time_cs / 100);
		ts->tv_nsec = (time_cs % 100) * 10000000;
	} else {
		ts->tv_sec = second;
		ts->tv_nsec = 0;
	}
}

/* Convert linear UNIX date to a FAT time/date pair. */
void fat_time_unix2fat(struct msdos_sb_info *sbi, struct timespec *ts,
		       __le16 *time, __le16 *date, u8 *time_cs)
{
	struct tm tm;
	time_to_tm(ts->tv_sec, sbi->options.tz_utc ? 0 :
		   -sys_tz.tz_minuteswest * 60, &tm);

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

int fat_sync_bhs(struct buffer_head **bhs, int nr_bhs)
{
	int i, err = 0;

	for (i = 0; i < nr_bhs; i++)
		write_dirty_buffer(bhs[i], WRITE);

	for (i = 0; i < nr_bhs; i++) {
		wait_on_buffer(bhs[i]);
		if (buffer_eopnotsupp(bhs[i])) {
			clear_buffer_eopnotsupp(bhs[i]);
			err = -EOPNOTSUPP;
		} else if (!err && !buffer_uptodate(bhs[i]))
			err = -EIO;
	}
	return err;
}
