/*
 * nilfs2_fs.h - NILFS2 on-disk structures and common declarations.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>
 *            Ryusuke Konishi <ryusuke@osrg.net>
 */
/*
 *  linux/include/linux/ext2_fs.h
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/include/linux/minix_fs.h
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#ifndef _LINUX_NILFS_FS_H
#define _LINUX_NILFS_FS_H

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Inode flags stored in nilfs_inode and on-memory nilfs inode
 *
 * We define these flags based on ext2-fs because of the
 * compatibility reason; to avoid problems in chattr(1)
 */
#define NILFS_SECRM_FL		0x00000001 /* Secure deletion */
#define NILFS_UNRM_FL		0x00000002 /* Undelete */
#define NILFS_SYNC_FL		0x00000008 /* Synchronous updates */
#define NILFS_IMMUTABLE_FL	0x00000010 /* Immutable file */
#define NILFS_APPEND_FL		0x00000020 /* writes to file may only append */
#define NILFS_NODUMP_FL		0x00000040 /* do not dump file */
#define NILFS_NOATIME_FL	0x00000080 /* do not update atime */
/* Reserved for compression usage... */
#define NILFS_NOTAIL_FL		0x00008000 /* file tail should not be merged */
#define NILFS_DIRSYNC_FL	0x00010000 /* dirsync behaviour */

#define NILFS_FL_USER_VISIBLE	0x0003DFFF /* User visible flags */
#define NILFS_FL_USER_MODIFIABLE	0x000380FF /* User modifiable flags */


#define NILFS_INODE_BMAP_SIZE	7
/**
 * struct nilfs_inode - structure of an inode on disk
 * @i_blocks: blocks count
 * @i_size: size in bytes
 * @i_ctime: creation time (seconds)
 * @i_mtime: modification time (seconds)
 * @i_ctime_nsec: creation time (nano seconds)
 * @i_mtime_nsec: modification time (nano seconds)
 * @i_uid: user id
 * @i_gid: group id
 * @i_mode: file mode
 * @i_links_count: links count
 * @i_flags: file flags
 * @i_bmap: block mapping
 * @i_xattr: extended attributes
 * @i_generation: file generation (for NFS)
 * @i_pad:	padding
 */
struct nilfs_inode {
	__le64	i_blocks;
	__le64	i_size;
	__le64	i_ctime;
	__le64	i_mtime;
	__le32	i_ctime_nsec;
	__le32	i_mtime_nsec;
	__le32	i_uid;
	__le32	i_gid;
	__le16	i_mode;
	__le16	i_links_count;
	__le32	i_flags;
	__le64	i_bmap[NILFS_INODE_BMAP_SIZE];
#define i_device_code	i_bmap[0]
	__le64	i_xattr;
	__le32	i_generation;
	__le32	i_pad;
};

/**
 * struct nilfs_super_root - structure of super root
 * @sr_sum: check sum
 * @sr_bytes: byte count of the structure
 * @sr_flags: flags (reserved)
 * @sr_nongc_ctime: write time of the last segment not for cleaner operation
 * @sr_dat: DAT file inode
 * @sr_cpfile: checkpoint file inode
 * @sr_sufile: segment usage file inode
 */
struct nilfs_super_root {
	__le32 sr_sum;
	__le16 sr_bytes;
	__le16 sr_flags;
	__le64 sr_nongc_ctime;
	struct nilfs_inode sr_dat;
	struct nilfs_inode sr_cpfile;
	struct nilfs_inode sr_sufile;
};

#define NILFS_SR_MDT_OFFSET(inode_size, i)  \
	((unsigned long)&((struct nilfs_super_root *)0)->sr_dat + \
			(inode_size) * (i))
#define NILFS_SR_DAT_OFFSET(inode_size)     NILFS_SR_MDT_OFFSET(inode_size, 0)
#define NILFS_SR_CPFILE_OFFSET(inode_size)  NILFS_SR_MDT_OFFSET(inode_size, 1)
#define NILFS_SR_SUFILE_OFFSET(inode_size)  NILFS_SR_MDT_OFFSET(inode_size, 2)
#define NILFS_SR_BYTES                  (sizeof(struct nilfs_super_root))

/*
 * Maximal mount counts
 */
#define NILFS_DFL_MAX_MNT_COUNT		50      /* 50 mounts */

/*
 * File system states (sbp->s_state, nilfs->ns_mount_state)
 */
