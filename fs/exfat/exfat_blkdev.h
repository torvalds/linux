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
/*  FILE    : exfat_blkdev.h                                            */
/*  PURPOSE : Header File for exFAT Block Device Driver Glue Layer      */
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

#ifndef _EXFAT_BLKDEV_H
#define _EXFAT_BLKDEV_H

#include "exfat_config.h"
#include "exfat_global.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /*----------------------------------------------------------------------*/
	/*  Constant & Macro Definitions (Non-Configurable)                     */
	/*----------------------------------------------------------------------*/

	/*----------------------------------------------------------------------*/
	/*  Type Definitions                                                    */
	/*----------------------------------------------------------------------*/

	typedef struct __BD_INFO_T {
		INT32 sector_size;      // in bytes
		INT32 sector_size_bits;
		INT32 sector_size_mask;
		INT32 num_sectors;      // total number of sectors in this block device
		BOOL  opened;           // opened or not
	} BD_INFO_T;

	/*----------------------------------------------------------------------*/
	/*  External Variable Declarations                                      */
	/*----------------------------------------------------------------------*/

	/*----------------------------------------------------------------------*/
	/*  External Function Declarations                                      */
	/*----------------------------------------------------------------------*/

	INT32 bdev_init(void);
	INT32 bdev_shutdown(void);
	INT32 bdev_open(struct super_block *sb);
	INT32 bdev_close(struct super_block *sb);
	INT32 bdev_read(struct super_block *sb, UINT32 secno, struct buffer_head **bh, UINT32 num_secs, INT32 read);
	INT32 bdev_write(struct super_block *sb, UINT32 secno, struct buffer_head *bh, UINT32 num_secs, INT32 sync);
	INT32 bdev_sync(struct super_block *sb);

#ifdef __cplusplus
}
#endif /* __cplusplus  */

#endif /* _EXFAT_BLKDEV_H */

/* end of exfat_blkdev.h */
