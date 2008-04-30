/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * ocfs2_fs.h
 *
 * On-disk structures for OCFS2.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef _OCFS2_FS_H
#define _OCFS2_FS_H

/* Version */
#define OCFS2_MAJOR_REV_LEVEL		0
#define OCFS2_MINOR_REV_LEVEL          	90

/*
 * An OCFS2 volume starts this way:
 * Sector 0: Valid ocfs1_vol_disk_hdr that cleanly fails to mount OCFS.
 * Sector 1: Valid ocfs1_vol_label that cleanly fails to mount OCFS.
 * Block OCFS2_SUPER_BLOCK_BLKNO: OCFS2 superblock.
 *
 * All other structures are found from the superblock information.
 *
 * OCFS2_SUPER_BLOCK_BLKNO is in blocks, not sectors.  eg, for a
 * blocksize of 2K, it is 4096 bytes into disk.
 */
#define OCFS2_SUPER_BLOCK_BLKNO		2

/*
 * Cluster size limits. The maximum is kept arbitrarily at 1 MB, and could
 * grow if needed.
 */
#define OCFS2_MIN_CLUSTERSIZE		4096
#define OCFS2_MAX_CLUSTERSIZE		1048576

/*
 * Blocks cannot be bigger than clusters, so the maximum blocksize is the
 * minimum cluster size.
 */
#define OCFS2_MIN_BLOCKSIZE		512
#define OCFS2_MAX_BLOCKSIZE		OCFS2_MIN_CLUSTERSIZE

/* Filesystem magic number */
#define OCFS2_SUPER_MAGIC		0x7461636f

/* Object signatures */
#define OCFS2_SUPER_BLOCK_SIGNATURE	"OCFSV2"
#define OCFS2_INODE_SIGNATURE		"INODE01"
#define OCFS2_EXTENT_BLOCK_SIGNATURE	"EXBLK01"
#define OCFS2_GROUP_DESC_SIGNATURE      "GROUP01"

/* Compatibility flags */
#define OCFS2_HAS_COMPAT_FEATURE(sb,mask)			\
	( OCFS2_SB(sb)->s_feature_compat & (mask) )
#define OCFS2_HAS_RO_COMPAT_FEATURE(sb,mask)			\
	( OCFS2_SB(sb)->s_feature_ro_compat & (mask) )
#define OCFS2_HAS_INCOMPAT_FEATURE(sb,mask)			\
	( OCFS2_SB(sb)->s_feature_incompat & (mask) )
#define OCFS2_SET_COMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_compat |= (mask)
#define OCFS2_SET_RO_COMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_ro_compat |= (mask)
#define OCFS2_SET_INCOMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_incompat |= (mask)
#define OCFS2_CLEAR_COMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_compat &= ~(mask)
#define OCFS2_CLEAR_RO_COMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_ro_compat &= ~(mask)
#define OCFS2_CLEAR_INCOMPAT_FEATURE(sb,mask)			\
	OCFS2_SB(sb)->s_feature_incompat &= ~(mask)

#define OCFS2_FEATURE_COMPAT_SUPP	OCFS2_FEATURE_COMPAT_BACKUP_SB
#define OCFS2_FEATURE_INCOMPAT_SUPP	(OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT \
					 | OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC \
					 | OCFS2_FEATURE_INCOMPAT_INLINE_DATA \
					 | OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP \
					 | OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK)
#define OCFS2_FEATURE_RO_COMPAT_SUPP	OCFS2_FEATURE_RO_COMPAT_UNWRITTEN

/*
 * Heartbeat-only devices are missing journals and other files.  The
 * filesystem driver can't load them, but the library can.  Never put
 * this in OCFS2_FEATURE_INCOMPAT_SUPP, *ever*.
 */
#define OCFS2_FEATURE_INCOMPAT_HEARTBEAT_DEV	0x0002

/*
 * tunefs sets this incompat flag before starting the resize and clears it
 * at the end. This flag protects users from inadvertently mounting the fs
 * after an aborted run without fsck-ing.
 */
#define OCFS2_FEATURE_INCOMPAT_RESIZE_INPROG    0x0004

/* Used to denote a non-clustered volume */
#define OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT	0x0008

/* Support for sparse allocation in b-trees */
#define OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC	0x0010

/*
 * Tunefs sets this incompat flag before starting an operation which
 * would require cleanup on abort. This is done to protect users from
 * inadvertently mounting the fs after an aborted run without
 * fsck-ing.
 *
 * s_tunefs_flags on the super block describes precisely which
 * operations were in progress.
 */
