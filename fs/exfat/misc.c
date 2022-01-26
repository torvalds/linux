// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Written 1992,1993 by Werner Almesberger
 *  22/11/2000 - Fixed fat_date_unix2dos for dates earlier than 01/01/1980
 *		 and date_dos2unix for date==0 by Igor Zhbanov(bsg@uniyar.ac.ru)
 * Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/blk_types.h>

#include "exfat_raw.h"
#include "exfat_fs.h"

/*
 * exfat_fs_error reports a file system problem that might indicate fa data
 * corruption/inconsistency. Depending on 'errors' mount option the
 * panic() is called, or error message is printed FAT and nothing is done,
 * or filesystem is remounted read-only (default behavior).
 * In case the file system is remounted read-only, it can be made writable
 * again by remounting it.
 */
void __exfat_fs_error(struct super_block *sb, int report, const char *fmt, ...)
{
	struct exfat_mount_options *opts = &EXFAT_SB(sb)->options;
	va_list args;
	struct va_format vaf;

	if (report) {
		va_start(args, fmt);
		vaf.fmt = fmt;
		vaf.va = &args;
		exfat_err(sb, "error, %pV", &vaf);
		va_end(args);
	}

	if (opts->errors == EXFAT_ERRORS_PANIC) {
		panic("exFAT-fs (%s): fs panic from previous error\n",
			sb->s_id);
	} else if (opts->errors == EXFAT_ERRORS_RO && !sb_rdonly(sb)) {
		sb->s_flags |= SB_RDONLY;
		exfat_err(sb, "Filesystem has been set read-only");
	}
}

/*
 * exfat_msg() - print preformated EXFAT specific messages.
 * All logs except what uses exfat_fs_error() should be written by exfat_msg()
 */
void exfat_msg(struct super_block *sb, const char *level, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	/* level means KERN_ pacility level */
	printk("%sexFAT-fs (%s): %pV\n", level, sb->s_id, &vaf);
	va_end(args);
}

#define SECS_PER_MIN    (60)
#define TIMEZONE_SEC(x)	((x) * 15 * SECS_PER_MIN)

static void exfat_adjust_tz(struct timespec64 *ts, u8 tz_off)
{
	if (tz_off <= 0x3F)
		ts->tv_sec -= TIMEZONE_SEC(tz_off);
	else /* 0x40 <= (tz_off & 0x7F) <=0x7F */
		ts->tv_sec += TIMEZONE_SEC(0x80 - tz_off);
}

/* Convert a EXFAT time/date pair to a UNIX date (seconds since 1 1 70). */
void exfat_get_entry_time(struct exfat_sb_info *sbi, struct timespec64 *ts,
		u8 tz, __le16 time, __le16 date, u8 time_cs)
{
	u16 t = le16_to_cpu(time);
	u16 d = le16_to_cpu(date);

	ts->tv_sec = mktime64(1980 + (d >> 9), d >> 5 & 0x000F, d & 0x001F,
			      t >> 11, (t >> 5) & 0x003F, (t & 0x001F) << 1);


	/* time_cs field represent 0 ~ 199cs(1990 ms) */
	if (time_cs) {
		ts->tv_sec += time_cs / 100;
		ts->tv_nsec = (time_cs % 100) * 10 * NSEC_PER_MSEC;
	} else
		ts->tv_nsec = 0;

	if (tz & EXFAT_TZ_VALID)
		/* Adjust timezone to UTC0. */
		exfat_adjust_tz(ts, tz & ~EXFAT_TZ_VALID);
	else
		/* Convert from local time to UTC using time_offset. */
		ts->tv_sec -= sbi->options.time_offset * SECS_PER_MIN;
}

/* Convert linear UNIX date to a EXFAT time/date pair. */
void exfat_set_entry_time(struct exfat_sb_info *sbi, struct timespec64 *ts,
		u8 *tz, __le16 *time, __le16 *date, u8 *time_cs)
{
	struct tm tm;
	u16 t, d;

	time64_to_tm(ts->tv_sec, 0, &tm);
	t = (tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec >> 1);
	d = ((tm.tm_year - 80) <<  9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday;

	*time = cpu_to_le16(t);
	*date = cpu_to_le16(d);

	/* time_cs field represent 0 ~ 199cs(1990 ms) */
	if (time_cs)
		*time_cs = (tm.tm_sec & 1) * 100 +
			ts->tv_nsec / (10 * NSEC_PER_MSEC);

	/*
	 * Record 00h value for OffsetFromUtc field and 1 value for OffsetValid
	 * to indicate that local time and UTC are the same.
	 */
	*tz = EXFAT_TZ_VALID;
}

/*
 * The timestamp for access_time has double seconds granularity.
 * (There is no 10msIncrement field for access_time unlike create/modify_time)
 * atime also has only a 2-second resolution.
 */
void exfat_truncate_atime(struct timespec64 *ts)
{
	ts->tv_sec = round_down(ts->tv_sec, 2);
	ts->tv_nsec = 0;
}

u16 exfat_calc_chksum16(void *data, int len, u16 chksum, int type)
{
	int i;
	u8 *c = (u8 *)data;

	for (i = 0; i < len; i++, c++) {
		if (unlikely(type == CS_DIR_ENTRY && (i == 2 || i == 3)))
			continue;
		chksum = ((chksum << 15) | (chksum >> 1)) + *c;
	}
	return chksum;
}

u32 exfat_calc_chksum32(void *data, int len, u32 chksum, int type)
{
	int i;
	u8 *c = (u8 *)data;

	for (i = 0; i < len; i++, c++) {
		if (unlikely(type == CS_BOOT_SECTOR &&
			     (i == 106 || i == 107 || i == 112)))
			continue;
		chksum = ((chksum << 31) | (chksum >> 1)) + *c;
	}
	return chksum;
}

void exfat_update_bh(struct buffer_head *bh, int sync)
{
	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);

	if (sync)
		sync_dirty_buffer(bh);
}

int exfat_update_bhs(struct buffer_head **bhs, int nr_bhs, int sync)
{
	int i, err = 0;

	for (i = 0; i < nr_bhs; i++) {
		set_buffer_uptodate(bhs[i]);
		mark_buffer_dirty(bhs[i]);
		if (sync)
			write_dirty_buffer(bhs[i], REQ_SYNC);
	}

	for (i = 0; i < nr_bhs && sync; i++) {
		wait_on_buffer(bhs[i]);
		if (!err && !buffer_uptodate(bhs[i]))
			err = -EIO;
	}
	return err;
}

void exfat_chain_set(struct exfat_chain *ec, unsigned int dir,
		unsigned int size, unsigned char flags)
{
	ec->dir = dir;
	ec->size = size;
	ec->flags = flags;
}

void exfat_chain_dup(struct exfat_chain *dup, struct exfat_chain *ec)
{
	return exfat_chain_set(dup, ec->dir, ec->size, ec->flags);
}
