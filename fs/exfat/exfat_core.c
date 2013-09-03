/* Some of the source code in this file came from "linux/fs/fat/misc.c".  */
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

/************************************************************************/
/*                                                                      */
/*  PROJECT : exFAT & FAT12/16/32 File System                           */
/*  FILE    : exfat.c                                                   */
/*  PURPOSE : exFAT File Manager                                        */
/*                                                                      */
/*----------------------------------------------------------------------*/
/*  NOTES                                                               */
/*                                                                      */
/*----------------------------------------------------------------------*/
/*  REVISION HISTORY (Ver 0.9)                                          */
/*                                                                      */
/*  - 2010.11.15 [Joosun Hahn] : first writing                          */
/*                                                                      */
/************************************************************************/

#include <linux/version.h>

#include "exfat_config.h"
#include "exfat_global.h"
#include "exfat_data.h"
#include "exfat_oal.h"

#include "exfat_blkdev.h"
#include "exfat_cache.h"
#include "exfat_nls.h"
#include "exfat_api.h"
#include "exfat_super.h"
#include "exfat.h"

#include <linux/blkdev.h>

/*----------------------------------------------------------------------*/
/*  Constant & Macro Definitions                                        */
/*----------------------------------------------------------------------*/

#define THERE_IS_MBR        0   /* if there is no MBR (e.g. memory card),
set this macro to 0 */

#if (THERE_IS_MBR == 1)
#include "exfat_part.h"
#endif

#define DELAYED_SYNC        0

#define ELAPSED_TIME        0

#if (ELAPSED_TIME == 1)
#include <linux/time.h>

static UINT32 __t1, __t2;
static UINT32 get_current_msec(void)
{
	struct timeval tm;
	do_gettimeofday(&tm);
	return (UINT32)(tm.tv_sec*1000000 + tm.tv_usec);
}
#define TIME_START()        do {__t1 = get_current_msec(); } while (0)
#define TIME_END()          do {__t2 = get_current_msec(); } while (0)
#define PRINT_TIME(n)       do {printk("[EXFAT] Elapsed time %d = %d (usec)\n", n, (__t2 - __t1)); } while (0)
#else
#define TIME_START()
#define TIME_END()
#define PRINT_TIME(n)
#endif

static void __set_sb_dirty(struct super_block *sb)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,7,0)
	sb->s_dirt = 1;
#else
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	sbi->s_dirt = 1;
#endif
}

/*----------------------------------------------------------------------*/
/*  Global Variable Definitions                                         */
/*----------------------------------------------------------------------*/

extern UINT8 uni_upcase[];

/*----------------------------------------------------------------------*/
/*  Local Variable Definitions                                          */
/*----------------------------------------------------------------------*/

static UINT8 name_buf[MAX_PATH_LENGTH *MAX_CHARSET_SIZE];

static INT8 *reserved_names[] = {
	"AUX     ", "CON     ", "NUL     ", "PRN     ",
	"COM1    ", "COM2    ", "COM3    ", "COM4    ",
	"COM5    ", "COM6    ", "COM7    ", "COM8    ", "COM9    ",
	"LPT1    ", "LPT2    ", "LPT3    ", "LPT4    ",
	"LPT5    ", "LPT6    ", "LPT7    ", "LPT8    ", "LPT9    ",
	NULL
};

static UINT8 free_bit[] = {
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, /*   0 ~  19 */
	0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, /*  20 ~  39 */
	0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, /*  40 ~  59 */
	0, 1, 0, 6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, /*  60 ~  79 */
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, /*  80 ~  99 */
	0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, /* 100 ~ 119 */
	0, 1, 0, 2, 0, 1, 0, 7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, /* 120 ~ 139 */
	0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, /* 140 ~ 159 */
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, /* 160 ~ 179 */
	0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6, 0, 1, 0, 2, 0, 1, 0, 3, /* 180 ~ 199 */
	0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, /* 200 ~ 219 */
	0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, /* 220 ~ 239 */
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0                 /* 240 ~ 254 */
};

static UINT8 used_bit[] = {
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, /*   0 ~  19 */
	2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2, 3, 3, 4, /*  20 ~  39 */
	2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, /*  40 ~  59 */
	4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, /*  60 ~  79 */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, /*  80 ~  99 */
	3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, /* 100 ~ 119 */
	4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, /* 120 ~ 139 */
	3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, /* 140 ~ 159 */
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, /* 160 ~ 179 */
	4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, /* 180 ~ 199 */
	3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, /* 200 ~ 219 */
	5, 6, 6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, /* 220 ~ 239 */
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8              /* 240 ~ 255 */
};

/*======================================================================*/
/*  Global Function Definitions                                         */
/*======================================================================*/

/* ffsInit : roll back to the initial state of the file system */
INT32 ffsInit(void)
{
	INT32 ret;

	ret = bdev_init();
	if (ret)
		return ret;

	ret = fs_init();
	if (ret)
		return ret;

	return FFS_SUCCESS;
} /* end of ffsInit */

/* ffsShutdown : make free all memory-alloced global buffers */
INT32 ffsShutdown(void)
{
	INT32 ret;
	ret = fs_shutdown();
	if (ret)
		return ret;

	ret = bdev_shutdown();
	if (ret)
		return ret;

	return FFS_SUCCESS;
} /* end of ffsShutdown */

/* ffsMountVol : mount the file system volume */
INT32 ffsMountVol(struct super_block *sb, INT32 drv)
{
	INT32 i, ret;
#if (THERE_IS_MBR == 1)
	MBR_SECTOR_T *p_mbr;
	PART_ENTRY_T *p_pte;
#endif
	PBR_SECTOR_T *p_pbr;
	struct buffer_head *tmp_bh = NULL;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	PRINTK("[EXFAT] trying to mount...\n");

	p_fs->drv = drv;
	p_fs->dev_ejected = FALSE;

	/* open the block device */
	if (bdev_open(sb))
		return FFS_MEDIAERR;

	if (p_bd->sector_size < sb->s_blocksize)
		return FFS_MEDIAERR;
	if (p_bd->sector_size > sb->s_blocksize)
		sb_set_blocksize(sb, p_bd->sector_size);

	/* read Sector 0 */
	if (sector_read(sb, 0, &tmp_bh, 1) != FFS_SUCCESS)
		return FFS_MEDIAERR;

#if (THERE_IS_MBR == 1)
	if (buf[0] != 0xEB) {
		/* MBR is read */
		p_mbr = (MBR_SECTOR_T *) tmp_bh->b_data;

		/* check the validity of MBR */
		if (GET16_A(p_mbr->signature) != MBR_SIGNATURE) {
			brelse(tmp_bh);
			bdev_close(sb);
			return FFS_FORMATERR;
		}

		p_pte = (PART_ENTRY_T *) p_mbr->partition + 0;
		p_fs->PBR_sector = GET32(p_pte->start_sector);
		p_fs->num_sectors = GET32(p_pte->num_sectors);

		if (p_fs->num_sectors == 0) {
			brelse(tmp_bh);
			bdev_close(sb);
			return FFS_ERROR;
		}

		/* read PBR */
		if (sector_read(sb, p_fs->PBR_sector, &tmp_bh, 1) != FFS_SUCCESS) {
			bdev_close(sb);
			return FFS_MEDIAERR;
		}
	} else {
#endif
		/* PRB is read */
		p_fs->PBR_sector = 0;
#if (THERE_IS_MBR == 1)
	}
#endif

	p_pbr = (PBR_SECTOR_T *) tmp_bh->b_data;

	/* check the validity of PBR */
	if (GET16_A(p_pbr->signature) != PBR_SIGNATURE) {
		brelse(tmp_bh);
		bdev_close(sb);
		return FFS_FORMATERR;
	}

	/* fill fs_stuct */
	for (i = 0; i < 53; i++)
		if (p_pbr->bpb[i])
			break;

	if (i < 53) {
		if (GET16(p_pbr->bpb+11)) /* num_fat_sectors */
			ret = fat16_mount(sb, p_pbr);
		else
			ret = fat32_mount(sb, p_pbr);
	} else {
		ret = exfat_mount(sb, p_pbr);
	}

	brelse(tmp_bh);

	if (ret) {
		bdev_close(sb);
		return ret;
	}

	if (p_fs->vol_type == EXFAT) {
		ret = load_alloc_bitmap(sb);
		if (ret) {
			bdev_close(sb);
			return ret;
		}
		ret = load_upcase_table(sb);
		if (ret) {
			free_alloc_bitmap(sb);
			bdev_close(sb);
			return ret;
		}
	}

	if (p_fs->dev_ejected) {
		if (p_fs->vol_type == EXFAT) {
			free_upcase_table(sb);
			free_alloc_bitmap(sb);
		}
		bdev_close(sb);
		return FFS_MEDIAERR;
	}

	PRINTK("[EXFAT] mounted successfully\n");
	return FFS_SUCCESS;
} /* end of ffsMountVol */

/* ffsUmountVol : umount the file system volume */
INT32 ffsUmountVol(struct super_block *sb)
{
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	PRINTK("[EXFAT] trying to unmount...\n");

	fs_sync(sb, 0);
	fs_set_vol_flags(sb, VOL_CLEAN);

	if (p_fs->vol_type == EXFAT) {
		free_upcase_table(sb);
		free_alloc_bitmap(sb);
	}

	FAT_release_all(sb);
	buf_release_all(sb);

	/* close the block device */
	bdev_close(sb);

	if (p_fs->dev_ejected) {
		PRINTK( "[EXFAT] unmounted with media errors. "
			"device's already ejected.\n");
		return FFS_MEDIAERR;
	}

	PRINTK("[EXFAT] unmounted successfully\n");
	return FFS_SUCCESS;
} /* end of ffsUmountVol */

/* ffsGetVolInfo : get the information of a file system volume */
INT32 ffsGetVolInfo(struct super_block *sb, VOL_INFO_T *info)
{
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_fs->used_clusters == (UINT32) ~0)
		p_fs->used_clusters = p_fs->fs_func->count_used_clusters(sb);

	info->FatType = p_fs->vol_type;
	info->ClusterSize = p_fs->cluster_size;
	info->NumClusters = p_fs->num_clusters - 2; /* clu 0 & 1 */
	info->UsedClusters = p_fs->used_clusters;
	info->FreeClusters = info->NumClusters - info->UsedClusters;

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return FFS_SUCCESS;
} /* end of ffsGetVolInfo */

/* ffsSyncVol : synchronize all file system volumes */
INT32 ffsSyncVol(struct super_block *sb, INT32 do_sync)
{
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* synchronize the file system */
	fs_sync(sb, do_sync);
	fs_set_vol_flags(sb, VOL_CLEAN);

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return FFS_SUCCESS;
} /* end of ffsSyncVol */

/*----------------------------------------------------------------------*/
/*  File Operation Functions                                            */
/*----------------------------------------------------------------------*/

/* ffsLookupFile : lookup a file */
INT32 ffsLookupFile(struct inode *inode, UINT8 *path, FILE_ID_T *fid)
{
	INT32 ret, dentry, num_entries;
	CHAIN_T dir;
	UNI_NAME_T uni_name;
	DOS_NAME_T dos_name;
	DENTRY_T *ep, *ep2;
	ENTRY_SET_CACHE_T *es=NULL;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	PRINTK("ffsLookupFile entered\n");

	/* check the validity of directory name in the given pathname */
	ret = resolve_path(inode, path, &dir, &uni_name);
	if (ret)
		return ret;

	ret = get_num_entries_and_dos_name(sb, &dir, &uni_name, &num_entries, &dos_name);
	if (ret)
		return ret;

	/* search the file name for directories */
	dentry = p_fs->fs_func->find_dir_entry(sb, &dir, &uni_name, num_entries, &dos_name, TYPE_ALL);
	if (dentry < -1)
		return FFS_NOTFOUND;

	fid->dir.dir = dir.dir;
	fid->dir.size = dir.size;
	fid->dir.flags = dir.flags;
	fid->entry = dentry;

	if (dentry == -1) {
		fid->type = TYPE_DIR;
		fid->rwoffset = 0;
		fid->hint_last_off = -1;

		fid->attr = ATTR_SUBDIR;
		fid->flags = 0x01;
		fid->size = 0;
		fid->start_clu = p_fs->root_dir;
	} else {
		if (p_fs->vol_type == EXFAT) {
			es = get_entry_set_in_dir(sb, &dir, dentry, ES_2_ENTRIES, &ep);
			if (!es)
				return FFS_MEDIAERR;
			ep2 = ep+1;
		} else {
			ep = get_entry_in_dir(sb, &dir, dentry, NULL);
			if (!ep)
				return FFS_MEDIAERR;
			ep2 = ep;
		}

		fid->type = p_fs->fs_func->get_entry_type(ep);
		fid->rwoffset = 0;
		fid->hint_last_off = -1;
		fid->attr = p_fs->fs_func->get_entry_attr(ep);

		fid->size = p_fs->fs_func->get_entry_size(ep2);
		if ((fid->type == TYPE_FILE) && (fid->size == 0)) {
			fid->flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;
			fid->start_clu = CLUSTER_32(~0);
		} else {
			fid->flags = p_fs->fs_func->get_entry_flag(ep2);
			fid->start_clu = p_fs->fs_func->get_entry_clu0(ep2);
		}

		if (p_fs->vol_type == EXFAT)
			release_entry_set(es);
	}

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	PRINTK("ffsLookupFile exited successfully\n");

	return FFS_SUCCESS;
} /* end of ffsLookupFile */

/* ffsCreateFile : create a file */
INT32 ffsCreateFile(struct inode *inode, UINT8 *path, UINT8 mode, FILE_ID_T *fid)
{
	INT32 ret/*, dentry*/;
	CHAIN_T dir;
	UNI_NAME_T uni_name;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of directory name in the given pathname */
	ret = resolve_path(inode, path, &dir, &uni_name);
	if (ret)
		return ret;

	fs_set_vol_flags(sb, VOL_DIRTY);

	/* create a new file */
	ret = create_file(inode, &dir, &uni_name, mode, fid);

#if (DELAYED_SYNC == 0)
	fs_sync(sb, 0);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return ret;
} /* end of ffsCreateFile */

/* ffsReadFile : read data from a opened file */
INT32 ffsReadFile(struct inode *inode, FILE_ID_T *fid, void *buffer, UINT64 count, UINT64 *rcount)
{
	INT32 offset, sec_offset, clu_offset;
	UINT32 clu, LogSector;
	UINT64 oneblkread, read_bytes;
	struct buffer_head *tmp_bh = NULL;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	/* check if the given file ID is opened */
	if (fid->type != TYPE_FILE)
		return FFS_PERMISSIONERR;

	if (fid->rwoffset > fid->size)
		fid->rwoffset = fid->size;

	if (count > (fid->size - fid->rwoffset))
		count = fid->size - fid->rwoffset;

	if (count == 0) {
		if (rcount != NULL)
			*rcount = 0;
		return FFS_EOF;
	}

	read_bytes = 0;

	while (count > 0) {
		clu_offset = (INT32)(fid->rwoffset >> p_fs->cluster_size_bits);
		clu = fid->start_clu;

		if (fid->flags == 0x03) {
			clu += clu_offset;
		} else {
			/* hint information */
			if ((clu_offset > 0) && (fid->hint_last_off > 0) &&
				(clu_offset >= fid->hint_last_off)) {
				clu_offset -= fid->hint_last_off;
				clu = fid->hint_last_clu;
			}

			while (clu_offset > 0) {
				/* clu = FAT_read(sb, clu); */
				if (FAT_read(sb, clu, &clu) == -1)
					return FFS_MEDIAERR;

				clu_offset--;
			}
		}

		/* hint information */
		fid->hint_last_off = (INT32)(fid->rwoffset >> p_fs->cluster_size_bits);
		fid->hint_last_clu = clu;

		offset = (INT32)(fid->rwoffset & (p_fs->cluster_size-1)); /* byte offset in cluster   */
		sec_offset = offset >> p_bd->sector_size_bits;            /* sector offset in cluster */
		offset &= p_bd->sector_size_mask;                         /* byte offset in sector    */

		LogSector = START_SECTOR(clu) + sec_offset;

		oneblkread = (UINT64)(p_bd->sector_size - offset);
		if (oneblkread > count)
			oneblkread = count;

		if ((offset == 0) && (oneblkread == p_bd->sector_size)) {
			if (sector_read(sb, LogSector, &tmp_bh, 1) != FFS_SUCCESS)
				goto err_out;
			MEMCPY(((INT8 *) buffer)+read_bytes, ((INT8 *) tmp_bh->b_data), (INT32) oneblkread);
		} else {
			if (sector_read(sb, LogSector, &tmp_bh, 1) != FFS_SUCCESS)
				goto err_out;
			MEMCPY(((INT8 *) buffer)+read_bytes, ((INT8 *) tmp_bh->b_data)+offset, (INT32) oneblkread);
		}
		count -= oneblkread;
		read_bytes += oneblkread;
		fid->rwoffset += oneblkread;
	}
	brelse(tmp_bh);

err_out:
	/* set the size of read bytes */
	if (rcount != NULL)
		*rcount = read_bytes;

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return FFS_SUCCESS;
} /* end of ffsReadFile */

/* ffsWriteFile : write data into a opened file */
INT32 ffsWriteFile(struct inode *inode, FILE_ID_T *fid, void *buffer, UINT64 count, UINT64 *wcount)
{
	INT32 modified = FALSE, offset, sec_offset, clu_offset;
	INT32 num_clusters, num_alloc, num_alloced = (INT32) ~0;
	UINT32 clu, last_clu, LogSector, sector = 0;
	UINT64 oneblkwrite, write_bytes;
	CHAIN_T new_clu;
	TIMESTAMP_T tm;
	DENTRY_T *ep, *ep2;
	ENTRY_SET_CACHE_T *es = NULL;
	struct buffer_head *tmp_bh = NULL;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	/* check if the given file ID is opened */
	if (fid->type != TYPE_FILE)
		return FFS_PERMISSIONERR;

	if (fid->rwoffset > fid->size)
		fid->rwoffset = fid->size;

	if (count == 0) {
		if (wcount != NULL)
			*wcount = 0;
		return FFS_SUCCESS;
	}

	fs_set_vol_flags(sb, VOL_DIRTY);

	if (fid->size == 0)
		num_clusters = 0;
	else
		num_clusters = (INT32)((fid->size-1) >> p_fs->cluster_size_bits) + 1;

	write_bytes = 0;

	while (count > 0) {
		clu_offset = (INT32)(fid->rwoffset >> p_fs->cluster_size_bits);
		clu = last_clu = fid->start_clu;

		if (fid->flags == 0x03) {
			if ((clu_offset > 0) && (clu != CLUSTER_32(~0))) {
				last_clu += clu_offset - 1;

				if (clu_offset == num_clusters)
					clu = CLUSTER_32(~0);
				else
					clu += clu_offset;
			}
		} else {
			/* hint information */
			if ((clu_offset > 0) && (fid->hint_last_off > 0) &&
				(clu_offset >= fid->hint_last_off)) {
				clu_offset -= fid->hint_last_off;
				clu = fid->hint_last_clu;
			}

			while ((clu_offset > 0) && (clu != CLUSTER_32(~0))) {
				last_clu = clu;
				/* clu = FAT_read(sb, clu); */
				if (FAT_read(sb, clu, &clu) == -1)
					return FFS_MEDIAERR;

				clu_offset--;
			}
		}

		if (clu == CLUSTER_32(~0)) {
			num_alloc = (INT32)((count-1) >> p_fs->cluster_size_bits) + 1;
			new_clu.dir = (last_clu == CLUSTER_32(~0)) ? CLUSTER_32(~0) : last_clu+1;
			new_clu.size = 0;
			new_clu.flags = fid->flags;

			/* (1) allocate a chain of clusters */
			num_alloced = p_fs->fs_func->alloc_cluster(sb, num_alloc, &new_clu);
			if (num_alloced == 0)
				break;

			/* (2) append to the FAT chain */
			if (last_clu == CLUSTER_32(~0)) {
				if (new_clu.flags == 0x01)
					fid->flags = 0x01;
				fid->start_clu = new_clu.dir;
				modified = TRUE;
			} else {
				if (new_clu.flags != fid->flags) {
					exfat_chain_cont_cluster(sb, fid->start_clu, num_clusters);
					fid->flags = 0x01;
					modified = TRUE;
				}
				if (new_clu.flags == 0x01)
					FAT_write(sb, last_clu, new_clu.dir);
			}

			num_clusters += num_alloced;
			clu = new_clu.dir;
		}

		/* hint information */
		fid->hint_last_off = (INT32)(fid->rwoffset >> p_fs->cluster_size_bits);
		fid->hint_last_clu = clu;

		offset = (INT32)(fid->rwoffset & (p_fs->cluster_size-1)); /* byte offset in cluster   */
		sec_offset = offset >> p_bd->sector_size_bits;            /* sector offset in cluster */
		offset &= p_bd->sector_size_mask;                         /* byte offset in sector    */

		LogSector = START_SECTOR(clu) + sec_offset;

		oneblkwrite = (UINT64)(p_bd->sector_size - offset);
		if (oneblkwrite > count)
			oneblkwrite = count;

		if ((offset == 0) && (oneblkwrite == p_bd->sector_size)) {
			if (sector_read(sb, LogSector, &tmp_bh, 0) != FFS_SUCCESS)
				goto err_out;
			MEMCPY(((INT8 *) tmp_bh->b_data), ((INT8 *) buffer)+write_bytes, (INT32) oneblkwrite);
			if (sector_write(sb, LogSector, tmp_bh, 0) != FFS_SUCCESS) {
				brelse(tmp_bh);
				goto err_out;
			}
		} else {
			if ((offset > 0) || ((fid->rwoffset+oneblkwrite) < fid->size)) {
				if (sector_read(sb, LogSector, &tmp_bh, 1) != FFS_SUCCESS)
					goto err_out;
			} else {
				if (sector_read(sb, LogSector, &tmp_bh, 0) != FFS_SUCCESS)
					goto err_out;
			}

			MEMCPY(((INT8 *) tmp_bh->b_data)+offset, ((INT8 *) buffer)+write_bytes, (INT32) oneblkwrite);
			if (sector_write(sb, LogSector, tmp_bh, 0) != FFS_SUCCESS) {
				brelse(tmp_bh);
				goto err_out;
			}
		}

		count -= oneblkwrite;
		write_bytes += oneblkwrite;
		fid->rwoffset += oneblkwrite;

		fid->attr |= ATTR_ARCHIVE;

		if (fid->size < fid->rwoffset) {
			fid->size = fid->rwoffset;
			modified = TRUE;
		}
	}

	brelse(tmp_bh);

	/* (3) update the direcoty entry */
	if (p_fs->vol_type == EXFAT) {
		es = get_entry_set_in_dir(sb, &(fid->dir), fid->entry, ES_ALL_ENTRIES, &ep);
		if (es == NULL)
			goto err_out;
		ep2 = ep+1;
	} else {
		ep = get_entry_in_dir(sb, &(fid->dir), fid->entry, &sector);
		if (!ep)
			goto err_out;
		ep2 = ep;
	}

	p_fs->fs_func->set_entry_time(ep, tm_current(&tm), TM_MODIFY);
	p_fs->fs_func->set_entry_attr(ep, fid->attr);

	if (p_fs->vol_type != EXFAT)
		buf_modify(sb, sector);

	if (modified) {
		if (p_fs->fs_func->get_entry_flag(ep2) != fid->flags)
			p_fs->fs_func->set_entry_flag(ep2, fid->flags);

		if (p_fs->fs_func->get_entry_size(ep2) != fid->size)
			p_fs->fs_func->set_entry_size(ep2, fid->size);

		if (p_fs->fs_func->get_entry_clu0(ep2) != fid->start_clu)
			p_fs->fs_func->set_entry_clu0(ep2, fid->start_clu);

		if (p_fs->vol_type != EXFAT)
			buf_modify(sb, sector);
	}

	if (p_fs->vol_type == EXFAT) {
		update_dir_checksum_with_entry_set(sb, es);
		release_entry_set(es);
	}

#if (DELAYED_SYNC == 0)
	fs_sync(sb, 0);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

err_out:
	/* set the size of written bytes */
	if (wcount != NULL)
		*wcount = write_bytes;

	if (num_alloced == 0)
		return FFS_FULL;

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return FFS_SUCCESS;
} /* end of ffsWriteFile */

