/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Defines for volume structures in NTFS Linux kernel driver.
 *
 * Copyright (c) 2001-2006 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#ifndef _LINUX_NTFS_VOLUME_H
#define _LINUX_NTFS_VOLUME_H

#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/uidgid.h>
#include <linux/workqueue.h>
#include <linux/errseq.h>

#include "layout.h"

#define NTFS_VOL_UID	BIT(1)
#define NTFS_VOL_GID	BIT(2)

/*
 * The NTFS in memory super block structure.
 *
 * @sb: Pointer back to the super_block.
 * @nr_blocks: Number of sb->s_blocksize bytes sized blocks on the device.
 * @flags: Miscellaneous flags, see below.
 * @uid: uid that files will be mounted as.
 * @gid: gid that files will be mounted as.
 * @fmask: The mask for file permissions.
 * @dmask: The mask for directory permissions.
 * @mft_zone_multiplier: Initial mft zone multiplier.
 * @on_errors: What to do on filesystem errors.
 * @wb_err: Writeback error tracking.
 * @sector_size: in bytes
 * @sector_size_bits: log2(sector_size)
 * @cluster_size: in bytes
 * @cluster_size_mask: cluster_size - 1
 * @cluster_size_bits: log2(cluster_size)
 * @mft_record_size: in bytes
 * @mft_record_size_mask: mft_record_size - 1
 * @mft_record_size_bits: log2(mft_record_size)
 * @index_record_size: in bytes
 * @index_record_size_mask: index_record_size - 1
 * @index_record_size_bits: log2(index_record_size)
 * @nr_clusters: Volume size in clusters == number of bits in lcn bitmap.
 * @mft_lcn: Cluster location of mft data.
 * @mftmirr_lcn: Cluster location of copy of mft.
 * @serial_no: The volume serial number.
 * @upcase_len: Number of entries in upcase[].
 * @upcase: The upcase table.
 * @attrdef_size: Size of the attribute definition table in bytes.
 * @attrdef: Table of attribute definitions. Obtained from FILE_AttrDef.
 * @mft_data_pos: Mft record number at which to allocate the next mft record.
 * @mft_zone_start: First cluster of the mft zone.
 * @mft_zone_end: First cluster beyond the mft zone.
 * @mft_zone_pos: Current position in the mft zone.
 * @data1_zone_pos: Current position in the first data zone.
 * @data2_zone_pos: Current position in the second data zone.
 * @mft_ino: The VFS inode of $MFT.
 * @mftbmp_ino: Attribute inode for $MFT/$BITMAP.
 * @mftbmp_lock: Lock for serializing accesses to the mft record bitmap.
 * @mftmirr_ino: The VFS inode of $MFTMirr.
 * @mftmirr_size: Size of mft mirror in mft records.
 * @logfile_ino: The VFS inode of LogFile.
 * @lcnbmp_ino: The VFS inode of $Bitmap.
 * @lcnbmp_lock: Lock for serializing accesses to the cluster bitmap
 * @vol_ino: The VFS inode of $Volume.
 * @vol_flags: Volume flags.
 * @major_ver: Ntfs major version of volume.
 * @minor_ver: Ntfs minor version of volume.
 * @volume_label: volume label.
 * @root_ino: The VFS inode of the root directory.
 * @secure_ino: The VFS inode of $Secure (NTFS3.0+ only, otherwise NULL).
 * @extend_ino: The VFS inode of $Extend (NTFS3.0+ only, otherwise NULL).
 * @quota_ino: The VFS inode of $Quota.
 * @quota_q_ino: Attribute inode for $Quota/$Q.
 * @nls_map: NLS (National Language Support) table.
 * @nls_utf8: NLS table for UTF-8.
 * @free_waitq: Wait queue for threads waiting for free clusters or MFT records.
 * @free_clusters: Track the number of free clusters.
 * @free_mft_records: Track the free mft records.
 * @dirty_clusters: Number of clusters that are dirty.
 * @sparse_compression_unit: Size of compression/sparse unit in clusters.
 * @lcn_empty_bits_per_page: Number of empty bits per page in the LCN bitmap.
 * @precalc_work: Work structure for background pre-calculation tasks.
 * @preallocated_size: reallocation size (in bytes).
 */
