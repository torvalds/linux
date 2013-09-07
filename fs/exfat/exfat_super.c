/* Some of the source code in this file came from "linux/fs/fat/file.c","linux/fs/fat/inode.c" and "linux/fs/fat/misc.c".  */
/*
 *  linux/fs/fat/file.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  regular file handling primitives for fat-based filesystems
 */

/*
 *  linux/fs/fat/inode.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  VFAT extensions by Gordon Chaffee, merged with msdos fs by Henrik Storner
 *  Rewritten for the constant inumbers support by Al Viro
 *
 *  Fixes:
 *
 *    Max Cohan: Fixed invalid FSINFO offset when info_sector is 0
 */

/*
 *  linux/fs/fat/misc.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  22/11/2000 - Fixed fat_date_unix2dos for dates earlier than 01/01/1980
 *         and date_dos2unix for date==0 by Igor Zhbanov(bsg@uniyar.ac.ru)
 */

/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/slab.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
#include <linux/smp_lock.h>
#endif
#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/parser.h>
#include <linux/uio.h>
#include <linux/writeback.h>
#include <linux/log2.h>
#include <linux/hash.h>
#include <linux/backing-dev.h>
#include <linux/sched.h>
#include <linux/fs_struct.h>
#include <linux/namei.h>
#include <asm/current.h>
#include <asm/unaligned.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#include <linux/aio.h>
#endif

#include "exfat_version.h"
#include "exfat_config.h"
#include "exfat_global.h"
#include "exfat_data.h"
#include "exfat_oal.h"

#include "exfat_blkdev.h"
#include "exfat_cache.h"
#include "exfat_part.h"
#include "exfat_nls.h"
#include "exfat_api.h"
#include "exfat.h"

#include "exfat_super.h"

static struct kmem_cache *exfat_inode_cachep;

static int exfat_default_codepage = CONFIG_EXFAT_DEFAULT_CODEPAGE;
static char exfat_default_iocharset[] = CONFIG_EXFAT_DEFAULT_IOCHARSET;

extern struct timezone sys_tz;

#define CHECK_ERR(x)	BUG_ON(x)
#define ELAPSED_TIME        0

#if (ELAPSED_TIME == 1)
#include <linux/time.h>

static UINT32 __t1, __t2;
static UINT32 get_current_msec(void)
{
	struct timeval tm;
	do_gettimeofday(&tm);
	return((UINT32)(tm.tv_sec*1000000 + tm.tv_usec));
}
#define TIME_START()        do {__t1 = get_current_msec();} while (0)
#define TIME_END()          do {__t2 = get_current_msec();} while (0)
#define PRINT_TIME(n)       do {printk("[EXFAT] Elapsed time %d = %d (usec)\n", n, (__t2 - __t1));} while (0)
#else
#define TIME_START()
#define TIME_END()
#define PRINT_TIME(n)
#endif

#define UNIX_SECS_1980    315532800L

#if BITS_PER_LONG == 64
#define UNIX_SECS_2108    4354819200L
#endif
/* days between 1.1.70 and 1.1.80 (2 leap days) */
#define DAYS_DELTA_DECADE    (365 * 10 + 2)
/* 120 (2100 - 1980) isn't leap year */
#define NO_LEAP_YEAR_2100    (120)
#define IS_LEAP_YEAR(y)    (!((y) & 0x3) && (y) != NO_LEAP_YEAR_2100)

#define SECS_PER_MIN    (60)
#define SECS_PER_HOUR   (60 * SECS_PER_MIN)
#define SECS_PER_DAY    (24 * SECS_PER_HOUR)

#define MAKE_LEAP_YEAR(leap_year, year)                         \
        do {                                                    \
                if (unlikely(year > NO_LEAP_YEAR_2100))         \
                        leap_year = ((year + 3) / 4) - 1;       \
                else                                            \
                        leap_year = ((year + 3) / 4);           \
        } while(0)

/* Linear day numbers of the respective 1sts in non-leap years. */
static time_t accum_days_in_year[] = {
	/* Jan  Feb  Mar  Apr  May  Jun  Jul  Aug  Sep  Oct  Nov  Dec */
	0,   0,  31,  59,  90, 120, 151, 181, 212, 243, 273, 304, 334, 0, 0, 0,
};

static void _exfat_truncate(struct inode *inode, loff_t old_size);

/* Convert a FAT time/date pair to a UNIX date (seconds since 1 1 70). */
void exfat_time_fat2unix(struct exfat_sb_info *sbi, struct timespec *ts,
						 DATE_TIME_T *tp)
{
	time_t year = tp->Year;
	time_t ld;

	MAKE_LEAP_YEAR(ld, year);

	if (IS_LEAP_YEAR(year) && (tp->Month) > 2)
		ld++;

	ts->tv_sec =  tp->Second  + tp->Minute * SECS_PER_MIN
				  + tp->Hour * SECS_PER_HOUR
				  + (year * 365 + ld + accum_days_in_year[(tp->Month)] + (tp->Day - 1) + DAYS_DELTA_DECADE) * SECS_PER_DAY
				  + sys_tz.tz_minuteswest * SECS_PER_MIN;
	ts->tv_nsec = 0;
}

/* Convert linear UNIX date to a FAT time/date pair. */
void exfat_time_unix2fat(struct exfat_sb_info *sbi, struct timespec *ts,
						 DATE_TIME_T *tp)
{
	time_t second = ts->tv_sec;
	time_t day, month, year;
	time_t ld;

	second -= sys_tz.tz_minuteswest * SECS_PER_MIN;

	/* Jan 1 GMT 00:00:00 1980. But what about another time zone? */
	if (second < UNIX_SECS_1980) {
		tp->Second  = 0;
		tp->Minute  = 0;
		tp->Hour = 0;
		tp->Day  = 1;
		tp->Month  = 1;
		tp->Year = 0;
		return;
	}
#if (BITS_PER_LONG == 64)
	if (second >= UNIX_SECS_2108) {
		tp->Second  = 59;
		tp->Minute  = 59;
		tp->Hour = 23;
		tp->Day  = 31;
		tp->Month  = 12;
		tp->Year = 127;
		return;
	}
#endif
	day = second / SECS_PER_DAY - DAYS_DELTA_DECADE;
	year = day / 365;
	MAKE_LEAP_YEAR(ld, year);
	if (year * 365 + ld > day)
		year--;

	MAKE_LEAP_YEAR(ld, year);
	day -= year * 365 + ld;

	if (IS_LEAP_YEAR(year) && day == accum_days_in_year[3]) {
		month = 2;
	} else {
		if (IS_LEAP_YEAR(year) && day > accum_days_in_year[3])
			day--;
		for (month = 1; month < 12; month++) {
			if (accum_days_in_year[month + 1] > day)
				break;
		}
	}
	day -= accum_days_in_year[month];

	tp->Second  = second % SECS_PER_MIN;
	tp->Minute  = (second / SECS_PER_MIN) % 60;
	tp->Hour = (second / SECS_PER_HOUR) % 24;
	tp->Day  = day + 1;
	tp->Month  = month;
	tp->Year = year;
}

static struct inode *exfat_iget(struct super_block *sb, loff_t i_pos);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
static int exfat_generic_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
#else
static long exfat_generic_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#endif
static int exfat_sync_inode(struct inode *inode);
static struct inode *exfat_build_inode(struct super_block *sb, FILE_ID_T *fid, loff_t i_pos);
static void exfat_detach(struct inode *inode);
static void exfat_attach(struct inode *inode, loff_t i_pos);
static inline unsigned long exfat_hash(loff_t i_pos);
static int exfat_write_inode(struct inode *inode, struct writeback_control *wbc);
static void exfat_write_super(struct super_block *sb);

static void __lock_super(struct super_block *sb)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
	lock_super(sb);
#else
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	mutex_lock(&sbi->s_lock);
#endif
}

static void __unlock_super(struct super_block *sb)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
	unlock_super(sb);
#else
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	mutex_unlock(&sbi->s_lock);
#endif
}

static int __is_sb_dirty(struct super_block *sb)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
        return sb->s_dirt;
#else
        struct exfat_sb_info *sbi = EXFAT_SB(sb);
        return sbi->s_dirt;
#endif
}

static void __set_sb_clean(struct super_block *sb)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
	sb->s_dirt = 0;
#else
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	sbi->s_dirt = 0;
#endif
}