/* ffsTruncateFile : resize the file length */
INT32 ffsTruncateFile(struct inode *inode, UINT64 old_size, UINT64 new_size)
{
	INT32 num_clusters;
	UINT32 last_clu = CLUSTER_32(0), sector = 0;
	CHAIN_T clu;
	TIMESTAMP_T tm;
	DENTRY_T *ep, *ep2;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	FILE_ID_T *fid = &(EXFAT_I(inode)->fid);
	ENTRY_SET_CACHE_T *es=NULL;

	/* check if the given file ID is opened */
	if (fid->type != TYPE_FILE)
		return FFS_PERMISSIONERR;

	if (fid->size != old_size) {
		printk(KERN_ERR "[EXFAT] truncate : can't skip it because of "
				"size-mismatch(old:%lld->fid:%lld).\n"
				,old_size, fid->size);
	}

	if (old_size <= new_size)
		return FFS_SUCCESS;

	fs_set_vol_flags(sb, VOL_DIRTY);

	clu.dir = fid->start_clu;
	clu.size = (INT32)((old_size-1) >> p_fs->cluster_size_bits) + 1;
	clu.flags = fid->flags;

	if (new_size > 0) {
		num_clusters = (INT32)((new_size-1) >> p_fs->cluster_size_bits) + 1;

		if (clu.flags == 0x03) {
			clu.dir += num_clusters;
		} else {
			while (num_clusters > 0) {
				last_clu = clu.dir;
				if (FAT_read(sb, clu.dir, &(clu.dir)) == -1)
					return FFS_MEDIAERR;
				num_clusters--;
			}
		}

		clu.size -= num_clusters;
	}

	fid->size = new_size;
	fid->attr |= ATTR_ARCHIVE;
	if (new_size == 0) {
		fid->flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;
		fid->start_clu = CLUSTER_32(~0);
	}

	/* (1) update the directory entry */
	if (p_fs->vol_type == EXFAT) {
		es = get_entry_set_in_dir(sb, &(fid->dir), fid->entry, ES_ALL_ENTRIES, &ep);
		if (es == NULL)
			return FFS_MEDIAERR;
		ep2 = ep+1;
	} else {
		ep = get_entry_in_dir(sb, &(fid->dir), fid->entry, &sector);
		if (!ep)
			return FFS_MEDIAERR;
		ep2 = ep;
	}

	p_fs->fs_func->set_entry_time(ep, tm_current(&tm), TM_MODIFY);
	p_fs->fs_func->set_entry_attr(ep, fid->attr);

	p_fs->fs_func->set_entry_size(ep2, new_size);
	if (new_size == 0) {
		p_fs->fs_func->set_entry_flag(ep2, 0x01);
		p_fs->fs_func->set_entry_clu0(ep2, CLUSTER_32(0));
	}

	if (p_fs->vol_type != EXFAT)
		buf_modify(sb, sector);
	else {
		update_dir_checksum_with_entry_set(sb, es);
		release_entry_set(es);
	}

	/* (2) cut off from the FAT chain */
	if (last_clu != CLUSTER_32(0)) {
		if (fid->flags == 0x01)
			FAT_write(sb, last_clu, CLUSTER_32(~0));
	}

	/* (3) free the clusters */
	p_fs->fs_func->free_cluster(sb, &clu, 0);

	/* hint information */
	fid->hint_last_off = -1;
	if (fid->rwoffset > fid->size) {
		fid->rwoffset = fid->size;
	}

#if (DELAYED_SYNC == 0)
	fs_sync(sb, 0);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return FFS_SUCCESS;
} /* end of ffsTruncateFile */

static void update_parent_info( FILE_ID_T *fid, struct inode *parent_inode)
{
        FS_INFO_T *p_fs = &(EXFAT_SB(parent_inode->i_sb)->fs_info);
	FILE_ID_T *parent_fid = &(EXFAT_I(parent_inode)->fid);

	if (unlikely((parent_fid->flags != fid->dir.flags)
		|| (parent_fid->size != (fid->dir.size<<p_fs->cluster_size_bits))
		|| (parent_fid->start_clu != fid->dir.dir))) {

		fid->dir.dir = parent_fid->start_clu;
		fid->dir.flags = parent_fid->flags;
		fid->dir.size = ((parent_fid->size + (p_fs->cluster_size-1))
						>> p_fs->cluster_size_bits);
	}
}

/* ffsMoveFile : move(rename) a old file into a new file */
INT32 ffsMoveFile(struct inode *old_parent_inode, FILE_ID_T *fid, struct inode *new_parent_inode, struct dentry *new_dentry)
{
	INT32 ret;
	INT32 dentry;
	CHAIN_T olddir, newdir;
	CHAIN_T *p_dir=NULL;
	UNI_NAME_T uni_name;
	DENTRY_T *ep;
	struct super_block *sb = old_parent_inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	UINT8 *new_path = (UINT8 *) new_dentry->d_name.name;
	struct inode *new_inode = new_dentry->d_inode;
	int num_entries;
	FILE_ID_T *new_fid = NULL;
	INT32 new_entry=0;

	/* check the validity of pointer parameters */
	if ((new_path == NULL) || (*new_path == '\0'))
		return FFS_ERROR;

	update_parent_info(fid, old_parent_inode);

	olddir.dir = fid->dir.dir;
	olddir.size = fid->dir.size;
	olddir.flags = fid->dir.flags;

	dentry = fid->entry;

	/* check if the old file is "." or ".." */
	if (p_fs->vol_type != EXFAT) {
		if ((olddir.dir != p_fs->root_dir) && (dentry < 2))
			return FFS_PERMISSIONERR;
	}

	ep = get_entry_in_dir(sb, &olddir, dentry, NULL);
	if (!ep)
		return FFS_MEDIAERR;

	if (p_fs->fs_func->get_entry_attr(ep) & ATTR_READONLY)
		return FFS_PERMISSIONERR;

	/* check whether new dir is existing directory and empty */
	if (new_inode) {
		UINT32 entry_type;

		ret = FFS_MEDIAERR;
		new_fid = &EXFAT_I(new_inode)->fid;

		update_parent_info(new_fid, new_parent_inode);

		p_dir = &(new_fid->dir);
		new_entry = new_fid->entry;
		ep = get_entry_in_dir(sb, p_dir, new_entry, NULL);
		if (!ep)
			goto out;

		entry_type = p_fs->fs_func->get_entry_type(ep);

		if (entry_type == TYPE_DIR) {
			CHAIN_T new_clu;
			new_clu.dir = new_fid->start_clu;
			new_clu.size = (INT32)((new_fid->size-1) >> p_fs->cluster_size_bits) + 1;
			new_clu.flags = new_fid->flags;

			if (!is_dir_empty(sb, &new_clu))
				return FFS_FILEEXIST;
		}
	}

	/* check the validity of directory name in the given new pathname */
	ret = resolve_path(new_parent_inode, new_path, &newdir, &uni_name);
	if (ret)
		return ret;

	fs_set_vol_flags(sb, VOL_DIRTY);

	if (olddir.dir == newdir.dir)
		ret = rename_file(new_parent_inode, &olddir, dentry, &uni_name, fid);
	else
		ret = move_file(new_parent_inode, &olddir, dentry, &newdir, &uni_name, fid);

	if ((ret == FFS_SUCCESS) && new_inode) {
		/* delete entries of new_dir */
		ep = get_entry_in_dir(sb, p_dir, new_entry, NULL);
		if (!ep)
			goto out;

		num_entries = p_fs->fs_func->count_ext_entries(sb, p_dir, new_entry, ep);
		if (num_entries < 0)
			goto out;
		p_fs->fs_func->delete_dir_entry(sb, p_dir, new_entry, 0, num_entries+1);
	}
out:
#if (DELAYED_SYNC == 0)
	fs_sync(sb, 0);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return ret;
} /* end of ffsMoveFile */

/* ffsRemoveFile : remove a file */
INT32 ffsRemoveFile(struct inode *inode, FILE_ID_T *fid)
{
	INT32 dentry;
	CHAIN_T dir, clu_to_free;
	DENTRY_T *ep;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	dir.dir = fid->dir.dir;
	dir.size = fid->dir.size;
	dir.flags = fid->dir.flags;

	dentry = fid->entry;

	ep = get_entry_in_dir(sb, &dir, dentry, NULL);
	if (!ep)
		return FFS_MEDIAERR;

	if (p_fs->fs_func->get_entry_attr(ep) & ATTR_READONLY)
		return FFS_PERMISSIONERR;

	fs_set_vol_flags(sb, VOL_DIRTY);

	/* (1) update the directory entry */
	remove_file(inode, &dir, dentry);

	clu_to_free.dir = fid->start_clu;
	clu_to_free.size = (INT32)((fid->size-1) >> p_fs->cluster_size_bits) + 1;
	clu_to_free.flags = fid->flags;

	/* (2) free the clusters */
	p_fs->fs_func->free_cluster(sb, &clu_to_free, 0);

	fid->size = 0;
	fid->start_clu = CLUSTER_32(~0);
	fid->flags = (p_fs->vol_type == EXFAT)? 0x03: 0x01;

#if (DELAYED_SYNC == 0)
	fs_sync(sb, 0);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return FFS_SUCCESS;
} /* end of ffsRemoveFile */

/* ffsSetAttr : set the attribute of a given file */
INT32 ffsSetAttr(struct inode *inode, UINT32 attr)
{
	UINT32 type, sector = 0;
	DENTRY_T *ep;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	FILE_ID_T *fid = &(EXFAT_I(inode)->fid);
	UINT8 is_dir = (fid->type == TYPE_DIR) ? 1 : 0;
	ENTRY_SET_CACHE_T *es = NULL;

	if (fid->attr == attr) {
		if (p_fs->dev_ejected)
			return FFS_MEDIAERR;
		return FFS_SUCCESS;
	}

	if (is_dir) {
		if ((fid->dir.dir == p_fs->root_dir) &&
			(fid->entry == -1)) {
			if (p_fs->dev_ejected)
				return FFS_MEDIAERR;
			return FFS_SUCCESS;
		}
	}

	/* get the directory entry of given file */
	if (p_fs->vol_type == EXFAT) {
		es = get_entry_set_in_dir(sb, &(fid->dir), fid->entry, ES_ALL_ENTRIES, &ep);
		if (es == NULL)
			return FFS_MEDIAERR;
	} else {
		ep = get_entry_in_dir(sb, &(fid->dir), fid->entry, &sector);
		if (!ep)
			return FFS_MEDIAERR;
	}

	type = p_fs->fs_func->get_entry_type(ep);

	if (((type == TYPE_FILE) && (attr & ATTR_SUBDIR)) ||
		((type == TYPE_DIR) && (!(attr & ATTR_SUBDIR)))) {
		INT32 err;
		if (p_fs->dev_ejected)
			err = FFS_MEDIAERR;
		else
			err = FFS_ERROR;

		if (p_fs->vol_type == EXFAT)
			release_entry_set(es);
		return err;
	}

	fs_set_vol_flags(sb, VOL_DIRTY);

	/* set the file attribute */
	fid->attr = attr;
	p_fs->fs_func->set_entry_attr(ep, attr);

	if (p_fs->vol_type != EXFAT)
		buf_modify(sb, sector);
	else {
		update_dir_checksum_with_entry_set(sb, es);
		release_entry_set(es);
	}

#if (DELAYED_SYNC == 0)
	fs_sync(sb, 0);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return FFS_SUCCESS;
} /* end of ffsSetAttr */

/* ffsGetStat : get the information of a given file */
INT32 ffsGetStat(struct inode *inode, DIR_ENTRY_T *info)
{
	UINT32 sector = 0;
	INT32 count;
	CHAIN_T dir;
	UNI_NAME_T uni_name;
	TIMESTAMP_T tm;
	DENTRY_T *ep, *ep2;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	FILE_ID_T *fid = &(EXFAT_I(inode)->fid);
	ENTRY_SET_CACHE_T *es=NULL;
	UINT8 is_dir = (fid->type == TYPE_DIR) ? 1 : 0;

	PRINTK("ffsGetStat entered\n");

	if (is_dir) {
		if ((fid->dir.dir == p_fs->root_dir) &&
			(fid->entry == -1)) {
			info->Attr = ATTR_SUBDIR;
			MEMSET((INT8 *) &info->CreateTimestamp, 0, sizeof(DATE_TIME_T));
			MEMSET((INT8 *) &info->ModifyTimestamp, 0, sizeof(DATE_TIME_T));
			MEMSET((INT8 *) &info->AccessTimestamp, 0, sizeof(DATE_TIME_T));
			STRCPY(info->ShortName, ".");
			STRCPY(info->Name, ".");

			dir.dir = p_fs->root_dir;
			dir.flags = 0x01;

			if (p_fs->root_dir == CLUSTER_32(0)) /* FAT16 root_dir */
				info->Size = p_fs->dentries_in_root << DENTRY_SIZE_BITS;
			else
				info->Size = count_num_clusters(sb, &dir) << p_fs->cluster_size_bits;

			count = count_dos_name_entries(sb, &dir, TYPE_DIR);
			if (count < 0)
				return FFS_MEDIAERR;
			info->NumSubdirs = count;

			if (p_fs->dev_ejected)
				return FFS_MEDIAERR;
			return FFS_SUCCESS;
		}
	}

	/* get the directory entry of given file or directory */
	if (p_fs->vol_type == EXFAT) {
		es = get_entry_set_in_dir(sb, &(fid->dir), fid->entry, ES_2_ENTRIES, &ep);
		if (es == NULL)
			return FFS_MEDIAERR;
		ep2 = ep+1;
	} else {
		ep = get_entry_in_dir(sb, &(fid->dir), fid->entry, &sector);
		if (!ep)
			return FFS_MEDIAERR;
		ep2 = ep;
		buf_lock(sb, sector);
	}

	/* set FILE_INFO structure using the acquired DENTRY_T */
	info->Attr = p_fs->fs_func->get_entry_attr(ep);

	p_fs->fs_func->get_entry_time(ep, &tm, TM_CREATE);
	info->CreateTimestamp.Year = tm.year;
	info->CreateTimestamp.Month = tm.mon;
	info->CreateTimestamp.Day = tm.day;
	info->CreateTimestamp.Hour = tm.hour;
	info->CreateTimestamp.Minute = tm.min;
	info->CreateTimestamp.Second = tm.sec;
	info->CreateTimestamp.MilliSecond = 0;

	p_fs->fs_func->get_entry_time(ep, &tm, TM_MODIFY);
	info->ModifyTimestamp.Year = tm.year;
	info->ModifyTimestamp.Month = tm.mon;
	info->ModifyTimestamp.Day = tm.day;
	info->ModifyTimestamp.Hour = tm.hour;
	info->ModifyTimestamp.Minute = tm.min;
	info->ModifyTimestamp.Second = tm.sec;
	info->ModifyTimestamp.MilliSecond = 0;

	MEMSET((INT8 *) &info->AccessTimestamp, 0, sizeof(DATE_TIME_T));

	*(uni_name.name) = 0x0;
	/* XXX this is very bad for exfat cuz name is already included in es.
	 API should be revised */
	p_fs->fs_func->get_uni_name_from_ext_entry(sb, &(fid->dir), fid->entry, uni_name.name);
	if (*(uni_name.name) == 0x0)
		get_uni_name_from_dos_entry(sb, (DOS_DENTRY_T *) ep, &uni_name, 0x1);
	nls_uniname_to_cstring(sb, info->Name, &uni_name);

	if (p_fs->vol_type == EXFAT) {
		info->NumSubdirs = 2;
	} else {
		buf_unlock(sb, sector);
		get_uni_name_from_dos_entry(sb, (DOS_DENTRY_T *) ep, &uni_name, 0x0);
		nls_uniname_to_cstring(sb, info->ShortName, &uni_name);
		info->NumSubdirs = 0;
	}

	info->Size = p_fs->fs_func->get_entry_size(ep2);

	if (p_fs->vol_type == EXFAT)
		release_entry_set(es);

	if (is_dir) {
		dir.dir = fid->start_clu;
		dir.flags = 0x01;

		if (info->Size == 0)
			info->Size = (UINT64) count_num_clusters(sb, &dir) << p_fs->cluster_size_bits;

		count = count_dos_name_entries(sb, &dir, TYPE_DIR);
		if (count < 0)
			return FFS_MEDIAERR;
		info->NumSubdirs += count;
	}

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	PRINTK("ffsGetStat exited successfully\n");
	return FFS_SUCCESS;
} /* end of ffsGetStat */

/* ffsSetStat : set the information of a given file */
INT32 ffsSetStat(struct inode *inode, DIR_ENTRY_T *info)
{
	UINT32 sector = 0;
	TIMESTAMP_T tm;
	DENTRY_T *ep, *ep2;
	ENTRY_SET_CACHE_T *es=NULL;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	FILE_ID_T *fid = &(EXFAT_I(inode)->fid);
	UINT8 is_dir = (fid->type == TYPE_DIR) ? 1 : 0;

	if (is_dir) {
		if ((fid->dir.dir == p_fs->root_dir) &&
			(fid->entry == -1)) {
			if (p_fs->dev_ejected)
				return FFS_MEDIAERR;
			return FFS_SUCCESS;
		}
	}

	fs_set_vol_flags(sb, VOL_DIRTY);

	/* get the directory entry of given file or directory */
	if (p_fs->vol_type == EXFAT) {
		es = get_entry_set_in_dir(sb, &(fid->dir), fid->entry, ES_ALL_ENTRIES, &ep);
		if (es == NULL)
			return FFS_MEDIAERR;
		ep2 = ep+1;
	} else {
		/* for other than exfat */
		ep = get_entry_in_dir(sb, &(fid->dir), fid->entry, &sector);
		if (!ep)
			return FFS_MEDIAERR;
		ep2 = ep;
	}


	p_fs->fs_func->set_entry_attr(ep, info->Attr);

	/* set FILE_INFO structure using the acquired DENTRY_T */
	tm.sec  = info->CreateTimestamp.Second;
	tm.min  = info->CreateTimestamp.Minute;
	tm.hour = info->CreateTimestamp.Hour;
	tm.day  = info->CreateTimestamp.Day;
	tm.mon  = info->CreateTimestamp.Month;
	tm.year = info->CreateTimestamp.Year;
	p_fs->fs_func->set_entry_time(ep, &tm, TM_CREATE);

	tm.sec  = info->ModifyTimestamp.Second;
	tm.min  = info->ModifyTimestamp.Minute;
	tm.hour = info->ModifyTimestamp.Hour;
	tm.day  = info->ModifyTimestamp.Day;
	tm.mon  = info->ModifyTimestamp.Month;
	tm.year = info->ModifyTimestamp.Year;
	p_fs->fs_func->set_entry_time(ep, &tm, TM_MODIFY);


	p_fs->fs_func->set_entry_size(ep2, info->Size);

	if (p_fs->vol_type != EXFAT) {
		buf_modify(sb, sector);
	} else {
		update_dir_checksum_with_entry_set(sb, es);
		release_entry_set(es);
	}

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return FFS_SUCCESS;
} /* end of ffsSetStat */

