/* SPDX-License-Identifier: LGPL-2.1+ WITH Linux-syscall-note */
/*
 * nilfs2_api.h - NILFS2 user space API
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 */

#ifndef _LINUX_NILFS2_API_H
#define _LINUX_NILFS2_API_H

#include <linux/types.h>
#include <linux/ioctl.h>

/**
 * struct nilfs_cpinfo - checkpoint information
 * @ci_flags: flags
 * @ci_pad: padding
 * @ci_cno: checkpoint number
 * @ci_create: creation timestamp
 * @ci_nblk_inc: number of blocks incremented by this checkpoint
 * @ci_inodes_count: inodes count
 * @ci_blocks_count: blocks count
 * @ci_next: next checkpoint number in snapshot list
 */
struct nilfs_cpinfo {
	__u32 ci_flags;
	__u32 ci_pad;
	__u64 ci_cno;
	__u64 ci_create;
	__u64 ci_nblk_inc;
	__u64 ci_inodes_count;
	__u64 ci_blocks_count;
	__u64 ci_next;
};

/* checkpoint flags */
enum {
	NILFS_CPINFO_SNAPSHOT,
	NILFS_CPINFO_INVALID,
	NILFS_CPINFO_SKETCH,
	NILFS_CPINFO_MINOR,
};