#define OCFS2_FEATURE_INCOMPAT_TUNEFS_INPROG	0x0020

/* Support for data packed into inode blocks */
#define OCFS2_FEATURE_INCOMPAT_INLINE_DATA	0x0040

/* Support for the extended slot map */
#define OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP 0x100


/*
 * Support for alternate, userspace cluster stacks.  If set, the superblock
 * field s_cluster_info contains a tag for the alternate stack in use as
 * well as the name of the cluster being joined.
 * mount.ocfs2 must pass in a matching stack name.
 *
 * If not set, the classic stack will be used.  This is compatbile with
 * all older versions.
 */
#define OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK	0x0080

/*
 * backup superblock flag is used to indicate that this volume
 * has backup superblocks.
 */
#define OCFS2_FEATURE_COMPAT_BACKUP_SB		0x0001

/*
 * Unwritten extents support.
 */
#define OCFS2_FEATURE_RO_COMPAT_UNWRITTEN	0x0001

/* The byte offset of the first backup block will be 1G.
 * The following will be 4G, 16G, 64G, 256G and 1T.
 */
#define OCFS2_BACKUP_SB_START			1 << 30

/* the max backup superblock nums */
#define OCFS2_MAX_BACKUP_SUPERBLOCKS	6

/*
 * Flags on ocfs2_super_block.s_tunefs_flags
 */
#define OCFS2_TUNEFS_INPROG_REMOVE_SLOT		0x0001	/* Removing slots */

/*
 * Flags on ocfs2_dinode.i_flags
 */
#define OCFS2_VALID_FL		(0x00000001)	/* Inode is valid */
#define OCFS2_UNUSED2_FL	(0x00000002)
#define OCFS2_ORPHANED_FL	(0x00000004)	/* On the orphan list */
#define OCFS2_UNUSED3_FL	(0x00000008)
/* System inode flags */
#define OCFS2_SYSTEM_FL		(0x00000010)	/* System inode */
#define OCFS2_SUPER_BLOCK_FL	(0x00000020)	/* Super block */
#define OCFS2_LOCAL_ALLOC_FL	(0x00000040)	/* Slot local alloc bitmap */
#define OCFS2_BITMAP_FL		(0x00000080)	/* Allocation bitmap */
#define OCFS2_JOURNAL_FL	(0x00000100)	/* Slot local journal */
#define OCFS2_HEARTBEAT_FL	(0x00000200)	/* Heartbeat area */
#define OCFS2_CHAIN_FL		(0x00000400)	/* Chain allocator */
#define OCFS2_DEALLOC_FL	(0x00000800)	/* Truncate log */

/*
 * Flags on ocfs2_dinode.i_dyn_features
 *
 * These can change much more often than i_flags. When adding flags,
 * keep in mind that i_dyn_features is only 16 bits wide.
 */
#define OCFS2_INLINE_DATA_FL	(0x0001)	/* Data stored in inode block */
#define OCFS2_HAS_XATTR_FL	(0x0002)
#define OCFS2_INLINE_XATTR_FL	(0x0004)
#define OCFS2_INDEXED_DIR_FL	(0x0008)

/* Inode attributes, keep in sync with EXT2 */
#define OCFS2_SECRM_FL		(0x00000001)	/* Secure deletion */
#define OCFS2_UNRM_FL		(0x00000002)	/* Undelete */
#define OCFS2_COMPR_FL		(0x00000004)	/* Compress file */
#define OCFS2_SYNC_FL		(0x00000008)	/* Synchronous updates */
#define OCFS2_IMMUTABLE_FL	(0x00000010)	/* Immutable file */
#define OCFS2_APPEND_FL		(0x00000020)	/* writes to file may only append */
#define OCFS2_NODUMP_FL		(0x00000040)	/* do not dump file */
#define OCFS2_NOATIME_FL	(0x00000080)	/* do not update atime */
#define OCFS2_DIRSYNC_FL	(0x00010000)	/* dirsync behaviour (directories only) */

#define OCFS2_FL_VISIBLE	(0x000100FF)	/* User visible flags */
#define OCFS2_FL_MODIFIABLE	(0x000100FF)	/* User modifiable flags */

/*
 * Extent record flags (e_node.leaf.flags)
 */
#define OCFS2_EXT_UNWRITTEN	(0x01)	/* Extent is allocated but
					 * unwritten */

/*
 * ioctl commands
 */
