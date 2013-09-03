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
/*  FILE    : exfat_api.c                                               */
/*  PURPOSE : exFAT API Glue Layer                                      */
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
#include <linux/module.h>
#include <linux/init.h>

#include "exfat_version.h"
#include "exfat_config.h"
#include "exfat_global.h"
#include "exfat_data.h"
#include "exfat_oal.h"

#include "exfat_part.h"
#include "exfat_nls.h"
#include "exfat_api.h"
#include "exfat_super.h"
#include "exfat.h"

/*----------------------------------------------------------------------*/
/*  Constant & Macro Definitions                                        */
/*----------------------------------------------------------------------*/

/*----------------------------------------------------------------------*/
/*  Global Variable Definitions                                         */
/*----------------------------------------------------------------------*/

extern FS_STRUCT_T      fs_struct[];

extern struct semaphore z_sem;

/*----------------------------------------------------------------------*/
/*  Local Variable Definitions                                          */
/*----------------------------------------------------------------------*/

/*----------------------------------------------------------------------*/
/*  Local Function Declarations                                         */
/*----------------------------------------------------------------------*/

/*======================================================================*/
/*  Global Function Definitions                                         */
/*    - All functions for global use have same return value format,     */
/*      that is, FFS_SUCCESS on success and several FS error code on    */
/*      various error condition.                                        */
/*======================================================================*/

/*----------------------------------------------------------------------*/
/*  exFAT Filesystem Init & Exit Functions                              */
/*----------------------------------------------------------------------*/

INT32 FsInit(void)
{
  INT32 i;

	/* initialize all volumes as un-mounted */
	for (i = 0; i < MAX_DRIVE; i++) {
		fs_struct[i].mounted = FALSE;
		fs_struct[i].sb = NULL;
		sm_init(&(fs_struct[i].v_sem));
	}

	return(ffsInit());
}

INT32 FsShutdown(void)
{
	INT32 i;

	/* unmount all volumes */
	for (i = 0; i < MAX_DRIVE; i++) {
		if (!fs_struct[i].mounted) continue;

		ffsUmountVol(fs_struct[i].sb);
	}

	return(ffsShutdown());
}

/*----------------------------------------------------------------------*/
/*  Volume Management Functions                                         */
/*----------------------------------------------------------------------*/

/* FsMountVol : mount the file system volume */
INT32 FsMountVol(struct super_block *sb)
{
	INT32 err, drv;

	sm_P(&z_sem);

	for (drv = 0; drv < MAX_DRIVE; drv++) {
		if (!fs_struct[drv].mounted) break;
	}

	if (drv >= MAX_DRIVE) return(FFS_ERROR);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[drv].v_sem));

	err = buf_init(sb);
	if (!err) {
		err = ffsMountVol(sb, drv);
	}

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[drv].v_sem));

	if (!err) {
		fs_struct[drv].mounted = TRUE;
		fs_struct[drv].sb = sb;
	} else {
		buf_shutdown(sb);
	}

	sm_V(&z_sem);

	return(err);
} /* end of FsMountVol */

/* FsUmountVol : unmount the file system volume */
INT32 FsUmountVol(struct super_block *sb)
{
	INT32 err;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	sm_P(&z_sem);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsUmountVol(sb);
	buf_shutdown(sb);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	fs_struct[p_fs->drv].mounted = FALSE;
	fs_struct[p_fs->drv].sb = NULL;

	sm_V(&z_sem);

	return(err);
} /* end of FsUmountVol */