INT32 ffsMapCluster(struct inode *inode, INT32 clu_offset, UINT32 *clu)
{
	INT32 num_clusters, num_alloced, modified = FALSE;
	UINT32 last_clu, sector = 0;
	CHAIN_T new_clu;
	DENTRY_T *ep;
	ENTRY_SET_CACHE_T *es = NULL;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	FILE_ID_T *fid = &(EXFAT_I(inode)->fid);

	fid->rwoffset = (INT64)(clu_offset) << p_fs->cluster_size_bits;

	if (EXFAT_I(inode)->mmu_private == 0)
		num_clusters = 0;
	else
		num_clusters = (INT32)((EXFAT_I(inode)->mmu_private-1) >> p_fs->cluster_size_bits) + 1;

	*clu = last_clu = fid->start_clu;

	if (fid->flags == 0x03) {
		if ((clu_offset > 0) && (*clu != CLUSTER_32(~0))) {
			last_clu += clu_offset - 1;

			if (clu_offset == num_clusters)
				*clu = CLUSTER_32(~0);
			else
				*clu += clu_offset;
		}
	} else {
		/* hint information */
		if ((clu_offset > 0) && (fid->hint_last_off > 0) &&
			(clu_offset >= fid->hint_last_off)) {
			clu_offset -= fid->hint_last_off;
			*clu = fid->hint_last_clu;
		}

		while ((clu_offset > 0) && (*clu != CLUSTER_32(~0))) {
			last_clu = *clu;
			if (FAT_read(sb, *clu, clu) == -1)
				return FFS_MEDIAERR;
			clu_offset--;
		}
	}

	if (*clu == CLUSTER_32(~0)) {
		fs_set_vol_flags(sb, VOL_DIRTY);

		new_clu.dir = (last_clu == CLUSTER_32(~0)) ? CLUSTER_32(~0) : last_clu+1;
		new_clu.size = 0;
		new_clu.flags = fid->flags;

		/* (1) allocate a cluster */
		num_alloced = p_fs->fs_func->alloc_cluster(sb, 1, &new_clu);
		if (num_alloced < 1)
			return FFS_FULL;

		/* (2) append to the FAT chain */
		if (last_clu == CLUSTER_32(~0)) {
			if (new_clu.flags == 0x01)
				fid->flags = 0x01;
			fid->start_clu = new_clu.dir;
			modified = TRUE;
		} else {
			if (new_clu.flags != fid->flags) {
				exfat_chain_cont_cluster(sb, fid->start_clu, num_clusters);
				fid->flags = 0x01;
				modified = TRUE;
			}
			if (new_clu.flags == 0x01)
				FAT_write(sb, last_clu, new_clu.dir);
		}

		*clu = new_clu.dir;

		if (p_fs->vol_type == EXFAT) {
			es = get_entry_set_in_dir(sb, &(fid->dir), fid->entry, ES_ALL_ENTRIES, &ep);
			if (es == NULL)
				return FFS_MEDIAERR;
			/* get stream entry */
			ep++;
		}

		/* (3) update directory entry */
		if (modified) {
			if (p_fs->vol_type != EXFAT) {
				ep = get_entry_in_dir(sb, &(fid->dir), fid->entry, &sector);
				if (!ep)
					return FFS_MEDIAERR;
			}

			if (p_fs->fs_func->get_entry_flag(ep) != fid->flags)
				p_fs->fs_func->set_entry_flag(ep, fid->flags);

			if (p_fs->fs_func->get_entry_clu0(ep) != fid->start_clu)
				p_fs->fs_func->set_entry_clu0(ep, fid->start_clu);

			if (p_fs->vol_type != EXFAT)
				buf_modify(sb, sector);
		}

		if (p_fs->vol_type == EXFAT) {
			update_dir_checksum_with_entry_set(sb, es);
			release_entry_set(es);
		}

		/* add number of new blocks to inode */
		inode->i_blocks += num_alloced << (p_fs->cluster_size_bits - 9);
	}

	/* hint information */
	fid->hint_last_off = (INT32)(fid->rwoffset >> p_fs->cluster_size_bits);
	fid->hint_last_clu = *clu;

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return FFS_SUCCESS;
} /* end of ffsMapCluster */

/*----------------------------------------------------------------------*/
/*  Directory Operation Functions                                       */
/*----------------------------------------------------------------------*/

/* ffsCreateDir : create(make) a directory */
INT32 ffsCreateDir(struct inode *inode, UINT8 *path, FILE_ID_T *fid)
{
	INT32 ret/*, dentry*/;
	CHAIN_T dir;
	UNI_NAME_T uni_name;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	PRINTK("ffsCreateDir entered\n");

	/* check the validity of directory name in the given old pathname */
	ret = resolve_path(inode, path, &dir, &uni_name);
	if (ret)
		return ret;

	fs_set_vol_flags(sb, VOL_DIRTY);

	ret = create_dir(inode, &dir, &uni_name, fid);

#if (DELAYED_SYNC == 0)
	fs_sync(sb, 0);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return ret;
} /* end of ffsCreateDir */

/* ffsReadDir : read a directory entry from the opened directory */
INT32 ffsReadDir(struct inode *inode, DIR_ENTRY_T *dir_entry)
{
	INT32 i, dentry, clu_offset;
	INT32 dentries_per_clu, dentries_per_clu_bits = 0;
	UINT32 type, sector;
	CHAIN_T dir, clu;
	UNI_NAME_T uni_name;
	TIMESTAMP_T tm;
	DENTRY_T *ep;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	FILE_ID_T *fid = &(EXFAT_I(inode)->fid);

	/* check if the given file ID is opened */
	if (fid->type != TYPE_DIR)
		return FFS_PERMISSIONERR;

	if (fid->entry == -1) {
		dir.dir = p_fs->root_dir;
		dir.flags = 0x01;
	} else {
		dir.dir = fid->start_clu;
		dir.size = (INT32)(fid->size >> p_fs->cluster_size_bits);
		dir.flags = fid->flags;
	}

	dentry = (INT32) fid->rwoffset;

	if (dir.dir == CLUSTER_32(0)) { /* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;

		if (dentry == dentries_per_clu) {
			clu.dir = CLUSTER_32(~0);
		} else {
			clu.dir = dir.dir;
			clu.size = dir.size;
			clu.flags = dir.flags;
		}
	} else {
		dentries_per_clu = p_fs->dentries_per_clu;
		dentries_per_clu_bits = my_log2(dentries_per_clu);

		clu_offset = dentry >> dentries_per_clu_bits;
		clu.dir = dir.dir;
		clu.size = dir.size;
		clu.flags = dir.flags;

		if (clu.flags == 0x03) {
			clu.dir += clu_offset;
			clu.size -= clu_offset;
		} else {
			/* hint_information */
			if ((clu_offset > 0) && (fid->hint_last_off > 0) &&
				(clu_offset >= fid->hint_last_off)) {
				clu_offset -= fid->hint_last_off;
				clu.dir = fid->hint_last_clu;
			}

			while (clu_offset > 0) {
				/* clu.dir = FAT_read(sb, clu.dir); */
				if (FAT_read(sb, clu.dir, &(clu.dir)) == -1)
					return FFS_MEDIAERR;

				clu_offset--;
			}
		}
	}

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		if (dir.dir == CLUSTER_32(0)) /* FAT16 root_dir */
			i = dentry % dentries_per_clu;
		else
			i = dentry & (dentries_per_clu-1);

		for ( ; i < dentries_per_clu; i++, dentry++) {
			ep = get_entry_in_dir(sb, &clu, i, &sector);
			if (!ep)
				return FFS_MEDIAERR;

			type = p_fs->fs_func->get_entry_type(ep);

			if (type == TYPE_UNUSED)
				break;

			if ((type != TYPE_FILE) && (type != TYPE_DIR))
				continue;

			buf_lock(sb, sector);
			dir_entry->Attr = p_fs->fs_func->get_entry_attr(ep);

			p_fs->fs_func->get_entry_time(ep, &tm, TM_CREATE);
			dir_entry->CreateTimestamp.Year = tm.year;
			dir_entry->CreateTimestamp.Month = tm.mon;
			dir_entry->CreateTimestamp.Day = tm.day;
			dir_entry->CreateTimestamp.Hour = tm.hour;
			dir_entry->CreateTimestamp.Minute = tm.min;
			dir_entry->CreateTimestamp.Second = tm.sec;
			dir_entry->CreateTimestamp.MilliSecond = 0;

			p_fs->fs_func->get_entry_time(ep, &tm, TM_MODIFY);
			dir_entry->ModifyTimestamp.Year = tm.year;
			dir_entry->ModifyTimestamp.Month = tm.mon;
			dir_entry->ModifyTimestamp.Day = tm.day;
			dir_entry->ModifyTimestamp.Hour = tm.hour;
			dir_entry->ModifyTimestamp.Minute = tm.min;
			dir_entry->ModifyTimestamp.Second = tm.sec;
			dir_entry->ModifyTimestamp.MilliSecond = 0;

			MEMSET((INT8 *) &dir_entry->AccessTimestamp, 0, sizeof(DATE_TIME_T));

			*(uni_name.name) = 0x0;
			p_fs->fs_func->get_uni_name_from_ext_entry(sb, &dir, dentry, uni_name.name);
			if (*(uni_name.name) == 0x0)
				get_uni_name_from_dos_entry(sb, (DOS_DENTRY_T *) ep, &uni_name, 0x1);
			nls_uniname_to_cstring(sb, dir_entry->Name, &uni_name);
			buf_unlock(sb, sector);

			if (p_fs->vol_type == EXFAT) {
				ep = get_entry_in_dir(sb, &clu, i+1, NULL);
				if (!ep)
					return FFS_MEDIAERR;
			} else {
				get_uni_name_from_dos_entry(sb, (DOS_DENTRY_T *) ep, &uni_name, 0x0);
				nls_uniname_to_cstring(sb, dir_entry->ShortName, &uni_name);
			}

			dir_entry->Size = p_fs->fs_func->get_entry_size(ep);

			/* hint information */
			if (dir.dir == CLUSTER_32(0)) { /* FAT16 root_dir */
			} else {
				fid->hint_last_off = dentry >> dentries_per_clu_bits;
				fid->hint_last_clu = clu.dir;
			}

			fid->rwoffset = (INT64) ++dentry;

			if (p_fs->dev_ejected)
				return FFS_MEDIAERR;

			return FFS_SUCCESS;
		}

		if (dir.dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (clu.flags == 0x03) {
			if ((--clu.size) > 0)
				clu.dir++;
			else
				clu.dir = CLUSTER_32(~0);
		} else {
			/* clu.dir = FAT_read(sb, clu.dir); */
			if (FAT_read(sb, clu.dir, &(clu.dir)) == -1)
				return FFS_MEDIAERR;
		}
	}

	*(dir_entry->Name) = '\0';

	fid->rwoffset = (INT64) ++dentry;

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return FFS_SUCCESS;
} /* end of ffsReadDir */

/* ffsRemoveDir : remove a directory */
INT32 ffsRemoveDir(struct inode *inode, FILE_ID_T *fid)
{
	INT32 dentry;
	CHAIN_T dir, clu_to_free;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	dir.dir = fid->dir.dir;
	dir.size = fid->dir.size;
	dir.flags = fid->dir.flags;

	dentry = fid->entry;

	/* check if the file is "." or ".." */
	if (p_fs->vol_type != EXFAT) {
		if ((dir.dir != p_fs->root_dir) && (dentry < 2))
			return FFS_PERMISSIONERR;
	}

	clu_to_free.dir = fid->start_clu;
	clu_to_free.size = (INT32)((fid->size-1) >> p_fs->cluster_size_bits) + 1;
	clu_to_free.flags = fid->flags;

	if (!is_dir_empty(sb, &clu_to_free))
		return FFS_FILEEXIST;

	fs_set_vol_flags(sb, VOL_DIRTY);

	/* (1) update the directory entry */
	remove_file(inode, &dir, dentry);

	/* (2) free the clusters */
	p_fs->fs_func->free_cluster(sb, &clu_to_free, 1);

	fid->size = 0;
	fid->start_clu = CLUSTER_32(~0);
	fid->flags = (p_fs->vol_type == EXFAT)? 0x03: 0x01;

#if (DELAYED_SYNC == 0)
	fs_sync(sb, 0);
	fs_set_vol_flags(sb, VOL_CLEAN);
#endif

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	return FFS_SUCCESS;
} /* end of ffsRemoveDir */

/*======================================================================*/
/*  Local Function Definitions                                          */
/*======================================================================*/

/*
 *  File System Management Functions
 */

INT32 fs_init(void)
{
	/* critical check for system requirement on size of DENTRY_T structure */
	if (sizeof(DENTRY_T) != DENTRY_SIZE) {
		return FFS_ALIGNMENTERR;
	}

	if (sizeof(DOS_DENTRY_T) != DENTRY_SIZE) {
		return FFS_ALIGNMENTERR;
	}

	if (sizeof(EXT_DENTRY_T) != DENTRY_SIZE) {
		return FFS_ALIGNMENTERR;
	}

	if (sizeof(FILE_DENTRY_T) != DENTRY_SIZE) {
		return FFS_ALIGNMENTERR;
	}

	if (sizeof(STRM_DENTRY_T) != DENTRY_SIZE) {
		return FFS_ALIGNMENTERR;
	}

	if (sizeof(NAME_DENTRY_T) != DENTRY_SIZE) {
		return FFS_ALIGNMENTERR;
	}

	if (sizeof(BMAP_DENTRY_T) != DENTRY_SIZE) {
		return FFS_ALIGNMENTERR;
	}

	if (sizeof(CASE_DENTRY_T) != DENTRY_SIZE) {
		return FFS_ALIGNMENTERR;
	}

	if (sizeof(VOLM_DENTRY_T) != DENTRY_SIZE) {
		return FFS_ALIGNMENTERR;
	}

	return FFS_SUCCESS;
} /* end of fs_init */

INT32 fs_shutdown(void)
{
	return FFS_SUCCESS;
} /* end of fs_shutdown */

void fs_set_vol_flags(struct super_block *sb, UINT32 new_flag)
{
	PBR_SECTOR_T *p_pbr;
	BPBEX_T *p_bpb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_fs->vol_flag == new_flag)
		return;

	p_fs->vol_flag = new_flag;

	if (p_fs->vol_type == EXFAT) {
		if (p_fs->pbr_bh == NULL) {
			if (sector_read(sb, p_fs->PBR_sector, &(p_fs->pbr_bh), 1) != FFS_SUCCESS)
				return;
		}

		p_pbr = (PBR_SECTOR_T *) p_fs->pbr_bh->b_data;
		p_bpb = (BPBEX_T *) p_pbr->bpb;
		SET16(p_bpb->vol_flags, (UINT16) new_flag);

		/* XXX duyoung
		 what can we do here? (cuz fs_set_vol_flags() is void) */
		if ((new_flag == VOL_DIRTY) && (!buffer_dirty(p_fs->pbr_bh)))
			sector_write(sb, p_fs->PBR_sector, p_fs->pbr_bh, 1);
		else
			sector_write(sb, p_fs->PBR_sector, p_fs->pbr_bh, 0);
	}
} /* end of fs_set_vol_flags */

void fs_sync(struct super_block *sb, INT32 do_sync)
{
	if (do_sync)
		bdev_sync(sb);
} /* end of fs_sync */

void fs_error(struct super_block *sb)
{
	struct exfat_mount_options *opts = &EXFAT_SB(sb)->options;

	if (opts->errors == EXFAT_ERRORS_PANIC)
		panic("[EXFAT] Filesystem panic from previous error\n");
	else if ((opts->errors == EXFAT_ERRORS_RO) && !(sb->s_flags & MS_RDONLY)) {
		sb->s_flags |= MS_RDONLY;
		printk(KERN_ERR "[EXFAT] Filesystem has been set read-only\n");
	}
}

/*
 *  Cluster Management Functions
 */

INT32 clear_cluster(struct super_block *sb, UINT32 clu)
{
	UINT32 s, n;
	INT32 ret = FFS_SUCCESS;
	struct buffer_head *tmp_bh = NULL;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (clu == CLUSTER_32(0)) { /* FAT16 root_dir */
		s = p_fs->root_start_sector;
		n = p_fs->data_start_sector;
	} else {
		s = START_SECTOR(clu);
		n = s + p_fs->sectors_per_clu;
	}

	for ( ; s < n; s++) {
		if ((ret = sector_read(sb, s, &tmp_bh, 0)) != FFS_SUCCESS)
			return ret;

		MEMSET((INT8 *) tmp_bh->b_data, 0x0, p_bd->sector_size);
		if ((ret = sector_write(sb, s, tmp_bh, 0)) !=FFS_SUCCESS)
			break;
	}

	brelse(tmp_bh);
	return ret;
} /* end of clear_cluster */

INT32 fat_alloc_cluster(struct super_block *sb, INT32 num_alloc, CHAIN_T *p_chain)
{
	INT32 i, num_clusters = 0;
	UINT32 new_clu, last_clu = CLUSTER_32(~0), read_clu;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	new_clu = p_chain->dir;
	if (new_clu == CLUSTER_32(~0))
		new_clu = p_fs->clu_srch_ptr;
	else if (new_clu >= p_fs->num_clusters)
		new_clu = 2;

	__set_sb_dirty(sb);

	p_chain->dir = CLUSTER_32(~0);

	for (i = 2; i < p_fs->num_clusters; i++) {
		if (FAT_read(sb, new_clu, &read_clu) != 0)
			return 0;

		if (read_clu == CLUSTER_32(0)) {
			FAT_write(sb, new_clu, CLUSTER_32(~0));
			num_clusters++;

			if (p_chain->dir == CLUSTER_32(~0))
				p_chain->dir = new_clu;
			else
				FAT_write(sb, last_clu, new_clu);

			last_clu = new_clu;

			if ((--num_alloc) == 0) {
				p_fs->clu_srch_ptr = new_clu;
				if (p_fs->used_clusters != (UINT32) ~0)
					p_fs->used_clusters += num_clusters;

				return(num_clusters);
			}
		}
		if ((++new_clu) >= p_fs->num_clusters)
			new_clu = 2;
	}

	p_fs->clu_srch_ptr = new_clu;
	if (p_fs->used_clusters != (UINT32) ~0)
		p_fs->used_clusters += num_clusters;

	return(num_clusters);
} /* end of fat_alloc_cluster */

INT32 exfat_alloc_cluster(struct super_block *sb, INT32 num_alloc, CHAIN_T *p_chain)
{
	INT32 num_clusters = 0;
	UINT32 hint_clu, new_clu, last_clu = CLUSTER_32(~0);
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	hint_clu = p_chain->dir;
	if (hint_clu == CLUSTER_32(~0)) {
		hint_clu = test_alloc_bitmap(sb, p_fs->clu_srch_ptr-2);
		if (hint_clu == CLUSTER_32(~0))
			return 0;
	} else if (hint_clu >= p_fs->num_clusters) {
		hint_clu = 2;
		p_chain->flags = 0x01;
	}

	__set_sb_dirty(sb);

	p_chain->dir = CLUSTER_32(~0);

	while ((new_clu = test_alloc_bitmap(sb, hint_clu-2)) != CLUSTER_32(~0)) {
		if (new_clu != hint_clu) {
			if (p_chain->flags == 0x03) {
				exfat_chain_cont_cluster(sb, p_chain->dir, num_clusters);
				p_chain->flags = 0x01;
			}
		}

		if (set_alloc_bitmap(sb, new_clu-2) != FFS_SUCCESS)
			return 0;

		num_clusters++;

		if (p_chain->flags == 0x01)
			FAT_write(sb, new_clu, CLUSTER_32(~0));

		if (p_chain->dir == CLUSTER_32(~0)) {
			p_chain->dir = new_clu;
		} else {
			if (p_chain->flags == 0x01)
				FAT_write(sb, last_clu, new_clu);
		}
		last_clu = new_clu;

		if ((--num_alloc) == 0) {
			p_fs->clu_srch_ptr = hint_clu;
			if (p_fs->used_clusters != (UINT32) ~0)
				p_fs->used_clusters += num_clusters;

			p_chain->size += num_clusters;
			return(num_clusters);
		}

		hint_clu = new_clu + 1;
		if (hint_clu >= p_fs->num_clusters) {
			hint_clu = 2;

			if (p_chain->flags == 0x03) {
				exfat_chain_cont_cluster(sb, p_chain->dir, num_clusters);
				p_chain->flags = 0x01;
			}
		}
	}

	p_fs->clu_srch_ptr = hint_clu;
	if (p_fs->used_clusters != (UINT32) ~0)
		p_fs->used_clusters += num_clusters;

	p_chain->size += num_clusters;
	return(num_clusters);
} /* end of exfat_alloc_cluster */