#define NILFS_VALID_FS			0x0001  /* Unmounted cleanly */
#define NILFS_ERROR_FS			0x0002  /* Errors detected */
#define NILFS_RESIZE_FS			0x0004	/* Resize required */

/*
 * Mount flags (sbi->s_mount_opt)
 */
#define NILFS_MOUNT_ERROR_MODE		0x0070  /* Error mode mask */
#define NILFS_MOUNT_ERRORS_CONT		0x0010  /* Continue on errors */
#define NILFS_MOUNT_ERRORS_RO		0x0020  /* Remount fs ro on errors */
#define NILFS_MOUNT_ERRORS_PANIC	0x0040  /* Panic on errors */
#define NILFS_MOUNT_SNAPSHOT		0x0080  /* Snapshot flag */
#define NILFS_MOUNT_BARRIER		0x1000  /* Use block barriers */
#define NILFS_MOUNT_STRICT_ORDER	0x2000  /* Apply strict in-order
						   semantics also for data */
#define NILFS_MOUNT_NORECOVERY		0x4000  /* Disable write access during
						   mount-time recovery */
#define NILFS_MOUNT_DISCARD		0x8000  /* Issue DISCARD requests */


/**
 * struct nilfs_super_block - structure of super block on disk
 */
struct nilfs_super_block {
	__le32	s_rev_level;		/* Revision level */
	__le16	s_minor_rev_level;	/* minor revision level */
	__le16	s_magic;		/* Magic signature */

	__le16  s_bytes;		/* Bytes count of CRC calculation
					   for this structure. s_reserved
					   is excluded. */
	__le16  s_flags;		/* flags */
	__le32  s_crc_seed;		/* Seed value of CRC calculation */
	__le32	s_sum;			/* Check sum of super block */

	__le32	s_log_block_size;	/* Block size represented as follows
					   blocksize =
					       1 << (s_log_block_size + 10) */
	__le64  s_nsegments;		/* Number of segments in filesystem */
	__le64  s_dev_size;		/* block device size in bytes */
	__le64	s_first_data_block;	/* 1st seg disk block number */
	__le32  s_blocks_per_segment;   /* number of blocks per full segment */
	__le32	s_r_segments_percentage; /* Reserved segments percentage */

	__le64  s_last_cno;		/* Last checkpoint number */
	__le64  s_last_pseg;		/* disk block addr pseg written last */
	__le64  s_last_seq;             /* seq. number of seg written last */
	__le64	s_free_blocks_count;	/* Free blocks count */

	__le64	s_ctime;		/* Creation time (execution time of
					   newfs) */
	__le64	s_mtime;		/* Mount time */
	__le64	s_wtime;		/* Write time */
	__le16	s_mnt_count;		/* Mount count */
	__le16	s_max_mnt_count;	/* Maximal mount count */
	__le16	s_state;		/* File system state */
	__le16	s_errors;		/* Behaviour when detecting errors */
	__le64	s_lastcheck;		/* time of last check */

	__le32	s_checkinterval;	/* max. time between checks */
	__le32	s_creator_os;		/* OS */
	__le16	s_def_resuid;		/* Default uid for reserved blocks */
	__le16	s_def_resgid;		/* Default gid for reserved blocks */
	__le32	s_first_ino; 		/* First non-reserved inode */

	__le16  s_inode_size; 		/* Size of an inode */
	__le16  s_dat_entry_size;       /* Size of a dat entry */
	__le16  s_checkpoint_size;      /* Size of a checkpoint */
	__le16	s_segment_usage_size;	/* Size of a segment usage */

	__u8	s_uuid[16];		/* 128-bit uuid for volume */
	char	s_volume_name[16]; 	/* volume name */
	char	s_last_mounted[64]; 	/* directory where last mounted */

	__le32  s_c_interval;           /* Commit interval of segment */
	__le32  s_c_block_max;          /* Threshold of data amount for
					   the segment construction */
	__u32	s_reserved[192];	/* padding to the end of the block */
};

/*
 * Codes for operating systems
 */
#define NILFS_OS_LINUX		0
/* Codes from 1 to 4 are reserved to keep compatibility with ext2 creator-OS */

/*
 * Revision levels
 */
#define NILFS_CURRENT_REV	2	/* current major revision */
#define NILFS_MINOR_REV		0	/* minor revision */

/*
 * Bytes count of super_block for CRC-calculation
 */
#define NILFS_SB_BYTES  \
	((long)&((struct nilfs_super_block *)0)->s_reserved)

/*
 * Special inode number
 */