#define OCFS2_IOC_GETFLAGS	_IOR('f', 1, long)
#define OCFS2_IOC_SETFLAGS	_IOW('f', 2, long)
#define OCFS2_IOC32_GETFLAGS	_IOR('f', 1, int)
#define OCFS2_IOC32_SETFLAGS	_IOW('f', 2, int)

/*
 * Space reservation / allocation / free ioctls and argument structure
 * are designed to be compatible with XFS.
 *
 * ALLOCSP* and FREESP* are not and will never be supported, but are
 * included here for completeness.
 */
struct ocfs2_space_resv {
	__s16		l_type;
	__s16		l_whence;
	__s64		l_start;
	__s64		l_len;		/* len == 0 means until end of file */
	__s32		l_sysid;
	__u32		l_pid;
	__s32		l_pad[4];	/* reserve area			    */
};

#define OCFS2_IOC_ALLOCSP		_IOW ('X', 10, struct ocfs2_space_resv)
#define OCFS2_IOC_FREESP		_IOW ('X', 11, struct ocfs2_space_resv)
#define OCFS2_IOC_RESVSP		_IOW ('X', 40, struct ocfs2_space_resv)
#define OCFS2_IOC_UNRESVSP	_IOW ('X', 41, struct ocfs2_space_resv)
#define OCFS2_IOC_ALLOCSP64	_IOW ('X', 36, struct ocfs2_space_resv)
#define OCFS2_IOC_FREESP64	_IOW ('X', 37, struct ocfs2_space_resv)
#define OCFS2_IOC_RESVSP64	_IOW ('X', 42, struct ocfs2_space_resv)
#define OCFS2_IOC_UNRESVSP64	_IOW ('X', 43, struct ocfs2_space_resv)

/* Used to pass group descriptor data when online resize is done */
struct ocfs2_new_group_input {
	__u64 group;		/* Group descriptor's blkno. */
	__u32 clusters;		/* Total number of clusters in this group */
	__u32 frees;		/* Total free clusters in this group */
	__u16 chain;		/* Chain for this group */
	__u16 reserved1;
	__u32 reserved2;
};

#define OCFS2_IOC_GROUP_EXTEND	_IOW('o', 1, int)
#define OCFS2_IOC_GROUP_ADD	_IOW('o', 2,struct ocfs2_new_group_input)
#define OCFS2_IOC_GROUP_ADD64	_IOW('o', 3,struct ocfs2_new_group_input)

/*
 * Journal Flags (ocfs2_dinode.id1.journal1.i_flags)
 */
#define OCFS2_JOURNAL_DIRTY_FL	(0x00000001)	/* Journal needs recovery */

/*
 * superblock s_state flags
 */
#define OCFS2_ERROR_FS		(0x00000001)	/* FS saw errors */

/* Limit of space in ocfs2_dir_entry */
#define OCFS2_MAX_FILENAME_LEN		255

/* Maximum slots on an ocfs2 file system */
#define OCFS2_MAX_SLOTS			255

/* Slot map indicator for an empty slot */
#define OCFS2_INVALID_SLOT		-1

#define OCFS2_VOL_UUID_LEN		16
#define OCFS2_MAX_VOL_LABEL_LEN		64

/* The alternate, userspace stack fields */
#define OCFS2_STACK_LABEL_LEN		4
#define OCFS2_CLUSTER_NAME_LEN		16

/* Journal limits (in bytes) */
#define OCFS2_MIN_JOURNAL_SIZE		(4 * 1024 * 1024)

/*
 * Default local alloc size (in megabytes)
 *
 * The value chosen should be such that most allocations, including new
 * block groups, use local alloc.
 */
#define OCFS2_DEFAULT_LOCAL_ALLOC_SIZE	8

struct ocfs2_system_inode_info {
	char	*si_name;
	int	si_iflags;
	int	si_mode;
};

/* System file index */
enum {
	BAD_BLOCK_SYSTEM_INODE = 0,
	GLOBAL_INODE_ALLOC_SYSTEM_INODE,
	SLOT_MAP_SYSTEM_INODE,
#define OCFS2_FIRST_ONLINE_SYSTEM_INODE SLOT_MAP_SYSTEM_INODE
	HEARTBEAT_SYSTEM_INODE,
	GLOBAL_BITMAP_SYSTEM_INODE,
#define OCFS2_LAST_GLOBAL_SYSTEM_INODE GLOBAL_BITMAP_SYSTEM_INODE
	ORPHAN_DIR_SYSTEM_INODE,
	EXTENT_ALLOC_SYSTEM_INODE,
	INODE_ALLOC_SYSTEM_INODE,
	JOURNAL_SYSTEM_INODE,
	LOCAL_ALLOC_SYSTEM_INODE,
	TRUNCATE_LOG_SYSTEM_INODE,
	NUM_SYSTEM_INODES
};