void fat_free_cluster(struct super_block *sb, CHAIN_T *p_chain, INT32 do_relse)
{
	INT32 num_clusters = 0;
	UINT32 clu, prev;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	INT32 i;
	UINT32 sector;

	if ((p_chain->dir == CLUSTER_32(0)) || (p_chain->dir == CLUSTER_32(~0)))
		return;
	__set_sb_dirty(sb);
	clu = p_chain->dir;

	if (p_chain->size <= 0)
		return;

	do {
		if (p_fs->dev_ejected)
			break;

		if (do_relse) {
			sector = START_SECTOR(clu);
			for (i = 0; i < p_fs->sectors_per_clu; i++) {
				buf_release(sb, sector+i);
			}
		}

		prev = clu;
		if (FAT_read(sb, clu, &clu) == -1)
			break;

		FAT_write(sb, prev, CLUSTER_32(0));
		num_clusters++;

	} while (clu != CLUSTER_32(~0));

	if (p_fs->used_clusters != (UINT32) ~0)
		p_fs->used_clusters -= num_clusters;
} /* end of fat_free_cluster */

void exfat_free_cluster(struct super_block *sb, CHAIN_T *p_chain, INT32 do_relse)
{
	INT32 num_clusters = 0;
	UINT32 clu;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	INT32 i;
	UINT32 sector;

	if ((p_chain->dir == CLUSTER_32(0)) || (p_chain->dir == CLUSTER_32(~0)))
		return;

	if (p_chain->size <= 0) {
		printk(KERN_ERR "[EXFAT] free_cluster : skip free-req clu:%u, "
				"because of zero-size truncation\n"
				,p_chain->dir);
		return;
	}

	__set_sb_dirty(sb);
	clu = p_chain->dir;

	if (p_chain->flags == 0x03) {
		do {
			if (do_relse) {
				sector = START_SECTOR(clu);
				for (i = 0; i < p_fs->sectors_per_clu; i++) {
					buf_release(sb, sector+i);
				}
			}

			if (clr_alloc_bitmap(sb, clu-2) != FFS_SUCCESS)
				break;
			clu++;

			num_clusters++;
		} while (num_clusters < p_chain->size);
	} else {
		do {
			if (p_fs->dev_ejected)
				break;

			if (do_relse) {
				sector = START_SECTOR(clu);
				for (i = 0; i < p_fs->sectors_per_clu; i++) {
					buf_release(sb, sector+i);
				}
			}

			if (clr_alloc_bitmap(sb, clu-2) != FFS_SUCCESS)
				break;

			if (FAT_read(sb, clu, &clu) == -1)
				break;
			num_clusters++;
		} while ((clu != CLUSTER_32(0)) && (clu != CLUSTER_32(~0)));
	}

	if (p_fs->used_clusters != (UINT32) ~0)
		p_fs->used_clusters -= num_clusters;
} /* end of exfat_free_cluster */

UINT32 find_last_cluster(struct super_block *sb, CHAIN_T *p_chain)
{
	UINT32 clu, next;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	clu = p_chain->dir;

	if (p_chain->flags == 0x03) {
		clu += p_chain->size - 1;
	} else {
		while((FAT_read(sb, clu, &next) == 0) && (next != CLUSTER_32(~0))) {
			if (p_fs->dev_ejected)
				break;
			clu = next;
		}
	}

	return(clu);
} /* end of find_last_cluster */

INT32 count_num_clusters(struct super_block *sb, CHAIN_T *p_chain)
{
	INT32 i, count = 0;
	UINT32 clu;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if ((p_chain->dir == CLUSTER_32(0)) || (p_chain->dir == CLUSTER_32(~0)))
		return 0;

	clu = p_chain->dir;

	if (p_chain->flags == 0x03) {
		count = p_chain->size;
	} else {
		for (i = 2; i < p_fs->num_clusters; i++) {
			count++;
			if (FAT_read(sb, clu, &clu) != 0)
				return 0;
			if (clu == CLUSTER_32(~0))
				break;
		}
	}

	return(count);
} /* end of count_num_clusters */

INT32 fat_count_used_clusters(struct super_block *sb)
{
	INT32 i, count = 0;
	UINT32 clu;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	for (i = 2; i < p_fs->num_clusters; i++) {
		if (FAT_read(sb, i, &clu) != 0)
			break;
		if (clu != CLUSTER_32(0))
			count++;
	}

	return(count);
} /* end of fat_count_used_clusters */

INT32 exfat_count_used_clusters(struct super_block *sb)
{
	INT32 i, map_i, map_b, count = 0;
	UINT8 k;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	map_i = map_b = 0;

	for (i = 2; i < p_fs->num_clusters; i += 8) {
		k = *(((UINT8 *) p_fs->vol_amap[map_i]->b_data) + map_b);
		count += used_bit[k];

		if ((++map_b) >= p_bd->sector_size) {
			map_i++;
			map_b = 0;
		}
	}

	return(count);
} /* end of exfat_count_used_clusters */

void exfat_chain_cont_cluster(struct super_block *sb, UINT32 chain, INT32 len)
{
	if (len == 0)
		return;

	while (len > 1) {
		FAT_write(sb, chain, chain+1);
		chain++;
		len--;
	}
	FAT_write(sb, chain, CLUSTER_32(~0));
} /* end of exfat_chain_cont_cluster */

/*
 *  Allocation Bitmap Management Functions
 */

INT32 load_alloc_bitmap(struct super_block *sb)
{
	INT32 i, j, ret;
	UINT32 map_size;
	UINT32 type, sector;
	CHAIN_T clu;
	BMAP_DENTRY_T *ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	clu.dir = p_fs->root_dir;
	clu.flags = 0x01;

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		for (i = 0; i < p_fs->dentries_per_clu; i++) {
			ep = (BMAP_DENTRY_T *) get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return FFS_MEDIAERR;

			type = p_fs->fs_func->get_entry_type((DENTRY_T *) ep);

			if (type == TYPE_UNUSED)
				break;
			if (type != TYPE_BITMAP)
				continue;

			if (ep->flags == 0x0) {
				p_fs->map_clu  = GET32_A(ep->start_clu);
				map_size = (UINT32) GET64_A(ep->size);

				p_fs->map_sectors = ((map_size-1) >> p_bd->sector_size_bits) + 1;

				p_fs->vol_amap = (struct buffer_head **) MALLOC(sizeof(struct buffer_head *) * p_fs->map_sectors);
				if (p_fs->vol_amap == NULL)
					return FFS_MEMORYERR;

				sector = START_SECTOR(p_fs->map_clu);

				for (j = 0; j < p_fs->map_sectors; j++) {
					p_fs->vol_amap[j] = NULL;
					ret = sector_read(sb, sector+j, &(p_fs->vol_amap[j]), 1);
					if (ret != FFS_SUCCESS) {
						/*  release all buffers and free vol_amap */
						i=0;
						while (i < j)
							brelse(p_fs->vol_amap[i++]);

						FREE(p_fs->vol_amap);
						p_fs->vol_amap = NULL;
						return ret;
					}
				}

				p_fs->pbr_bh = NULL;
				return FFS_SUCCESS;
			}
		}

		if (FAT_read(sb, clu.dir, &(clu.dir)) != 0)
			return FFS_MEDIAERR;
	}

	return FFS_FORMATERR;
} /* end of load_alloc_bitmap */

void free_alloc_bitmap(struct super_block *sb)
{
	INT32 i;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	brelse(p_fs->pbr_bh);

	for (i = 0; i < p_fs->map_sectors; i++) {
		__brelse(p_fs->vol_amap[i]);
	}

	FREE(p_fs->vol_amap);
	p_fs->vol_amap = NULL;
} /* end of free_alloc_bitmap */

INT32 set_alloc_bitmap(struct super_block *sb, UINT32 clu)
{
	INT32 i, b;
	UINT32 sector;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	i = clu >> (p_bd->sector_size_bits + 3);
	b = clu & ((p_bd->sector_size << 3) - 1);

	sector = START_SECTOR(p_fs->map_clu) + i;

	Bitmap_set((UINT8 *) p_fs->vol_amap[i]->b_data, b);

	return (sector_write(sb, sector, p_fs->vol_amap[i], 0));
} /* end of set_alloc_bitmap */

INT32 clr_alloc_bitmap(struct super_block *sb, UINT32 clu)
{
	INT32 i, b;
	UINT32 sector;
#if EXFAT_CONFIG_DISCARD
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_mount_options *opts = &sbi->options;
	int ret;
#endif /* EXFAT_CONFIG_DISCARD */
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	i = clu >> (p_bd->sector_size_bits + 3);
	b = clu & ((p_bd->sector_size << 3) - 1);

	sector = START_SECTOR(p_fs->map_clu) + i;

	Bitmap_clear((UINT8 *) p_fs->vol_amap[i]->b_data, b);

	return (sector_write(sb, sector, p_fs->vol_amap[i], 0));

#if EXFAT_CONFIG_DISCARD
	if (opts->discard) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
		ret = sb_issue_discard(sb, START_SECTOR(clu), (1 << p_fs->sectors_per_clu_bits));
#else
		ret = sb_issue_discard(sb, START_SECTOR(clu), (1 << p_fs->sectors_per_clu_bits), GFP_NOFS, 0);
#endif
		if (ret == -EOPNOTSUPP) {
			printk(KERN_WARNING "discard not supported by device, disabling");
			opts->discard = 0;
		}
	}
#endif /* EXFAT_CONFIG_DISCARD */
} /* end of clr_alloc_bitmap */

UINT32 test_alloc_bitmap(struct super_block *sb, UINT32 clu)
{
	INT32 i, map_i, map_b;
	UINT32 clu_base, clu_free;
	UINT8 k, clu_mask;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	clu_base = (clu & ~(0x7)) + 2;
	clu_mask = (1 << (clu - clu_base + 2)) - 1;

	map_i = clu >> (p_bd->sector_size_bits + 3);
	map_b = (clu >> 3) & p_bd->sector_size_mask;

	for (i = 2; i < p_fs->num_clusters; i += 8) {
		k = *(((UINT8 *) p_fs->vol_amap[map_i]->b_data) + map_b);
		if (clu_mask > 0) {
			k |= clu_mask;
			clu_mask = 0;
		}
		if (k < 0xFF) {
			clu_free = clu_base + free_bit[k];
			if (clu_free < p_fs->num_clusters)
				return(clu_free);
		}
		clu_base += 8;

		if (((++map_b) >= p_bd->sector_size) || (clu_base >= p_fs->num_clusters)) {
			if ((++map_i) >= p_fs->map_sectors) {
				clu_base = 2;
				map_i = 0;
			}
			map_b = 0;
		}
	}

	return(CLUSTER_32(~0));
} /* end of test_alloc_bitmap */

void sync_alloc_bitmap(struct super_block *sb)
{
	INT32 i;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_fs->vol_amap == NULL)
		return;

	for (i = 0; i < p_fs->map_sectors; i++) {
		sync_dirty_buffer(p_fs->vol_amap[i]);
	}
} /* end of sync_alloc_bitmap */

/*
 *  Upcase table Management Functions
 */
INT32 __load_upcase_table(struct super_block *sb, UINT32 sector, UINT32 num_sectors, UINT32 utbl_checksum)
{
	INT32 i, ret = FFS_ERROR;
	UINT32 j;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);
	struct buffer_head *tmp_bh = NULL;

	UINT8	skip = FALSE;
	UINT32	index = 0;
	UINT16	uni = 0;
	UINT16 **upcase_table;

	UINT32 checksum = 0;

	upcase_table = p_fs->vol_utbl = (UINT16 **) MALLOC(UTBL_COL_COUNT * sizeof(UINT16 *));
	if(upcase_table == NULL)
		return FFS_MEMORYERR;
	MEMSET(upcase_table, 0, UTBL_COL_COUNT * sizeof(UINT16 *));

	num_sectors += sector;

	while(sector < num_sectors) {
		ret = sector_read(sb, sector, &tmp_bh, 1);
		if (ret != FFS_SUCCESS) {
			PRINTK("sector read (0x%X)fail\n", sector);
			goto error;
		}
		sector++;

		for(i = 0; i < p_bd->sector_size && index <= 0xFFFF; i += 2) {
			uni = GET16(((UINT8 *) tmp_bh->b_data)+i);

			checksum = ((checksum & 1) ? 0x80000000 : 0 ) + (checksum >> 1) + *(((UINT8 *) tmp_bh->b_data)+i);
			checksum = ((checksum & 1) ? 0x80000000 : 0 ) + (checksum >> 1) + *(((UINT8 *) tmp_bh->b_data)+(i+1));

			if(skip) {
				PRINTK("skip from 0x%X ", index);
				index += uni;
				PRINTK("to 0x%X (amount of 0x%X)\n", index, uni);
				skip = FALSE;
			} else if(uni == index)
				index++;
			else if(uni == 0xFFFF)
				skip = TRUE;
			else { /* uni != index , uni != 0xFFFF */
				UINT16 col_index = get_col_index(index);

				if(upcase_table[col_index]== NULL) {
					PRINTK("alloc = 0x%X\n", col_index);
					upcase_table[col_index] = (UINT16 *) MALLOC(UTBL_ROW_COUNT * sizeof(UINT16));
					if(upcase_table[col_index] == NULL) {
						ret = FFS_MEMORYERR;
						goto error;
					}

					for(j = 0 ; j < UTBL_ROW_COUNT  ; j++)
						upcase_table[col_index][j] = (col_index << LOW_INDEX_BIT) | j;
				}

				upcase_table[col_index][get_row_index(index)] = uni;
				index++;
			}
		}
	}
	if(index >= 0xFFFF && utbl_checksum == checksum) {
		if(tmp_bh)
			brelse(tmp_bh);
		return FFS_SUCCESS;
	}
	ret = FFS_ERROR;
error:
	if(tmp_bh)
		brelse(tmp_bh);
	free_upcase_table(sb);
	return ret;
}

INT32 __load_default_upcase_table(struct super_block *sb)
{
	INT32 i, ret = FFS_ERROR;
	UINT32 j;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	UINT8	skip = FALSE;
	UINT32	index = 0;
	UINT16	uni = 0;
	UINT16 **upcase_table;

	upcase_table = p_fs->vol_utbl = (UINT16 **) MALLOC(UTBL_COL_COUNT * sizeof(UINT16 *));
	if(upcase_table == NULL)
		return FFS_MEMORYERR;
	MEMSET(upcase_table, 0, UTBL_COL_COUNT * sizeof(UINT16 *));

	for(i = 0; index <= 0xFFFF && i < NUM_UPCASE*2; i += 2) {
		uni = GET16(uni_upcase + i);
		if(skip) {
			PRINTK("skip from 0x%X ", index);
			index += uni;
			PRINTK("to 0x%X (amount of 0x%X)\n", index, uni);
			skip = FALSE;
		} else if(uni == index)
			index++;
		else if(uni == 0xFFFF)
			skip = TRUE;
		else { /* uni != index , uni != 0xFFFF */
			UINT16 col_index = get_col_index(index);

			if(upcase_table[col_index]== NULL) {
				PRINTK("alloc = 0x%X\n", col_index);
				upcase_table[col_index] = (UINT16 *) MALLOC(UTBL_ROW_COUNT * sizeof(UINT16));
				if(upcase_table[col_index] == NULL) {
					ret = FFS_MEMORYERR;
					goto error;
				}

				for(j = 0 ; j < UTBL_ROW_COUNT  ; j++)
					upcase_table[col_index][j] = (col_index << LOW_INDEX_BIT) | j;
			}

			upcase_table[col_index][get_row_index(index)] = uni;
			index ++;
		}
	}

	if(index >= 0xFFFF)
		return FFS_SUCCESS;

error:
	/* FATAL error: default upcase table has error */
	free_upcase_table(sb);
	return ret;
}

INT32 load_upcase_table(struct super_block *sb)
{
	INT32 i;
	UINT32 tbl_clu, tbl_size;
	UINT32 type, sector, num_sectors;
	CHAIN_T clu;
	CASE_DENTRY_T *ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	clu.dir = p_fs->root_dir;
	clu.flags = 0x01;

	if (p_fs->dev_ejected)
		return FFS_MEDIAERR;

	while (clu.dir != CLUSTER_32(~0)) {
		for (i = 0; i < p_fs->dentries_per_clu; i++) {
			ep = (CASE_DENTRY_T *) get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return FFS_MEDIAERR;

			type = p_fs->fs_func->get_entry_type((DENTRY_T *) ep);

			if (type == TYPE_UNUSED)
				break;
			if (type != TYPE_UPCASE)
				continue;

			tbl_clu  = GET32_A(ep->start_clu);
			tbl_size = (UINT32) GET64_A(ep->size);

			sector = START_SECTOR(tbl_clu);
			num_sectors = ((tbl_size-1) >> p_bd->sector_size_bits) + 1;
			if(__load_upcase_table(sb, sector, num_sectors, GET32_A(ep->checksum)) != FFS_SUCCESS)
				break;
			else
				return FFS_SUCCESS;
		}
		if (FAT_read(sb, clu.dir, &(clu.dir)) != 0)
			return FFS_MEDIAERR;
	}
	/* load default upcase table */
	return __load_default_upcase_table(sb);
} /* end of load_upcase_table */

void free_upcase_table(struct super_block *sb)
{
	UINT32 i;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	UINT16 **upcase_table;

	upcase_table = p_fs->vol_utbl;
	for(i = 0 ; i < UTBL_COL_COUNT ; i ++)
		FREE(upcase_table[i]);

	FREE(p_fs->vol_utbl);

	p_fs->vol_utbl = NULL;
} /* end of free_upcase_table */

/*
 *  Directory Entry Management Functions
 */

UINT32 fat_get_entry_type(DENTRY_T *p_entry)
{
	DOS_DENTRY_T *ep = (DOS_DENTRY_T *) p_entry;

	if (*(ep->name) == 0x0)
		return TYPE_UNUSED;

	else if (*(ep->name) == 0xE5)
		return TYPE_DELETED;

	else if (ep->attr == ATTR_EXTEND)
		return TYPE_EXTEND;

	else if ((ep->attr & (ATTR_SUBDIR|ATTR_VOLUME)) == ATTR_VOLUME)
		return TYPE_VOLUME;

	else if ((ep->attr & (ATTR_SUBDIR|ATTR_VOLUME)) == ATTR_SUBDIR)
		return TYPE_DIR;

	return TYPE_FILE;
} /* end of fat_get_entry_type */

UINT32 exfat_get_entry_type(DENTRY_T *p_entry)
{
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;

	if (ep->type == 0x0) {
		return TYPE_UNUSED;
	} else if (ep->type < 0x80) {
		return TYPE_DELETED;
	} else if (ep->type == 0x80) {
		return TYPE_INVALID;
	} else if (ep->type < 0xA0) {
		if (ep->type == 0x81) {
			return TYPE_BITMAP;
		} else if (ep->type == 0x82) {
			return TYPE_UPCASE;
		} else if (ep->type == 0x83) {
			return TYPE_VOLUME;
		} else if (ep->type == 0x85) {
			if (GET16_A(ep->attr) & ATTR_SUBDIR)
				return TYPE_DIR;
			else
				return TYPE_FILE;
		}
		return TYPE_CRITICAL_PRI;
	} else if (ep->type < 0xC0) {
		if (ep->type == 0xA0) {
			return TYPE_GUID;
		} else if (ep->type == 0xA1) {
			return TYPE_PADDING;
		} else if (ep->type == 0xA2) {
			return TYPE_ACLTAB;
		}
		return TYPE_BENIGN_PRI;
	} else if (ep->type < 0xE0) {
		if (ep->type == 0xC0) {
			return TYPE_STREAM;
		} else if (ep->type == 0xC1) {
			return TYPE_EXTEND;
		} else if (ep->type == 0xC2) {
			return TYPE_ACL;
		}
		return TYPE_CRITICAL_SEC;
	}

	return TYPE_BENIGN_SEC;
} /* end of exfat_get_entry_type */

void fat_set_entry_type(DENTRY_T *p_entry, UINT32 type)
{
	DOS_DENTRY_T *ep = (DOS_DENTRY_T *) p_entry;

	if (type == TYPE_UNUSED)
		*(ep->name) = 0x0;

	else if (type == TYPE_DELETED)
		*(ep->name) = 0xE5;

	else if (type == TYPE_EXTEND)
		ep->attr = ATTR_EXTEND;

	else if (type == TYPE_DIR)
		ep->attr = ATTR_SUBDIR;

	else if (type == TYPE_FILE)
		ep->attr = ATTR_ARCHIVE;

	else if (type == TYPE_SYMLINK)
		ep->attr = ATTR_ARCHIVE | ATTR_SYMLINK;
} /* end of fat_set_entry_type */

