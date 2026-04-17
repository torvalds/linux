/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Defines for NTFS Linux kernel driver.
 *
 * Copyright (c) 2001-2014 Anton Altaparmakov and Tuxera Inc.
 * Copyright (C) 2002 Richard Russon
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#ifndef _LINUX_NTFS_H
#define _LINUX_NTFS_H

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/hex.h>
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/nls.h>
#include <linux/smp.h>
#include <linux/pagemap.h>
#include <linux/uidgid.h>

#include "volume.h"
#include "layout.h"
#include "inode.h"

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/*
 * Default pre-allocation size is optimize runlist merge overhead
 * with small chunk size.
 */
#define NTFS_DEF_PREALLOC_SIZE		65536

/*
 * The log2 of the standard number of clusters per compression block.
 * A value of 4 corresponds to 16 clusters (1 << 4), which is the
 * default chunk size used by NTFS LZNT1 compression.
 */
#define STANDARD_COMPRESSION_UNIT	4

/*
 * The maximum cluster size (4KB) allowed for compression to be enabled.
 * By design, NTFS does not support compression on volumes where the
 * cluster size exceeds 4096 bytes.
 */
#define MAX_COMPRESSION_CLUSTER_SIZE 4096

#define NTFS_B_TO_CLU(vol, b) ((b) >> (vol)->cluster_size_bits)
#define NTFS_CLU_TO_B(vol, clu) ((u64)(clu) << (vol)->cluster_size_bits)
#define NTFS_B_TO_CLU_OFS(vol, clu) ((u64)(clu) & (vol)->cluster_size_mask)

#define NTFS_MFT_NR_TO_CLU(vol, mft_no) (((u64)mft_no << (vol)->mft_record_size_bits) >> \
					 (vol)->cluster_size_bits)
#define NTFS_MFT_NR_TO_PIDX(vol, mft_no) (mft_no >> (PAGE_SHIFT - \
					  (vol)->mft_record_size_bits))
#define NTFS_MFT_NR_TO_POFS(vol, mft_no) (((u64)mft_no << (vol)->mft_record_size_bits) & \
					  ~PAGE_MASK)

#define NTFS_PIDX_TO_BLK(vol, idx) (((u64)idx << PAGE_SHIFT) >> \
				    ((vol)->sb)->s_blocksize_bits)
#define NTFS_PIDX_TO_CLU(vol, idx) (((u64)idx << PAGE_SHIFT) >> \
				    (vol)->cluster_size_bits)
#define NTFS_CLU_TO_PIDX(vol, clu) (((u64)(clu) << (vol)->cluster_size_bits) >> \
				    PAGE_SHIFT)
#define NTFS_CLU_TO_POFS(vol, clu) (((u64)(clu) << (vol)->cluster_size_bits) & \
				    ~PAGE_MASK)

#define NTFS_B_TO_SECTOR(vol, b) ((b) >> ((vol)->sb)->s_blocksize_bits)

enum {
	NTFS_BLOCK_SIZE		= 512,
	NTFS_BLOCK_SIZE_BITS	= 9,
	NTFS_SB_MAGIC		= 0x5346544e,	/* 'NTFS' */
	NTFS_MAX_NAME_LEN	= 255,
	NTFS_MAX_LABEL_LEN	= 128,
};

enum {
	CASE_SENSITIVE = 0,
	IGNORE_CASE = 1,
};

/*
 * Conversion helpers for NTFS units.
 */

/* Convert bytes to cluster count */
static inline u64 ntfs_bytes_to_cluster(const struct ntfs_volume *vol,
		s64 bytes)
{
	return bytes >> vol->cluster_size_bits;
}

/* Convert cluster count to bytes */
static inline u64 ntfs_cluster_to_bytes(const struct ntfs_volume *vol,
		u64 clusters)
{
	return clusters << vol->cluster_size_bits;
}

/* Get the byte offset within a cluster from a linear byte address */
static inline u64 ntfs_bytes_to_cluster_off(const struct ntfs_volume *vol,
		u64 bytes)
{
	return bytes & vol->cluster_size_mask;
}

/* Calculate the physical cluster number containing a specific MFT record. */
static inline u64 ntfs_mft_no_to_cluster(const struct ntfs_volume *vol,
		unsigned long mft_no)
{
	return ((u64)mft_no << vol->mft_record_size_bits) >>
		vol->cluster_size_bits;
}

/* Calculate the folio index where the MFT record resides. */
static inline pgoff_t ntfs_mft_no_to_pidx(const struct ntfs_volume *vol,
		unsigned long mft_no)
{
	return mft_no >> (PAGE_SHIFT - vol->mft_record_size_bits);
}

/* Calculate the byte offset within a folio for an MFT record. */
static inline u64 ntfs_mft_no_to_poff(const struct ntfs_volume *vol,
		unsigned long mft_no)
{
	return ((u64)mft_no << vol->mft_record_size_bits) & ~PAGE_MASK;
}

/* Convert folio index to cluster number. */
static inline u64 ntfs_pidx_to_cluster(const struct ntfs_volume *vol,
		pgoff_t idx)
{
	return ((u64)idx << PAGE_SHIFT) >> vol->cluster_size_bits;
}

/* Convert cluster number to folio index. */
static inline pgoff_t ntfs_cluster_to_pidx(const struct ntfs_volume *vol,
		u64 clu)
{
	return (clu << vol->cluster_size_bits) >> PAGE_SHIFT;
}