#define NILFS_ROOT_INO		2	/* Root file inode */
#define NILFS_DAT_INO		3	/* DAT file */
#define NILFS_CPFILE_INO	4	/* checkpoint file */
#define NILFS_SUFILE_INO	5	/* segment usage file */
#define NILFS_IFILE_INO		6	/* ifile */
#define NILFS_ATIME_INO		7	/* Atime file (reserved) */
#define NILFS_XATTR_INO		8	/* Xattribute file (reserved) */
#define NILFS_SKETCH_INO	10	/* Sketch file */
#define NILFS_USER_INO		11	/* Fisrt user's file inode number */

#define NILFS_SB_OFFSET_BYTES	1024	/* byte offset of nilfs superblock */
#define NILFS_SUPER_MAGIC	0x3434	/* NILFS filesystem  magic number */

#define NILFS_SEG_MIN_BLOCKS	16	/* Minimum number of blocks in
					   a full segment */
#define NILFS_PSEG_MIN_BLOCKS	2	/* Minimum number of blocks in
					   a partial segment */
#define NILFS_MIN_NRSVSEGS	8	/* Minimum number of reserved
					   segments */

/*
 * bytes offset of secondary super block
 */
#define NILFS_SB2_OFFSET_BYTES(devsize)	((((devsize) >> 12) - 1) << 12)

/*
 * Maximal count of links to a file
 */
#define NILFS_LINK_MAX		32000

/*
 * Structure of a directory entry
 *  (Same as ext2)
 */

#define NILFS_NAME_LEN 255

/*
 * The new version of the directory entry.  Since V0 structures are
 * stored in intel byte order, and the name_len field could never be
 * bigger than 255 chars, it's safe to reclaim the extra byte for the
 * file_type field.
 */
struct nilfs_dir_entry {
	__le64	inode;			/* Inode number */
	__le16	rec_len;		/* Directory entry length */
	__u8	name_len;		/* Name length */
	__u8	file_type;
	char	name[NILFS_NAME_LEN];	/* File name */
	char    pad;
};

/*
 * NILFS directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 */
enum {
	NILFS_FT_UNKNOWN,
	NILFS_FT_REG_FILE,
	NILFS_FT_DIR,
	NILFS_FT_CHRDEV,
	NILFS_FT_BLKDEV,
	NILFS_FT_FIFO,
	NILFS_FT_SOCK,
	NILFS_FT_SYMLINK,
	NILFS_FT_MAX
};

/*
 * NILFS_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 8
 */
#define NILFS_DIR_PAD			8
#define NILFS_DIR_ROUND			(NILFS_DIR_PAD - 1)
#define NILFS_DIR_REC_LEN(name_len)	(((name_len) + 12 + NILFS_DIR_ROUND) & \
					~NILFS_DIR_ROUND)


/**
 * struct nilfs_finfo - file information
 * @fi_ino: inode number
 * @fi_cno: checkpoint number
 * @fi_nblocks: number of blocks (including intermediate blocks)
 * @fi_ndatablk: number of file data blocks
 */
struct nilfs_finfo {
	__le64 fi_ino;
	__le64 fi_cno;
	__le32 fi_nblocks;
	__le32 fi_ndatablk;
	/* array of virtual block numbers */
};

/**
 * struct nilfs_binfo_v - information for the block to which a virtual block number is assigned
 * @bi_vblocknr: virtual block number
 * @bi_blkoff: block offset
 */
struct nilfs_binfo_v {
	__le64 bi_vblocknr;
	__le64 bi_blkoff;
};

/**
 * struct nilfs_binfo_dat - information for the block which belongs to the DAT file
 * @bi_blkoff: block offset
 * @bi_level: level
 * @bi_pad: padding
 */
struct nilfs_binfo_dat {
	__le64 bi_blkoff;
	__u8 bi_level;
	__u8 bi_pad[7];
};

/**
 * union nilfs_binfo: block information
 * @bi_v: nilfs_binfo_v structure
 * @bi_dat: nilfs_binfo_dat structure
 */
union nilfs_binfo {
	struct nilfs_binfo_v bi_v;
	struct nilfs_binfo_dat bi_dat;
};

/**
 * struct nilfs_segment_summary - segment summary
 * @ss_datasum: checksum of data
 * @ss_sumsum: checksum of segment summary
 * @ss_magic: magic number
 * @ss_bytes: size of this structure in bytes
 * @ss_flags: flags
 * @ss_seq: sequence number
 * @ss_create: creation timestamp
 * @ss_next: next segment
 * @ss_nblocks: number of blocks
 * @ss_nfinfo: number of finfo structures
 * @ss_sumbytes: total size of segment summary in bytes
 * @ss_pad: padding
 */