void exfat_set_entry_type(DENTRY_T *p_entry, UINT32 type)
{
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;

	if (type == TYPE_UNUSED) {
		ep->type = 0x0;
	} else if (type == TYPE_DELETED) {
		ep->type &= ~0x80;
	} else if (type == TYPE_STREAM) {
		ep->type = 0xC0;
	} else if (type == TYPE_EXTEND) {
		ep->type = 0xC1;
	} else if (type == TYPE_BITMAP) {
		ep->type = 0x81;
	} else if (type == TYPE_UPCASE) {
		ep->type = 0x82;
	} else if (type == TYPE_VOLUME) {
		ep->type = 0x83;
	} else if (type == TYPE_DIR) {
		ep->type = 0x85;
		SET16_A(ep->attr, ATTR_SUBDIR);
	} else if (type == TYPE_FILE) {
		ep->type = 0x85;
		SET16_A(ep->attr, ATTR_ARCHIVE);
	} else if (type == TYPE_SYMLINK) {
		ep->type = 0x85;
		SET16_A(ep->attr, ATTR_ARCHIVE | ATTR_SYMLINK);
	}
} /* end of exfat_set_entry_type */

UINT32 fat_get_entry_attr(DENTRY_T *p_entry)
{
	DOS_DENTRY_T *ep = (DOS_DENTRY_T *) p_entry;
	return((UINT32) ep->attr);
} /* end of fat_get_entry_attr */

UINT32 exfat_get_entry_attr(DENTRY_T *p_entry)
{
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;
	return((UINT32) GET16_A(ep->attr));
} /* end of exfat_get_entry_attr */

void fat_set_entry_attr(DENTRY_T *p_entry, UINT32 attr)
{
	DOS_DENTRY_T *ep = (DOS_DENTRY_T *) p_entry;
	ep->attr = (UINT8) attr;
} /* end of fat_set_entry_attr */

void exfat_set_entry_attr(DENTRY_T *p_entry, UINT32 attr)
{
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;
	SET16_A(ep->attr, (UINT16) attr);
} /* end of exfat_set_entry_attr */

UINT8 fat_get_entry_flag(DENTRY_T *p_entry)
{
	return 0x01;
} /* end of fat_get_entry_flag */

UINT8 exfat_get_entry_flag(DENTRY_T *p_entry)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	return(ep->flags);
} /* end of exfat_get_entry_flag */

void fat_set_entry_flag(DENTRY_T *p_entry, UINT8 flags)
{
} /* end of fat_set_entry_flag */

void exfat_set_entry_flag(DENTRY_T *p_entry, UINT8 flags)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	ep->flags = flags;
} /* end of exfat_set_entry_flag */

UINT32 fat_get_entry_clu0(DENTRY_T *p_entry)
{
	DOS_DENTRY_T *ep = (DOS_DENTRY_T *) p_entry;
	return((GET32_A(ep->start_clu_hi) << 16) | GET16_A(ep->start_clu_lo));
} /* end of fat_get_entry_clu0 */

UINT32 exfat_get_entry_clu0(DENTRY_T *p_entry)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	return(GET32_A(ep->start_clu));
} /* end of exfat_get_entry_clu0 */

void fat_set_entry_clu0(DENTRY_T *p_entry, UINT32 start_clu)
{
	DOS_DENTRY_T *ep = (DOS_DENTRY_T *) p_entry;
	SET16_A(ep->start_clu_lo, CLUSTER_16(start_clu));
	SET16_A(ep->start_clu_hi, CLUSTER_16(start_clu >> 16));
} /* end of fat_set_entry_clu0 */

void exfat_set_entry_clu0(DENTRY_T *p_entry, UINT32 start_clu)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	SET32_A(ep->start_clu, start_clu);
} /* end of exfat_set_entry_clu0 */

UINT64 fat_get_entry_size(DENTRY_T *p_entry)
{
	DOS_DENTRY_T *ep = (DOS_DENTRY_T *) p_entry;
	return((UINT64) GET32_A(ep->size));
} /* end of fat_get_entry_size */

UINT64 exfat_get_entry_size(DENTRY_T *p_entry)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	return(GET64_A(ep->valid_size));
} /* end of exfat_get_entry_size */

void fat_set_entry_size(DENTRY_T *p_entry, UINT64 size)
{
	DOS_DENTRY_T *ep = (DOS_DENTRY_T *) p_entry;
	SET32_A(ep->size, (UINT32) size);
} /* end of fat_set_entry_size */

void exfat_set_entry_size(DENTRY_T *p_entry, UINT64 size)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	SET64_A(ep->valid_size, size);
	SET64_A(ep->size, size);
} /* end of exfat_set_entry_size */

void fat_get_entry_time(DENTRY_T *p_entry, TIMESTAMP_T *tp, UINT8 mode)
{
	UINT16 t = 0x00, d = 0x21;
	DOS_DENTRY_T *ep = (DOS_DENTRY_T *) p_entry;

	switch (mode) {
	case TM_CREATE:
		t = GET16_A(ep->create_time);
		d = GET16_A(ep->create_date);
		break;
	case TM_MODIFY:
		t = GET16_A(ep->modify_time);
		d = GET16_A(ep->modify_date);
		break;
	}

	tp->sec  = (t & 0x001F) << 1;
	tp->min  = (t >> 5) & 0x003F;
	tp->hour = (t >> 11);
	tp->day  = (d & 0x001F);
	tp->mon  = (d >> 5) & 0x000F;
	tp->year = (d >> 9);
} /* end of fat_get_entry_time */

void exfat_get_entry_time(DENTRY_T *p_entry, TIMESTAMP_T *tp, UINT8 mode)
{
	UINT16 t = 0x00, d = 0x21;
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;

	switch (mode) {
	case TM_CREATE:
		t = GET16_A(ep->create_time);
		d = GET16_A(ep->create_date);
		break;
	case TM_MODIFY:
		t = GET16_A(ep->modify_time);
		d = GET16_A(ep->modify_date);
		break;
	case TM_ACCESS:
		t = GET16_A(ep->access_time);
		d = GET16_A(ep->access_date);
		break;
	}

	tp->sec  = (t & 0x001F) << 1;
	tp->min  = (t >> 5) & 0x003F;
	tp->hour = (t >> 11);
	tp->day  = (d & 0x001F);
	tp->mon  = (d >> 5) & 0x000F;
	tp->year = (d >> 9);
} /* end of exfat_get_entry_time */

void fat_set_entry_time(DENTRY_T *p_entry, TIMESTAMP_T *tp, UINT8 mode)
{
	UINT16 t, d;
	DOS_DENTRY_T *ep = (DOS_DENTRY_T *) p_entry;

	t = (tp->hour << 11) | (tp->min << 5) | (tp->sec >> 1);
	d = (tp->year <<  9) | (tp->mon << 5) |  tp->day;

	switch (mode) {
	case TM_CREATE:
		SET16_A(ep->create_time, t);
		SET16_A(ep->create_date, d);
		break;
	case TM_MODIFY:
		SET16_A(ep->modify_time, t);
		SET16_A(ep->modify_date, d);
		break;
	}
} /* end of fat_set_entry_time */

void exfat_set_entry_time(DENTRY_T *p_entry, TIMESTAMP_T *tp, UINT8 mode)
{
	UINT16 t, d;
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;

	t = (tp->hour << 11) | (tp->min << 5) | (tp->sec >> 1);
	d = (tp->year <<  9) | (tp->mon << 5) |  tp->day;

	switch (mode) {
	case TM_CREATE:
		SET16_A(ep->create_time, t);
		SET16_A(ep->create_date, d);
		break;
	case TM_MODIFY:
		SET16_A(ep->modify_time, t);
		SET16_A(ep->modify_date, d);
		break;
	case TM_ACCESS:
		SET16_A(ep->access_time, t);
		SET16_A(ep->access_date, d);
		break;
	}
} /* end of exfat_set_entry_time */

INT32 fat_init_dir_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT32 type,
						 UINT32 start_clu, UINT64 size)
{
	UINT32 sector;
	DOS_DENTRY_T *dos_ep;

	dos_ep = (DOS_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry, &sector);
	if (!dos_ep)
		return FFS_MEDIAERR;

	init_dos_entry(dos_ep, type, start_clu);
	buf_modify(sb, sector);

	return FFS_SUCCESS;
} /* end of fat_init_dir_entry */

INT32 exfat_init_dir_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT32 type,
						   UINT32 start_clu, UINT64 size)
{
	UINT32 sector;
	UINT8 flags;
	FILE_DENTRY_T *file_ep;
	STRM_DENTRY_T *strm_ep;

	flags = (type == TYPE_FILE) ? 0x01 : 0x03;

	/* we cannot use get_entry_set_in_dir here because file ep is not initialized yet */
	file_ep = (FILE_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry, &sector);
	if (!file_ep)
		return FFS_MEDIAERR;

	strm_ep = (STRM_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry+1, &sector);
	if (!strm_ep)
		return FFS_MEDIAERR;

	init_file_entry(file_ep, type);
	buf_modify(sb, sector);

	init_strm_entry(strm_ep, flags, start_clu, size);
	buf_modify(sb, sector);

	return FFS_SUCCESS;
} /* end of exfat_init_dir_entry */

INT32 fat_init_ext_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, INT32 num_entries,
						 UNI_NAME_T *p_uniname, DOS_NAME_T *p_dosname)
{
	INT32 i;
	UINT32 sector;
	UINT8 chksum;
	UINT16 *uniname = p_uniname->name;
	DOS_DENTRY_T *dos_ep;
	EXT_DENTRY_T *ext_ep;

	dos_ep = (DOS_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry, &sector);
	if (!dos_ep)
		return FFS_MEDIAERR;

	dos_ep->lcase = p_dosname->name_case;
	MEMCPY(dos_ep->name, p_dosname->name, DOS_NAME_LENGTH);
	buf_modify(sb, sector);

	if ((--num_entries) > 0) {
		chksum = calc_checksum_1byte((void *) dos_ep->name, DOS_NAME_LENGTH, 0);

		for (i = 1; i < num_entries; i++) {
			ext_ep = (EXT_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry-i, &sector);
			if (!ext_ep)
				return FFS_MEDIAERR;

			init_ext_entry(ext_ep, i, chksum, uniname);
			buf_modify(sb, sector);
			uniname += 13;
		}

		ext_ep = (EXT_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry-i, &sector);
		if (!ext_ep)
			return FFS_MEDIAERR;

		init_ext_entry(ext_ep, i+0x40, chksum, uniname);
		buf_modify(sb, sector);
	}

	return FFS_SUCCESS;
} /* end of fat_init_ext_entry */

INT32 exfat_init_ext_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, INT32 num_entries,
						   UNI_NAME_T *p_uniname, DOS_NAME_T *p_dosname)
{
	INT32 i;
	UINT32 sector;
	UINT16 *uniname = p_uniname->name;
	FILE_DENTRY_T *file_ep;
	STRM_DENTRY_T *strm_ep;
	NAME_DENTRY_T *name_ep;

	file_ep = (FILE_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry, &sector);
	if (!file_ep)
		return FFS_MEDIAERR;

	file_ep->num_ext = (UINT8)(num_entries - 1);
	buf_modify(sb, sector);

	strm_ep = (STRM_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry+1, &sector);
	if (!strm_ep)
		return FFS_MEDIAERR;

	strm_ep->name_len = p_uniname->name_len;
	SET16_A(strm_ep->name_hash, p_uniname->name_hash);
	buf_modify(sb, sector);

	for (i = 2; i < num_entries; i++) {
		name_ep = (NAME_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry+i, &sector);
		if (!name_ep)
			return FFS_MEDIAERR;

		init_name_entry(name_ep, uniname);
		buf_modify(sb, sector);
		uniname += 15;
	}

	update_dir_checksum(sb, p_dir, entry);

	return FFS_SUCCESS;
} /* end of exfat_init_ext_entry */

void init_dos_entry(DOS_DENTRY_T *ep, UINT32 type, UINT32 start_clu)
{
	TIMESTAMP_T tm, *tp;

	fat_set_entry_type((DENTRY_T *) ep, type);
	SET16_A(ep->start_clu_lo, CLUSTER_16(start_clu));
	SET16_A(ep->start_clu_hi, CLUSTER_16(start_clu >> 16));
	SET32_A(ep->size, 0);

	tp = tm_current(&tm);
	fat_set_entry_time((DENTRY_T *) ep, tp, TM_CREATE);
	fat_set_entry_time((DENTRY_T *) ep, tp, TM_MODIFY);
	SET16_A(ep->access_date, 0);
	ep->create_time_ms = 0;
} /* end of init_dos_entry */

void init_ext_entry(EXT_DENTRY_T *ep, INT32 order, UINT8 chksum, UINT16 *uniname)
{
	INT32 i;
	UINT8 end = FALSE;

	fat_set_entry_type((DENTRY_T *) ep, TYPE_EXTEND);
	ep->order = (UINT8) order;
	ep->sysid = 0;
	ep->checksum = chksum;
	SET16_A(ep->start_clu, 0);

	for (i = 0; i < 10; i += 2) {
		if (!end) {
			SET16(ep->unicode_0_4+i, *uniname);
			if (*uniname == 0x0)
				end = TRUE;
			else
				uniname++;
		} else {
			SET16(ep->unicode_0_4+i, 0xFFFF);
		}
	}

	for (i = 0; i < 12; i += 2) {
		if (!end) {
			SET16_A(ep->unicode_5_10+i, *uniname);
			if (*uniname == 0x0)
				end = TRUE;
			else
				uniname++;
		} else {
			SET16_A(ep->unicode_5_10+i, 0xFFFF);
		}
	}

	for (i = 0; i < 4; i += 2) {
		if (!end) {
			SET16_A(ep->unicode_11_12+i, *uniname);
			if (*uniname == 0x0)
				end = TRUE;
			else
				uniname++;
		} else {
			SET16_A(ep->unicode_11_12+i, 0xFFFF);
		}
	}
} /* end of init_ext_entry */

void init_file_entry(FILE_DENTRY_T *ep, UINT32 type)
{
	TIMESTAMP_T tm, *tp;

	exfat_set_entry_type((DENTRY_T *) ep, type);

	tp = tm_current(&tm);
	exfat_set_entry_time((DENTRY_T *) ep, tp, TM_CREATE);
	exfat_set_entry_time((DENTRY_T *) ep, tp, TM_MODIFY);
	exfat_set_entry_time((DENTRY_T *) ep, tp, TM_ACCESS);
	ep->create_time_ms = 0;
	ep->modify_time_ms = 0;
	ep->access_time_ms = 0;
} /* end of init_file_entry */

void init_strm_entry(STRM_DENTRY_T *ep, UINT8 flags, UINT32 start_clu, UINT64 size)
{
	exfat_set_entry_type((DENTRY_T *) ep, TYPE_STREAM);
	ep->flags = flags;
	SET32_A(ep->start_clu, start_clu);
	SET64_A(ep->valid_size, size);
	SET64_A(ep->size, size);
} /* end of init_strm_entry */

void init_name_entry(NAME_DENTRY_T *ep, UINT16 *uniname)
{
	INT32 i;

	exfat_set_entry_type((DENTRY_T *) ep, TYPE_EXTEND);
	ep->flags = 0x0;

	for (i = 0; i < 30; i++, i++) {
		SET16_A(ep->unicode_0_14+i, *uniname);
		if (*uniname == 0x0)
			break;
		uniname++;
	}
} /* end of init_name_entry */

void fat_delete_dir_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, INT32 order, INT32 num_entries)
{
	INT32 i;
	UINT32 sector;
	DENTRY_T *ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	for (i = num_entries-1; i >= order; i--) {
		ep = get_entry_in_dir(sb, p_dir, entry-i, &sector);
		if (!ep)
			return;

		p_fs->fs_func->set_entry_type(ep, TYPE_DELETED);
		buf_modify(sb, sector);
	}
} /* end of fat_delete_dir_entry */

void exfat_delete_dir_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, INT32 order, INT32 num_entries)
{
	INT32 i;
	UINT32 sector;
	DENTRY_T *ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	for (i = order; i < num_entries; i++) {
		ep = get_entry_in_dir(sb, p_dir, entry+i, &sector);
		if (!ep)
			return;

		p_fs->fs_func->set_entry_type(ep, TYPE_DELETED);
		buf_modify(sb, sector);
	}
} /* end of exfat_delete_dir_entry */

void update_dir_checksum(struct super_block *sb, CHAIN_T *p_dir, INT32 entry)
{
	INT32 i, num_entries;
	UINT32 sector;
	UINT16 chksum;
	FILE_DENTRY_T *file_ep;
	DENTRY_T *ep;

	file_ep = (FILE_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry, &sector);
	if (!file_ep)
		return;

	buf_lock(sb, sector);

	num_entries = (INT32) file_ep->num_ext + 1;
	chksum = calc_checksum_2byte((void *) file_ep, DENTRY_SIZE, 0, CS_DIR_ENTRY);

	for (i = 1; i < num_entries; i++) {
		ep = get_entry_in_dir(sb, p_dir, entry+i, NULL);
		if (!ep) {
			buf_unlock(sb, sector);
			return;
		}

		chksum = calc_checksum_2byte((void *) ep, DENTRY_SIZE, chksum, CS_DEFAULT);
	}

	SET16_A(file_ep->checksum, chksum);
	buf_modify(sb, sector);
	buf_unlock(sb, sector);
} /* end of update_dir_checksum */

void update_dir_checksum_with_entry_set (struct super_block *sb, ENTRY_SET_CACHE_T *es)
{
	DENTRY_T *ep;
	UINT16 chksum = 0;
	INT32 chksum_type = CS_DIR_ENTRY, i;

	ep = (DENTRY_T *)&(es->__buf);
	for (i=0; i < es->num_entries; i++) {
		PRINTK ("update_dir_checksum_with_entry_set ep %p\n", ep);
		chksum = calc_checksum_2byte((void *) ep, DENTRY_SIZE, chksum, chksum_type);
		ep++;
		chksum_type = CS_DEFAULT;
	}

	ep = (DENTRY_T *)&(es->__buf);
	SET16_A(((FILE_DENTRY_T *)ep)->checksum, chksum);
	write_whole_entry_set(sb, es);
}

static INT32 _walk_fat_chain (struct super_block *sb, CHAIN_T *p_dir, INT32 byte_offset, UINT32 *clu)
{
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	INT32 clu_offset;
	UINT32 cur_clu;

	clu_offset = byte_offset >> p_fs->cluster_size_bits;
	cur_clu = p_dir->dir;

	if (p_dir->flags == 0x03) {
		cur_clu += clu_offset;
	} else {
		while (clu_offset > 0) {
			if (FAT_read(sb, cur_clu, &cur_clu) == -1)
				return FFS_MEDIAERR;
			clu_offset--;
		}
	}

	if (clu)
		*clu = cur_clu;
	return FFS_SUCCESS;
}
INT32 find_location(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT32 *sector, INT32 *offset)
{
	INT32 off, ret;
	UINT32 clu=0;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	off = entry << DENTRY_SIZE_BITS;

	if (p_dir->dir == CLUSTER_32(0)) { /* FAT16 root_dir */
		*offset = off & p_bd->sector_size_mask;
		*sector = off >> p_bd->sector_size_bits;
		*sector += p_fs->root_start_sector;
	} else {
		ret =_walk_fat_chain(sb, p_dir, off, &clu);
		if (ret != FFS_SUCCESS)
			return ret;

		off &= p_fs->cluster_size - 1;                  /* byte offset in cluster */

		*offset = off & p_bd->sector_size_mask;  /* byte offset in sector    */
		*sector = off >> p_bd->sector_size_bits; /* sector offset in cluster */
		*sector += START_SECTOR(clu);
	}
	return FFS_SUCCESS;
} /* end of find_location */

DENTRY_T *get_entry_with_sector(struct super_block *sb, UINT32 sector, INT32 offset)
{
	UINT8 *buf;

	buf = buf_getblk(sb, sector);

	if (buf == NULL)
		return NULL;

	return((DENTRY_T *)(buf + offset));
} /* end of get_entry_with_sector */

DENTRY_T *get_entry_in_dir(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT32 *sector)
{
	INT32 off;
	UINT32 sec;
	UINT8 *buf;

	if (find_location(sb, p_dir, entry, &sec, &off) != FFS_SUCCESS)
		return NULL;

	buf = buf_getblk(sb, sec);

	if (buf == NULL)
		return NULL;

	if (sector != NULL)
		*sector = sec;
	return((DENTRY_T *)(buf + off));
} /* end of get_entry_in_dir */


/* returns a set of dentries for a file or dir.
 * Note that this is a copy (dump) of dentries so that user should call write_entry_set()
 * to apply changes made in this entry set to the real device.
 * in:
 *   sb+p_dir+entry: indicates a file/dir
 *   type:  specifies how many dentries should be included.
 * out:
 *   file_ep: will point the first dentry(= file dentry) on success
 * return:
 *   pointer of entry set on success,
 *   NULL on failure.
 */