/* Get the byte offset within a folio from a cluster number */
static inline u64 ntfs_cluster_to_poff(const struct ntfs_volume *vol,
		u64 clu)
{
	return (clu << vol->cluster_size_bits) & ~PAGE_MASK;
}

/* Convert byte offset to sector (block) number. */
static inline sector_t ntfs_bytes_to_sector(const struct ntfs_volume *vol,
		u64 bytes)
{
	return bytes >> vol->sb->s_blocksize_bits;
}

/* Global variables. */

/* Slab caches (from super.c). */
extern struct kmem_cache *ntfs_name_cache;
extern struct kmem_cache *ntfs_inode_cache;
extern struct kmem_cache *ntfs_big_inode_cache;
extern struct kmem_cache *ntfs_attr_ctx_cache;
extern struct kmem_cache *ntfs_index_ctx_cache;

/* The various operations structs defined throughout the driver files. */
extern const struct address_space_operations ntfs_aops;
extern const struct address_space_operations ntfs_mft_aops;

extern const struct  file_operations ntfs_file_ops;
extern const struct inode_operations ntfs_file_inode_ops;
extern const  struct inode_operations ntfs_symlink_inode_operations;
extern const struct inode_operations ntfs_special_inode_operations;

extern const struct  file_operations ntfs_dir_ops;
extern const struct inode_operations ntfs_dir_inode_ops;

extern const struct  file_operations ntfs_empty_file_ops;
extern const struct inode_operations ntfs_empty_inode_ops;

extern const struct export_operations ntfs_export_ops;

/*
 * NTFS_SB - return the ntfs volume given a vfs super block
 * @sb:		VFS super block
 *
 * NTFS_SB() returns the ntfs volume associated with the VFS super block @sb.
 */
static inline struct ntfs_volume *NTFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

/* Declarations of functions and global variables. */

/* From fs/ntfs/compress.c */
int ntfs_read_compressed_block(struct folio *folio);
int allocate_compression_buffers(void);
void free_compression_buffers(void);
int ntfs_compress_write(struct ntfs_inode *ni, loff_t pos, size_t count,
		struct iov_iter *from);

/* From fs/ntfs/super.c */
#define default_upcase_len 0x10000
extern struct mutex ntfs_lock;

struct option_t {
	int val;
	char *str;
};
extern const struct option_t on_errors_arr[];
int ntfs_set_volume_flags(struct ntfs_volume *vol, __le16 flags);
int ntfs_clear_volume_flags(struct ntfs_volume *vol, __le16 flags);
int ntfs_write_volume_label(struct ntfs_volume *vol, char *label);

/* From fs/ntfs/mst.c */
int post_read_mst_fixup(struct ntfs_record *b, const u32 size);
int pre_write_mst_fixup(struct ntfs_record *b, const u32 size);
void post_write_mst_fixup(struct ntfs_record *b);

/* From fs/ntfs/unistr.c */
bool ntfs_are_names_equal(const __le16 *s1, size_t s1_len,
		const __le16 *s2, size_t s2_len,
		const u32 ic,
		const __le16 *upcase, const u32 upcase_size);
int ntfs_collate_names(const __le16 *name1, const u32 name1_len,
		const __le16 *name2, const u32 name2_len,
		const int err_val, const u32 ic,
		const __le16 *upcase, const u32 upcase_len);
int ntfs_ucsncmp(const __le16 *s1, const __le16 *s2, size_t n);
int ntfs_ucsncasecmp(const __le16 *s1, const __le16 *s2, size_t n,
		const __le16 *upcase, const u32 upcase_size);
int ntfs_file_compare_values(const struct file_name_attr *file_name_attr1,
		const struct file_name_attr *file_name_attr2,
		const int err_val, const u32 ic,
		const __le16 *upcase, const u32 upcase_len);
int ntfs_nlstoucs(const struct ntfs_volume *vol, const char *ins,
		const int ins_len, __le16 **outs, int max_name_len);
int ntfs_ucstonls(const struct ntfs_volume *vol, const __le16 *ins,
		const int ins_len, unsigned char **outs, int outs_len);
__le16 *ntfs_ucsndup(const __le16 *s, u32 maxlen);
bool ntfs_names_are_equal(const __le16 *s1, size_t s1_len,
		const __le16 *s2, size_t s2_len,
		const u32 ic,
		const __le16 *upcase, const u32 upcase_size);
int ntfs_force_shutdown(struct super_block *sb, u32 flags);
long ntfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#ifdef CONFIG_COMPAT
long ntfs_compat_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg);
#endif

/* From fs/ntfs/upcase.c */
__le16 *generate_default_upcase(void);

static inline int ntfs_ffs(int x)
{
	int r = 1;

	if (!x)
		return 0;
	if (!(x & 0xffff)) {
		x >>= 16;
		r += 16;
	}
	if (!(x & 0xff)) {
		x >>= 8;
		r += 8;
	}
	if (!(x & 0xf)) {
		x >>= 4;
		r += 4;
	}
	if (!(x & 3)) {
		x >>= 2;
		r += 2;
	}
	if (!(x & 1))
		r += 1;
	return r;
}

/* From fs/ntfs/bdev-io.c */
int ntfs_bdev_read(struct block_device *bdev, char *data, loff_t start, size_t size);
int ntfs_bdev_write(struct super_block *sb, void *buf, loff_t start, size_t size);

#endif /* _LINUX_NTFS_H */
