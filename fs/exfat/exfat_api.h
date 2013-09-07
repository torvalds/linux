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
/*  FILE    : exfat_api.h                                               */
/*  PURPOSE : Header File for exFAT API Glue Layer                      */
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

#ifndef _EXFAT_API_H
#define _EXFAT_API_H

#include "exfat_config.h"
#include "exfat_global.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /*----------------------------------------------------------------------*/
	/*  Constant & Macro Definitions                                        */
	/*----------------------------------------------------------------------*/

#define EXFAT_SUPER_MAGIC       (0x2011BAB0L)
#define EXFAT_ROOT_INO          1

	/* FAT types */
#define FAT12                   0x01    // FAT12
#define FAT16                   0x0E    // Win95 FAT16 (LBA)
#define FAT32                   0x0C    // Win95 FAT32 (LBA)
#define EXFAT                   0x07    // exFAT

	/* file name lengths */
#define MAX_CHARSET_SIZE        3       // max size of multi-byte character
#define MAX_PATH_DEPTH          15      // max depth of path name
#define MAX_NAME_LENGTH         256     // max len of file name including NULL
#define MAX_PATH_LENGTH         260     // max len of path name including NULL
#define DOS_NAME_LENGTH         11      // DOS file name length excluding NULL
#define DOS_PATH_LENGTH         80      // DOS path name length excluding NULL

	/* file attributes */
#define ATTR_NORMAL             0x0000
#define ATTR_READONLY           0x0001
#define ATTR_HIDDEN             0x0002
#define ATTR_SYSTEM             0x0004
#define ATTR_VOLUME             0x0008
#define ATTR_SUBDIR             0x0010
#define ATTR_ARCHIVE            0x0020
#define ATTR_SYMLINK            0x0040
#define ATTR_EXTEND             0x000F
#define ATTR_RWMASK             0x007E

	/* file creation modes */
#define FM_REGULAR              0x00
#define FM_SYMLINK              0x40

	/* return values */
#define FFS_SUCCESS             0
#define FFS_MEDIAERR            1
#define FFS_FORMATERR           2
#define FFS_MOUNTED             3
#define FFS_NOTMOUNTED          4
#define FFS_ALIGNMENTERR        5
#define FFS_SEMAPHOREERR        6
#define FFS_INVALIDPATH         7
#define FFS_INVALIDFID          8
#define FFS_NOTFOUND            9
#define FFS_FILEEXIST           10
#define FFS_PERMISSIONERR       11
#define FFS_NOTOPENED           12
#define FFS_MAXOPENED           13
#define FFS_FULL                14
#define FFS_EOF                 15
#define FFS_DIRBUSY             16
#define FFS_MEMORYERR           17
#define FFS_NAMETOOLONG		18
#define FFS_ERROR               19      // generic error code

	/*----------------------------------------------------------------------*/
	/*  Type Definitions                                                    */
	/*----------------------------------------------------------------------*/

	typedef struct {
		UINT16      Year;
		UINT16      Month;
		UINT16      Day;
		UINT16      Hour;
		UINT16      Minute;
		UINT16      Second;
		UINT16      MilliSecond;
	} DATE_TIME_T;

	typedef struct {
		UINT32      Offset;    // start sector number of the partition
		UINT32      Size;      // in sectors
	} PART_INFO_T;

	typedef struct {
		UINT32      SecSize;    // sector size in bytes
		UINT32      DevSize;    // block device size in sectors
	} DEV_INFO_T;

	typedef struct {
		UINT32      FatType;
		UINT32      ClusterSize;
		UINT32      NumClusters;
		UINT32      FreeClusters;
		UINT32      UsedClusters;
	} VOL_INFO_T;

	/* directory structure */
	typedef struct {
		UINT32      dir;
		INT32       size;
		UINT8       flags;
	} CHAIN_T;

	/* file id structure */
	typedef struct {
		CHAIN_T     dir;
		UINT8       flags;
		INT32       entry;
		UINT32      type;
		UINT32      attr;
		UINT32      start_clu;
		INT32       hint_last_off;
		UINT32      hint_last_clu;
		INT64       rwoffset;
		UINT64      size;
	} FILE_ID_T;

	typedef struct {
		INT8        Name[MAX_NAME_LENGTH *MAX_CHARSET_SIZE];
		INT8        ShortName[DOS_NAME_LENGTH + 2];     // used only for FAT12/16/32, not used for exFAT
		UINT32      Attr;
		UINT64      Size;
		UINT32      NumSubdirs;
		DATE_TIME_T CreateTimestamp;
		DATE_TIME_T ModifyTimestamp;
		DATE_TIME_T AccessTimestamp;
	} DIR_ENTRY_T;

	/*======================================================================*/
	/*                                                                      */
	/*                     API FUNCTION DECLARATIONS                        */
	/*                  (CHANGE THIS PART IF REQUIRED)                      */
	/*                                                                      */
	/*======================================================================*/

	/*----------------------------------------------------------------------*/
	/*  External Function Declarations                                      */
	/*----------------------------------------------------------------------*/

	/* file system initialization & shutdown functions */
	INT32 FsInit(void);
	INT32 FsShutdown(void);

	/* volume management functions */
	INT32 FsMountVol(struct super_block *sb);
	INT32 FsUmountVol(struct super_block *sb);
	INT32 FsGetVolInfo(struct super_block *sb, VOL_INFO_T *info);
	INT32 FsSyncVol(struct super_block *sb, INT32 do_sync);

	/* file management functions */
	INT32 FsLookupFile(struct inode *inode, UINT8 *path, FILE_ID_T *fid);
	INT32 FsCreateFile(struct inode *inode, UINT8 *path, UINT8 mode, FILE_ID_T *fid);
	INT32 FsReadFile(struct inode *inode, FILE_ID_T *fid, void *buffer, UINT64 count, UINT64 *rcount);
	INT32 FsWriteFile(struct inode *inode, FILE_ID_T *fid, void *buffer, UINT64 count, UINT64 *wcount);
	INT32 FsTruncateFile(struct inode *inode, UINT64 old_size, UINT64 new_size);
	INT32 FsMoveFile(struct inode *old_parent_inode, FILE_ID_T *fid, struct inode *new_parent_inode, struct dentry *new_dentry);
	INT32 FsRemoveFile(struct inode *inode, FILE_ID_T *fid);
	INT32 FsSetAttr(struct inode *inode, UINT32 attr);
	INT32 FsReadStat(struct inode *inode, DIR_ENTRY_T *info);
	INT32 FsWriteStat(struct inode *inode, DIR_ENTRY_T *info);
	INT32 FsMapCluster(struct inode *inode, INT32 clu_offset, UINT32 *clu);

	/* directory management functions */
	INT32 FsCreateDir(struct inode *inode, UINT8 *path, FILE_ID_T *fid);
	INT32 FsReadDir(struct inode *inode, DIR_ENTRY_T *dir_entry);
	INT32 FsRemoveDir(struct inode *inode, FILE_ID_T *fid);

	/* debug functions */
	INT32 FsReleaseCache(struct super_block *sb);

	/* partition management functions */
//INT32 FsSetPartition(INT32 dev, INT32 num_vol, PART_INFO_T *vol_spec);
//INT32 FsGetPartition(INT32 dev, INT32 *num_vol, PART_INFO_T *vol_spec);
//INT32 FsGetDevInfo(INT32 dev, DEV_INFO_T *info);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _EXFAT_API_H */

/* end of exfat_api.h */