#define ES_MODE_STARTED										0
#define ES_MODE_GET_FILE_ENTRY						1
#define ES_MODE_GET_STRM_ENTRY						2
#define ES_MODE_GET_NAME_ENTRY						3
#define ES_MODE_GET_CRITICAL_SEC_ENTRY		4
ENTRY_SET_CACHE_T *get_entry_set_in_dir (struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT32 type, DENTRY_T **file_ep)
{
	INT32 off, ret, byte_offset;
	UINT32 clu=0;
	UINT32 sec, entry_type;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);
	ENTRY_SET_CACHE_T *es = NULL;
	DENTRY_T *ep, *pos;
	UINT8 *buf;
	UINT8 num_entries;
	INT32 mode = ES_MODE_STARTED;

	PRINTK("get_entry_set_in_dir entered\n");
	PRINTK("p_dir dir %u flags %x size %d\n", p_dir->dir, p_dir->flags, p_dir->size);

	byte_offset = entry << DENTRY_SIZE_BITS;
	ret =_walk_fat_chain(sb, p_dir, byte_offset, &clu);
	if (ret != FFS_SUCCESS)
		return NULL;


	byte_offset &= p_fs->cluster_size - 1;                  /* byte offset in cluster */

	off = byte_offset & p_bd->sector_size_mask;  /* byte offset in sector    */
	sec = byte_offset >> p_bd->sector_size_bits; /* sector offset in cluster */
	sec += START_SECTOR(clu);

	buf = buf_getblk(sb, sec);
	if (buf == NULL)
		goto err_out;


	ep = (DENTRY_T *)(buf + off);
	entry_type = p_fs->fs_func->get_entry_type(ep);

	if ((entry_type != TYPE_FILE)
		&& (entry_type != TYPE_DIR))
		goto err_out;

	if (type == ES_ALL_ENTRIES)
		num_entries = ((FILE_DENTRY_T *)ep)->num_ext+1;
	else
		num_entries = type;

	PRINTK("trying to malloc %x bytes for %d entries\n", offsetof(ENTRY_SET_CACHE_T, __buf) + (num_entries)  * sizeof(DENTRY_T), num_entries);
	es = MALLOC(offsetof(ENTRY_SET_CACHE_T, __buf) + (num_entries)  * sizeof(DENTRY_T));
	if (es == NULL)
		goto err_out;

	es->num_entries = num_entries;
	es->sector = sec;
	es->offset = off;
	es->alloc_flag = p_dir->flags;

	pos = (DENTRY_T *) &(es->__buf);

	while(num_entries) {
		/* instead of copying whole sector, we will check every entry.
		 * this will provide minimum stablity and consistancy.
		 */

		entry_type = p_fs->fs_func->get_entry_type(ep);

		if ((entry_type == TYPE_UNUSED) || (entry_type == TYPE_DELETED))
			goto err_out;

		switch(mode) {
		case ES_MODE_STARTED:
			if  ((entry_type == TYPE_FILE) || (entry_type == TYPE_DIR))
				mode = ES_MODE_GET_FILE_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_FILE_ENTRY:
			if (entry_type == TYPE_STREAM)
				mode = ES_MODE_GET_STRM_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_STRM_ENTRY:
			if (entry_type == TYPE_EXTEND)
				mode = ES_MODE_GET_NAME_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_NAME_ENTRY:
			if (entry_type == TYPE_EXTEND)
				break;
			else if (entry_type == TYPE_STREAM)
				goto err_out;
			else if (entry_type & TYPE_CRITICAL_SEC)
				mode = ES_MODE_GET_CRITICAL_SEC_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_CRITICAL_SEC_ENTRY:
			if ((entry_type == TYPE_EXTEND) || (entry_type == TYPE_STREAM))
				goto err_out;
			else if ((entry_type & TYPE_CRITICAL_SEC) != TYPE_CRITICAL_SEC)
				goto err_out;
			break;
		}

		COPY_DENTRY(pos, ep);

		if (--num_entries == 0)
			break;

		if (((off + DENTRY_SIZE) & p_bd->sector_size_mask) < (off &  p_bd->sector_size_mask)) {
			/* get the next sector */
			if (IS_LAST_SECTOR_IN_CLUSTER(sec)) {
				if (es->alloc_flag == 0x03) {
					clu++;
				} else {
					if (FAT_read(sb, clu, &clu) == -1)
						goto err_out;
				}
				sec = START_SECTOR(clu);
			} else {
				sec++;
			}
			buf = buf_getblk(sb, sec);
			if (buf == NULL)
				goto err_out;
			off = 0;
			ep = (DENTRY_T *)(buf);
		} else {
			ep++;
			off += DENTRY_SIZE;
		}
		pos++;
	}

	if (file_ep)
		*file_ep = (DENTRY_T *)&(es->__buf);

	PRINTK("es sec %u offset %d flags %d, num_entries %u buf ptr %p\n",
		   es->sector, es->offset, es->alloc_flag, es->num_entries, &(es->__buf));
	PRINTK("get_entry_set_in_dir exited %p\n", es);
	return es;
err_out:
	PRINTK("get_entry_set_in_dir exited NULL (es %p)\n", es);
	FREE(es);
	return NULL;
}

void release_entry_set (ENTRY_SET_CACHE_T *es)
{
	PRINTK("release_entry_set %p\n", es);
	FREE(es);
}


static INT32 __write_partial_entries_in_entry_set (struct super_block *sb, ENTRY_SET_CACHE_T *es, UINT32 sec, INT32 off, UINT32 count)
{
	INT32 num_entries, buf_off = (off - es->offset);
	UINT32 remaining_byte_in_sector, copy_entries;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);
	UINT32 clu;
	UINT8 *buf, *esbuf = (UINT8 *)&(es->__buf);

	PRINTK("__write_partial_entries_in_entry_set entered\n");
	PRINTK("es %p sec %u off %d count %d\n", es, sec, off, count);
	num_entries = count;

	while(num_entries) {
		/* white per sector base */
		remaining_byte_in_sector = (1 << p_bd->sector_size_bits) - off;
		copy_entries = MIN(remaining_byte_in_sector>> DENTRY_SIZE_BITS , num_entries);
		buf = buf_getblk(sb, sec);
		if (buf == NULL)
			goto err_out;
		PRINTK("es->buf %p buf_off %u\n", esbuf, buf_off);
		PRINTK("copying %d entries from %p to sector %u\n", copy_entries, (esbuf + buf_off), sec);
		MEMCPY(buf + off, esbuf + buf_off, copy_entries << DENTRY_SIZE_BITS);
		buf_modify(sb, sec);
		num_entries -= copy_entries;

		if (num_entries) {
			/* get next sector */
			if (IS_LAST_SECTOR_IN_CLUSTER(sec)) {
				clu = GET_CLUSTER_FROM_SECTOR(sec);
				if (es->alloc_flag == 0x03) {
					clu++;
				} else {
					if (FAT_read(sb, clu, &clu) == -1)
						goto err_out;
				}
				sec = START_SECTOR(clu);
			} else {
				sec++;
			}
			off = 0;
			buf_off += copy_entries << DENTRY_SIZE_BITS;
		}
	}

	PRINTK("__write_partial_entries_in_entry_set exited successfully\n");
	return FFS_SUCCESS;
err_out:
	PRINTK("__write_partial_entries_in_entry_set failed\n");
	return FFS_ERROR;
}

/* write back all entries in entry set */
INT32 write_whole_entry_set (struct super_block *sb, ENTRY_SET_CACHE_T *es)
{
	return (__write_partial_entries_in_entry_set(sb, es, es->sector,es->offset, es->num_entries));
}

/* write back some entries in entry set */
INT32 write_partial_entries_in_entry_set (struct super_block *sb, ENTRY_SET_CACHE_T *es, DENTRY_T *ep, UINT32 count)
{
	INT32 ret, byte_offset, off;
	UINT32 clu=0, sec;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);
	CHAIN_T dir;

	/* vaidity check */
	if (ep + count  > ((DENTRY_T *)&(es->__buf)) + es->num_entries)
		return FFS_ERROR;

	dir.dir = GET_CLUSTER_FROM_SECTOR(es->sector);
	dir.flags = es->alloc_flag;
	dir.size = 0xffffffff;		/* XXX */

	byte_offset = (es->sector - START_SECTOR(dir.dir)) << p_bd->sector_size_bits;
	byte_offset += ((INT32 *)ep - (INT32 *)&(es->__buf)) + es->offset;

	ret =_walk_fat_chain(sb, &dir, byte_offset, &clu);
	if (ret != FFS_SUCCESS)
		return ret;
	byte_offset &= p_fs->cluster_size - 1;                  /* byte offset in cluster */
	off = byte_offset & p_bd->sector_size_mask;  /* byte offset in sector    */
	sec = byte_offset >> p_bd->sector_size_bits; /* sector offset in cluster */
	sec += START_SECTOR(clu);
	return (__write_partial_entries_in_entry_set(sb, es, sec, off, count));
}

/* search EMPTY CONTINUOUS "num_entries" entries */
INT32 search_deleted_or_unused_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 num_entries)
{
	INT32 i, dentry, num_empty = 0;
	INT32 dentries_per_clu;
	UINT32 type;
	CHAIN_T clu;
	DENTRY_T *ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;
	else
		dentries_per_clu = p_fs->dentries_per_clu;

	if (p_fs->hint_uentry.dir == p_dir->dir) {
		if (p_fs->hint_uentry.entry == -1)
			return -1;

		clu.dir = p_fs->hint_uentry.clu.dir;
		clu.size = p_fs->hint_uentry.clu.size;
		clu.flags = p_fs->hint_uentry.clu.flags;

		dentry = p_fs->hint_uentry.entry;
	} else {
		p_fs->hint_uentry.entry = -1;

		clu.dir = p_dir->dir;
		clu.size = p_dir->size;
		clu.flags = p_dir->flags;

		dentry = 0;
	}

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
			i = dentry % dentries_per_clu;
		else
			i = dentry & (dentries_per_clu-1);

		for ( ; i < dentries_per_clu; i++, dentry++) {
			ep = get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return -1;

			type = p_fs->fs_func->get_entry_type(ep);

			if (type == TYPE_UNUSED) {
				num_empty++;
				if (p_fs->hint_uentry.entry == -1) {
					p_fs->hint_uentry.dir = p_dir->dir;
					p_fs->hint_uentry.entry = dentry;

					p_fs->hint_uentry.clu.dir = clu.dir;
					p_fs->hint_uentry.clu.size = clu.size;
					p_fs->hint_uentry.clu.flags = clu.flags;
				}
			} else if (type == TYPE_DELETED) {
				num_empty++;
			} else {
				num_empty = 0;
			}

			if (num_empty >= num_entries) {
				p_fs->hint_uentry.dir = CLUSTER_32(~0);
				p_fs->hint_uentry.entry = -1;

				if (p_fs->vol_type == EXFAT)
					return(dentry - (num_entries-1));
				else
					return(dentry);
			}
		}

		if (p_dir->dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (clu.flags == 0x03) {
			if ((--clu.size) > 0)
				clu.dir++;
			else
				clu.dir = CLUSTER_32(~0);
		} else {
			if (FAT_read(sb, clu.dir, &(clu.dir)) != 0)
				return -1;
		}
	}

	return -1;
} /* end of search_deleted_or_unused_entry */

INT32 find_empty_entry(struct inode *inode, CHAIN_T *p_dir, INT32 num_entries)
{
	INT32 ret, dentry;
	UINT32 last_clu, sector;
	UINT64 size = 0;
	CHAIN_T clu;
	DENTRY_T *ep = NULL;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	FILE_ID_T *fid = &(EXFAT_I(inode)->fid);

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		return(search_deleted_or_unused_entry(sb, p_dir, num_entries));

	while ((dentry = search_deleted_or_unused_entry(sb, p_dir, num_entries)) < 0) {
		if (p_fs->dev_ejected)
			break;

		if (p_fs->vol_type == EXFAT) {
			if (p_dir->dir != p_fs->root_dir) {
				size = i_size_read(inode);
			}
		}

		last_clu = find_last_cluster(sb, p_dir);
		clu.dir = last_clu + 1;
		clu.size = 0;
		clu.flags = p_dir->flags;

		/* (1) allocate a cluster */
		ret = p_fs->fs_func->alloc_cluster(sb, 1, &clu);
		if (ret < 1)
			return -1;

		if (clear_cluster(sb, clu.dir) != FFS_SUCCESS)
			return -1;

		/* (2) append to the FAT chain */
		if (clu.flags != p_dir->flags) {
			exfat_chain_cont_cluster(sb, p_dir->dir, p_dir->size);
			p_dir->flags = 0x01;
			p_fs->hint_uentry.clu.flags = 0x01;
		}
		if (clu.flags == 0x01)
			FAT_write(sb, last_clu, clu.dir);

		if (p_fs->hint_uentry.entry == -1) {
			p_fs->hint_uentry.dir = p_dir->dir;
			p_fs->hint_uentry.entry = p_dir->size << (p_fs->cluster_size_bits - DENTRY_SIZE_BITS);

			p_fs->hint_uentry.clu.dir = clu.dir;
			p_fs->hint_uentry.clu.size = 0;
			p_fs->hint_uentry.clu.flags = clu.flags;
		}
		p_fs->hint_uentry.clu.size++;
		p_dir->size++;

		/* (3) update the directory entry */
		if (p_fs->vol_type == EXFAT) {
			if (p_dir->dir != p_fs->root_dir) {
				size += p_fs->cluster_size;

				ep = get_entry_in_dir(sb, &(fid->dir), fid->entry+1, &sector);
				if (!ep)
					return -1;
				p_fs->fs_func->set_entry_size(ep, size);
				p_fs->fs_func->set_entry_flag(ep, p_dir->flags);
				buf_modify(sb, sector);

				update_dir_checksum(sb, &(fid->dir), fid->entry);
			}
		}

		i_size_write(inode, i_size_read(inode)+p_fs->cluster_size);
		EXFAT_I(inode)->mmu_private += p_fs->cluster_size;
		EXFAT_I(inode)->fid.size += p_fs->cluster_size;
		EXFAT_I(inode)->fid.flags = p_dir->flags;
		inode->i_blocks += 1 << (p_fs->cluster_size_bits - 9);
	}

	return(dentry);
} /* end of find_empty_entry */

/* return values of fat_find_dir_entry()
   >= 0 : return dir entiry position with the name in dir
   -1 : (root dir, ".") it is the root dir itself
   -2 : entry with the name does not exist */
INT32 fat_find_dir_entry(struct super_block *sb, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, INT32 num_entries, DOS_NAME_T *p_dosname, UINT32 type)
{
	INT32 i, dentry = 0, len;
	INT32 order = 0, is_feasible_entry = TRUE, has_ext_entry = FALSE;
	INT32 dentries_per_clu;
	UINT32 entry_type;
	UINT16 entry_uniname[14], *uniname = NULL, unichar;
	CHAIN_T clu;
	DENTRY_T *ep;
	DOS_DENTRY_T *dos_ep;
	EXT_DENTRY_T *ext_ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_dir->dir == p_fs->root_dir) {
		if ((!nls_uniname_cmp(sb, p_uniname->name, (UINT16 *) UNI_CUR_DIR_NAME)) ||
			(!nls_uniname_cmp(sb, p_uniname->name, (UINT16 *) UNI_PAR_DIR_NAME)))
			return -1; // special case, root directory itself
	}

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;
	else
		dentries_per_clu = p_fs->dentries_per_clu;

	clu.dir = p_dir->dir;
	clu.flags = p_dir->flags;

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		for (i = 0; i < dentries_per_clu; i++, dentry++) {
			ep = get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return -2;

			entry_type = p_fs->fs_func->get_entry_type(ep);

			if ((entry_type == TYPE_FILE) || (entry_type == TYPE_DIR)) {
				if ((type == TYPE_ALL) || (type == entry_type)) {
					if (is_feasible_entry && has_ext_entry)
						return(dentry);

					dos_ep = (DOS_DENTRY_T *) ep;
					if (!nls_dosname_cmp(sb, p_dosname->name, dos_ep->name))
						return(dentry);
				}
				is_feasible_entry = TRUE;
				has_ext_entry = FALSE;
			} else if (entry_type == TYPE_EXTEND) {
				if (is_feasible_entry) {
					ext_ep = (EXT_DENTRY_T *) ep;
					if (ext_ep->order > 0x40) {
						order = (INT32)(ext_ep->order - 0x40);
						uniname = p_uniname->name + 13 * (order-1);
					} else {
						order = (INT32) ext_ep->order;
						uniname -= 13;
					}

					len = extract_uni_name_from_ext_entry(ext_ep, entry_uniname, order);

					unichar = *(uniname+len);
					*(uniname+len) = 0x0;

					if (nls_uniname_cmp(sb, uniname, entry_uniname)) {
						is_feasible_entry = FALSE;
					}

					*(uniname+len) = unichar;
				}
				has_ext_entry = TRUE;
			} else if (entry_type == TYPE_UNUSED) {
				return -2;
			} else {
				is_feasible_entry = TRUE;
				has_ext_entry = FALSE;
			}
		}

		if (p_dir->dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (FAT_read(sb, clu.dir, &(clu.dir)) != 0)
			return -2;
	}

	return -2;
} /* end of fat_find_dir_entry */

/* return values of exfat_find_dir_entry()
   >= 0 : return dir entiry position with the name in dir
   -1 : (root dir, ".") it is the root dir itself
   -2 : entry with the name does not exist */
INT32 exfat_find_dir_entry(struct super_block *sb, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, INT32 num_entries, DOS_NAME_T *p_dosname, UINT32 type)
{
	INT32 i, dentry = 0, num_ext_entries = 0, len;
	INT32 order = 0, is_feasible_entry = FALSE;
	INT32 dentries_per_clu, num_empty = 0;
	UINT32 entry_type;
	UINT16 entry_uniname[16], *uniname = NULL, unichar;
	CHAIN_T clu;
	DENTRY_T *ep;
	FILE_DENTRY_T *file_ep;
	STRM_DENTRY_T *strm_ep;
	NAME_DENTRY_T *name_ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_dir->dir == p_fs->root_dir) {
		if ((!nls_uniname_cmp(sb, p_uniname->name, (UINT16 *) UNI_CUR_DIR_NAME)) ||
			(!nls_uniname_cmp(sb, p_uniname->name, (UINT16 *) UNI_PAR_DIR_NAME)))
			return -1; // special case, root directory itself
	}

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;
	else
		dentries_per_clu = p_fs->dentries_per_clu;

	clu.dir = p_dir->dir;
	clu.size = p_dir->size;
	clu.flags = p_dir->flags;

	p_fs->hint_uentry.dir = p_dir->dir;
	p_fs->hint_uentry.entry = -1;

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		for (i = 0; i < dentries_per_clu; i++, dentry++) {
			ep = get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return -2;

			entry_type = p_fs->fs_func->get_entry_type(ep);

			if ((entry_type == TYPE_UNUSED) || (entry_type == TYPE_DELETED)) {
				is_feasible_entry = FALSE;

				if (p_fs->hint_uentry.entry == -1) {
					num_empty++;

					if (num_empty == 1) {
						p_fs->hint_uentry.clu.dir = clu.dir;
						p_fs->hint_uentry.clu.size = clu.size;
						p_fs->hint_uentry.clu.flags = clu.flags;
					}
					if ((num_empty >= num_entries) || (entry_type == TYPE_UNUSED)) {
						p_fs->hint_uentry.entry = dentry - (num_empty-1);
					}
				}

				if (entry_type == TYPE_UNUSED) {
					return -2;
				}
			} else {
				num_empty = 0;

				if ((entry_type == TYPE_FILE) || (entry_type == TYPE_DIR)) {
					if ((type == TYPE_ALL) || (type == entry_type)) {
						file_ep = (FILE_DENTRY_T *) ep;
						num_ext_entries = file_ep->num_ext;
						is_feasible_entry = TRUE;
					} else {
						is_feasible_entry = FALSE;
					}
				} else if (entry_type == TYPE_STREAM) {
					if (is_feasible_entry) {
						strm_ep = (STRM_DENTRY_T *) ep;
						if (p_uniname->name_len == strm_ep->name_len) {
							order = 1;
						} else {
							is_feasible_entry = FALSE;
						}
					}
				} else if (entry_type == TYPE_EXTEND) {
					if (is_feasible_entry) {
						name_ep = (NAME_DENTRY_T *) ep;

						if ((++order) == 2)
							uniname = p_uniname->name;
						else
							uniname += 15;

						len = extract_uni_name_from_name_entry(name_ep, entry_uniname, order);

						unichar = *(uniname+len);
						*(uniname+len) = 0x0;

						if (nls_uniname_cmp(sb, uniname, entry_uniname)) {
							is_feasible_entry = FALSE;
						} else if (order == num_ext_entries) {
							p_fs->hint_uentry.dir = CLUSTER_32(~0);
							p_fs->hint_uentry.entry = -1;
							return(dentry - (num_ext_entries));
						}

						*(uniname+len) = unichar;
					}
				} else {
					is_feasible_entry = FALSE;
				}
			}
		}

		if (p_dir->dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (clu.flags == 0x03) {
			if ((--clu.size) > 0)
				clu.dir++;
			else
				clu.dir = CLUSTER_32(~0);
		} else {
			if (FAT_read(sb, clu.dir, &(clu.dir)) != 0)
				return -2;
		}
	}

	return -2;
} /* end of exfat_find_dir_entry */