/*======================================================================*/
/*  Directory Entry Operations                                          */
/*======================================================================*/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)
static int exfat_readdir(struct file *filp, struct dir_context *ctx)
#else
static int exfat_readdir(struct file *filp, void *dirent, filldir_t filldir)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)
	struct inode *inode = file_inode(filp);
#else
	struct inode *inode = filp->f_path.dentry->d_inode;
#endif
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	FS_INFO_T *p_fs = &(sbi->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);
	DIR_ENTRY_T de;
	unsigned long inum;
	loff_t cpos;
	int err = 0;

	__lock_super(sb);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)
	cpos = ctx->pos;
#else
	cpos = filp->f_pos;
#endif
	/* Fake . and .. for the root directory. */
	if ((p_fs->vol_type == EXFAT) || (inode->i_ino == EXFAT_ROOT_INO)) {
		while (cpos < 2) {
			if (inode->i_ino == EXFAT_ROOT_INO)
				inum = EXFAT_ROOT_INO;
			else if (cpos == 0)
				inum = inode->i_ino;
			else /* (cpos == 1) */
				inum = parent_ino(filp->f_path.dentry);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)
			if (!dir_emit_dots(filp, ctx))
#else
			if (filldir(dirent, "..", cpos+1, cpos, inum, DT_DIR) < 0)
#endif
				goto out;
			cpos++;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)
			ctx->pos++;
#else
			filp->f_pos++;
#endif
		}
		if (cpos == 2) {
			cpos = 0;
		}
	}
	if (cpos & (DENTRY_SIZE - 1)) {
		err = -ENOENT;
		goto out;
	}

get_new:
	EXFAT_I(inode)->fid.size = i_size_read(inode);
	EXFAT_I(inode)->fid.rwoffset = cpos >> DENTRY_SIZE_BITS;

	err = FsReadDir(inode, &de);
	if (err) {
		/* at least we tried to read a sector
		 * move cpos to next sector position (should be aligned)
		 */
		if (err == FFS_MEDIAERR) {
			cpos += 1 << p_bd->sector_size_bits;
			cpos &= ~((1 << p_bd->sector_size_bits)-1);
		}

		err = -EIO;
		goto end_of_dir;
	}

	cpos = EXFAT_I(inode)->fid.rwoffset << DENTRY_SIZE_BITS;

	if (!de.Name[0])
		goto end_of_dir;

	if (!memcmp(de.ShortName, DOS_CUR_DIR_NAME, DOS_NAME_LENGTH)) {
		inum = inode->i_ino;
	} else if (!memcmp(de.ShortName, DOS_PAR_DIR_NAME, DOS_NAME_LENGTH)) {
		inum = parent_ino(filp->f_path.dentry);
	} else {
		loff_t i_pos = ((loff_t) EXFAT_I(inode)->fid.start_clu << 32) |
					   ((EXFAT_I(inode)->fid.rwoffset-1) & 0xffffffff);

		struct inode *tmp = exfat_iget(sb, i_pos);
		if (tmp) {
			inum = tmp->i_ino;
			iput(tmp);
		} else {
			inum = iunique(sb, EXFAT_ROOT_INO);
		}
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)
	if (!dir_emit(ctx, de.Name, strlen(de.Name), inum,
		(de.Attr & ATTR_SUBDIR) ? DT_DIR : DT_REG))
#else
	if (filldir(dirent, de.Name, strlen(de.Name), cpos-1, inum,
				(de.Attr & ATTR_SUBDIR) ? DT_DIR : DT_REG) < 0)
#endif
		goto out;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)
	ctx->pos = cpos;
#else
	filp->f_pos = cpos;
#endif
	goto get_new;

end_of_dir:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)
	ctx->pos = cpos;
#else
	filp->f_pos = cpos;
#endif
out:
	__unlock_super(sb);
	return err;
}

static int exfat_ioctl_volume_id(struct inode *dir)
{
	struct super_block *sb = dir->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	FS_INFO_T *p_fs = &(sbi->fs_info);

	return p_fs->vol_id;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
static int exfat_generic_ioctl(struct inode *inode, struct file *filp,
							   unsigned int cmd, unsigned long arg)
#else
static long exfat_generic_ioctl(struct file *filp,
								unsigned int cmd, unsigned long arg)
#endif
{
#if EXFAT_CONFIG_KERNEL_DEBUG
#if !(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))
	struct inode *inode = filp->f_dentry->d_inode;
#endif
	unsigned int flags;
#endif /* EXFAT_CONFIG_KERNEL_DEBUG */

	switch (cmd) {
	case EXFAT_IOCTL_GET_VOLUME_ID:
		return exfat_ioctl_volume_id(inode);
#if EXFAT_CONFIG_KERNEL_DEBUG
	case EXFAT_IOC_GET_DEBUGFLAGS: {
		struct super_block *sb = inode->i_sb;
		struct exfat_sb_info *sbi = EXFAT_SB(sb);

		flags = sbi->debug_flags;
		return put_user(flags, (int __user *)arg);
	}
	case EXFAT_IOC_SET_DEBUGFLAGS: {
		struct super_block *sb = inode->i_sb;
		struct exfat_sb_info *sbi = EXFAT_SB(sb);

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (get_user(flags, (int __user *) arg))
			return -EFAULT;

		__lock_super(sb);
		sbi->debug_flags = flags;
		__unlock_super(sb);

		return 0;
	}
#endif /* EXFAT_CONFIG_KERNEL_DEBUG */
	default:
		return -ENOTTY; /* Inappropriate ioctl for device */
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
static int exfat_file_fsync(struct file *filp, int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	struct super_block *sb = inode->i_sb;
	int res, err;

	res = generic_file_fsync(filp, datasync);
	err = FsSyncVol(sb, 1);

	return res ? res : err;
}
#endif

const struct file_operations exfat_dir_operations = {
	.llseek     = generic_file_llseek,
	.read       = generic_read_dir,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,11,0)
	.iterate    = exfat_readdir,
#else
	.readdir    = exfat_readdir,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
	.ioctl      = exfat_generic_ioctl,
	.fsync      = exfat_file_fsync,
#else
	.unlocked_ioctl = exfat_generic_ioctl,
	.fsync      = generic_file_fsync,
#endif
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,00)
static int exfat_create(struct inode *dir, struct dentry *dentry, umode_t mode,
						bool excl)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,00)
static int exfat_create(struct inode *dir, struct dentry *dentry, umode_t mode,
						struct nameidata *nd)
#else
static int exfat_create(struct inode *dir, struct dentry *dentry, int mode,
						struct nameidata *nd)
#endif
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct timespec ts;
	FILE_ID_T fid;
	loff_t i_pos;
	int err;

	__lock_super(sb);

	PRINTK("exfat_create entered\n");

	ts = CURRENT_TIME_SEC;

	err = FsCreateFile(dir, (UINT8 *) dentry->d_name.name, FM_REGULAR, &fid);
	if (err) {
		if (err == FFS_INVALIDPATH)
			err = -EINVAL;
		else if (err == FFS_FILEEXIST)
			err = -EEXIST;
		else if (err == FFS_FULL)
			err = -ENOSPC;
		else if (err == FFS_NAMETOOLONG)
			err = -ENAMETOOLONG;
		else
			err = -EIO;
		goto out;
	}
	dir->i_version++;
	dir->i_ctime = dir->i_mtime = dir->i_atime = ts;
	if (IS_DIRSYNC(dir))
		(void) exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	i_pos = ((loff_t) fid.dir.dir << 32) | (fid.entry & 0xffffffff);

	inode = exfat_build_inode(sb, &fid, i_pos);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	inode->i_version++;
	inode->i_mtime = inode->i_atime = inode->i_ctime = ts;
	/* timestamp is already written, so mark_inode_dirty() is unnecessary. */

	dentry->d_time = dentry->d_parent->d_inode->i_version;
	d_instantiate(dentry, inode);

out:
	__unlock_super(sb);
	PRINTK("exfat_create exited\n");
	return err;
}

static int exfat_find(struct inode *dir, struct qstr *qname,
					  FILE_ID_T *fid)
{
	int err;

	if (qname->len == 0)
		return -ENOENT;

	err = FsLookupFile(dir, (UINT8 *) qname->name, fid);
	if (err)
		return -ENOENT;

	return 0;
}