static struct ocfs2_system_inode_info ocfs2_system_inodes[NUM_SYSTEM_INODES] = {
	/* Global system inodes (single copy) */
	/* The first two are only used from userspace mfks/tunefs */
	[BAD_BLOCK_SYSTEM_INODE]		= { "bad_blocks", 0, S_IFREG | 0644 },
	[GLOBAL_INODE_ALLOC_SYSTEM_INODE] 	= { "global_inode_alloc", OCFS2_BITMAP_FL | OCFS2_CHAIN_FL, S_IFREG | 0644 },

	/* These are used by the running filesystem */
	[SLOT_MAP_SYSTEM_INODE]			= { "slot_map", 0, S_IFREG | 0644 },
	[HEARTBEAT_SYSTEM_INODE]		= { "heartbeat", OCFS2_HEARTBEAT_FL, S_IFREG | 0644 },
	[GLOBAL_BITMAP_SYSTEM_INODE]		= { "global_bitmap", 0, S_IFREG | 0644 },

	/* Slot-specific system inodes (one copy per slot) */
	[ORPHAN_DIR_SYSTEM_INODE]		= { "orphan_dir:%04d", 0, S_IFDIR | 0755 },
	[EXTENT_ALLOC_SYSTEM_INODE]		= { "extent_alloc:%04d", OCFS2_BITMAP_FL | OCFS2_CHAIN_FL, S_IFREG | 0644 },
	[INODE_ALLOC_SYSTEM_INODE]		= { "inode_alloc:%04d", OCFS2_BITMAP_FL | OCFS2_CHAIN_FL, S_IFREG | 0644 },
	[JOURNAL_SYSTEM_INODE]			= { "journal:%04d", OCFS2_JOURNAL_FL, S_IFREG | 0644 },
	[LOCAL_ALLOC_SYSTEM_INODE]		= { "local_alloc:%04d", OCFS2_BITMAP_FL | OCFS2_LOCAL_ALLOC_FL, S_IFREG | 0644 },
	[TRUNCATE_LOG_SYSTEM_INODE]		= { "truncate_log:%04d", OCFS2_DEALLOC_FL, S_IFREG | 0644 }
};

/* Parameter passed from mount.ocfs2 to module */
#define OCFS2_HB_NONE			"heartbeat=none"
#define OCFS2_HB_LOCAL			"heartbeat=local"

/*
 * OCFS2 directory file types.  Only the low 3 bits are used.  The
 * other bits are reserved for now.
 */
#define OCFS2_FT_UNKNOWN	0
#define OCFS2_FT_REG_FILE	1
#define OCFS2_FT_DIR		2
#define OCFS2_FT_CHRDEV		3
#define OCFS2_FT_BLKDEV		4
#define OCFS2_FT_FIFO		5
#define OCFS2_FT_SOCK		6
#define OCFS2_FT_SYMLINK	7

#define OCFS2_FT_MAX		8

/*
 * OCFS2_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define OCFS2_DIR_PAD			4
#define OCFS2_DIR_ROUND			(OCFS2_DIR_PAD - 1)
#define OCFS2_DIR_MEMBER_LEN 		offsetof(struct ocfs2_dir_entry, name)
#define OCFS2_DIR_REC_LEN(name_len)	(((name_len) + OCFS2_DIR_MEMBER_LEN + \
                                          OCFS2_DIR_ROUND) & \
					 ~OCFS2_DIR_ROUND)

#define OCFS2_LINK_MAX		32000

#define S_SHIFT			12
static unsigned char ocfs2_type_by_mode[S_IFMT >> S_SHIFT] = {
	[S_IFREG >> S_SHIFT]  = OCFS2_FT_REG_FILE,
	[S_IFDIR >> S_SHIFT]  = OCFS2_FT_DIR,
	[S_IFCHR >> S_SHIFT]  = OCFS2_FT_CHRDEV,
	[S_IFBLK >> S_SHIFT]  = OCFS2_FT_BLKDEV,
	[S_IFIFO >> S_SHIFT]  = OCFS2_FT_FIFO,
	[S_IFSOCK >> S_SHIFT] = OCFS2_FT_SOCK,
	[S_IFLNK >> S_SHIFT]  = OCFS2_FT_SYMLINK,
};


/*
 * Convenience casts
 */
