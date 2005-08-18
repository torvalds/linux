/*
 * volume.h - Defines for volume structures in NTFS Linux kernel driver. Part
 *	      of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2005 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_VOLUME_H
#define _LINUX_NTFS_VOLUME_H

#include <linux/rwsem.h>

#include "types.h"
#include "layout.h"

/*
 * The NTFS in memory super block structure.
 */
typedef struct {
	/*
	 * FIXME: Reorder to have commonly used together element within the
	 * same cache line, aiming at a cache line size of 32 bytes. Aim for
	 * 64 bytes for less commonly used together elements. Put most commonly
	 * used elements to front of structure. Obviously do this only when the
	 * structure has stabilized... (AIA)
	 */
	/* Device specifics. */
	struct super_block *sb;		/* Pointer back to the super_block,
					   so we don't have to get the offset
					   every time. */
	LCN nr_blocks;			/* Number of NTFS_BLOCK_SIZE bytes
					   sized blocks on the device. */
	/* Configuration provided by user at mount time. */
	unsigned long flags;		/* Miscellaneous flags, see below. */
	uid_t uid;			/* uid that files will be mounted as. */
	gid_t gid;			/* gid that files will be mounted as. */
	mode_t fmask;			/* The mask for file permissions. */
	mode_t dmask;			/* The mask for directory
					   permissions. */
	u8 mft_zone_multiplier;		/* Initial mft zone multiplier. */
	u8 on_errors;			/* What to do on filesystem errors. */
	/* NTFS bootsector provided information. */
	u16 sector_size;		/* in bytes */
	u8 sector_size_bits;		/* log2(sector_size) */
	u32 cluster_size;		/* in bytes */
	u32 cluster_size_mask;		/* cluster_size - 1 */
	u8 cluster_size_bits;		/* log2(cluster_size) */
	u32 mft_record_size;		/* in bytes */
	u32 mft_record_size_mask;	/* mft_record_size - 1 */
	u8 mft_record_size_bits;	/* log2(mft_record_size) */
	u32 index_record_size;		/* in bytes */
	u32 index_record_size_mask;	/* index_record_size - 1 */
	u8 index_record_size_bits;	/* log2(index_record_size) */
	LCN nr_clusters;		/* Volume size in clusters == number of
					   bits in lcn bitmap. */
	LCN mft_lcn;			/* Cluster location of mft data. */
	LCN mftmirr_lcn;		/* Cluster location of copy of mft. */
	u64 serial_no;			/* The volume serial number. */
	/* Mount specific NTFS information. */
	u32 upcase_len;			/* Number of entries in upcase[]. */
	ntfschar *upcase;		/* The upcase table. */

	s32 attrdef_size;		/* Size of the attribute definition
					   table in bytes. */
	ATTR_DEF *attrdef;		/* Table of attribute definitions.
					   Obtained from FILE_AttrDef. */

#ifdef NTFS_RW
	/* Variables used by the cluster and mft allocators. */
	s64 mft_data_pos;		/* Mft record number at which to
					   allocate the next mft record. */
	LCN mft_zone_start;		/* First cluster of the mft zone. */
	LCN mft_zone_end;		/* First cluster beyond the mft zone. */
	LCN mft_zone_pos;		/* Current position in the mft zone. */
	LCN data1_zone_pos;		/* Current position in the first data
					   zone. */
	LCN data2_zone_pos;		/* Current position in the second data
					   zone. */
#endif /* NTFS_RW */

	struct inode *mft_ino;		/* The VFS inode of $MFT. */

	struct inode *mftbmp_ino;	/* Attribute inode for $MFT/$BITMAP. */
	struct rw_semaphore mftbmp_lock; /* Lock for serializing accesses to the
					    mft record bitmap ($MFT/$BITMAP). */
#ifdef NTFS_RW
	struct inode *mftmirr_ino;	/* The VFS inode of $MFTMirr. */
	int mftmirr_size;		/* Size of mft mirror in mft records. */

	struct inode *logfile_ino;	/* The VFS inode of $LogFile. */
#endif /* NTFS_RW */

	struct inode *lcnbmp_ino;	/* The VFS inode of $Bitmap. */
	struct rw_semaphore lcnbmp_lock; /* Lock for serializing accesses to the
					    cluster bitmap ($Bitmap/$DATA). */

	struct inode *vol_ino;		/* The VFS inode of $Volume. */
	VOLUME_FLAGS vol_flags;		/* Volume flags. */
	u8 major_ver;			/* Ntfs major version of volume. */
	u8 minor_ver;			/* Ntfs minor version of volume. */

	struct inode *root_ino;		/* The VFS inode of the root
					   directory. */
	struct inode *secure_ino;	/* The VFS inode of $Secure (NTFS3.0+
					   only, otherwise NULL). */
	struct inode *extend_ino;	/* The VFS inode of $Extend (NTFS3.0+
					   only, otherwise NULL). */
#ifdef NTFS_RW
	/* $Quota stuff is NTFS3.0+ specific.  Unused/NULL otherwise. */
	struct inode *quota_ino;	/* The VFS inode of $Quota. */
	struct inode *quota_q_ino;	/* Attribute inode for $Quota/$Q. */
	/* $UsnJrnl stuff is NTFS3.0+ specific.  Unused/NULL otherwise. */
	struct inode *usnjrnl_ino;	/* The VFS inode of $UsnJrnl. */
	struct inode *usnjrnl_max_ino;	/* Attribute inode for $UsnJrnl/$Max. */
	struct inode *usnjrnl_j_ino;	/* Attribute inode for $UsnJrnl/$J. */
#endif /* NTFS_RW */
	struct nls_table *nls_map;
} ntfs_volume;

/*
 * Defined bits for the flags field in the ntfs_volume structure.
 */
typedef enum {
	NV_Errors,		/* 1: Volume has errors, prevent remount rw. */
	NV_ShowSystemFiles,	/* 1: Return system files in ntfs_readdir(). */
	NV_CaseSensitive,	/* 1: Treat file names as case sensitive and
				      create filenames in the POSIX namespace.
				      Otherwise be case insensitive and create
				      file names in WIN32 namespace. */
	NV_LogFileEmpty,	/* 1: $LogFile journal is empty. */
	NV_QuotaOutOfDate,	/* 1: $Quota is out of date. */
	NV_UsnJrnlStamped,	/* 1: $UsnJrnl has been stamped. */
	NV_SparseEnabled,	/* 1: May create sparse files. */
} ntfs_volume_flags;

/*
 * Macro tricks to expand the NVolFoo(), NVolSetFoo(), and NVolClearFoo()
 * functions.
 */
#define NVOL_FNS(flag)					\
static inline int NVol##flag(ntfs_volume *vol)		\
{							\
	return test_bit(NV_##flag, &(vol)->flags);	\
}							\
static inline void NVolSet##flag(ntfs_volume *vol)	\
{							\
	set_bit(NV_##flag, &(vol)->flags);		\
}							\
static inline void NVolClear##flag(ntfs_volume *vol)	\
{							\
	clear_bit(NV_##flag, &(vol)->flags);		\
}

/* Emit the ntfs volume bitops functions. */
NVOL_FNS(Errors)
NVOL_FNS(ShowSystemFiles)
NVOL_FNS(CaseSensitive)
NVOL_FNS(LogFileEmpty)
NVOL_FNS(QuotaOutOfDate)
NVOL_FNS(UsnJrnlStamped)
NVOL_FNS(SparseEnabled)

#endif /* _LINUX_NTFS_VOLUME_H */