#define NILFS_CPINFO_FNS(flag, name)					\
static inline int							\
nilfs_cpinfo_##name(const struct nilfs_cpinfo *cpinfo)			\
{									\
	return !!(cpinfo->ci_flags & (1UL << NILFS_CPINFO_##flag));	\
}

NILFS_CPINFO_FNS(SNAPSHOT, snapshot)
NILFS_CPINFO_FNS(INVALID, invalid)
NILFS_CPINFO_FNS(MINOR, minor)

/**
 * struct nilfs_suinfo - segment usage information
 * @sui_lastmod: timestamp of last modification
 * @sui_nblocks: number of written blocks in segment
 * @sui_flags: segment usage flags
 */
struct nilfs_suinfo {
	__u64 sui_lastmod;
	__u32 sui_nblocks;
	__u32 sui_flags;
};

/* segment usage flags */
enum {
	NILFS_SUINFO_ACTIVE,
	NILFS_SUINFO_DIRTY,
	NILFS_SUINFO_ERROR,
};

#define NILFS_SUINFO_FNS(flag, name)					\
static inline int							\
nilfs_suinfo_##name(const struct nilfs_suinfo *si)			\
{									\
	return si->sui_flags & (1UL << NILFS_SUINFO_##flag);		\
}

NILFS_SUINFO_FNS(ACTIVE, active)
NILFS_SUINFO_FNS(DIRTY, dirty)
NILFS_SUINFO_FNS(ERROR, error)

static inline int nilfs_suinfo_clean(const struct nilfs_suinfo *si)
{
	return !si->sui_flags;
}

/**
 * struct nilfs_suinfo_update - segment usage information update
 * @sup_segnum: segment number
 * @sup_flags: flags for which fields are active in sup_sui
 * @sup_reserved: reserved necessary for alignment
 * @sup_sui: segment usage information
 */
struct nilfs_suinfo_update {
	__u64 sup_segnum;
	__u32 sup_flags;
	__u32 sup_reserved;
	struct nilfs_suinfo sup_sui;
};

enum {
	NILFS_SUINFO_UPDATE_LASTMOD,
	NILFS_SUINFO_UPDATE_NBLOCKS,
	NILFS_SUINFO_UPDATE_FLAGS,
	__NR_NILFS_SUINFO_UPDATE_FIELDS,
};

#define NILFS_SUINFO_UPDATE_FNS(flag, name)				\
static inline void							\
nilfs_suinfo_update_set_##name(struct nilfs_suinfo_update *sup)		\
{									\
	sup->sup_flags |= 1UL << NILFS_SUINFO_UPDATE_##flag;		\
}									\
static inline void							\
nilfs_suinfo_update_clear_##name(struct nilfs_suinfo_update *sup)	\
{									\
	sup->sup_flags &= ~(1UL << NILFS_SUINFO_UPDATE_##flag);		\
}									\
static inline int							\
nilfs_suinfo_update_##name(const struct nilfs_suinfo_update *sup)	\
{									\
	return !!(sup->sup_flags & (1UL << NILFS_SUINFO_UPDATE_##flag));\
}

NILFS_SUINFO_UPDATE_FNS(LASTMOD, lastmod)
NILFS_SUINFO_UPDATE_FNS(NBLOCKS, nblocks)
NILFS_SUINFO_UPDATE_FNS(FLAGS, flags)

enum {
	NILFS_CHECKPOINT,
	NILFS_SNAPSHOT,
};

/**
 * struct nilfs_cpmode - change checkpoint mode structure
 * @cm_cno: checkpoint number
 * @cm_mode: mode of checkpoint
 * @cm_pad: padding
 */
struct nilfs_cpmode {
	__u64 cm_cno;
	__u32 cm_mode;
	__u32 cm_pad;
};

/**
 * struct nilfs_argv - argument vector
 * @v_base: pointer on data array from userspace
 * @v_nmembs: number of members in data array
 * @v_size: size of data array in bytes
 * @v_flags: flags
 * @v_index: start number of target data items
 */
struct nilfs_argv {
	__u64 v_base;
	__u32 v_nmembs;	/* number of members */
	__u16 v_size;	/* size of members */
	__u16 v_flags;
	__u64 v_index;
};

/**
 * struct nilfs_period - period of checkpoint numbers
 * @p_start: start checkpoint number (inclusive)
 * @p_end: end checkpoint number (exclusive)
 */
struct nilfs_period {
	__u64 p_start;
	__u64 p_end;
};

/**
 * struct nilfs_cpstat - checkpoint statistics
 * @cs_cno: checkpoint number
 * @cs_ncps: number of checkpoints
 * @cs_nsss: number of snapshots
 */
struct nilfs_cpstat {
	__u64 cs_cno;
	__u64 cs_ncps;
	__u64 cs_nsss;
};

/**
 * struct nilfs_sustat - segment usage statistics
 * @ss_nsegs: number of segments
 * @ss_ncleansegs: number of clean segments
 * @ss_ndirtysegs: number of dirty segments
 * @ss_ctime: creation time of the last segment
 * @ss_nongc_ctime: creation time of the last segment not for GC
 * @ss_prot_seq: least sequence number of segments which must not be reclaimed
 */
struct nilfs_sustat {
	__u64 ss_nsegs;
	__u64 ss_ncleansegs;
	__u64 ss_ndirtysegs;
	__u64 ss_ctime;
	__u64 ss_nongc_ctime;
	__u64 ss_prot_seq;
};

/**
 * struct nilfs_vinfo - virtual block number information
 * @vi_vblocknr: virtual block number
 * @vi_start: start checkpoint number (inclusive)
 * @vi_end: end checkpoint number (exclusive)
 * @vi_blocknr: disk block number
 */
struct nilfs_vinfo {
	__u64 vi_vblocknr;
	__u64 vi_start;
	__u64 vi_end;
	__u64 vi_blocknr;
};

/**
 * struct nilfs_vdesc - descriptor of virtual block number
 * @vd_ino: inode number
 * @vd_cno: checkpoint number
 * @vd_vblocknr: virtual block number
 * @vd_period: period of checkpoint numbers
 * @vd_blocknr: disk block number
 * @vd_offset: logical block offset inside a file
 * @vd_flags: flags (data or node block)
 * @vd_pad: padding
 */
struct nilfs_vdesc {
	__u64 vd_ino;
	__u64 vd_cno;
	__u64 vd_vblocknr;
	struct nilfs_period vd_period;
	__u64 vd_blocknr;
	__u64 vd_offset;
	__u32 vd_flags;
	__u32 vd_pad;
};

/**
 * struct nilfs_bdesc - descriptor of disk block number
 * @bd_ino: inode number
 * @bd_oblocknr: disk block address (for skipping dead blocks)
 * @bd_blocknr: disk block address
 * @bd_offset: logical block offset inside a file
 * @bd_level: level in the b-tree organization
 * @bd_pad: padding
 */
struct nilfs_bdesc {
	__u64 bd_ino;
	__u64 bd_oblocknr;
	__u64 bd_blocknr;
	__u64 bd_offset;
	__u32 bd_level;
	__u32 bd_pad;
};

#define NILFS_IOCTL_IDENT	'n'

#define NILFS_IOCTL_CHANGE_CPMODE					\
	_IOW(NILFS_IOCTL_IDENT, 0x80, struct nilfs_cpmode)
#define NILFS_IOCTL_DELETE_CHECKPOINT					\
	_IOW(NILFS_IOCTL_IDENT, 0x81, __u64)
#define NILFS_IOCTL_GET_CPINFO						\
	_IOR(NILFS_IOCTL_IDENT, 0x82, struct nilfs_argv)
#define NILFS_IOCTL_GET_CPSTAT						\
	_IOR(NILFS_IOCTL_IDENT, 0x83, struct nilfs_cpstat)
#define NILFS_IOCTL_GET_SUINFO						\
	_IOR(NILFS_IOCTL_IDENT, 0x84, struct nilfs_argv)
#define NILFS_IOCTL_GET_SUSTAT						\
	_IOR(NILFS_IOCTL_IDENT, 0x85, struct nilfs_sustat)
#define NILFS_IOCTL_GET_VINFO						\
	_IOWR(NILFS_IOCTL_IDENT, 0x86, struct nilfs_argv)
#define NILFS_IOCTL_GET_BDESCS						\
	_IOWR(NILFS_IOCTL_IDENT, 0x87, struct nilfs_argv)
#define NILFS_IOCTL_CLEAN_SEGMENTS					\
	_IOW(NILFS_IOCTL_IDENT, 0x88, struct nilfs_argv[5])
#define NILFS_IOCTL_SYNC						\
	_IOR(NILFS_IOCTL_IDENT, 0x8A, __u64)
#define NILFS_IOCTL_RESIZE						\
	_IOW(NILFS_IOCTL_IDENT, 0x8B, __u64)
#define NILFS_IOCTL_SET_ALLOC_RANGE					\
	_IOW(NILFS_IOCTL_IDENT, 0x8C, __u64[2])
#define NILFS_IOCTL_SET_SUINFO						\
	_IOW(NILFS_IOCTL_IDENT, 0x8D, struct nilfs_argv)

#endif /* _LINUX_NILFS2_API_H */
