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
/*  FILE    : exfat_data.h                                              */
/*  PURPOSE : Header File for exFAT Configuable Constants               */
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

#ifndef _EXFAT_DATA_H
#define _EXFAT_DATA_H

#include "exfat_config.h"
#include "exfat_global.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /*======================================================================*/
	/*                                                                      */
	/*                        FFS CONFIGURATIONS                            */
	/*                  (CHANGE THIS PART IF REQUIRED)                      */
	/*                                                                      */
	/*======================================================================*/

	/* max number of block devices                      */
#define MAX_DEVICE              2

	/* max number of volumes on all block devices       */
#define MAX_DRIVE               2

	/* max number of open files                         */
#define MAX_OPEN                20

	/* max number of root directory entries in FAT12/16 */
	/* (should be an exponential value of 2)            */
#define MAX_DENTRY              512

	/* cache size (in number of sectors)                */
	/* (should be an exponential value of 2)            */
#define FAT_CACHE_SIZE          128
#define FAT_CACHE_HASH_SIZE     64
#define BUF_CACHE_SIZE          256
#define BUF_CACHE_HASH_SIZE     64

#ifndef CONFIG_EXFAT_DEFAULT_CODEPAGE
#define CONFIG_EXFAT_DEFAULT_CODEPAGE	437
#define CONFIG_EXFAT_DEFAULT_IOCHARSET	"utf8"
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _EXFAT_DATA_H */

/* end of exfat_data.h */