#define OCFS2_RAW_SB(dinode)		(&((dinode)->id2.i_super))

/*
 * On disk extent record for OCFS2
 * It describes a range of clusters on disk.
 *
 * Length fields are divided into interior and leaf node versions.
 * This leaves room for a flags field (OCFS2_EXT_*) in the leaf nodes.
 */
struct ocfs2_extent_rec {
/*00*/	__le32 e_cpos;		/* Offset into the file, in clusters */
	union {
		__le32 e_int_clusters; /* Clusters covered by all children */
		struct {
			__le16 e_leaf_clusters; /* Clusters covered by this
						   extent */
			__u8 e_reserved1;
			__u8 e_flags; /* Extent flags */
		};
	};
	__le64 e_blkno;		/* Physical disk offset, in blocks */
/*10*/
};

struct ocfs2_chain_rec {
	__le32 c_free;	/* Number of free bits in this chain. */
	__le32 c_total;	/* Number of total bits in this chain */
	__le64 c_blkno;	/* Physical disk offset (blocks) of 1st group */
};

struct ocfs2_truncate_rec {
	__le32 t_start;		/* 1st cluster in this log */
	__le32 t_clusters;	/* Number of total clusters covered */
};

/*
 * On disk extent list for OCFS2 (node in the tree).  Note that this
 * is contained inside ocfs2_dinode or ocfs2_extent_block, so the
 * offsets are relative to ocfs2_dinode.id2.i_list or
 * ocfs2_extent_block.h_list, respectively.
 */
struct ocfs2_extent_list {
/*00*/	__le16 l_tree_depth;		/* Extent tree depth from this
					   point.  0 means data extents
					   hang directly off this
					   header (a leaf)
					   NOTE: The high 8 bits cannot be
					   used - tree_depth is never that big.
					*/
	__le16 l_count;			/* Number of extent records */
	__le16 l_next_free_rec;		/* Next unused extent slot */
	__le16 l_reserved1;
	__le64 l_reserved2;		/* Pad to
					   sizeof(ocfs2_extent_rec) */
/*10*/	struct ocfs2_extent_rec l_recs[0];	/* Extent records */
};

/*
 * On disk allocation chain list for OCFS2.  Note that this is
 * contained inside ocfs2_dinode, so the offsets are relative to
 * ocfs2_dinode.id2.i_chain.
 */
struct ocfs2_chain_list {
/*00*/	__le16 cl_cpg;			/* Clusters per Block Group */
	__le16 cl_bpc;			/* Bits per cluster */
	__le16 cl_count;		/* Total chains in this list */
	__le16 cl_next_free_rec;	/* Next unused chain slot */
	__le64 cl_reserved1;
/*10*/	struct ocfs2_chain_rec cl_recs[0];	/* Chain records */
};

/*
 * On disk deallocation log for OCFS2.  Note that this is
 * contained inside ocfs2_dinode, so the offsets are relative to
 * ocfs2_dinode.id2.i_dealloc.
 */
struct ocfs2_truncate_log {
/*00*/	__le16 tl_count;		/* Total records in this log */
	__le16 tl_used;			/* Number of records in use */
	__le32 tl_reserved1;
/*08*/	struct ocfs2_truncate_rec tl_recs[0];	/* Truncate records */
};

/*
 * On disk extent block (indirect block) for OCFS2
 */
struct ocfs2_extent_block
{
/*00*/	__u8 h_signature[8];		/* Signature for verification */
	__le64 h_reserved1;
/*10*/	__le16 h_suballoc_slot;		/* Slot suballocator this
					   extent_header belongs to */
	__le16 h_suballoc_bit;		/* Bit offset in suballocator
					   block group */
	__le32 h_fs_generation;		/* Must match super block */
	__le64 h_blkno;			/* Offset on disk, in blocks */
/*20*/	__le64 h_reserved3;
	__le64 h_next_leaf_blk;		/* Offset on disk, in blocks,
					   of next leaf header pointing
					   to data */
/*30*/	struct ocfs2_extent_list h_list;	/* Extent record list */
/* Actual on-disk size is one block */
};

/*
 * On disk slot map for OCFS2.  This defines the contents of the "slot_map"
 * system file.  A slot is valid if it contains a node number >= 0.  The
 * value -1 (0xFFFF) is OCFS2_INVALID_SLOT.  This marks a slot empty.
 */