struct ntfs_volume {
	struct super_block *sb;
	s64 nr_blocks;
	unsigned long flags;
	kuid_t uid;
	kgid_t gid;
	umode_t fmask;
	umode_t dmask;
	u8 mft_zone_multiplier;
	u8 on_errors;
	errseq_t wb_err;
	u16 sector_size;
	u8 sector_size_bits;
	u32 cluster_size;
	u32 cluster_size_mask;
	u8 cluster_size_bits;
	u32 mft_record_size;
	u32 mft_record_size_mask;
	u8 mft_record_size_bits;
	u32 index_record_size;
	u32 index_record_size_mask;
	u8 index_record_size_bits;
	s64 nr_clusters;
	s64 mft_lcn;
	s64 mftmirr_lcn;
	u64 serial_no;
	u32 upcase_len;
	__le16 *upcase;
	s32 attrdef_size;
	struct attr_def *attrdef;
	s64 mft_data_pos;
	s64 mft_zone_start;
	s64 mft_zone_end;
	s64 mft_zone_pos;
	s64 data1_zone_pos;
	s64 data2_zone_pos;
	struct inode *mft_ino;
	struct inode *mftbmp_ino;
	struct rw_semaphore mftbmp_lock;
	struct inode *mftmirr_ino;
	int mftmirr_size;
	struct inode *logfile_ino;
	struct inode *lcnbmp_ino;
	struct rw_semaphore lcnbmp_lock;
	struct inode *vol_ino;
	__le16 vol_flags;
	u8 major_ver;
	u8 minor_ver;
	unsigned char *volume_label;
	struct inode *root_ino;
	struct inode *secure_ino;
	struct inode *extend_ino;
	struct inode *quota_ino;
	struct inode *quota_q_ino;
	struct nls_table *nls_map;
	bool nls_utf8;
	wait_queue_head_t free_waitq;
	atomic64_t free_clusters;
	atomic64_t free_mft_records;
	atomic64_t dirty_clusters;
	u8 sparse_compression_unit;
	unsigned int *lcn_empty_bits_per_page;
	struct work_struct precalc_work;
	loff_t preallocated_size;
};

/*
 * Defined bits for the flags field in the ntfs_volume structure.
 *
 * NV_Errors			Volume has errors, prevent remount rw.
 * NV_ShowSystemFiles		Return system files in ntfs_readdir().
 * NV_CaseSensitive		Treat file names as case sensitive and
 *				create filenames in the POSIX namespace.
 *				Otherwise be case insensitive but still
 *				create file names in POSIX namespace.
 * NV_LogFileEmpty		LogFile journal is empty.
 * NV_QuotaOutOfDate		Quota is out of date.
 * NV_UsnJrnlStamped		UsnJrnl has been stamped.
 * NV_ReadOnly			Volume is mounted read-only.
 * NV_Compression		Volume supports compression.
 * NV_FreeClusterKnown		Free cluster count is known and up-to-date.
 * NV_Shutdown			Volume is in shutdown state
 * NV_SysImmutable		Protect system files from deletion.
 * NV_ShowHiddenFiles		Return hidden files in ntfs_readdir().
 * NV_HideDotFiles		Hide names beginning with a dot (".").
 * NV_CheckWindowsNames		Refuse creation/rename of files with
 *				Windows-reserved names (CON, AUX, NUL, COM1,
 *				LPT1, etc.) or invalid characters.
 *
 * NV_Discard			Issue discard/TRIM commands for freed clusters.
 * NV_DisableSparse		Disable creation of sparse regions.
 */
enum {
	NV_Errors,
	NV_ShowSystemFiles,
	NV_CaseSensitive,
	NV_LogFileEmpty,
	NV_QuotaOutOfDate,
	NV_UsnJrnlStamped,
	NV_ReadOnly,
	NV_Compression,
	NV_FreeClusterKnown,
	NV_Shutdown,
	NV_SysImmutable,
	NV_ShowHiddenFiles,
	NV_HideDotFiles,
	NV_CheckWindowsNames,
	NV_Discard,
	NV_DisableSparse,
};