struct nilfs_segment_summary {
	__le32 ss_datasum;
	__le32 ss_sumsum;
	__le32 ss_magic;
	__le16 ss_bytes;
	__le16 ss_flags;
	__le64 ss_seq;
	__le64 ss_create;
	__le64 ss_next;
	__le32 ss_nblocks;
	__le32 ss_nfinfo;
	__le32 ss_sumbytes;
	__le32 ss_pad;
	/* array of finfo structures */
};

#define NILFS_SEGSUM_MAGIC	0x1eaffa11  /* segment summary magic number */

/*
 * Segment summary flags
 */
#define NILFS_SS_LOGBGN 0x0001  /* begins a logical segment */
#define NILFS_SS_LOGEND 0x0002  /* ends a logical segment */
#define NILFS_SS_SR     0x0004  /* has super root */
#define NILFS_SS_SYNDT  0x0008  /* includes data only updates */
#define NILFS_SS_GC     0x0010  /* segment written for cleaner operation */

/**
 * struct nilfs_btree_node - B-tree node
 * @bn_flags: flags
 * @bn_level: level
 * @bn_nchildren: number of children
 * @bn_pad: padding
 */
struct nilfs_btree_node {
	__u8 bn_flags;
	__u8 bn_level;
	__le16 bn_nchildren;
	__le32 bn_pad;
};

/* flags */
#define NILFS_BTREE_NODE_ROOT   0x01

/* level */
#define NILFS_BTREE_LEVEL_DATA          0
#define NILFS_BTREE_LEVEL_NODE_MIN      (NILFS_BTREE_LEVEL_DATA + 1)
#define NILFS_BTREE_LEVEL_MAX           14

/**
 * struct nilfs_palloc_group_desc - block group descriptor
 * @pg_nfrees: number of free entries in block group
 */
struct nilfs_palloc_group_desc {
	__le32 pg_nfrees;
};

/**
 * struct nilfs_dat_entry - disk address translation entry
 * @dt_blocknr: block number
 * @dt_start: start checkpoint number
 * @dt_end: end checkpoint number
 * @dt_rsv: reserved for future use
 */
struct nilfs_dat_entry {
	__le64 de_blocknr;
	__le64 de_start;
	__le64 de_end;
	__le64 de_rsv;
};

/**
 * struct nilfs_snapshot_list - snapshot list
 * @ssl_next: next checkpoint number on snapshot list
 * @ssl_prev: previous checkpoint number on snapshot list
 */
struct nilfs_snapshot_list {
	__le64 ssl_next;
	__le64 ssl_prev;
};

/**
 * struct nilfs_checkpoint - checkpoint structure
 * @cp_flags: flags
 * @cp_checkpoints_count: checkpoints count in a block
 * @cp_snapshot_list: snapshot list
 * @cp_cno: checkpoint number
 * @cp_create: creation timestamp
 * @cp_nblk_inc: number of blocks incremented by this checkpoint
 * @cp_inodes_count: inodes count
 * @cp_blocks_count: blocks count
 * @cp_ifile_inode: inode of ifile
 */
struct nilfs_checkpoint {
	__le32 cp_flags;
	__le32 cp_checkpoints_count;
	struct nilfs_snapshot_list cp_snapshot_list;
	__le64 cp_cno;
	__le64 cp_create;
	__le64 cp_nblk_inc;
	__le64 cp_inodes_count;
	__le64 cp_blocks_count;		/* Reserved (might be deleted) */

	/* Do not change the byte offset of ifile inode.
	   To keep the compatibility of the disk format,
	   additional fields should be added behind cp_ifile_inode. */
	struct nilfs_inode cp_ifile_inode;
};

/* checkpoint flags */
enum {
	NILFS_CHECKPOINT_SNAPSHOT,
	NILFS_CHECKPOINT_INVALID,
	NILFS_CHECKPOINT_SKETCH,
	NILFS_CHECKPOINT_MINOR,
};

