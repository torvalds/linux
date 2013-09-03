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
/*  FILE    : exfat_blkdev.c                                            */
/*  PURPOSE : exFAT Block Device Driver Glue Layer                      */
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

#include <linux/blkdev.h>

#include "exfat_config.h"
#include "exfat_global.h"
#include "exfat_blkdev.h"
#include "exfat_data.h"
#include "exfat_api.h"
#include "exfat_super.h"

/*----------------------------------------------------------------------*/
/*  Constant & Macro Definitions                                        */
/*----------------------------------------------------------------------*/

/*----------------------------------------------------------------------*/
/*  Global Variable Definitions                                         */
/*----------------------------------------------------------------------*/

/*----------------------------------------------------------------------*/
/*  Local Variable Definitions                                          */
/*----------------------------------------------------------------------*/

/*======================================================================*/
/*  Function Definitions                                                */
/*======================================================================*/

INT32 bdev_init(void)
{
	return(FFS_SUCCESS);
}

INT32 bdev_shutdown(void)
{
	return(FFS_SUCCESS);
}

INT32 bdev_open(struct super_block *sb)
{
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (p_bd->opened) return(FFS_SUCCESS);

	p_bd->sector_size      = bdev_logical_block_size(sb->s_bdev);
	p_bd->sector_size_bits = my_log2(p_bd->sector_size);
	p_bd->sector_size_mask = p_bd->sector_size - 1;
	p_bd->num_sectors      = i_size_read(sb->s_bdev->bd_inode) >> p_bd->sector_size_bits;

	p_bd->opened = TRUE;

	return(FFS_SUCCESS);
}

INT32 bdev_close(struct super_block *sb)
{
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);

	if (!p_bd->opened) return(FFS_SUCCESS);

	p_bd->opened = FALSE;
	return(FFS_SUCCESS);
}

INT32 bdev_read(struct super_block *sb, UINT32 secno, struct buffer_head **bh, UINT32 num_secs, INT32 read)
{
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
#if EXFAT_CONFIG_KERNEL_DEBUG
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	long flags = sbi->debug_flags;

	if (flags & EXFAT_DEBUGFLAGS_ERROR_RW)	return (FFS_MEDIAERR);
#endif /* EXFAT_CONFIG_KERNEL_DEBUG */

	if (!p_bd->opened) return(FFS_MEDIAERR);

	if (*bh) __brelse(*bh);

	if (read)
		*bh = __bread(sb->s_bdev, secno, num_secs << p_bd->sector_size_bits);
	else
		*bh = __getblk(sb->s_bdev, secno, num_secs << p_bd->sector_size_bits);

	if (*bh) return(FFS_SUCCESS);

	WARN(!p_fs->dev_ejected,
		"[EXFAT] No bh, device seems wrong or to be ejected.\n");

	return(FFS_MEDIAERR);
}

INT32 bdev_write(struct super_block *sb, UINT32 secno, struct buffer_head *bh, UINT32 num_secs, INT32 sync)
{
	INT32 count;
	struct buffer_head *bh2;
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);
#if EXFAT_CONFIG_KERNEL_DEBUG
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	long flags = sbi->debug_flags;

	if (flags & EXFAT_DEBUGFLAGS_ERROR_RW)	return (FFS_MEDIAERR);
#endif /* EXFAT_CONFIG_KERNEL_DEBUG */

	if (!p_bd->opened) return(FFS_MEDIAERR);

	if (secno == bh->b_blocknr) {
		lock_buffer(bh);
		set_buffer_uptodate(bh);
		mark_buffer_dirty(bh);
		unlock_buffer(bh);
		if (sync && (sync_dirty_buffer(bh) != 0))
			return (FFS_MEDIAERR);
	} else {
		count = num_secs << p_bd->sector_size_bits;

		bh2 = __getblk(sb->s_bdev, secno, count);

		if (bh2 == NULL)
			goto no_bh;

		lock_buffer(bh2);
		MEMCPY(bh2->b_data, bh->b_data, count);
		set_buffer_uptodate(bh2);
		mark_buffer_dirty(bh2);
		unlock_buffer(bh2);
		if (sync && (sync_dirty_buffer(bh2) != 0)) {
			__brelse(bh2);
			goto no_bh;
		}
		__brelse(bh2);
	}

	return(FFS_SUCCESS);

no_bh:
	WARN(!p_fs->dev_ejected,
		"[EXFAT] No bh, device seems wrong or to be ejected.\n");

	return (FFS_MEDIAERR);
}

INT32 bdev_sync(struct super_block *sb)
{
	BD_INFO_T *p_bd = &(EXFAT_SB(sb)->bd_info);
#if EXFAT_CONFIG_KERNEL_DEBUG
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	long flags = sbi->debug_flags;

	if (flags & EXFAT_DEBUGFLAGS_ERROR_RW)	return (FFS_MEDIAERR);
#endif /* EXFAT_CONFIG_KERNEL_DEBUG */

	if (!p_bd->opened) return(FFS_MEDIAERR);

	return sync_blockdev(sb->s_bdev);
}

/* end of exfat_blkdev.c */