/* FsGetVolInfo : get the information of a file system volume */
INT32 FsGetVolInfo(struct super_block *sb, VOL_INFO_T *info)
{
	INT32 err;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of pointer parameters */
	if (info == NULL) return(FFS_ERROR);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsGetVolInfo(sb, info);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsGetVolInfo */

/* FsSyncVol : synchronize a file system volume */
INT32 FsSyncVol(struct super_block *sb, INT32 do_sync)
{
	INT32 err;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsSyncVol(sb, do_sync);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsSyncVol */


/*----------------------------------------------------------------------*/
/*  File Operation Functions                                            */
/*----------------------------------------------------------------------*/

/* FsCreateFile : create a file */
INT32 FsLookupFile(struct inode *inode, UINT8 *path, FILE_ID_T *fid)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of pointer parameters */
	if ((fid == NULL) || (path == NULL) || (*path == '\0'))
		return(FFS_ERROR);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsLookupFile(inode, path, fid);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsLookupFile */

/* FsCreateFile : create a file */
INT32 FsCreateFile(struct inode *inode, UINT8 *path, UINT8 mode, FILE_ID_T *fid)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of pointer parameters */
	if ((fid == NULL) || (path == NULL) || (*path == '\0'))
		return(FFS_ERROR);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsCreateFile(inode, path, mode, fid);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsCreateFile */

INT32 FsReadFile(struct inode *inode, FILE_ID_T *fid, void *buffer, UINT64 count, UINT64 *rcount)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of the given file id */
	if (fid == NULL) return(FFS_INVALIDFID);

	/* check the validity of pointer parameters */
	if (buffer == NULL) return(FFS_ERROR);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsReadFile(inode, fid, buffer, count, rcount);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsReadFile */

INT32 FsWriteFile(struct inode *inode, FILE_ID_T *fid, void *buffer, UINT64 count, UINT64 *wcount)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of the given file id */
	if (fid == NULL) return(FFS_INVALIDFID);

	/* check the validity of pointer parameters */
	if (buffer == NULL) return(FFS_ERROR);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsWriteFile(inode, fid, buffer, count, wcount);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsWriteFile */

/* FsTruncateFile : resize the file length */
INT32 FsTruncateFile(struct inode *inode, UINT64 old_size, UINT64 new_size)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	PRINTK("FsTruncateFile entered (inode %p size %llu)\n", inode, new_size);

	err = ffsTruncateFile(inode, old_size, new_size);

	PRINTK("FsTruncateFile exitted (%d)\n", err);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsTruncateFile */

/* FsMoveFile : move(rename) a old file into a new file */
INT32 FsMoveFile(struct inode *old_parent_inode, FILE_ID_T *fid, struct inode *new_parent_inode, struct dentry *new_dentry)
{
	INT32 err;
	struct super_block *sb = old_parent_inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of the given file id */
	if (fid == NULL) return(FFS_INVALIDFID);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsMoveFile(old_parent_inode, fid, new_parent_inode, new_dentry);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsMoveFile */

/* FsRemoveFile : remove a file */
INT32 FsRemoveFile(struct inode *inode, FILE_ID_T *fid)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of the given file id */
	if (fid == NULL) return(FFS_INVALIDFID);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsRemoveFile(inode, fid);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsRemoveFile */

/* FsSetAttr : set the attribute of a given file */
INT32 FsSetAttr(struct inode *inode, UINT32 attr)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsSetAttr(inode, attr);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsSetAttr */

/* FsReadStat : get the information of a given file */
INT32 FsReadStat(struct inode *inode, DIR_ENTRY_T *info)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsGetStat(inode, info);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsReadStat */

/* FsWriteStat : set the information of a given file */
INT32 FsWriteStat(struct inode *inode, DIR_ENTRY_T *info)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	PRINTK("FsWriteStat entered (inode %p info %p\n", inode, info);

	err = ffsSetStat(inode, info);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	PRINTK("FsWriteStat exited (%d)\n", err);

	return(err);
} /* end of FsWriteStat */

/* FsMapCluster : return the cluster number in the given cluster offset */
INT32 FsMapCluster(struct inode *inode, INT32 clu_offset, UINT32 *clu)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of pointer parameters */
	if (clu == NULL) return(FFS_ERROR);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsMapCluster(inode, clu_offset, clu);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsMapCluster */

/*----------------------------------------------------------------------*/
/*  Directory Operation Functions                                       */
/*----------------------------------------------------------------------*/

/* FsCreateDir : create(make) a directory */
INT32 FsCreateDir(struct inode *inode, UINT8 *path, FILE_ID_T *fid)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of pointer parameters */
	if ((fid == NULL) || (path == NULL) || (*path == '\0'))
		return(FFS_ERROR);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsCreateDir(inode, path, fid);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsCreateDir */

/* FsReadDir : read a directory entry from the opened directory */
INT32 FsReadDir(struct inode *inode, DIR_ENTRY_T *dir_entry)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of pointer parameters */
	if (dir_entry == NULL) return(FFS_ERROR);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsReadDir(inode, dir_entry);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsReadDir */

/* FsRemoveDir : remove a directory */
INT32 FsRemoveDir(struct inode *inode, FILE_ID_T *fid)
{
	INT32 err;
	struct super_block *sb = inode->i_sb;
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* check the validity of the given file id */
	if (fid == NULL) return(FFS_INVALIDFID);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	err = ffsRemoveDir(inode, fid);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return(err);
} /* end of FsRemoveDir */

EXPORT_SYMBOL(FsMountVol);
EXPORT_SYMBOL(FsUmountVol);
EXPORT_SYMBOL(FsGetVolInfo);
EXPORT_SYMBOL(FsSyncVol);
EXPORT_SYMBOL(FsLookupFile);
EXPORT_SYMBOL(FsCreateFile);
EXPORT_SYMBOL(FsReadFile);
EXPORT_SYMBOL(FsWriteFile);
EXPORT_SYMBOL(FsTruncateFile);
EXPORT_SYMBOL(FsMoveFile);
EXPORT_SYMBOL(FsRemoveFile);
EXPORT_SYMBOL(FsSetAttr);
EXPORT_SYMBOL(FsReadStat);
EXPORT_SYMBOL(FsWriteStat);
EXPORT_SYMBOL(FsMapCluster);
EXPORT_SYMBOL(FsCreateDir);
EXPORT_SYMBOL(FsReadDir);
EXPORT_SYMBOL(FsRemoveDir);

#if EXFAT_CONFIG_KERNEL_DEBUG
/* FsReleaseCache: Release FAT & buf cache */
INT32 FsReleaseCache(struct super_block *sb)
{
	FS_INFO_T *p_fs = &(EXFAT_SB(sb)->fs_info);

	/* acquire the lock for file system critical section */
	sm_P(&(fs_struct[p_fs->drv].v_sem));

	FAT_release_all(sb);
	buf_release_all(sb);

	/* release the lock for file system critical section */
	sm_V(&(fs_struct[p_fs->drv].v_sem));

	return 0;
}
/* FsReleaseCache */

EXPORT_SYMBOL(FsReleaseCache);
#endif /* EXFAT_CONFIG_KERNEL_DEBUG */

/*======================================================================*/
/*  Local Function Definitions                                          */
/*======================================================================*/

/* end of exfat_api.c */