/* returns -1 on error */
INT32 fat_count_ext_entries(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, DENTRY_T *p_entry)
{
	INT32 count = 0;
	UINT8 chksum;
	DOS_DENTRY_T *dos_ep = (DOS_DENTRY_T *) p_entry;
	EXT_DENTRY_T *ext_ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	chksum = calc_checksum_1byte((void *) dos_ep->name, DOS_NAME_LENGTH, 0);

	for (entry--; entry >= 0; entry--) {
		ext_ep = (EXT_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry, NULL);
		if (!ext_ep)
			return -1;

		if ((p_fs->fs_func->get_entry_type((DENTRY_T *) ext_ep) == TYPE_EXTEND) &&
			(ext_ep->checksum == chksum)) {
			count++;
			if (ext_ep->order > 0x40)
				return(count);
		} else {
			return(count);
		}
	}

	return(count);
} /* end of fat_count_ext_entries */

/* returns -1 on error */
INT32 exfat_count_ext_entries(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, DENTRY_T *p_entry)
{
	INT32 i, count = 0;
	UINT32 type;
	FILE_DENTRY_T *file_ep = (FILE_DENTRY_T *) p_entry;
	DENTRY_T *ext_ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	for (i = 0, entry++; i < file_ep->num_ext; i++, entry++) {
		ext_ep = get_entry_in_dir(sb, p_dir, entry, NULL);
		if (!ext_ep)
			return -1;

		type = p_fs->fs_func->get_entry_type(ext_ep);
		if ((type == TYPE_EXTEND) || (type == TYPE_STREAM)) {
			count++;
		} else {
			return(count);
		}
	}

	return(count);
} /* end of exfat_count_ext_entries */

/* returns -1 on error */
INT32 count_dos_name_entries(struct super_block *sb, CHAIN_T *p_dir, UINT32 type)
{
	INT32 i, count = 0;
	INT32 dentries_per_clu;
	UINT32 entry_type;
	CHAIN_T clu;
	DENTRY_T *ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;
	else
		dentries_per_clu = p_fs->dentries_per_clu;

	clu.dir = p_dir->dir;
	clu.size = p_dir->size;
	clu.flags = p_dir->flags;

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		for (i = 0; i < dentries_per_clu; i++) {
			ep = get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return -1;

			entry_type = p_fs->fs_func->get_entry_type(ep);

			if (entry_type == TYPE_UNUSED)
				return(count);
			if (!(type & TYPE_CRITICAL_PRI) && !(type & TYPE_BENIGN_PRI))
				continue;

			if ((type == TYPE_ALL) || (type == entry_type))
				count++;
		}

		if (p_dir->dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (clu.flags == 0x03) {
			if ((--clu.size) > 0)
				clu.dir++;
			else
				clu.dir = CLUSTER_32(~0);
		} else {
			if (FAT_read(sb, clu.dir, &(clu.dir)) != 0)
				return -1;
		}
	}

	return(count);
} /* end of count_dos_name_entries */

BOOL is_dir_empty(struct super_block *sb, CHAIN_T *p_dir)
{
	INT32 i, count = 0;
	INT32 dentries_per_clu;
	UINT32 type;
	CHAIN_T clu;
	DENTRY_T *ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;
	else
		dentries_per_clu = p_fs->dentries_per_clu;

	clu.dir = p_dir->dir;
	clu.size = p_dir->size;
	clu.flags = p_dir->flags;

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		for (i = 0; i < dentries_per_clu; i++) {
			ep = get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				break;

			type = p_fs->fs_func->get_entry_type(ep);

			if (type == TYPE_UNUSED)
				return TRUE;
			if ((type != TYPE_FILE) && (type != TYPE_DIR))
				continue;

			if (p_dir->dir == CLUSTER_32(0)) { /* FAT16 root_dir */
				return FALSE;
			} else {
				if (p_fs->vol_type == EXFAT)
					return FALSE;
				if ((p_dir->dir == p_fs->root_dir) || ((++count) > 2))
					return FALSE;
			}
		}

		if (p_dir->dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (clu.flags == 0x03) {
			if ((--clu.size) > 0)
				clu.dir++;
			else
				clu.dir = CLUSTER_32(~0);
		} else {
			if (FAT_read(sb, clu.dir, &(clu.dir)) != 0)
				break;
		}
	}

	return TRUE;
} /* end of is_dir_empty */

/*
 *  Name Conversion Functions
 */

/* input  : dir, uni_name
   output : num_of_entry, dos_name(format : aaaaaa~1.bbb) */
INT32 get_num_entries_and_dos_name(struct super_block *sb, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, INT32 *entries, DOS_NAME_T *p_dosname)
{
	INT32 ret, num_entries, lossy = FALSE;
	INT8 **r;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	num_entries = p_fs->fs_func->calc_num_entries(p_uniname);
	if (num_entries == 0)
		return FFS_INVALIDPATH;

	if (p_fs->vol_type != EXFAT) {
		nls_uniname_to_dosname(sb, p_dosname, p_uniname, &lossy);

		if (lossy) {
			ret = fat_generate_dos_name(sb, p_dir, p_dosname);
			if (ret)
				return ret;
		} else {
			for (r = reserved_names; *r; r++) {
				if (!STRNCMP((void *) p_dosname->name, *r, 8))
					return FFS_INVALIDPATH;
			}

			if (p_dosname->name_case != 0xFF)
				num_entries = 1;
		}

		if (num_entries > 1)
			p_dosname->name_case = 0x0;
	}

	*entries = num_entries;

	return FFS_SUCCESS;
} /* end of get_num_entries_and_dos_name */

void get_uni_name_from_dos_entry(struct super_block *sb, DOS_DENTRY_T *ep, UNI_NAME_T *p_uniname, UINT8 mode)
{
	DOS_NAME_T dos_name;

	if (mode == 0x0)
		dos_name.name_case = 0x0;
	else
		dos_name.name_case = ep->lcase;

	MEMCPY(dos_name.name, ep->name, DOS_NAME_LENGTH);
	nls_dosname_to_uniname(sb, p_uniname, &dos_name);
} /* end of get_uni_name_from_dos_entry */

void fat_get_uni_name_from_ext_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT16 *uniname)
{
	INT32 i;
	EXT_DENTRY_T *ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	for (entry--, i = 1; entry >= 0; entry--, i++) {
		ep = (EXT_DENTRY_T *) get_entry_in_dir(sb, p_dir, entry, NULL);
		if (!ep)
			return;

		if (p_fs->fs_func->get_entry_type((DENTRY_T *) ep) == TYPE_EXTEND) {
			extract_uni_name_from_ext_entry(ep, uniname, i);
			if (ep->order > 0x40)
				return;
		} else {
			return;
		}

		uniname += 13;
	}
} /* end of fat_get_uni_name_from_ext_entry */

void exfat_get_uni_name_from_ext_entry(struct super_block *sb, CHAIN_T *p_dir, INT32 entry, UINT16 *uniname)
{
	INT32 i;
	DENTRY_T *ep;
	ENTRY_SET_CACHE_T *es;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	es = get_entry_set_in_dir(sb, p_dir, entry, ES_ALL_ENTRIES, &ep);
	if (es == NULL || es->num_entries < 3) {
		if(es) {
			release_entry_set(es);
		}
		return;
	}

	ep += 2;

	/*
	* First entry  : file entry
	* Second entry : stream-extension entry
	* Third entry  : first file-name entry
	* So, the index of first file-name dentry should start from 2.
	*/
	for (i = 2; i < es->num_entries; i++, ep++) {
		if (p_fs->fs_func->get_entry_type(ep) == TYPE_EXTEND) {
			extract_uni_name_from_name_entry((NAME_DENTRY_T *)ep, uniname, i);
		} else {
			/* end of name entry */
			goto out;
		}
		uniname += 15;
	}

out:
	release_entry_set(es);
} /* end of exfat_get_uni_name_from_ext_entry */

INT32 extract_uni_name_from_ext_entry(EXT_DENTRY_T *ep, UINT16 *uniname, INT32 order)
{
	INT32 i, len = 0;

	for (i = 0; i < 10; i += 2) {
		*uniname = GET16(ep->unicode_0_4+i);
		if (*uniname == 0x0)
			return(len);
		uniname++;
		len++;
	}

	if (order < 20) {
		for (i = 0; i < 12; i += 2) {
			*uniname = GET16_A(ep->unicode_5_10+i);
			if (*uniname == 0x0)
				return(len);
			uniname++;
			len++;
		}
	} else {
		for (i = 0; i < 8; i += 2) {
			*uniname = GET16_A(ep->unicode_5_10+i);
			if (*uniname == 0x0)
				return(len);
			uniname++;
			len++;
		}
		*uniname = 0x0; /* uniname[MAX_NAME_LENGTH-1] */
		return(len);
	}

	for (i = 0; i < 4; i += 2) {
		*uniname = GET16_A(ep->unicode_11_12+i);
		if (*uniname == 0x0)
			return(len);
		uniname++;
		len++;
	}

	*uniname = 0x0;
	return(len);

} /* end of extract_uni_name_from_ext_entry */

INT32 extract_uni_name_from_name_entry(NAME_DENTRY_T *ep, UINT16 *uniname, INT32 order)
{
	INT32 i, len = 0;

	for (i = 0; i < 30; i += 2) {
		*uniname = GET16_A(ep->unicode_0_14+i);
		if (*uniname == 0x0)
			return(len);
		uniname++;
		len++;
	}

	*uniname = 0x0;
	return(len);

} /* end of extract_uni_name_from_name_entry */

INT32 fat_generate_dos_name(struct super_block *sb, CHAIN_T *p_dir, DOS_NAME_T *p_dosname)
{
	INT32 i, j, count = 0, count_begin = FALSE;
	INT32 dentries_per_clu;
	UINT32 type;
	UINT8 bmap[128/* 1 ~ 1023 */];
	CHAIN_T clu;
	DOS_DENTRY_T *ep;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	Bitmap_clear_all(bmap, 128);
	Bitmap_set(bmap, 0);

	if (p_dir->dir == CLUSTER_32(0)) /* FAT16 root_dir */
		dentries_per_clu = p_fs->dentries_in_root;
	else
		dentries_per_clu = p_fs->dentries_per_clu;

	clu.dir = p_dir->dir;
	clu.flags = p_dir->flags;

	while (clu.dir != CLUSTER_32(~0)) {
		if (p_fs->dev_ejected)
			break;

		for (i = 0; i < dentries_per_clu; i++) {
			ep = (DOS_DENTRY_T *) get_entry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return FFS_MEDIAERR;

			type = p_fs->fs_func->get_entry_type((DENTRY_T *) ep);

			if (type == TYPE_UNUSED)
				break;
			if ((type != TYPE_FILE) && (type != TYPE_DIR))
				continue;

			count = 0;
			count_begin = FALSE;

			for (j = 0; j < 8; j++) {
				if (ep->name[j] == ' ')
					break;

				if (ep->name[j] == '~') {
					count_begin = TRUE;
				} else if (count_begin) {
					if ((ep->name[j] >= '0') && (ep->name[j] <= '9')) {
						count = count * 10 + (ep->name[j] - '0');
					} else {
						count = 0;
						count_begin = FALSE;
					}
				}
			}

			if ((count > 0) && (count < 1024))
				Bitmap_set(bmap, count);
		}

		if (p_dir->dir == CLUSTER_32(0))
			break; /* FAT16 root_dir */

		if (FAT_read(sb, clu.dir, &(clu.dir)) != 0)
			return FFS_MEDIAERR;
	}

	count = 0;
	for (i = 0; i < 128; i++) {
		if (bmap[i] != 0xFF) {
			for (j = 0; j < 8; j++) {
				if (Bitmap_test(&(bmap[i]), j) == 0) {
					count = (i << 3) + j;
					break;
				}
			}
			if (count != 0)
				break;
		}
	}

	if ((count == 0) || (count >= 1024))
		return FFS_FILEEXIST;
	else
		fat_attach_count_to_dos_name(p_dosname->name, count);

	/* Now dos_name has DOS~????.EXT */
	return FFS_SUCCESS;
} /* end of generate_dos_name */

void fat_attach_count_to_dos_name(UINT8 *dosname, INT32 count)
{
	INT32 i, j, length;
	INT8 str_count[6];

	str_count[0] = '~';
	str_count[1] = '\0';
	my_itoa(&(str_count[1]), count);
	length = STRLEN(str_count);

	i = j = 0;
	while (j <= (8 - length)) {
		i = j;
		if (dosname[j] == ' ')
			break;
		if (dosname[j] & 0x80)
			j += 2;
		else
			j++;
	}

	for (j = 0; j < length; i++, j++)
		dosname[i] = (UINT8) str_count[j];

	if (i == 7)
		dosname[7] = ' ';

} /* end of attach_count_to_dos_name */

INT32 fat_calc_num_entries(UNI_NAME_T *p_uniname)
{
	INT32 len;

	len = p_uniname->name_len;
	if (len == 0)
		return 0;

	/* 1 dos name entry + extended entries */
	return((len-1) / 13 + 2);

} /* end of calc_num_enties */

INT32 exfat_calc_num_entries(UNI_NAME_T *p_uniname)
{
	INT32 len;

	len = p_uniname->name_len;
	if (len == 0)
		return 0;

	/* 1 file entry + 1 stream entry + name entries */
	return((len-1) / 15 + 3);

} /* end of exfat_calc_num_enties */

UINT8 calc_checksum_1byte(void *data, INT32 len, UINT8 chksum)
{
	INT32 i;
	UINT8 *c = (UINT8 *) data;

	for (i = 0; i < len; i++, c++)
		chksum = (((chksum & 1) << 7) | ((chksum & 0xFE) >> 1)) + *c;

	return(chksum);
} /* end of calc_checksum_1byte */

UINT16 calc_checksum_2byte(void *data, INT32 len, UINT16 chksum, INT32 type)
{
	INT32 i;
	UINT8 *c = (UINT8 *) data;

	switch (type) {
	case CS_DIR_ENTRY:
		for (i = 0; i < len; i++, c++) {
			if ((i == 2) || (i == 3))
				continue;
			chksum = (((chksum & 1) << 15) | ((chksum & 0xFFFE) >> 1)) + (UINT16) *c;
		}
		break;
	default
			:
		for (i = 0; i < len; i++, c++) {
			chksum = (((chksum & 1) << 15) | ((chksum & 0xFFFE) >> 1)) + (UINT16) *c;
		}
	}

	return(chksum);
} /* end of calc_checksum_2byte */

UINT32 calc_checksum_4byte(void *data, INT32 len, UINT32 chksum, INT32 type)
{
	INT32 i;
	UINT8 *c = (UINT8 *) data;

	switch (type) {
	case CS_PBR_SECTOR:
		for (i = 0; i < len; i++, c++) {
			if ((i == 106) || (i == 107) || (i == 112))
				continue;
			chksum = (((chksum & 1) << 31) | ((chksum & 0xFFFFFFFE) >> 1)) + (UINT32) *c;
		}
		break;
	default
			:
		for (i = 0; i < len; i++, c++) {
			chksum = (((chksum & 1) << 31) | ((chksum & 0xFFFFFFFE) >> 1)) + (UINT32) *c;
		}
	}

	return(chksum);
} /* end of calc_checksum_4byte */

/*
 *  Name Resolution Functions
 */

/* return values of resolve_path()
   > 0 : return the length of the path
   < 0 : return error */
INT32 resolve_path(struct inode *inode, UINT8 *path, CHAIN_T *p_dir, UNI_NAME_T *p_uniname)
{
	INT32 lossy = FALSE;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	FILE_ID_T *fid = &(EXFAT_I(inode)->fid);

	if (STRLEN(path) >= (MAX_NAME_LENGTH * MAX_CHARSET_SIZE))
		return(FFS_INVALIDPATH);

	STRCPY(name_buf, path);

	nls_cstring_to_uniname(sb, p_uniname, name_buf, &lossy);
	if (lossy)
		return(FFS_INVALIDPATH);

	fid->size = i_size_read(inode);

	p_dir->dir = fid->start_clu;
	p_dir->size = (INT32)(fid->size >> p_fs->cluster_size_bits);
	p_dir->flags = fid->flags;

	return(FFS_SUCCESS);
}

/*
 *  File Operation Functions
 */
static FS_FUNC_T fat_fs_func = {
	.alloc_cluster = fat_alloc_cluster,
	.free_cluster = fat_free_cluster,
	.count_used_clusters = fat_count_used_clusters,

	.init_dir_entry = fat_init_dir_entry,
	.init_ext_entry = fat_init_ext_entry,
	.find_dir_entry = fat_find_dir_entry,
	.delete_dir_entry = fat_delete_dir_entry,
	.get_uni_name_from_ext_entry = fat_get_uni_name_from_ext_entry,
	.count_ext_entries = fat_count_ext_entries,
	.calc_num_entries = fat_calc_num_entries,

	.get_entry_type = fat_get_entry_type,
	.set_entry_type = fat_set_entry_type,
	.get_entry_attr = fat_get_entry_attr,
	.set_entry_attr = fat_set_entry_attr,
	.get_entry_flag = fat_get_entry_flag,
	.set_entry_flag = fat_set_entry_flag,
	.get_entry_clu0 = fat_get_entry_clu0,
	.set_entry_clu0 = fat_set_entry_clu0,
	.get_entry_size = fat_get_entry_size,
	.set_entry_size = fat_set_entry_size,
	.get_entry_time = fat_get_entry_time,
	.set_entry_time = fat_set_entry_time,
};


INT32 fat16_mount(struct super_block *sb, PBR_SECTOR_T *p_pbr)
{
	INT32 num_reserved, num_root_sectors;
	BPB16_T *p_bpb = (BPB16_T *) p_pbr->bpb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (p_bpb->num_fats == 0)
		return FFS_FORMATERR;

	num_root_sectors = GET16(p_bpb->num_root_entries) << DENTRY_SIZE_BITS;
	num_root_sectors = ((num_root_sectors-1) >> p_bd->sector_size_bits) + 1;

	p_fs->sectors_per_clu = p_bpb->sectors_per_clu;
	p_fs->sectors_per_clu_bits = my_log2(p_bpb->sectors_per_clu);
	p_fs->cluster_size_bits = p_fs->sectors_per_clu_bits + p_bd->sector_size_bits;
	p_fs->cluster_size = 1 << p_fs->cluster_size_bits;

	p_fs->num_FAT_sectors = GET16(p_bpb->num_fat_sectors);

	p_fs->FAT1_start_sector = p_fs->PBR_sector + GET16(p_bpb->num_reserved);
	if (p_bpb->num_fats == 1)
		p_fs->FAT2_start_sector = p_fs->FAT1_start_sector;
	else
		p_fs->FAT2_start_sector = p_fs->FAT1_start_sector + p_fs->num_FAT_sectors;

	p_fs->root_start_sector = p_fs->FAT2_start_sector + p_fs->num_FAT_sectors;
	p_fs->data_start_sector = p_fs->root_start_sector + num_root_sectors;

	p_fs->num_sectors = GET16(p_bpb->num_sectors);
	if (p_fs->num_sectors == 0)
		p_fs->num_sectors = GET32(p_bpb->num_huge_sectors);

	num_reserved = p_fs->data_start_sector - p_fs->PBR_sector;
	p_fs->num_clusters = ((p_fs->num_sectors - num_reserved) >> p_fs->sectors_per_clu_bits) + 2;
	/* because the cluster index starts with 2 */

	if (p_fs->num_clusters < FAT12_THRESHOLD)
		p_fs->vol_type = FAT12;
	else
		p_fs->vol_type = FAT16;
	p_fs->vol_id = GET32(p_bpb->vol_serial);

	p_fs->root_dir = 0;
	p_fs->dentries_in_root = GET16(p_bpb->num_root_entries);
	p_fs->dentries_per_clu = 1 << (p_fs->cluster_size_bits - DENTRY_SIZE_BITS);

	p_fs->vol_flag = VOL_CLEAN;
	p_fs->clu_srch_ptr = 2;
	p_fs->used_clusters = (UINT32) ~0;

	p_fs->fs_func = &fat_fs_func;

	return FFS_SUCCESS;
} /* end of fat16_mount */