#define NILFS_CHECKPOINT_FNS(flag, name)				\
static inline void							\
nilfs_checkpoint_set_##name(struct nilfs_checkpoint *cp)		\
{									\
	cp->cp_flags = cpu_to_le32(le32_to_cpu(cp->cp_flags) |		\
				   (1UL << NILFS_CHECKPOINT_##flag));	\
}									\
static inline void							\
nilfs_checkpoint_clear_##name(struct nilfs_checkpoint *cp)		\
{									\
	cp->cp_flags = cpu_to_le32(le32_to_cpu(cp->cp_flags) &		\
				   ~(1UL << NILFS_CHECKPOINT_##flag));	\
}									\
static inline int							\
nilfs_checkpoint_##name(const struct nilfs_checkpoint *cp)		\
{									\
	return !!(le32_to_cpu(cp->cp_flags) &				\
		  (1UL << NILFS_CHECKPOINT_##flag));			\
}

NILFS_CHECKPOINT_FNS(SNAPSHOT, snapshot)
NILFS_CHECKPOINT_FNS(INVALID, invalid)
NILFS_CHECKPOINT_FNS(MINOR, minor)

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

#define NILFS_CPINFO_FNS(flag, name)					\
static inline int							\
nilfs_cpinfo_##name(const struct nilfs_cpinfo *cpinfo)			\
{									\
	return !!(cpinfo->ci_flags & (1UL << NILFS_CHECKPOINT_##flag));	\
}

NILFS_CPINFO_FNS(SNAPSHOT, snapshot)
NILFS_CPINFO_FNS(INVALID, invalid)
NILFS_CPINFO_FNS(MINOR, minor)


/**
 * struct nilfs_cpfile_header - checkpoint file header
 * @ch_ncheckpoints: number of checkpoints
 * @ch_nsnapshots: number of snapshots
 * @ch_snapshot_list: snapshot list
 */
struct nilfs_cpfile_header {
	__le64 ch_ncheckpoints;
	__le64 ch_nsnapshots;
	struct nilfs_snapshot_list ch_snapshot_list;
};

#define NILFS_CPFILE_FIRST_CHECKPOINT_OFFSET	\
	((sizeof(struct nilfs_cpfile_header) +				\
	  sizeof(struct nilfs_checkpoint) - 1) /			\
			sizeof(struct nilfs_checkpoint))

/**
 * struct nilfs_segment_usage - segment usage
 * @su_lastmod: last modified timestamp
 * @su_nblocks: number of blocks in segment
 * @su_flags: flags
 */
struct nilfs_segment_usage {
	__le64 su_lastmod;
	__le32 su_nblocks;
	__le32 su_flags;
};

/* segment usage flag */
enum {
	NILFS_SEGMENT_USAGE_ACTIVE,
	NILFS_SEGMENT_USAGE_DIRTY,
	NILFS_SEGMENT_USAGE_ERROR,

	/* ... */
};

#define NILFS_SEGMENT_USAGE_FNS(flag, name)				\
static inline void							\
nilfs_segment_usage_set_##name(struct nilfs_segment_usage *su)		\
{									\
	su->su_flags = cpu_to_le32(le32_to_cpu(su->su_flags) |		\
				   (1UL << NILFS_SEGMENT_USAGE_##flag));\
}									\
static inline void							\
nilfs_segment_usage_clear_##name(struct nilfs_segment_usage *su)	\
{									\
	su->su_flags =							\
		cpu_to_le32(le32_to_cpu(su->su_flags) &			\
			    ~(1UL << NILFS_SEGMENT_USAGE_##flag));      \
}									\
static inline int							\
nilfs_segment_usage_##name(const struct nilfs_segment_usage *su)	\
{									\
	return !!(le32_to_cpu(su->su_flags) &				\
		  (1UL << NILFS_SEGMENT_USAGE_##flag));			\
}

NILFS_SEGMENT_USAGE_FNS(ACTIVE, active)
NILFS_SEGMENT_USAGE_FNS(DIRTY, dirty)
NILFS_SEGMENT_USAGE_FNS(ERROR, error)

static inline void
nilfs_segment_usage_set_clean(struct nilfs_segment_usage *su)
{
	su->su_lastmod = cpu_to_le64(0);
	su->su_nblocks = cpu_to_le32(0);
	su->su_flags = cpu_to_le32(0);
}

static inline int
nilfs_segment_usage_clean(const struct nilfs_segment_usage *su)
{
	return !le32_to_cpu(su->su_flags);
}

/**
 * struct nilfs_sufile_header - segment usage file header
 * @sh_ncleansegs: number of clean segments
 * @sh_ndirtysegs: number of dirty segments
 * @sh_last_alloc: last allocated segment number
 */
struct nilfs_sufile_header {
	__le64 sh_ncleansegs;
	__le64 sh_ndirtysegs;
	__le64 sh_last_alloc;
	/* ... */
};

#define NILFS_SUFILE_FIRST_SEGMENT_USAGE_OFFSET	\
	((sizeof(struct nilfs_sufile_header) +				\
	  sizeof(struct nilfs_segment_usage) - 1) /			\
			 sizeof(struct nilfs_segment_usage))

/**
 * nilfs_suinfo - segment usage information
 * @sui_lastmod:
 * @sui_nblocks:
 * @sui_flags:
 */
struct nilfs_suinfo {
	__u64 sui_lastmod;
	__u32 sui_nblocks;
	__u32 sui_flags;
};

#define NILFS_SUINFO_FNS(flag, name)					\
static inline int							\
nilfs_suinfo_##name(const struct nilfs_suinfo *si)			\
{									\
	return si->sui_flags & (1UL << NILFS_SEGMENT_USAGE_##flag);	\
}

NILFS_SUINFO_FNS(ACTIVE, active)
NILFS_SUINFO_FNS(DIRTY, dirty)
NILFS_SUINFO_FNS(ERROR, error)

static inline int nilfs_suinfo_clean(const struct nilfs_suinfo *si)
{
	return !si->sui_flags;
}

/* ioctl */
enum {
	NILFS_CHECKPOINT,
	NILFS_SNAPSHOT,
};

/**
 * struct nilfs_cpmode -
 * @cc_cno:
 * @cc_mode:
 */
struct nilfs_cpmode {
	__u64 cm_cno;
	__u32 cm_mode;
	__u32 cm_pad;
};

/**
 * struct nilfs_argv - argument vector
 * @v_base:
 * @v_nmembs:
 * @v_size:
 * @v_flags:
 * @v_index:
 */
struct nilfs_argv {
	__u64 v_base;
	__u32 v_nmembs;	/* number of members */
	__u16 v_size;	/* size of members */
	__u16 v_flags;
	__u64 v_index;
};

/**
 * struct nilfs_period -
 * @p_start:
 * @p_end:
 */
struct nilfs_period {
	__u64 p_start;
	__u64 p_end;
};

/**
 * struct nilfs_cpstat -
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
 * struct nilfs_sustat -
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
 * @vi_vblocknr:
 * @vi_start:
 * @vi_end:
 * @vi_blocknr:
 */
struct nilfs_vinfo {
	__u64 vi_vblocknr;
	__u64 vi_start;
	__u64 vi_end;
	__u64 vi_blocknr;
};

/**
 * struct nilfs_vdesc -
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
 * struct nilfs_bdesc -
 */
struct nilfs_bdesc {
	__u64 bd_ino;
	__u64 bd_oblocknr;
	__u64 bd_blocknr;
	__u64 bd_offset;
	__u32 bd_level;
	__u32 bd_pad;
};

#define NILFS_IOCTL_IDENT		'n'

#define NILFS_IOCTL_CHANGE_CPMODE  \
	_IOW(NILFS_IOCTL_IDENT, 0x80, struct nilfs_cpmode)
#define NILFS_IOCTL_DELETE_CHECKPOINT  \
	_IOW(NILFS_IOCTL_IDENT, 0x81, __u64)
#define NILFS_IOCTL_GET_CPINFO  \
	_IOR(NILFS_IOCTL_IDENT, 0x82, struct nilfs_argv)
#define NILFS_IOCTL_GET_CPSTAT  \
	_IOR(NILFS_IOCTL_IDENT, 0x83, struct nilfs_cpstat)
#define NILFS_IOCTL_GET_SUINFO  \
	_IOR(NILFS_IOCTL_IDENT, 0x84, struct nilfs_argv)
#define NILFS_IOCTL_GET_SUSTAT  \
	_IOR(NILFS_IOCTL_IDENT, 0x85, struct nilfs_sustat)
#define NILFS_IOCTL_GET_VINFO  \
	_IOWR(NILFS_IOCTL_IDENT, 0x86, struct nilfs_argv)
#define NILFS_IOCTL_GET_BDESCS  \
	_IOWR(NILFS_IOCTL_IDENT, 0x87, struct nilfs_argv)
#define NILFS_IOCTL_CLEAN_SEGMENTS  \
	_IOW(NILFS_IOCTL_IDENT, 0x88, struct nilfs_argv[5])
#define NILFS_IOCTL_SYNC  \
	_IOR(NILFS_IOCTL_IDENT, 0x8A, __u64)
#define NILFS_IOCTL_RESIZE  \
	_IOW(NILFS_IOCTL_IDENT, 0x8B, __u64)

#endif	/* _LINUX_NILFS_FS_H */