struct ocfs2_slot_map {
/*00*/	__le16 sm_slots[0];
/*
 * Actual on-disk size is one block.  OCFS2_MAX_SLOTS is 255,
 * 255 * sizeof(__le16) == 512B, within the 512B block minimum blocksize.
 */
};

struct ocfs2_extended_slot {
/*00*/	__u8	es_valid;
	__u8	es_reserved1[3];
	__le32	es_node_num;
/*10*/
};

/*
 * The extended slot map, used when OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP
 * is set.  It separates out the valid marker from the node number, and
 * has room to grow.  Unlike the old slot map, this format is defined by
 * i_size.
 */
struct ocfs2_slot_map_extended {
/*00*/	struct ocfs2_extended_slot se_slots[0];
/*
 * Actual size is i_size of the slot_map system file.  It should
 * match s_max_slots * sizeof(struct ocfs2_extended_slot)
 */
};

struct ocfs2_cluster_info {
/*00*/	__u8   ci_stack[OCFS2_STACK_LABEL_LEN];
	__le32 ci_reserved;
/*08*/	__u8   ci_cluster[OCFS2_CLUSTER_NAME_LEN];
/*18*/
};

/*
 * On disk superblock for OCFS2
 * Note that it is contained inside an ocfs2_dinode, so all offsets
 * are relative to the start of ocfs2_dinode.id2.
 */
struct ocfs2_super_block {
/*00*/	__le16 s_major_rev_level;
	__le16 s_minor_rev_level;
	__le16 s_mnt_count;
	__le16 s_max_mnt_count;
	__le16 s_state;			/* File system state */
	__le16 s_errors;			/* Behaviour when detecting errors */
	__le32 s_checkinterval;		/* Max time between checks */
/*10*/	__le64 s_lastcheck;		/* Time of last check */
	__le32 s_creator_os;		/* OS */
	__le32 s_feature_compat;		/* Compatible feature set */
/*20*/	__le32 s_feature_incompat;	/* Incompatible feature set */
	__le32 s_feature_ro_compat;	/* Readonly-compatible feature set */
	__le64 s_root_blkno;		/* Offset, in blocks, of root directory
					   dinode */
/*30*/	__le64 s_system_dir_blkno;	/* Offset, in blocks, of system
					   directory dinode */
	__le32 s_blocksize_bits;		/* Blocksize for this fs */
	__le32 s_clustersize_bits;	/* Clustersize for this fs */
/*40*/	__le16 s_max_slots;		/* Max number of simultaneous mounts
					   before tunefs required */
	__le16 s_tunefs_flag;
	__le32 s_reserved1;
	__le64 s_first_cluster_group;	/* Block offset of 1st cluster
					 * group header */
/*50*/	__u8  s_label[OCFS2_MAX_VOL_LABEL_LEN];	/* Label for mounting, etc. */
/*90*/	__u8  s_uuid[OCFS2_VOL_UUID_LEN];	/* 128-bit uuid */
/*A0*/  struct ocfs2_cluster_info s_cluster_info; /* Selected userspace
						     stack.  Only valid
						     with INCOMPAT flag. */
/*B8*/  __le64 s_reserved2[17];		/* Fill out superblock */
/*140*/

	/*
	 * NOTE: As stated above, all offsets are relative to
	 * ocfs2_dinode.id2, which is at 0xC0 in the inode.
	 * 0xC0 + 0x140 = 0x200 or 512 bytes.  A superblock must fit within
	 * our smallest blocksize, which is 512 bytes.  To ensure this,
	 * we reserve the space in s_reserved2.  Anything past s_reserved2
	 * will not be available on the smallest blocksize.
	 */
};

/*
 * Local allocation bitmap for OCFS2 slots
 * Note that it exists inside an ocfs2_dinode, so all offsets are
 * relative to the start of ocfs2_dinode.id2.
 */
struct ocfs2_local_alloc
{
/*00*/	__le32 la_bm_off;	/* Starting bit offset in main bitmap */
	__le16 la_size;		/* Size of included bitmap, in bytes */
	__le16 la_reserved1;
	__le64 la_reserved2;
/*10*/	__u8   la_bitmap[0];
};

/*
 * Data-in-inode header. This is only used if i_dyn_features has
 * OCFS2_INLINE_DATA_FL set.
 */
struct ocfs2_inline_data
{
/*00*/	__le16	id_count;	/* Number of bytes that can be used
				 * for data, starting at id_data */
	__le16	id_reserved0;
	__le32	id_reserved1;
	__u8	id_data[0];	/* Start of user data */
};

/*
 * On disk inode for OCFS2
 */