static int exfat_d_anon_disconn(struct dentry *dentry)
{
	return IS_ROOT(dentry) && (dentry->d_flags & DCACHE_DISCONNECTED);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,00)
static struct dentry *exfat_lookup(struct inode *dir, struct dentry *dentry,
						   unsigned int flags)
#else
static struct dentry *exfat_lookup(struct inode *dir, struct dentry *dentry,
						   struct nameidata *nd)
#endif
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct dentry *alias;
	int err;
	FILE_ID_T fid;
	loff_t i_pos;
	UINT64 ret;
	mode_t i_mode;

	__lock_super(sb);
	PRINTK("exfat_lookup entered\n");
	err = exfat_find(dir, &dentry->d_name, &fid);
	if (err) {
		if (err == -ENOENT) {
			inode = NULL;
			goto out;
		}
		goto error;
	}

	i_pos = ((loff_t) fid.dir.dir << 32) | (fid.entry & 0xffffffff);
	inode = exfat_build_inode(sb, &fid, i_pos);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto error;
	}

	i_mode = inode->i_mode;
	if (S_ISLNK(i_mode)) {
		EXFAT_I(inode)->target = MALLOC(i_size_read(inode)+1);
		if (!EXFAT_I(inode)->target) {
			err = -ENOMEM;
			goto error;
		}
		FsReadFile(dir, &fid, EXFAT_I(inode)->target, i_size_read(inode), &ret);
		*(EXFAT_I(inode)->target + i_size_read(inode)) = '\0';
	}

	alias = d_find_alias(inode);
	if (alias && !exfat_d_anon_disconn(alias)) {
		CHECK_ERR(d_unhashed(alias));
		if (!S_ISDIR(i_mode))
			d_move(alias, dentry);
		iput(inode);
		__unlock_super(sb);
		PRINTK("exfat_lookup exited 1\n");
		return alias;
	} else {
		dput(alias);
	}
out:
	__unlock_super(sb);
	dentry->d_time = dentry->d_parent->d_inode->i_version;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
	dentry->d_op = sb->s_root->d_op;
	dentry = d_splice_alias(inode, dentry);
	if (dentry) {
		dentry->d_op = sb->s_root->d_op;
		dentry->d_time = dentry->d_parent->d_inode->i_version;
	}
#else
	dentry = d_splice_alias(inode, dentry);
	if (dentry)
		dentry->d_time = dentry->d_parent->d_inode->i_version;
#endif
	PRINTK("exfat_lookup exited 2\n");
	return dentry;

error:
	__unlock_super(sb);
	PRINTK("exfat_lookup exited 3\n");
	return ERR_PTR(err);
}

static int exfat_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = dir->i_sb;
	struct timespec ts;
	int err;

	__lock_super(sb);

	PRINTK("exfat_unlink entered\n");

	ts = CURRENT_TIME_SEC;

	EXFAT_I(inode)->fid.size = i_size_read(inode);

	err = FsRemoveFile(dir, &(EXFAT_I(inode)->fid));
	if (err) {
		if (err == FFS_PERMISSIONERR)
			err = -EPERM;
		else
			err = -EIO;
		goto out;
	}
	dir->i_version++;
	dir->i_mtime = dir->i_atime = ts;
	if (IS_DIRSYNC(dir))
		(void) exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	clear_nlink(inode);
	inode->i_mtime = inode->i_atime = ts;
	exfat_detach(inode);
	remove_inode_hash(inode);

out:
	__unlock_super(sb);
	PRINTK("exfat_unlink exited\n");
	return err;
}

static int exfat_symlink(struct inode *dir, struct dentry *dentry, const char *target)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct timespec ts;
	FILE_ID_T fid;
	loff_t i_pos;
	int err;
	UINT64 len = (UINT64) strlen(target);
	UINT64 ret;

	__lock_super(sb);

	PRINTK("exfat_symlink entered\n");

	ts = CURRENT_TIME_SEC;

	err = FsCreateFile(dir, (UINT8 *) dentry->d_name.name, FM_SYMLINK, &fid);
	if (err) {
		if (err == FFS_INVALIDPATH)
			err = -EINVAL;
		else if (err == FFS_FILEEXIST)
			err = -EEXIST;
		else if (err == FFS_FULL)
			err = -ENOSPC;
		else
			err = -EIO;
		goto out;
	}

	err = FsWriteFile(dir, &fid, (char *) target, len, &ret);

	if (err) {
		FsRemoveFile(dir, &fid);

		if (err == FFS_FULL)
			err = -ENOSPC;
		else
			err = -EIO;
		goto out;
	}

	dir->i_version++;
	dir->i_ctime = dir->i_mtime = dir->i_atime = ts;
	if (IS_DIRSYNC(dir))
		(void) exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	i_pos = ((loff_t) fid.dir.dir << 32) | (fid.entry & 0xffffffff);

	inode = exfat_build_inode(sb, &fid, i_pos);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	inode->i_version++;
	inode->i_mtime = inode->i_atime = inode->i_ctime = ts;
	/* timestamp is already written, so mark_inode_dirty() is unneeded. */

	EXFAT_I(inode)->target = MALLOC(len+1);
	if (!EXFAT_I(inode)->target) {
		err = -ENOMEM;
		goto out;
	}
	MEMCPY(EXFAT_I(inode)->target, target, len+1);

	dentry->d_time = dentry->d_parent->d_inode->i_version;
	d_instantiate(dentry, inode);

out:
	__unlock_super(sb);
	PRINTK("exfat_symlink exited\n");
	return err;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,00)
static int exfat_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
#else
static int exfat_mkdir(struct inode *dir, struct dentry *dentry, int mode)
#endif
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	struct timespec ts;
	FILE_ID_T fid;
	loff_t i_pos;
	int err;

	__lock_super(sb);

	PRINTK("exfat_mkdir entered\n");

	ts = CURRENT_TIME_SEC;

	err = FsCreateDir(dir, (UINT8 *) dentry->d_name.name, &fid);
	if (err) {
		if (err == FFS_INVALIDPATH)
			err = -EINVAL;
		else if (err == FFS_FILEEXIST)
			err = -EEXIST;
		else if (err == FFS_FULL)
			err = -ENOSPC;
		else if (err == FFS_NAMETOOLONG)
			err = -ENAMETOOLONG;
		else
			err = -EIO;
		goto out;
	}
	dir->i_version++;
	dir->i_ctime = dir->i_mtime = dir->i_atime = ts;
	if (IS_DIRSYNC(dir))
		(void) exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);
	inc_nlink(dir);

	i_pos = ((loff_t) fid.dir.dir << 32) | (fid.entry & 0xffffffff);

	inode = exfat_build_inode(sb, &fid, i_pos);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	inode->i_version++;
	inode->i_mtime = inode->i_atime = inode->i_ctime = ts;
	/* timestamp is already written, so mark_inode_dirty() is unneeded. */

	dentry->d_time = dentry->d_parent->d_inode->i_version;
	d_instantiate(dentry, inode);

out:
	__unlock_super(sb);
	PRINTK("exfat_mkdir exited\n");
	return err;
}

static int exfat_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = dir->i_sb;
	struct timespec ts;
	int err;

	__lock_super(sb);

	PRINTK("exfat_rmdir entered\n");

	ts = CURRENT_TIME_SEC;

	EXFAT_I(inode)->fid.size = i_size_read(inode);

	err = FsRemoveDir(dir, &(EXFAT_I(inode)->fid));
	if (err) {
		if (err == FFS_INVALIDPATH)
			err = -EINVAL;
		else if (err == FFS_FILEEXIST)
			err = -ENOTEMPTY;
		else if (err == FFS_NOTFOUND)
			err = -ENOENT;
		else if (err == FFS_DIRBUSY)
			err = -EBUSY;
		else
			err = -EIO;
		goto out;
	}
	dir->i_version++;
	dir->i_mtime = dir->i_atime = ts;
	if (IS_DIRSYNC(dir))
		(void) exfat_sync_inode(dir);
	else
		mark_inode_dirty(dir);
	drop_nlink(dir);

	clear_nlink(inode);
	inode->i_mtime = inode->i_atime = ts;
	exfat_detach(inode);
	remove_inode_hash(inode);

out:
	__unlock_super(sb);
	PRINTK("exfat_rmdir exited\n");
	return err;
}