/*
 * Macro tricks to expand the NVolFoo(), NVolSetFoo(), and NVolClearFoo()
 * functions.
 */
#define DEFINE_NVOL_BIT_OPS(flag)					\
static inline int NVol##flag(struct ntfs_volume *vol)		\
{								\
	return test_bit(NV_##flag, &(vol)->flags);		\
}								\
static inline void NVolSet##flag(struct ntfs_volume *vol)	\
{								\
	set_bit(NV_##flag, &(vol)->flags);			\
}								\
static inline void NVolClear##flag(struct ntfs_volume *vol)	\
{								\
	clear_bit(NV_##flag, &(vol)->flags);			\
}

/* Emit the ntfs volume bitops functions. */
DEFINE_NVOL_BIT_OPS(Errors)
DEFINE_NVOL_BIT_OPS(ShowSystemFiles)
DEFINE_NVOL_BIT_OPS(CaseSensitive)
DEFINE_NVOL_BIT_OPS(LogFileEmpty)
DEFINE_NVOL_BIT_OPS(QuotaOutOfDate)
DEFINE_NVOL_BIT_OPS(UsnJrnlStamped)
DEFINE_NVOL_BIT_OPS(ReadOnly)
DEFINE_NVOL_BIT_OPS(Compression)
DEFINE_NVOL_BIT_OPS(FreeClusterKnown)
DEFINE_NVOL_BIT_OPS(Shutdown)
DEFINE_NVOL_BIT_OPS(SysImmutable)
DEFINE_NVOL_BIT_OPS(ShowHiddenFiles)
DEFINE_NVOL_BIT_OPS(HideDotFiles)
DEFINE_NVOL_BIT_OPS(CheckWindowsNames)
DEFINE_NVOL_BIT_OPS(Discard)
DEFINE_NVOL_BIT_OPS(DisableSparse)

static inline void ntfs_inc_free_clusters(struct ntfs_volume *vol, s64 nr)
{
	if (!NVolFreeClusterKnown(vol))
		wait_event(vol->free_waitq, NVolFreeClusterKnown(vol));
	atomic64_add(nr, &vol->free_clusters);
}

static inline void ntfs_dec_free_clusters(struct ntfs_volume *vol, s64 nr)
{
	if (!NVolFreeClusterKnown(vol))
		wait_event(vol->free_waitq, NVolFreeClusterKnown(vol));
	atomic64_sub(nr, &vol->free_clusters);
}

static inline void ntfs_inc_free_mft_records(struct ntfs_volume *vol, s64 nr)
{
	if (!NVolFreeClusterKnown(vol))
		return;

	atomic64_add(nr, &vol->free_mft_records);
}

static inline void ntfs_dec_free_mft_records(struct ntfs_volume *vol, s64 nr)
{
	if (!NVolFreeClusterKnown(vol))
		return;

	atomic64_sub(nr, &vol->free_mft_records);
}

static inline void ntfs_set_lcn_empty_bits(struct ntfs_volume *vol, unsigned long index,
		u8 val, unsigned int count)
{
	if (!NVolFreeClusterKnown(vol))
		wait_event(vol->free_waitq, NVolFreeClusterKnown(vol));

	if (val)
		vol->lcn_empty_bits_per_page[index] -= count;
	else
		vol->lcn_empty_bits_per_page[index] += count;
}

static __always_inline void ntfs_hold_dirty_clusters(struct ntfs_volume *vol, s64 nr_clusters)
{
	atomic64_add(nr_clusters, &vol->dirty_clusters);
}

static __always_inline void ntfs_release_dirty_clusters(struct ntfs_volume *vol, s64 nr_clusters)
{
	if (atomic64_read(&vol->dirty_clusters) < nr_clusters)
		atomic64_set(&vol->dirty_clusters, 0);
	else
		atomic64_sub(nr_clusters, &vol->dirty_clusters);
}

s64 ntfs_available_clusters_count(struct ntfs_volume *vol, s64 nr_clusters);
s64 get_nr_free_clusters(struct ntfs_volume *vol);
#endif /* _LINUX_NTFS_VOLUME_H */