struct ocfs2_dinode {
/*00*/	__u8 i_signature[8];		/* Signature for validation */
	__le32 i_generation;		/* Generation number */
	__le16 i_suballoc_slot;		/* Slot suballocator this inode
					   belongs to */
	__le16 i_suballoc_bit;		/* Bit offset in suballocator
					   block group */
/*10*/	__le32 i_reserved0;
	__le32 i_clusters;		/* Cluster count */
	__le32 i_uid;			/* Owner UID */
	__le32 i_gid;			/* Owning GID */
/*20*/	__le64 i_size;			/* Size in bytes */
	__le16 i_mode;			/* File mode */
	__le16 i_links_count;		/* Links count */
	__le32 i_flags;			/* File flags */
/*30*/	__le64 i_atime;			/* Access time */
	__le64 i_ctime;			/* Creation time */
/*40*/	__le64 i_mtime;			/* Modification time */
	__le64 i_dtime;			/* Deletion time */
/*50*/	__le64 i_blkno;			/* Offset on disk, in blocks */
	__le64 i_last_eb_blk;		/* Pointer to last extent
					   block */
/*60*/	__le32 i_fs_generation;		/* Generation per fs-instance */
	__le32 i_atime_nsec;
	__le32 i_ctime_nsec;
	__le32 i_mtime_nsec;
	__le32 i_attr;
	__le16 i_orphaned_slot;		/* Only valid when OCFS2_ORPHANED_FL
					   was set in i_flags */
	__le16 i_dyn_features;
/*70*/	__le64 i_reserved2[8];
/*B8*/	union {
		__le64 i_pad1;		/* Generic way to refer to this
					   64bit union */
		struct {
			__le64 i_rdev;	/* Device number */
		} dev1;
		struct {		/* Info for bitmap system
					   inodes */
			__le32 i_used;	/* Bits (ie, clusters) used  */
			__le32 i_total;	/* Total bits (clusters)
					   available */
		} bitmap1;
		struct {		/* Info for journal system
					   inodes */
			__le32 ij_flags;	/* Mounted, version, etc. */
			__le32 ij_pad;
		} journal1;
	} id1;				/* Inode type dependant 1 */
/*C0*/	union {
		struct ocfs2_super_block	i_super;
		struct ocfs2_local_alloc	i_lab;
		struct ocfs2_chain_list		i_chain;
		struct ocfs2_extent_list	i_list;
		struct ocfs2_truncate_log	i_dealloc;
		struct ocfs2_inline_data	i_data;
		__u8               		i_symlink[0];
	} id2;
/* Actual on-disk size is one block */
};

/*
 * On-disk directory entry structure for OCFS2
 *
 * Packed as this structure could be accessed unaligned on 64-bit platforms
 */
struct ocfs2_dir_entry {
/*00*/	__le64   inode;                  /* Inode number */
	__le16   rec_len;                /* Directory entry length */
	__u8    name_len;               /* Name length */
	__u8    file_type;
/*0C*/	char    name[OCFS2_MAX_FILENAME_LEN];   /* File name */
/* Actual on-disk length specified by rec_len */
} __attribute__ ((packed));

/*
 * On disk allocator group structure for OCFS2
 */
struct ocfs2_group_desc
{
/*00*/	__u8    bg_signature[8];        /* Signature for validation */
	__le16   bg_size;                /* Size of included bitmap in
					   bytes. */
	__le16   bg_bits;                /* Bits represented by this
					   group. */
	__le16	bg_free_bits_count;     /* Free bits count */
	__le16   bg_chain;               /* What chain I am in. */
/*10*/	__le32   bg_generation;
	__le32	bg_reserved1;
	__le64   bg_next_group;          /* Next group in my list, in
					   blocks */
/*20*/	__le64   bg_parent_dinode;       /* dinode which owns me, in
					   blocks */
	__le64   bg_blkno;               /* Offset on disk, in blocks */
/*30*/	__le64   bg_reserved2[2];
/*40*/	__u8    bg_bitmap[0];
};

#ifdef __KERNEL__
static inline int ocfs2_fast_symlink_chars(struct super_block *sb)
{
	return  sb->s_blocksize -
		 offsetof(struct ocfs2_dinode, id2.i_symlink);
}

static inline int ocfs2_max_inline_data(struct super_block *sb)
{
	return sb->s_blocksize -
		offsetof(struct ocfs2_dinode, id2.i_data.id_data);
}