static int exfat_rename(struct inode *old_dir, struct dentry *old_dentry,
						struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *old_inode, *new_inode;
	struct super_block *sb = old_dir->i_sb;
	struct timespec ts;
	loff_t i_pos;
	int err;

	__lock_super(sb);

	PRINTK("exfat_rename entered\n");

	old_inode = old_dentry->d_inode;
	new_inode = new_dentry->d_inode;

	ts = CURRENT_TIME_SEC;

	EXFAT_I(old_inode)->fid.size = i_size_read(old_inode);

	err = FsMoveFile(old_dir, &(EXFAT_I(old_inode)->fid), new_dir, new_dentry);
	if (err) {
		if (err == FFS_PERMISSIONERR)
			err = -EPERM;
		else if (err == FFS_INVALIDPATH)
			err = -EINVAL;
		else if (err == FFS_FILEEXIST)
			err = -EEXIST;
		else if (err == FFS_NOTFOUND)
			err = -ENOENT;
		else if (err == FFS_FULL)
			err = -ENOSPC;
		else
			err = -EIO;
		goto out;
	}
	new_dir->i_version++;
	new_dir->i_ctime = new_dir->i_mtime = new_dir->i_atime = ts;
	if (IS_DIRSYNC(new_dir))
		(void) exfat_sync_inode(new_dir);
	else
		mark_inode_dirty(new_dir);

	i_pos = ((loff_t) EXFAT_I(old_inode)->fid.dir.dir << 32) |
			(EXFAT_I(old_inode)->fid.entry & 0xffffffff);

	exfat_detach(old_inode);
	exfat_attach(old_inode, i_pos);
	if (IS_DIRSYNC(new_dir))
		(void) exfat_sync_inode(old_inode);
	else
		mark_inode_dirty(old_inode);

	if ((S_ISDIR(old_inode->i_mode)) && (old_dir != new_dir)) {
		drop_nlink(old_dir);
		if (!new_inode) inc_nlink(new_dir);
	}

	old_dir->i_version++;
	old_dir->i_ctime = old_dir->i_mtime = ts;
	if (IS_DIRSYNC(old_dir))
		(void) exfat_sync_inode(old_dir);
	else
		mark_inode_dirty(old_dir);

	if (new_inode) {
		exfat_detach(new_inode);
		drop_nlink(new_inode);
		if (S_ISDIR(new_inode->i_mode))
			drop_nlink(new_inode);
		new_inode->i_ctime = ts;
	}

out:
	__unlock_super(sb);
	PRINTK("exfat_rename exited\n");
	return err;
}

static int exfat_cont_expand(struct inode *inode, loff_t size)
{
	struct address_space *mapping = inode->i_mapping;
	loff_t start = i_size_read(inode), count = size - i_size_read(inode);
	int err, err2;

	if ((err = generic_cont_expand_simple(inode, size)) != 0)
		return err;

	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);

	if (IS_SYNC(inode)) {
		err = filemap_fdatawrite_range(mapping, start, start + count - 1);
		err2 = sync_mapping_buffers(mapping);
		err = (err)?(err):(err2);
		err2 = write_inode_now(inode, 1);
		err = (err)?(err):(err2);
		if (!err) {
			err =  filemap_fdatawait_range(mapping, start, start + count - 1);
		}
	}
	return err;
}

static int exfat_allow_set_time(struct exfat_sb_info *sbi, struct inode *inode)
{
	mode_t allow_utime = sbi->options.allow_utime;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
	if (!uid_eq(current_fsuid(), inode->i_uid))
#else
	if (current_fsuid() != inode->i_uid)
#endif
	{
		if (in_group_p(inode->i_gid))
			allow_utime >>= 3;
		if (allow_utime & MAY_WRITE)
			return 1;
	}

	/* use a default check */
	return 0;
}

static int exfat_sanitize_mode(const struct exfat_sb_info *sbi,
							   struct inode *inode, umode_t *mode_ptr)
{
	mode_t i_mode, mask, perm;

	i_mode = inode->i_mode;

	if (S_ISREG(i_mode) || S_ISLNK(i_mode))
		mask = sbi->options.fs_fmask;
	else
		mask = sbi->options.fs_dmask;

	perm = *mode_ptr & ~(S_IFMT | mask);

	/* Of the r and x bits, all (subject to umask) must be present.*/
	if ((perm & (S_IRUGO | S_IXUGO)) != (i_mode & (S_IRUGO|S_IXUGO)))
		return -EPERM;

	if (exfat_mode_can_hold_ro(inode)) {
		/* Of the w bits, either all (subject to umask) or none must be present. */
		if ((perm & S_IWUGO) && ((perm & S_IWUGO) != (S_IWUGO & ~mask)))
			return -EPERM;
	} else {
		/* If exfat_mode_can_hold_ro(inode) is false, can't change w bits. */
		if ((perm & S_IWUGO) != (S_IWUGO & ~mask))
			return -EPERM;
	}

	*mode_ptr &= S_IFMT | perm;

	return 0;
}

static int exfat_setattr(struct dentry *dentry, struct iattr *attr)
{

	struct exfat_sb_info *sbi = EXFAT_SB(dentry->d_sb);
	struct inode *inode = dentry->d_inode;
	unsigned int ia_valid;
	int error;
	loff_t old_size;

	PRINTK("exfat_setattr entered\n");

	if ((attr->ia_valid & ATTR_SIZE)
		&& (attr->ia_size > i_size_read(inode))) {
		error = exfat_cont_expand(inode, attr->ia_size);
		if (error || attr->ia_valid == ATTR_SIZE)
			return error;
		attr->ia_valid &= ~ATTR_SIZE;
	}

	ia_valid = attr->ia_valid;

	if ((ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET | ATTR_TIMES_SET))
		&& exfat_allow_set_time(sbi, inode)) {
		attr->ia_valid &= ~(ATTR_MTIME_SET | ATTR_ATIME_SET | ATTR_TIMES_SET);
	}

	error = inode_change_ok(inode, attr);
	attr->ia_valid = ia_valid;
	if (error) {
		return error;
	}

	if (((attr->ia_valid & ATTR_UID) &&
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
		 (!uid_eq(attr->ia_uid, sbi->options.fs_uid))) ||
		((attr->ia_valid & ATTR_GID) &&
		 (!gid_eq(attr->ia_gid, sbi->options.fs_gid))) ||
#else
		 (attr->ia_uid != sbi->options.fs_uid)) ||
		((attr->ia_valid & ATTR_GID) &&
		 (attr->ia_gid != sbi->options.fs_gid)) ||
#endif
		((attr->ia_valid & ATTR_MODE) &&
		 (attr->ia_mode & ~(S_IFREG | S_IFLNK | S_IFDIR | S_IRWXUGO)))) {
		return -EPERM;
	}

	/*
	 * We don't return -EPERM here. Yes, strange, but this is too
	 * old behavior.
	 */
	if (attr->ia_valid & ATTR_MODE) {
		if (exfat_sanitize_mode(sbi, inode, &attr->ia_mode) < 0)
			attr->ia_valid &= ~ATTR_MODE;
	}

	EXFAT_I(inode)->fid.size = i_size_read(inode);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
	if (attr->ia_valid)
		error = inode_setattr(inode, attr);
#else
	if (attr->ia_valid & ATTR_SIZE) {
		old_size = i_size_read(inode);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,00)
                down_write(&EXFAT_I(inode)->truncate_lock);
		truncate_setsize(inode, attr->ia_size);
		_exfat_truncate(inode, old_size);
		up_write(&EXFAT_I(inode)->truncate_lock);
#else
		truncate_setsize(inode, attr->ia_size);
		_exfat_truncate(inode, old_size);
#endif
	}
	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
#endif

	PRINTK("exfat_setattr exited\n");
	return error;
}

static int exfat_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;

	PRINTK("exfat_getattr entered\n");

	generic_fillattr(inode, stat);
	stat->blksize = EXFAT_SB(inode->i_sb)->fs_info.cluster_size;

	PRINTK("exfat_getattr exited\n");
	return 0;
}

const struct inode_operations exfat_dir_inode_operations = {
	.create        = exfat_create,
	.lookup        = exfat_lookup,
	.unlink        = exfat_unlink,
	.symlink       = exfat_symlink,
	.mkdir         = exfat_mkdir,
	.rmdir         = exfat_rmdir,
	.rename        = exfat_rename,
	.setattr       = exfat_setattr,
	.getattr       = exfat_getattr,
};