INT32 fat32_mount(struct super_block *sb, PBR_SECTOR_T *p_pbr)
{
	INT32 num_reserved;
	BPB32_T *p_bpb = (BPB32_T *) p_pbr->bpb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (p_bpb->num_fats == 0)
		return FFS_FORMATERR;

	p_fs->sectors_per_clu = p_bpb->sectors_per_clu;
	p_fs->sectors_per_clu_bits = my_log2(p_bpb->sectors_per_clu);
	p_fs->cluster_size_bits = p_fs->sectors_per_clu_bits + p_bd->sector_size_bits;
	p_fs->cluster_size = 1 << p_fs->cluster_size_bits;

	p_fs->num_FAT_sectors = GET32(p_bpb->num_fat32_sectors);

	p_fs->FAT1_start_sector = p_fs->PBR_sector + GET16(p_bpb->num_reserved);
	if (p_bpb->num_fats == 1)
		p_fs->FAT2_start_sector = p_fs->FAT1_start_sector;
	else
		p_fs->FAT2_start_sector = p_fs->FAT1_start_sector + p_fs->num_FAT_sectors;

	p_fs->root_start_sector = p_fs->FAT2_start_sector + p_fs->num_FAT_sectors;
	p_fs->data_start_sector = p_fs->root_start_sector;

	p_fs->num_sectors = GET32(p_bpb->num_huge_sectors);
	num_reserved = p_fs->data_start_sector - p_fs->PBR_sector;

	p_fs->num_clusters = ((p_fs->num_sectors-num_reserved) >> p_fs->sectors_per_clu_bits) + 2;
	/* because the cluster index starts with 2 */

	p_fs->vol_type = FAT32;
	p_fs->vol_id = GET32(p_bpb->vol_serial);

	p_fs->root_dir = GET32(p_bpb->root_cluster);
	p_fs->dentries_in_root = 0;
	p_fs->dentries_per_clu = 1 << (p_fs->cluster_size_bits - DENTRY_SIZE_BITS);

	p_fs->vol_flag = VOL_CLEAN;
	p_fs->clu_srch_ptr = 2;
	p_fs->used_clusters = (UINT32) ~0;

	p_fs->fs_func = &fat_fs_func;

	return FFS_SUCCESS;
} /* end of fat32_mount */

static FS_FUNC_T exfat_fs_func = {
	.alloc_cluster = exfat_alloc_cluster,
	.free_cluster = exfat_free_cluster,
	.count_used_clusters = exfat_count_used_clusters,

	.init_dir_entry = exfat_init_dir_entry,
	.init_ext_entry = exfat_init_ext_entry,
	.find_dir_entry = exfat_find_dir_entry,
	.delete_dir_entry = exfat_delete_dir_entry,
	.get_uni_name_from_ext_entry = exfat_get_uni_name_from_ext_entry,
	.count_ext_entries = exfat_count_ext_entries,
	.calc_num_entries = exfat_calc_num_entries,

	.get_entry_type = exfat_get_entry_type,
	.set_entry_type = exfat_set_entry_type,
	.get_entry_attr = exfat_get_entry_attr,
	.set_entry_attr = exfat_set_entry_attr,
	.get_entry_flag = exfat_get_entry_flag,
	.set_entry_flag = exfat_set_entry_flag,
	.get_entry_clu0 = exfat_get_entry_clu0,
	.set_entry_clu0 = exfat_set_entry_clu0,
	.get_entry_size = exfat_get_entry_size,
	.set_entry_size = exfat_set_entry_size,
	.get_entry_time = exfat_get_entry_time,
	.set_entry_time = exfat_set_entry_time,
};

INT32 exfat_mount(struct super_block *sb, PBR_SECTOR_T *p_pbr)
{
	BPBEX_T *p_bpb = (BPBEX_T *) p_pbr->bpb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (p_bpb->num_fats == 0)
		return FFS_FORMATERR;

	p_fs->sectors_per_clu = 1 << p_bpb->sectors_per_clu_bits;
	p_fs->sectors_per_clu_bits = p_bpb->sectors_per_clu_bits;
	p_fs->cluster_size_bits = p_fs->sectors_per_clu_bits + p_bd->sector_size_bits;
	p_fs->cluster_size = 1 << p_fs->cluster_size_bits;

	p_fs->num_FAT_sectors = GET32(p_bpb->fat_length);

	p_fs->FAT1_start_sector = p_fs->PBR_sector + GET32(p_bpb->fat_offset);
	if (p_bpb->num_fats == 1)
		p_fs->FAT2_start_sector = p_fs->FAT1_start_sector;
	else
		p_fs->FAT2_start_sector = p_fs->FAT1_start_sector + p_fs->num_FAT_sectors;

	p_fs->root_start_sector = p_fs->PBR_sector + GET32(p_bpb->clu_offset);
	p_fs->data_start_sector = p_fs->root_start_sector;

	p_fs->num_sectors = GET64(p_bpb->vol_length);
	p_fs->num_clusters = GET32(p_bpb->clu_count) + 2;
	/* because the cluster index starts with 2 */

	p_fs->vol_type = EXFAT;
	p_fs->vol_id = GET32(p_bpb->vol_serial);

	p_fs->root_dir = GET32(p_bpb->root_cluster);
	p_fs->dentries_in_root = 0;
	p_fs->dentries_per_clu = 1 << (p_fs->cluster_size_bits - DENTRY_SIZE_BITS);

	p_fs->vol_flag = (UINT32) GET16(p_bpb->vol_flags);
	p_fs->clu_srch_ptr = 2;
	p_fs->used_clusters = (UINT32) ~0;

	p_fs->fs_func = &exfat_fs_func;

	return FFS_SUCCESS;
} /* end of exfat_mount */

INT32 create_dir(struct inode *inode, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, FILE_ID_T *fid)
{
	INT32 ret, dentry, num_entries;
	UINT64 size;
	CHAIN_T clu;
	DOS_NAME_T dos_name, dot_name;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	ret = get_num_entries_and_dos_name(sb, p_dir, p_uniname, &num_entries, &dos_name);
	if (ret)
		return ret;

	/* find_empty_entry must be called before alloc_cluster */
	dentry = find_empty_entry(inode, p_dir, num_entries);
	if (dentry < 0)
		return FFS_FULL;

	clu.dir = CLUSTER_32(~0);
	clu.size = 0;
	clu.flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;

	/* (1) allocate a cluster */
	ret = p_fs->fs_func->alloc_cluster(sb, 1, &clu);
	if (ret < 1)
		return FFS_FULL;

	ret = clear_cluster(sb, clu.dir);
	if (ret != FFS_SUCCESS)
		return ret;

	if (p_fs->vol_type == EXFAT) {
		size = p_fs->cluster_size;
	} else {
		size = 0;

		/* initialize the . and .. entry
		   Information for . points to itself
		   Information for .. points to parent dir */

		dot_name.name_case = 0x0;
		MEMCPY(dot_name.name, DOS_CUR_DIR_NAME, DOS_NAME_LENGTH);

		ret = p_fs->fs_func->init_dir_entry(sb, &clu, 0, TYPE_DIR, clu.dir, 0);
		if (ret != FFS_SUCCESS)
			return ret;

		ret = p_fs->fs_func->init_ext_entry(sb, &clu, 0, 1, NULL, &dot_name);
		if (ret != FFS_SUCCESS)
			return ret;

		MEMCPY(dot_name.name, DOS_PAR_DIR_NAME, DOS_NAME_LENGTH);

		if (p_dir->dir == p_fs->root_dir)
			ret = p_fs->fs_func->init_dir_entry(sb, &clu, 1, TYPE_DIR, CLUSTER_32(0), 0);
		else
			ret = p_fs->fs_func->init_dir_entry(sb, &clu, 1, TYPE_DIR, p_dir->dir, 0);

		if (ret != FFS_SUCCESS)
			return ret;

		ret = p_fs->fs_func->init_ext_entry(sb, &clu, 1, 1, NULL, &dot_name);
		if (ret != FFS_SUCCESS)
			return ret;
	}

	/* (2) update the directory entry */
	/* make sub-dir entry in parent directory */
	ret = p_fs->fs_func->init_dir_entry(sb, p_dir, dentry, TYPE_DIR, clu.dir, size);
	if (ret != FFS_SUCCESS)
		return ret;

	ret = p_fs->fs_func->init_ext_entry(sb, p_dir, dentry, num_entries, p_uniname, &dos_name);
	if (ret != FFS_SUCCESS)
		return ret;

	fid->dir.dir = p_dir->dir;
	fid->dir.size = p_dir->size;
	fid->dir.flags = p_dir->flags;
	fid->entry = dentry;

	fid->attr = ATTR_SUBDIR;
	fid->flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;
	fid->size = size;
	fid->start_clu = clu.dir;

	fid->type= TYPE_DIR;
	fid->rwoffset = 0;
	fid->hint_last_off = -1;

	return FFS_SUCCESS;
} /* end of create_dir */

INT32 create_file(struct inode *inode, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, UINT8 mode, FILE_ID_T *fid)
{
	INT32 ret, dentry, num_entries;
	DOS_NAME_T dos_name;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	ret = get_num_entries_and_dos_name(sb, p_dir, p_uniname, &num_entries, &dos_name);
	if (ret)
		return ret;

	/* find_empty_entry must be called before alloc_cluster() */
	dentry = find_empty_entry(inode, p_dir, num_entries);
	if (dentry < 0)
		return FFS_FULL;

	/* (1) update the directory entry */
	/* fill the dos name directory entry information of the created file.
	   the first cluster is not determined yet. (0) */
	ret = p_fs->fs_func->init_dir_entry(sb, p_dir, dentry, TYPE_FILE | mode, CLUSTER_32(0), 0);
	if (ret != FFS_SUCCESS)
		return ret;

	ret = p_fs->fs_func->init_ext_entry(sb, p_dir, dentry, num_entries, p_uniname, &dos_name);
	if (ret != FFS_SUCCESS)
		return ret;

	fid->dir.dir = p_dir->dir;
	fid->dir.size = p_dir->size;
	fid->dir.flags = p_dir->flags;
	fid->entry = dentry;

	fid->attr = ATTR_ARCHIVE | mode;
	fid->flags = (p_fs->vol_type == EXFAT) ? 0x03 : 0x01;
	fid->size = 0;
	fid->start_clu = CLUSTER_32(~0);

	fid->type= TYPE_FILE;
	fid->rwoffset = 0;
	fid->hint_last_off = -1;

	return FFS_SUCCESS;
} /* end of create_file */

void remove_file(struct inode *inode, CHAIN_T *p_dir, INT32 entry)
{
	INT32 num_entries;
	UINT32 sector;
	DENTRY_T *ep;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	ep = get_entry_in_dir(sb, p_dir, entry, &sector);
	if (!ep)
		return;

	buf_lock(sb, sector);

	/* buf_lock() before call count_ext_entries() */
	num_entries = p_fs->fs_func->count_ext_entries(sb, p_dir, entry, ep);
	if (num_entries < 0) {
		buf_unlock(sb, sector);
		return;
	}
	num_entries++;

	buf_unlock(sb, sector);

	/* (1) update the directory entry */
	p_fs->fs_func->delete_dir_entry(sb, p_dir, entry, 0, num_entries);
} /* end of remove_file */

INT32 rename_file(struct inode *inode, CHAIN_T *p_dir, INT32 oldentry, UNI_NAME_T *p_uniname, FILE_ID_T *fid)
{
	INT32 ret, newentry = -1, num_old_entries, num_new_entries;
	UINT32 sector_old, sector_new;
	DOS_NAME_T dos_name;
	DENTRY_T *epold, *epnew;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	epold = get_entry_in_dir(sb, p_dir, oldentry, &sector_old);
	if (!epold)
		return FFS_MEDIAERR;

	buf_lock(sb, sector_old);

	/* buf_lock() before call count_ext_entries() */
	num_old_entries = p_fs->fs_func->count_ext_entries(sb, p_dir, oldentry, epold);
	if (num_old_entries < 0) {
		buf_unlock(sb, sector_old);
		return FFS_MEDIAERR;
	}
	num_old_entries++;

	ret = get_num_entries_and_dos_name(sb, p_dir, p_uniname, &num_new_entries, &dos_name);
	if (ret) {
		buf_unlock(sb, sector_old);
		return ret;
	}

	if (num_old_entries < num_new_entries) {
		newentry = find_empty_entry(inode, p_dir, num_new_entries);
		if (newentry < 0) {
			buf_unlock(sb, sector_old);
			return FFS_FULL;
		}

		epnew = get_entry_in_dir(sb, p_dir, newentry, &sector_new);
		if (!epnew) {
			buf_unlock(sb, sector_old);
			return FFS_MEDIAERR;
		}

		MEMCPY((void *) epnew, (void *) epold, DENTRY_SIZE);
		if (p_fs->fs_func->get_entry_type(epnew) == TYPE_FILE) {
			p_fs->fs_func->set_entry_attr(epnew, p_fs->fs_func->get_entry_attr(epnew) | ATTR_ARCHIVE);
			fid->attr |= ATTR_ARCHIVE;
		}
		buf_modify(sb, sector_new);
		buf_unlock(sb, sector_old);

		if (p_fs->vol_type == EXFAT) {
			epold = get_entry_in_dir(sb, p_dir, oldentry+1, &sector_old);
			buf_lock(sb, sector_old);
			epnew = get_entry_in_dir(sb, p_dir, newentry+1, &sector_new);

			if (!epold || !epnew) {
				buf_unlock(sb, sector_old);
				return FFS_MEDIAERR;
			}

			MEMCPY((void *) epnew, (void *) epold, DENTRY_SIZE);
			buf_modify(sb, sector_new);
			buf_unlock(sb, sector_old);
		}

		ret = p_fs->fs_func->init_ext_entry(sb, p_dir, newentry, num_new_entries, p_uniname, &dos_name);
		if (ret != FFS_SUCCESS)
			return ret;

		p_fs->fs_func->delete_dir_entry(sb, p_dir, oldentry, 0, num_old_entries);
		fid->entry = newentry;
	} else {
		if (p_fs->fs_func->get_entry_type(epold) == TYPE_FILE) {
			p_fs->fs_func->set_entry_attr(epold, p_fs->fs_func->get_entry_attr(epold) | ATTR_ARCHIVE);
			fid->attr |= ATTR_ARCHIVE;
		}
		buf_modify(sb, sector_old);
		buf_unlock(sb, sector_old);

		ret = p_fs->fs_func->init_ext_entry(sb, p_dir, oldentry, num_new_entries, p_uniname, &dos_name);
		if (ret != FFS_SUCCESS)
			return ret;

		p_fs->fs_func->delete_dir_entry(sb, p_dir, oldentry, num_new_entries, num_old_entries);
	}

	return FFS_SUCCESS;
} /* end of rename_file */

INT32 move_file(struct inode *inode, CHAIN_T *p_olddir, INT32 oldentry, CHAIN_T *p_newdir, UNI_NAME_T *p_uniname, FILE_ID_T *fid)
{
	INT32 ret, newentry, num_new_entries, num_old_entries;
	UINT32 sector_mov, sector_new;
	CHAIN_T clu;
	DOS_NAME_T dos_name;
	DENTRY_T *epmov, *epnew;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	epmov = get_entry_in_dir(sb, p_olddir, oldentry, &sector_mov);
	if (!epmov)
		return FFS_MEDIAERR;

	/* check if the source and target directory is the same */
	if (p_fs->fs_func->get_entry_type(epmov) == TYPE_DIR &&
		p_fs->fs_func->get_entry_clu0(epmov) == p_newdir->dir)
		return FFS_INVALIDPATH;

	buf_lock(sb, sector_mov);

	/* buf_lock() before call count_ext_entries() */
	num_old_entries = p_fs->fs_func->count_ext_entries(sb, p_olddir, oldentry, epmov);
	if (num_old_entries < 0) {
		buf_unlock(sb, sector_mov);
		return FFS_MEDIAERR;
	}
	num_old_entries++;

	ret = get_num_entries_and_dos_name(sb, p_newdir, p_uniname, &num_new_entries, &dos_name);
	if (ret) {
		buf_unlock(sb, sector_mov);
		return ret;
	}

	newentry = find_empty_entry(inode, p_newdir, num_new_entries);
	if (newentry < 0) {
		buf_unlock(sb, sector_mov);
		return FFS_FULL;
	}

	epnew = get_entry_in_dir(sb, p_newdir, newentry, &sector_new);
	if (!epnew) {
		buf_unlock(sb, sector_mov);
		return FFS_MEDIAERR;
	}

	MEMCPY((void *) epnew, (void *) epmov, DENTRY_SIZE);
	if (p_fs->fs_func->get_entry_type(epnew) == TYPE_FILE) {
		p_fs->fs_func->set_entry_attr(epnew, p_fs->fs_func->get_entry_attr(epnew) | ATTR_ARCHIVE);
		fid->attr |= ATTR_ARCHIVE;
	}
	buf_modify(sb, sector_new);
	buf_unlock(sb, sector_mov);

	if (p_fs->vol_type == EXFAT) {
		epmov = get_entry_in_dir(sb, p_olddir, oldentry+1, &sector_mov);
		buf_lock(sb, sector_mov);
		epnew = get_entry_in_dir(sb, p_newdir, newentry+1, &sector_new);
		if (!epmov || !epnew) {
			buf_unlock(sb, sector_mov);
			return FFS_MEDIAERR;
		}

		MEMCPY((void *) epnew, (void *) epmov, DENTRY_SIZE);
		buf_modify(sb, sector_new);
		buf_unlock(sb, sector_mov);
	} else if (p_fs->fs_func->get_entry_type(epnew) == TYPE_DIR) {
		/* change ".." pointer to new parent dir */
		clu.dir = p_fs->fs_func->get_entry_clu0(epnew);
		clu.flags = 0x01;

		epnew = get_entry_in_dir(sb, &clu, 1, &sector_new);
		if (!epnew)
			return FFS_MEDIAERR;

		if (p_newdir->dir == p_fs->root_dir)
			p_fs->fs_func->set_entry_clu0(epnew, CLUSTER_32(0));
		else
			p_fs->fs_func->set_entry_clu0(epnew, p_newdir->dir);
		buf_modify(sb, sector_new);
	}

	ret = p_fs->fs_func->init_ext_entry(sb, p_newdir, newentry, num_new_entries, p_uniname, &dos_name);
	if (ret != FFS_SUCCESS)
		return ret;

	p_fs->fs_func->delete_dir_entry(sb, p_olddir, oldentry, 0, num_old_entries);

	fid->dir.dir = p_newdir->dir;
	fid->dir.size = p_newdir->size;
	fid->dir.flags = p_newdir->flags;

	fid->entry = newentry;

	return FFS_SUCCESS;
} /* end of move_file */

/*
 *  Sector Read/Write Functions
 */

INT32 sector_read(struct super_block *sb, UINT32 sec, struct buffer_head **bh, INT32 read)
{
	INT32 ret = FFS_MEDIAERR;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if ((sec >= (p_fs->PBR_sector+p_fs->num_sectors)) && (p_fs->num_sectors > 0)) {
		PRINT("[EXFAT] sector_read: out of range error! (sec = %d)\n", sec);
		fs_error(sb);
		return ret;
	}

	if (!p_fs->dev_ejected) {
		ret = bdev_read(sb, sec, bh, 1, read);
		if (ret != FFS_SUCCESS)
			p_fs->dev_ejected = TRUE;
	}

	return ret;
} /* end of sector_read */

INT32 sector_write(struct super_block *sb, UINT32 sec, struct buffer_head *bh, INT32 sync)
{
	INT32 ret = FFS_MEDIAERR;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (sec >= (p_fs->PBR_sector+p_fs->num_sectors) && (p_fs->num_sectors > 0)) {
		PRINT("[EXFAT] sector_write: out of range error! (sec = %d)\n", sec);
		fs_error(sb);
		return ret;
	}
	if (bh == NULL) {
		PRINT("[EXFAT] sector_write: bh is NULL!\n");
		fs_error(sb);
		return ret;
	}

	if (!p_fs->dev_ejected) {
		ret = bdev_write(sb, sec, bh, 1, sync);
		if (ret != FFS_SUCCESS)
			p_fs->dev_ejected = TRUE;
	}

	return ret;
} /* end of sector_write */

INT32 multi_sector_read(struct super_block *sb, UINT32 sec, struct buffer_head **bh, INT32 num_secs, INT32 read)
{
	INT32 ret = FFS_MEDIAERR;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if (((sec+num_secs) > (p_fs->PBR_sector+p_fs->num_sectors)) && (p_fs->num_sectors > 0)) {
		PRINT("[EXFAT] multi_sector_read: out of range error! (sec = %d, num_secs = %d)\n", sec, num_secs);
		fs_error(sb);
		return ret;
	}

	if (!p_fs->dev_ejected) {
		ret = bdev_read(sb, sec, bh, num_secs, read);
		if (ret != FFS_SUCCESS)
			p_fs->dev_ejected = TRUE;
	}

	return ret;
} /* end of multi_sector_read */

INT32 multi_sector_write(struct super_block *sb, UINT32 sec, struct buffer_head *bh, INT32 num_secs, INT32 sync)
{
	INT32 ret = FFS_MEDIAERR;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	if ((sec+num_secs) > (p_fs->PBR_sector+p_fs->num_sectors) && (p_fs->num_sectors > 0)) {
		PRINT("[EXFAT] multi_sector_write: out of range error! (sec = %d, num_secs = %d)\n", sec, num_secs);
		fs_error(sb);
		return ret;
	}
	if (bh == NULL) {
		PRINT("[EXFAT] multi_sector_write: bh is NULL!\n");
		fs_error(sb);
		return ret;
	}

	if (!p_fs->dev_ejected) {
		ret = bdev_write(sb, sec, bh, num_secs, sync);
		if (ret != FFS_SUCCESS)
			p_fs->dev_ejected = TRUE;
	}

	return ret;
} /* end of multi_sector_write */

/* end of exfat_core.c */