static inline int ocfs2_extent_recs_per_inode(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_dinode, id2.i_list.l_recs);

	return size / sizeof(struct ocfs2_extent_rec);
}

static inline int ocfs2_chain_recs_per_inode(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_dinode, id2.i_chain.cl_recs);

	return size / sizeof(struct ocfs2_chain_rec);
}

static inline u16 ocfs2_extent_recs_per_eb(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_extent_block, h_list.l_recs);

	return size / sizeof(struct ocfs2_extent_rec);
}

static inline u16 ocfs2_local_alloc_size(struct super_block *sb)
{
	u16 size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_dinode, id2.i_lab.la_bitmap);

	return size;
}

static inline int ocfs2_group_bitmap_size(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_group_desc, bg_bitmap);

	return size;
}

static inline int ocfs2_truncate_recs_per_inode(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_dinode, id2.i_dealloc.tl_recs);

	return size / sizeof(struct ocfs2_truncate_rec);
}

static inline u64 ocfs2_backup_super_blkno(struct super_block *sb, int index)
{
	u64 offset = OCFS2_BACKUP_SB_START;

	if (index >= 0 && index < OCFS2_MAX_BACKUP_SUPERBLOCKS) {
		offset <<= (2 * index);
		offset >>= sb->s_blocksize_bits;
		return offset;
	}

	return 0;

}
#else
static inline int ocfs2_fast_symlink_chars(int blocksize)
{
	return blocksize - offsetof(struct ocfs2_dinode, id2.i_symlink);
}

static inline int ocfs2_max_inline_data(int blocksize)
{
	return blocksize - offsetof(struct ocfs2_dinode, id2.i_data.id_data);
}

static inline int ocfs2_extent_recs_per_inode(int blocksize)
{
	int size;

	size = blocksize -
		offsetof(struct ocfs2_dinode, id2.i_list.l_recs);

	return size / sizeof(struct ocfs2_extent_rec);
}

static inline int ocfs2_chain_recs_per_inode(int blocksize)
{
	int size;

	size = blocksize -
		offsetof(struct ocfs2_dinode, id2.i_chain.cl_recs);

	return size / sizeof(struct ocfs2_chain_rec);
}

static inline int ocfs2_extent_recs_per_eb(int blocksize)
{
	int size;

	size = blocksize -
		offsetof(struct ocfs2_extent_block, h_list.l_recs);

	return size / sizeof(struct ocfs2_extent_rec);
}

static inline int ocfs2_local_alloc_size(int blocksize)
{
	int size;

	size = blocksize -
		offsetof(struct ocfs2_dinode, id2.i_lab.la_bitmap);

	return size;
}

static inline int ocfs2_group_bitmap_size(int blocksize)
{
	int size;

	size = blocksize -
		offsetof(struct ocfs2_group_desc, bg_bitmap);

	return size;
}

static inline int ocfs2_truncate_recs_per_inode(int blocksize)
{
	int size;

	size = blocksize -
		offsetof(struct ocfs2_dinode, id2.i_dealloc.tl_recs);

	return size / sizeof(struct ocfs2_truncate_rec);
}

static inline uint64_t ocfs2_backup_super_blkno(int blocksize, int index)
{
	uint64_t offset = OCFS2_BACKUP_SB_START;

	if (index >= 0 && index < OCFS2_MAX_BACKUP_SUPERBLOCKS) {
		offset <<= (2 * index);
		offset /= blocksize;
		return offset;
	}

	return 0;
}
#endif  /* __KERNEL__ */


static inline int ocfs2_system_inode_is_global(int type)
{
	return ((type >= 0) &&
		(type <= OCFS2_LAST_GLOBAL_SYSTEM_INODE));
}

static inline int ocfs2_sprintf_system_inode_name(char *buf, int len,
						  int type, int slot)
{
	int chars;

        /*
         * Global system inodes can only have one copy.  Everything
         * after OCFS2_LAST_GLOBAL_SYSTEM_INODE in the system inode
         * list has a copy per slot.
         */
	if (type <= OCFS2_LAST_GLOBAL_SYSTEM_INODE)
		chars = snprintf(buf, len,
				 ocfs2_system_inodes[type].si_name);
	else
		chars = snprintf(buf, len,
				 ocfs2_system_inodes[type].si_name,
				 slot);

	return chars;
}

static inline void ocfs2_set_de_type(struct ocfs2_dir_entry *de,
				    umode_t mode)
{
	de->file_type = ocfs2_type_by_mode[(mode & S_IFMT)>>S_SHIFT];
}

#endif  /* _OCFS2_FS_H */