/*======================================================================*/
/*  File Operations                                                     */
/*======================================================================*/

static void *exfat_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct exfat_inode_info *ei = EXFAT_I(dentry->d_inode);
	nd_set_link(nd, (char *)(ei->target));
	return NULL;
}

const struct inode_operations exfat_symlink_inode_operations = {
	.readlink    = generic_readlink,
	.follow_link = exfat_follow_link,
};

static int exfat_file_release(struct inode *inode, struct file *filp)
{
	struct super_block *sb = inode->i_sb;

	EXFAT_I(inode)->fid.size = i_size_read(inode);
	FsSyncVol(sb, 0);
	return 0;
}

const struct file_operations exfat_file_operations = {
	.llseek      = generic_file_llseek,
	.read        = do_sync_read,
	.write       = do_sync_write,
	.aio_read    = generic_file_aio_read,
	.aio_write   = generic_file_aio_write,
	.mmap        = generic_file_mmap,
	.release     = exfat_file_release,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
	.ioctl       = exfat_generic_ioctl,
	.fsync       = exfat_file_fsync,
#else
	.unlocked_ioctl  = exfat_generic_ioctl,
	.fsync       = generic_file_fsync,
#endif
	.splice_read = generic_file_splice_read,
};

static void _exfat_truncate(struct inode *inode, loff_t old_size)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	FS_INFO_T *p_fs = &(sbi->fs_info);
	int err;

	__lock_super(sb);

	/*
	 * This protects against truncating a file bigger than it was then
	 * trying to write into the hole.
	 */
	if (EXFAT_I(inode)->mmu_private > i_size_read(inode))
		EXFAT_I(inode)->mmu_private = i_size_read(inode);

	if (EXFAT_I(inode)->fid.start_clu == 0) goto out;

	err = FsTruncateFile(inode, old_size, i_size_read(inode));
	if (err) goto out;

	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	if (IS_DIRSYNC(inode))
		(void) exfat_sync_inode(inode);
	else
		mark_inode_dirty(inode);

	inode->i_blocks = ((i_size_read(inode) + (p_fs->cluster_size - 1))
					   & ~((loff_t)p_fs->cluster_size - 1)) >> 9;
out:
	__unlock_super(sb);
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,36)
static void exfat_truncate(struct inode *inode)
{
	_exfat_truncate(inode, i_size_read(inode));
}
#endif

const struct inode_operations exfat_file_inode_operations = {
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,36)
	.truncate    = exfat_truncate,
#endif
	.setattr     = exfat_setattr,
	.getattr     = exfat_getattr,
};

/*======================================================================*/
/*  Address Space Operations                                            */
/*======================================================================*/

static int exfat_bmap(struct inode *inode, sector_t sector, sector_t *phys,
					  unsigned long *mapped_blocks, int *create)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	FS_INFO_T *p_fs = &(sbi->fs_info);
	BD_INFO_T *p_bd = &(sbi->bd_info);
	const unsigned long blocksize = sb->s_blocksize;
	const unsigned char blocksize_bits = sb->s_blocksize_bits;
	sector_t last_block;
	int err, clu_offset, sec_offset;
	unsigned int cluster;

	*phys = 0;
	*mapped_blocks = 0;

	if ((p_fs->vol_type == FAT12) || (p_fs->vol_type == FAT16)) {
		if (inode->i_ino == EXFAT_ROOT_INO) {
			if (sector < (p_fs->dentries_in_root >> (p_bd->sector_size_bits-DENTRY_SIZE_BITS))) {
				*phys = sector + p_fs->root_start_sector;
				*mapped_blocks = 1;
			}
			return 0;
		}
	}

	last_block = (i_size_read(inode) + (blocksize - 1)) >> blocksize_bits;
	if (sector >= last_block) {
		if (*create == 0) return 0;
	} else {
		*create = 0;
	}

	clu_offset = sector >> p_fs->sectors_per_clu_bits;  /* cluster offset */
	sec_offset = sector & (p_fs->sectors_per_clu - 1);  /* sector offset in cluster */

	EXFAT_I(inode)->fid.size = i_size_read(inode);

	err = FsMapCluster(inode, clu_offset, &cluster);

	if (err) {
		if (err == FFS_FULL)
			return -ENOSPC;
		else
			return -EIO;
	} else if (cluster != CLUSTER_32(~0)) {
		*phys = START_SECTOR(cluster) + sec_offset;
		*mapped_blocks = p_fs->sectors_per_clu - sec_offset;
	}

	return 0;
}

static int exfat_get_block(struct inode *inode, sector_t iblock,
						   struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	unsigned long max_blocks = bh_result->b_size >> inode->i_blkbits;
	int err;
	unsigned long mapped_blocks;
	sector_t phys;

	__lock_super(sb);

	err = exfat_bmap(inode, iblock, &phys, &mapped_blocks, &create);
	if (err) {
		__unlock_super(sb);
		return err;
	}

	if (phys) {
		max_blocks = min(mapped_blocks, max_blocks);
		if (create) {
			EXFAT_I(inode)->mmu_private += max_blocks << sb->s_blocksize_bits;
			set_buffer_new(bh_result);
		}
		map_bh(bh_result, sb, phys);
	}

	bh_result->b_size = max_blocks << sb->s_blocksize_bits;
	__unlock_super(sb);

	return 0;
}

static int exfat_readpage(struct file *file, struct page *page)
{
	int ret;
	ret =  mpage_readpage(page, exfat_get_block);
	return ret;
}

static int exfat_readpages(struct file *file, struct address_space *mapping,
				   struct list_head *pages, unsigned nr_pages)
{
	int ret;
	ret =  mpage_readpages(mapping, pages, nr_pages, exfat_get_block);
	return ret;
}

static int exfat_writepage(struct page *page, struct writeback_control *wbc)
{
	int ret;
	ret = block_write_full_page(page, exfat_get_block, wbc);
	return ret;
}

static int exfat_writepages(struct address_space *mapping,
						struct writeback_control *wbc)
{
	int ret;
	ret = mpage_writepages(mapping, wbc, exfat_get_block);
	return ret;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,34)
static void exfat_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;
	if (to > i_size_read(inode)) {
		truncate_pagecache(inode, to, i_size_read(inode));
		EXFAT_I(inode)->fid.size = i_size_read(inode);
		_exfat_truncate(inode, i_size_read(inode));
	}
}
#endif


static int exfat_write_begin(struct file *file, struct address_space *mapping,
				 loff_t pos, unsigned len, unsigned flags,
					 struct page **pagep, void **fsdata)
{
	int ret;
	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				   exfat_get_block,
				   &EXFAT_I(mapping->host)->mmu_private);

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,34)
	if (ret < 0)
		exfat_write_failed(mapping, pos+len);
#endif
	return ret;
}

static int exfat_write_end(struct file *file, struct address_space *mapping,
				   loff_t pos, unsigned len, unsigned copied,
					   struct page *pagep, void *fsdata)
{
	struct inode *inode = mapping->host;
	FILE_ID_T *fid = &(EXFAT_I(inode)->fid);
	int err;

	err = generic_write_end(file, mapping, pos, len, copied, pagep, fsdata);

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,34)
	if (err < len)
		exfat_write_failed(mapping, pos+len);
#endif

	if (!(err < 0) && !(fid->attr & ATTR_ARCHIVE)) {
		inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;
		fid->attr |= ATTR_ARCHIVE;
		mark_inode_dirty(inode);
	}
	return err;
}

static ssize_t exfat_direct_IO(int rw, struct kiocb *iocb,
					   const struct iovec *iov,
					   loff_t offset, unsigned long nr_segs)
{
	struct inode *inode = iocb->ki_filp->f_mapping->host;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,34)
	struct address_space *mapping = iocb->ki_filp->f_mapping;
#endif
	ssize_t ret;

	if (rw == WRITE) {
		if (EXFAT_I(inode)->mmu_private < (offset + iov_length(iov, nr_segs)))
			return 0;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,00)
	ret = blockdev_direct_IO(rw, iocb, inode, iov,
					offset, nr_segs, exfat_get_block);
#else
        ret = blockdev_direct_IO(rw, iocb, inode, inode->i_sb->s_bdev, iov,
					offset, nr_segs, exfat_get_block, NULL);
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,34)
	if ((ret < 0) && (rw & WRITE))
		exfat_write_failed(mapping, offset+iov_length(iov, nr_segs));
