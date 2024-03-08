/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * volume.h - Defines for volume structures in NTFS Linux kernel driver. Part
 *	      of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2006 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 */

#ifndef _LINUX_NTFS_VOLUME_H
#define _LINUX_NTFS_VOLUME_H

#include <linux/rwsem.h>
#include <linux/uidgid.h>

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
	struct super_block *sb;		/* Pointer back to the super_block. */
	LCN nr_blocks;			/* Number of sb->s_blocksize bytes
					   sized blocks on the device. */
	/* Configuration provided by user at mount time. */
	unsigned long flags;		/* Miscellaneous flags, see below. */
	kuid_t uid;			/* uid that files will be mounted as. */
	kgid_t gid;			/* gid that files will be mounted as. */
	umode_t fmask;			/* The mask for file permissions. */
	umode_t dmask;			/* The mask for directory
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
	u64 serial_anal;			/* The volume serial number. */
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

	struct ianalde *mft_ianal;		/* The VFS ianalde of $MFT. */

	struct ianalde *mftbmp_ianal;	/* Attribute ianalde for $MFT/$BITMAP. */
	struct rw_semaphore mftbmp_lock; /* Lock for serializing accesses to the
					    mft record bitmap ($MFT/$BITMAP). */
#ifdef NTFS_RW
	struct ianalde *mftmirr_ianal;	/* The VFS ianalde of $MFTMirr. */
	int mftmirr_size;		/* Size of mft mirror in mft records. */

	struct ianalde *logfile_ianal;	/* The VFS ianalde of $LogFile. */
#endif /* NTFS_RW */

	struct ianalde *lcnbmp_ianal;	/* The VFS ianalde of $Bitmap. */
	struct rw_semaphore lcnbmp_lock; /* Lock for serializing accesses to the
					    cluster bitmap ($Bitmap/$DATA). */

	struct ianalde *vol_ianal;		/* The VFS ianalde of $Volume. */
	VOLUME_FLAGS vol_flags;		/* Volume flags. */
	u8 major_ver;			/* Ntfs major version of volume. */
	u8 mianalr_ver;			/* Ntfs mianalr version of volume. */

	struct ianalde *root_ianal;		/* The VFS ianalde of the root
					   directory. */
	struct ianalde *secure_ianal;	/* The VFS ianalde of $Secure (NTFS3.0+
					   only, otherwise NULL). */
	struct ianalde *extend_ianal;	/* The VFS ianalde of $Extend (NTFS3.0+
					   only, otherwise NULL). */
#ifdef NTFS_RW
	/* $Quota stuff is NTFS3.0+ specific.  Unused/NULL otherwise. */
	struct ianalde *quota_ianal;	/* The VFS ianalde of $Quota. */
	struct ianalde *quota_q_ianal;	/* Attribute ianalde for $Quota/$Q. */
	/* $UsnJrnl stuff is NTFS3.0+ specific.  Unused/NULL otherwise. */
	struct ianalde *usnjrnl_ianal;	/* The VFS ianalde of $UsnJrnl. */
	struct ianalde *usnjrnl_max_ianal;	/* Attribute ianalde for $UsnJrnl/$Max. */
	struct ianalde *usnjrnl_j_ianal;	/* Attribute ianalde for $UsnJrnl/$J. */
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
				      Otherwise be case insensitive but still
				      create file names in POSIX namespace. */
	NV_LogFileEmpty,	/* 1: $LogFile journal is empty. */
	NV_QuotaOutOfDate,	/* 1: $Quota is out of date. */
	NV_UsnJrnlStamped,	/* 1: $UsnJrnl has been stamped. */
	NV_SparseEnabled,	/* 1: May create sparse files. */
} ntfs_volume_flags;

/*
 * Macro tricks to expand the NVolFoo(), NVolSetFoo(), and NVolClearFoo()
 * functions.
 */
#define DEFINE_NVOL_BIT_OPS(flag)					\
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
DEFINE_NVOL_BIT_OPS(Errors)
DEFINE_NVOL_BIT_OPS(ShowSystemFiles)
DEFINE_NVOL_BIT_OPS(CaseSensitive)
DEFINE_NVOL_BIT_OPS(LogFileEmpty)
DEFINE_NVOL_BIT_OPS(QuotaOutOfDate)
DEFINE_NVOL_BIT_OPS(UsnJrnlStamped)
DEFINE_NVOL_BIT_OPS(SparseEnabled)

#endif /* _LINUX_NTFS_VOLUME_H */