#endif
	return ret;
}

static sector_t _exfat_bmap(struct address_space *mapping, sector_t block)
{
	sector_t blocknr;

	/* exfat_get_cluster() assumes the requested blocknr isn't truncated. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,00)
	down_read(&EXFAT_I(mapping->host)->truncate_lock);
	blocknr = generic_block_bmap(mapping, block, exfat_get_block);
	up_read(&EXFAT_I(mapping->host)->truncate_lock);
#else
	down_read(&EXFAT_I(mapping->host)->i_alloc_sem);
	blocknr = generic_block_bmap(mapping, block, exfat_get_block);
	up_read(&EXFAT_I(mapping->host)->i_alloc_sem);
#endif

	return blocknr;
}

const struct address_space_operations exfat_aops = {
	.readpage    = exfat_readpage,
	.readpages   = exfat_readpages,
	.writepage   = exfat_writepage,
	.writepages  = exfat_writepages,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)
	.sync_page   = block_sync_page,
#endif
	.write_begin = exfat_write_begin,
	.write_end   = exfat_write_end,
	.direct_IO   = exfat_direct_IO,
	.bmap        = _exfat_bmap
};

/*======================================================================*/
/*  Super Operations                                                    */
/*======================================================================*/

static inline unsigned long exfat_hash(loff_t i_pos)
{
	return hash_32(i_pos, EXFAT_HASH_BITS);
}

static struct inode *exfat_iget(struct super_block *sb, loff_t i_pos) {
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *info;
	struct hlist_head *head = sbi->inode_hashtable + exfat_hash(i_pos);
	struct inode *inode = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
	struct hlist_node *node;

	spin_lock(&sbi->inode_hash_lock);
	hlist_for_each_entry(info, node, head, i_hash_fat) {
#else
	spin_lock(&sbi->inode_hash_lock);
	hlist_for_each_entry(info, head, i_hash_fat) {
#endif
		CHECK_ERR(info->vfs_inode.i_sb != sb);

		if (i_pos != info->i_pos)
			continue;
		inode = igrab(&info->vfs_inode);
		if (inode)
			break;
	}
	spin_unlock(&sbi->inode_hash_lock);
	return inode;
}

static void exfat_attach(struct inode *inode, loff_t i_pos)
{
	struct exfat_sb_info *sbi = EXFAT_SB(inode->i_sb);
	struct hlist_head *head = sbi->inode_hashtable + exfat_hash(i_pos);

	spin_lock(&sbi->inode_hash_lock);
	EXFAT_I(inode)->i_pos = i_pos;
	hlist_add_head(&EXFAT_I(inode)->i_hash_fat, head);
	spin_unlock(&sbi->inode_hash_lock);
}

static void exfat_detach(struct inode *inode)
{
	struct exfat_sb_info *sbi = EXFAT_SB(inode->i_sb);

	spin_lock(&sbi->inode_hash_lock);
	hlist_del_init(&EXFAT_I(inode)->i_hash_fat);
	EXFAT_I(inode)->i_pos = 0;
	spin_unlock(&sbi->inode_hash_lock);
}

/* doesn't deal with root inode */
static int exfat_fill_inode(struct inode *inode, FILE_ID_T *fid)
{
	struct exfat_sb_info *sbi = EXFAT_SB(inode->i_sb);
	FS_INFO_T *p_fs = &(sbi->fs_info);
	DIR_ENTRY_T info;

	memcpy(&(EXFAT_I(inode)->fid), fid, sizeof(FILE_ID_T));

	FsReadStat(inode, &info);

	EXFAT_I(inode)->i_pos = 0;
	EXFAT_I(inode)->target = NULL;
	inode->i_uid = sbi->options.fs_uid;
	inode->i_gid = sbi->options.fs_gid;
	inode->i_version++;
	inode->i_generation = get_seconds();

	if (info.Attr & ATTR_SUBDIR) { /* directory */
		inode->i_generation &= ~1;
		inode->i_mode = exfat_make_mode(sbi, info.Attr, S_IRWXUGO);
		inode->i_op = &exfat_dir_inode_operations;
		inode->i_fop = &exfat_dir_operations;

		i_size_write(inode, info.Size);
		EXFAT_I(inode)->mmu_private = i_size_read(inode);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,00)
		set_nlink(inode,info.NumSubdirs);
#else
		inode->i_nlink = info.NumSubdirs;
#endif
	} else if (info.Attr & ATTR_SYMLINK) { /* symbolic link */
		inode->i_generation |= 1;
		inode->i_mode = exfat_make_mode(sbi, info.Attr, S_IRWXUGO);
		inode->i_op = &exfat_symlink_inode_operations;

		i_size_write(inode, info.Size);
		EXFAT_I(inode)->mmu_private = i_size_read(inode);
	} else { /* regular file */
		inode->i_generation |= 1;
		inode->i_mode = exfat_make_mode(sbi, info.Attr, S_IRWXUGO);
		inode->i_op = &exfat_file_inode_operations;
		inode->i_fop = &exfat_file_operations;
		inode->i_mapping->a_ops = &exfat_aops;
		inode->i_mapping->nrpages = 0;

		i_size_write(inode, info.Size);
		EXFAT_I(inode)->mmu_private = i_size_read(inode);
	}
	exfat_save_attr(inode, info.Attr);

	inode->i_blocks = ((i_size_read(inode) + (p_fs->cluster_size - 1))
					   & ~((loff_t)p_fs->cluster_size - 1)) >> 9;

	exfat_time_fat2unix(sbi, &inode->i_mtime, &info.ModifyTimestamp);
	exfat_time_fat2unix(sbi, &inode->i_ctime, &info.CreateTimestamp);
	exfat_time_fat2unix(sbi, &inode->i_atime, &info.AccessTimestamp);

	return 0;
}

static struct inode *exfat_build_inode(struct super_block *sb,
									   FILE_ID_T *fid, loff_t i_pos) {
	struct inode *inode;
	int err;

	inode = exfat_iget(sb, i_pos);
	if (inode)
		goto out;
	inode = new_inode(sb);
	if (!inode) {
		inode = ERR_PTR(-ENOMEM);
		goto out;
	}
	inode->i_ino = iunique(sb, EXFAT_ROOT_INO);
	inode->i_version = 1;
	err = exfat_fill_inode(inode, fid);
	if (err) {
		iput(inode);
		inode = ERR_PTR(err);
		goto out;
	}
	exfat_attach(inode, i_pos);
	insert_inode_hash(inode);
out:
	return inode;
}

static int exfat_sync_inode(struct inode *inode)
{
	return exfat_write_inode(inode, NULL);
}

static struct inode *exfat_alloc_inode(struct super_block *sb) {
	struct exfat_inode_info *ei;

	ei = kmem_cache_alloc(exfat_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,00)
	init_rwsem(&ei->truncate_lock);
#endif

	return &ei->vfs_inode;
}

static void exfat_destroy_inode(struct inode *inode)
{
	FREE(EXFAT_I(inode)->target);
	EXFAT_I(inode)->target = NULL;

	kmem_cache_free(exfat_inode_cachep, EXFAT_I(inode));
}

static int exfat_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	DIR_ENTRY_T info;

	if (inode->i_ino == EXFAT_ROOT_INO)
		return 0;

	info.Attr = exfat_make_attr(inode);
	info.Size = i_size_read(inode);

	exfat_time_unix2fat(sbi, &inode->i_mtime, &info.ModifyTimestamp);
	exfat_time_unix2fat(sbi, &inode->i_ctime, &info.CreateTimestamp);
	exfat_time_unix2fat(sbi, &inode->i_atime, &info.AccessTimestamp);

	FsWriteStat(inode, &info);

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
static void exfat_delete_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
}

static void exfat_clear_inode(struct inode *inode)
{
	exfat_detach(inode);
	remove_inode_hash(inode);
}
#else
static void exfat_evict_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);

	if (!inode->i_nlink)
		i_size_write(inode, 0);
	invalidate_inode_buffers(inode);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,00)
	end_writeback(inode);
#else
	clear_inode(inode);
#endif
	exfat_detach(inode);

	remove_inode_hash(inode);
}
#endif

static void exfat_free_super(struct exfat_sb_info *sbi)
{
	if (sbi->nls_disk)
		unload_nls(sbi->nls_disk);
	if (sbi->nls_io)
		unload_nls(sbi->nls_io);
	if (sbi->options.iocharset != exfat_default_iocharset)
		kfree(sbi->options.iocharset);
	mutex_destroy(&sbi->s_lock);
	kfree(sbi);
}

static void exfat_put_super(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	if (__is_sb_dirty(sb))
		exfat_write_super(sb);

	FsUmountVol(sb);

	sb->s_fs_info = NULL;
	exfat_free_super(sbi);
}

static void exfat_write_super(struct super_block *sb)
{
	__lock_super(sb);

	__set_sb_clean(sb);

	if (!(sb->s_flags & MS_RDONLY))
		FsSyncVol(sb, 1);

	__unlock_super(sb);
}

static int exfat_sync_fs(struct super_block *sb, int wait)
{
	int err = 0;

	if (__is_sb_dirty(sb)) {
		__lock_super(sb);
		__set_sb_clean(sb);
		err = FsSyncVol(sb, 1);
		__unlock_super(sb);
	}

	return err;
}

static int exfat_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	VOL_INFO_T info;

	if (p_fs->used_clusters == (UINT32) ~0) {
		if (FFS_MEDIAERR == FsGetVolInfo(sb, &info))
			return -EIO;

	} else {
		info.FatType = p_fs->vol_type;
		info.ClusterSize = p_fs->cluster_size;
		info.NumClusters = p_fs->num_clusters - 2;
		info.UsedClusters = p_fs->used_clusters;
		info.FreeClusters = info.NumClusters - info.UsedClusters;

		if (p_fs->dev_ejected)
			return -EIO;
	}

	buf->f_type = sb->s_magic;
	buf->f_bsize = info.ClusterSize;
	buf->f_blocks = info.NumClusters;
	buf->f_bfree = info.FreeClusters;
	buf->f_bavail = info.FreeClusters;
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);
	buf->f_namelen = 260;

	return 0;
}

static int exfat_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= MS_NODIRATIME;
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,00)
static int exfat_show_options(struct seq_file *m, struct dentry *root)
{
	struct exfat_sb_info *sbi = EXFAT_SB(root->d_sb);
#else
static int exfat_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct exfat_sb_info *sbi = EXFAT_SB(mnt->mnt_sb);
#endif
	struct exfat_mount_options *opts = &sbi->options;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
	if (__kuid_val(opts->fs_uid))
		seq_printf(m, ",uid=%u", __kuid_val(opts->fs_uid));
	if (__kgid_val(opts->fs_gid))
		seq_printf(m, ",gid=%u", __kgid_val(opts->fs_gid));
#else
	if (opts->fs_uid != 0)
		seq_printf(m, ",uid=%u", opts->fs_uid);
	if (opts->fs_gid != 0)
		seq_printf(m, ",gid=%u", opts->fs_gid);
#endif
	seq_printf(m, ",fmask=%04o", opts->fs_fmask);
	seq_printf(m, ",dmask=%04o", opts->fs_dmask);
	if (opts->allow_utime)
		seq_printf(m, ",allow_utime=%04o", opts->allow_utime);
	if (sbi->nls_disk)
		seq_printf(m, ",codepage=%s", sbi->nls_disk->charset);
	if (sbi->nls_io)
		seq_printf(m, ",iocharset=%s", sbi->nls_io->charset);
	seq_printf(m, ",namecase=%u", opts->casesensitive);
	if (opts->errors == EXFAT_ERRORS_CONT)
		seq_puts(m, ",errors=continue");
	else if (opts->errors == EXFAT_ERRORS_PANIC)
		seq_puts(m, ",errors=panic");
	else
		seq_puts(m, ",errors=remount-ro");
#if EXFAT_CONFIG_DISCARD
	if (opts->discard)
		seq_printf(m, ",discard");
#endif
	return 0;
}

const struct super_operations exfat_sops = {
	.alloc_inode   = exfat_alloc_inode,
	.destroy_inode = exfat_destroy_inode,
	.write_inode   = exfat_write_inode,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
	.delete_inode  = exfat_delete_inode,
	.clear_inode   = exfat_clear_inode,
#else
	.evict_inode  = exfat_evict_inode,
#endif
	.put_super     = exfat_put_super,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
	.write_super   = exfat_write_super,
#endif
	.sync_fs       = exfat_sync_fs,
	.statfs        = exfat_statfs,
	.remount_fs    = exfat_remount,
	.show_options  = exfat_show_options,
};

/*======================================================================*/
/*  Super Block Read Operations                                         */
/*======================================================================*/

enum {
	Opt_uid,
	Opt_gid,
	Opt_umask,
	Opt_dmask,
	Opt_fmask,
	Opt_allow_utime,
	Opt_codepage,
	Opt_charset,
	Opt_namecase,
	Opt_debug,
	Opt_err_cont,
	Opt_err_panic,
	Opt_err_ro,
	Opt_err,
#if EXFAT_CONFIG_DISCARD
	Opt_discard,
#endif /* EXFAT_CONFIG_DISCARD */
};

static const match_table_t exfat_tokens = {
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_umask, "umask=%o"},
	{Opt_dmask, "dmask=%o"},
	{Opt_fmask, "fmask=%o"},
	{Opt_allow_utime, "allow_utime=%o"},
	{Opt_codepage, "codepage=%u"},
	{Opt_charset, "iocharset=%s"},
	{Opt_namecase, "namecase=%u"},
	{Opt_debug, "debug"},
	{Opt_err_cont, "errors=continue"},
	{Opt_err_panic, "errors=panic"},
	{Opt_err_ro, "errors=remount-ro"},
#if EXFAT_CONFIG_DISCARD
	{Opt_discard, "discard"},
#endif /* EXFAT_CONFIG_DISCARD */
	{Opt_err, NULL}
};

static int parse_options(char *options, int silent, int *debug,
						 struct exfat_mount_options *opts)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	char *iocharset;

	opts->fs_uid = current_uid();
	opts->fs_gid = current_gid();
	opts->fs_fmask = opts->fs_dmask = current->fs->umask;
	opts->allow_utime = (unsigned short) -1;
	opts->codepage = exfat_default_codepage;
	opts->iocharset = exfat_default_iocharset;
	opts->casesensitive = 0;
	opts->errors = EXFAT_ERRORS_RO;
#if EXFAT_CONFIG_DISCARD
	opts->discard = 0;
#endif
	*debug = 0;

	if (!options)
		goto out;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, exfat_tokens, args);
		switch (token) {
		case Opt_uid:
			if (match_int(&args[0], &option))
				return 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
			opts->fs_uid = KUIDT_INIT(option);
#else
			opts->fs_uid = option;
#endif
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
			opts->fs_gid = KGIDT_INIT(option);
#else
			opts->fs_gid = option;
#endif
			break;
		case Opt_umask:
		case Opt_dmask:
		case Opt_fmask:
			if (match_octal(&args[0], &option))
				return 0;
			if (token != Opt_dmask)
				opts->fs_fmask = option;
			if (token != Opt_fmask)
				opts->fs_dmask = option;
			break;
		case Opt_allow_utime:
			if (match_octal(&args[0], &option))
				return 0;
			opts->allow_utime = option & (S_IWGRP | S_IWOTH);
			break;
		case Opt_codepage:
			if (match_int(&args[0], &option))
				return 0;
			opts->codepage = option;
			break;
		case Opt_charset:
			if (opts->iocharset != exfat_default_iocharset)
				kfree(opts->iocharset);
			iocharset = match_strdup(&args[0]);
			if (!iocharset)
				return -ENOMEM;
			opts->iocharset = iocharset;
			break;
		case Opt_namecase:
			if (match_int(&args[0], &option))
				return 0;
			opts->casesensitive = option;
			break;
		case Opt_err_cont:
			opts->errors = EXFAT_ERRORS_CONT;
			break;
		case Opt_err_panic:
			opts->errors = EXFAT_ERRORS_PANIC;
			break;
		case Opt_err_ro:
			opts->errors = EXFAT_ERRORS_RO;
			break;
		case Opt_debug:
			*debug = 1;
			break;
#if EXFAT_CONFIG_DISCARD
		case Opt_discard:
			opts->discard = 1;
			break;
#endif /* EXFAT_CONFIG_DISCARD */
		default:
			if (!silent) {
				printk(KERN_ERR "[EXFAT] Unrecognized mount option %s or missing value\n", p);
			}
			return -EINVAL;
		}
	}

out:
	if (opts->allow_utime == (unsigned short) -1)
		opts->allow_utime = ~opts->fs_dmask & (S_IWGRP | S_IWOTH);

	return 0;
}

static void exfat_hash_init(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	int i;

	spin_lock_init(&sbi->inode_hash_lock);
	for (i = 0; i < EXFAT_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&sbi->inode_hashtable[i]);
}

static int exfat_read_root(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct timespec ts;
	FS_INFO_T *p_fs = &(sbi->fs_info);
	DIR_ENTRY_T info;

	ts = CURRENT_TIME_SEC;

	EXFAT_I(inode)->fid.dir.dir = p_fs->root_dir;
	EXFAT_I(inode)->fid.dir.flags = 0x01;
	EXFAT_I(inode)->fid.entry = -1;
	EXFAT_I(inode)->fid.start_clu = p_fs->root_dir;
	EXFAT_I(inode)->fid.flags = 0x01;
	EXFAT_I(inode)->fid.type = TYPE_DIR;
	EXFAT_I(inode)->fid.rwoffset = 0;
	EXFAT_I(inode)->fid.hint_last_off = -1;

	EXFAT_I(inode)->target = NULL;

	FsReadStat(inode, &info);

	inode->i_uid = sbi->options.fs_uid;
	inode->i_gid = sbi->options.fs_gid;
	inode->i_version++;
	inode->i_generation = 0;
	inode->i_mode = exfat_make_mode(sbi, ATTR_SUBDIR, S_IRWXUGO);
	inode->i_op = &exfat_dir_inode_operations;
	inode->i_fop = &exfat_dir_operations;

	i_size_write(inode, info.Size);
	inode->i_blocks = ((i_size_read(inode) + (p_fs->cluster_size - 1))
					   & ~((loff_t)p_fs->cluster_size - 1)) >> 9;
	EXFAT_I(inode)->i_pos = ((loff_t) p_fs->root_dir << 32) | 0xffffffff;
	EXFAT_I(inode)->mmu_private = i_size_read(inode);

	exfat_save_attr(inode, ATTR_SUBDIR);
	inode->i_mtime = inode->i_atime = inode->i_ctime = ts;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,00)
	set_nlink(inode,info.NumSubdirs + 2);
#else
	inode->i_nlink = info.NumSubdirs + 2;
#endif

	return 0;
}

static int exfat_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode = NULL;
	struct exfat_sb_info *sbi;
	int debug, ret;
	long error;
	char buf[50];

	/*
	 * GFP_KERNEL is ok here, because while we do hold the
	 * supeblock lock, memory pressure can't call back into
	 * the filesystem, since we're only just about to mount
	 * it and have no inodes etc active!
	 */
	sbi = kzalloc(sizeof(struct exfat_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
	mutex_init(&sbi->s_lock);
#endif
	sb->s_fs_info = sbi;

	sb->s_flags |= MS_NODIRATIME;
	sb->s_magic = EXFAT_SUPER_MAGIC;
	sb->s_op = &exfat_sops;

	error = parse_options(data, silent, &debug, &sbi->options);
	if (error)
		goto out_fail;

	error = -EIO;
	sb_min_blocksize(sb, 512);
	sb->s_maxbytes = 0x7fffffffffffffffLL;    // maximum file size

	ret = FsMountVol(sb);
	if (ret) {
		if (!silent)
			printk(KERN_ERR "[EXFAT] FsMountVol failed\n");

		goto out_fail;
	}

	/* set up enough so that it can read an inode */
	exfat_hash_init(sb);

	/*
	 * The low byte of FAT's first entry must have same value with
	 * media-field.  But in real world, too many devices is
	 * writing wrong value.  So, removed that validity check.
	 *
	 * if (FAT_FIRST_ENT(sb, media) != first)
	 */

	if (sbi->fs_info.vol_type != EXFAT) {
		error = -EINVAL;
		sprintf(buf, "cp%d", sbi->options.codepage);
		sbi->nls_disk = load_nls(buf);
		if (!sbi->nls_disk) {
			printk(KERN_ERR "[EXFAT] Codepage %s not found\n", buf);
			goto out_fail2;
		}
	}

	sbi->nls_io = load_nls(sbi->options.iocharset);
	if (!sbi->nls_io) {
		printk(KERN_ERR "[EXFAT] IO charset %s not found\n",
			   sbi->options.iocharset);
		goto out_fail2;
	}

	error = -ENOMEM;
	root_inode = new_inode(sb);
	if (!root_inode)
		goto out_fail2;
	root_inode->i_ino = EXFAT_ROOT_INO;
	root_inode->i_version = 1;
	error = exfat_read_root(root_inode);
	if (error < 0)
		goto out_fail2;
	error = -ENOMEM;
	exfat_attach(root_inode, EXFAT_I(root_inode)->i_pos);
	insert_inode_hash(root_inode);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,00)
	sb->s_root = d_make_root(root_inode);
#else
	sb->s_root = d_alloc_root(root_inode);
#endif
	if (!sb->s_root) {
		printk(KERN_ERR "[EXFAT] Getting the root inode failed\n");
		goto out_fail2;
	}

	return 0;

out_fail2:
	FsUmountVol(sb);
out_fail:
	if (root_inode)
		iput(root_inode);
	sb->s_fs_info = NULL;
	exfat_free_super(sbi);
	return error;
}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
static int exfat_get_sb(struct file_system_type *fs_type,
						int flags, const char *dev_name,
						void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, exfat_fill_super, mnt);
}
#else
static struct dentry *exfat_fs_mount(struct file_system_type *fs_type,
									 int flags, const char *dev_name,
									 void *data) {
	return mount_bdev(fs_type, flags, dev_name, data, exfat_fill_super);
}
#endif

static void init_once(void *foo)
{
	struct exfat_inode_info *ei = (struct exfat_inode_info *)foo;

	INIT_HLIST_NODE(&ei->i_hash_fat);
	inode_init_once(&ei->vfs_inode);
}

static int __init exfat_init_inodecache(void)
{
	exfat_inode_cachep = kmem_cache_create("exfat_inode_cache",
										   sizeof(struct exfat_inode_info),
										   0, (SLAB_RECLAIM_ACCOUNT|
												   SLAB_MEM_SPREAD),
										   init_once);
	if (exfat_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void __exit exfat_destroy_inodecache(void)
{
	kmem_cache_destroy(exfat_inode_cachep);
}

#if EXFAT_CONFIG_KERNEL_DEBUG
static void exfat_debug_kill_sb(struct super_block *sb)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct block_device *bdev = sb->s_bdev;

	long flags;

	if (sbi) {
		flags = sbi->debug_flags;

		if (flags & EXFAT_DEBUGFLAGS_INVALID_UMOUNT) {
			/* invalidate_bdev drops all device cache include dirty.
			   we use this to simulate device removal */
			FsReleaseCache(sb);
			invalidate_bdev(bdev);
		}
	}

	kill_block_super(sb);
}
#endif /* EXFAT_CONFIG_KERNEL_DEBUG */

static struct file_system_type exfat_fs_type = {
	.owner       = THIS_MODULE,
	.name        = "exfat",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
	.get_sb      = exfat_get_sb,
#else
	.mount       = exfat_fs_mount,
#endif
#if EXFAT_CONFIG_KERNEL_DEBUG
	.kill_sb    = exfat_debug_kill_sb,
#else
	.kill_sb    = kill_block_super,
#endif /* EXFAT_CONFIG_KERNLE_DEBUG */
	.fs_flags    = FS_REQUIRES_DEV,
};

static int __init init_exfat(void)
{
	int err;

	err = FsInit();
	if (err) {
		if (err == FFS_MEMORYERR)
			return -ENOMEM;
		else
			return -EIO;
	}

	printk(KERN_INFO "exFAT: Version %s\n", EXFAT_VERSION);

	err = exfat_init_inodecache();
	if (err) goto out;

	err = register_filesystem(&exfat_fs_type);
	if (err) goto out;

	return 0;
out:
	FsShutdown();
	return err;
}

static void __exit exit_exfat(void)
{
	exfat_destroy_inodecache();
	unregister_filesystem(&exfat_fs_type);
	FsShutdown();
}

module_init(init_exfat);
module_exit(exit_exfat);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("exFAT Filesystem Driver");
#ifdef MODULE_ALIAS_FS
MODULE_ALIAS_FS("exfat");
#endif
