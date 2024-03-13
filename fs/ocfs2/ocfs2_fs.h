/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ocfs2_fs.h
 *
 * On-disk structures for OCFS2.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef _OCFS2_FS_H
#define _OCFS2_FS_H

#include <linux/magic.h>

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

/* Object signatures */
#define OCFS2_SUPER_BLOCK_SIGNATURE	"OCFSV2"
#define OCFS2_INODE_SIGNATURE		"INODE01"
#define OCFS2_EXTENT_BLOCK_SIGNATURE	"EXBLK01"
#define OCFS2_GROUP_DESC_SIGNATURE      "GROUP01"
#define OCFS2_XATTR_BLOCK_SIGNATURE	"XATTR01"
#define OCFS2_DIR_TRAILER_SIGNATURE	"DIRTRL1"
#define OCFS2_DX_ROOT_SIGNATURE		"DXDIR01"
#define OCFS2_DX_LEAF_SIGNATURE		"DXLEAF1"
#define OCFS2_REFCOUNT_BLOCK_SIGNATURE	"REFCNT1"

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

#define OCFS2_FEATURE_COMPAT_SUPP	(OCFS2_FEATURE_COMPAT_BACKUP_SB	\
					 | OCFS2_FEATURE_COMPAT_JBD2_SB)
#define OCFS2_FEATURE_INCOMPAT_SUPP	(OCFS2_FEATURE_INCOMPAT_LOCAL_MOUNT \
					 | OCFS2_FEATURE_INCOMPAT_SPARSE_ALLOC \
					 | OCFS2_FEATURE_INCOMPAT_INLINE_DATA \
					 | OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP \
					 | OCFS2_FEATURE_INCOMPAT_USERSPACE_STACK \
					 | OCFS2_FEATURE_INCOMPAT_XATTR \
					 | OCFS2_FEATURE_INCOMPAT_META_ECC \
					 | OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS \
					 | OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE \
					 | OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG	\
					 | OCFS2_FEATURE_INCOMPAT_CLUSTERINFO \
					 | OCFS2_FEATURE_INCOMPAT_APPEND_DIO)
#define OCFS2_FEATURE_RO_COMPAT_SUPP	(OCFS2_FEATURE_RO_COMPAT_UNWRITTEN \
					 | OCFS2_FEATURE_RO_COMPAT_USRQUOTA \
					 | OCFS2_FEATURE_RO_COMPAT_GRPQUOTA)

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

/* Support for the extended slot map */
#define OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP 0x100

/* Support for extended attributes */
#define OCFS2_FEATURE_INCOMPAT_XATTR		0x0200

/* Support for indexed directores */
#define OCFS2_FEATURE_INCOMPAT_INDEXED_DIRS	0x0400

/* Metadata checksum and error correction */
#define OCFS2_FEATURE_INCOMPAT_META_ECC		0x0800

/* Refcount tree support */
#define OCFS2_FEATURE_INCOMPAT_REFCOUNT_TREE	0x1000

/* Discontiguous block groups */
#define OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG	0x2000

/*
 * Incompat bit to indicate useable clusterinfo with stackflags for all
 * cluster stacks (userspace adnd o2cb). If this bit is set,
 * INCOMPAT_USERSPACE_STACK becomes superfluous and thus should not be set.
 */
#define OCFS2_FEATURE_INCOMPAT_CLUSTERINFO	0x4000

/*
 * Append Direct IO support
 */
#define OCFS2_FEATURE_INCOMPAT_APPEND_DIO	0x8000

/*
 * backup superblock flag is used to indicate that this volume
 * has backup superblocks.
 */
#define OCFS2_FEATURE_COMPAT_BACKUP_SB		0x0001

/*
 * The filesystem will correctly handle journal feature bits.
 */
#define OCFS2_FEATURE_COMPAT_JBD2_SB		0x0002

/*
 * Unwritten extents support.
 */
#define OCFS2_FEATURE_RO_COMPAT_UNWRITTEN	0x0001

/*
 * Maintain quota information for this filesystem
 */
#define OCFS2_FEATURE_RO_COMPAT_USRQUOTA	0x0002
#define OCFS2_FEATURE_RO_COMPAT_GRPQUOTA	0x0004


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
#define OCFS2_QUOTA_FL		(0x00001000)	/* Quota file */
#define OCFS2_DIO_ORPHANED_FL	(0X00002000)	/* On the orphan list especially
						 * for dio */

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
#define OCFS2_HAS_REFCOUNT_FL   (0x0010)

/* Inode attributes, keep in sync with EXT2 */
#define OCFS2_SECRM_FL			FS_SECRM_FL	/* Secure deletion */
#define OCFS2_UNRM_FL			FS_UNRM_FL	/* Undelete */
#define OCFS2_COMPR_FL			FS_COMPR_FL	/* Compress file */
#define OCFS2_SYNC_FL			FS_SYNC_FL	/* Synchronous updates */
#define OCFS2_IMMUTABLE_FL		FS_IMMUTABLE_FL	/* Immutable file */
#define OCFS2_APPEND_FL			FS_APPEND_FL	/* writes to file may only append */
#define OCFS2_NODUMP_FL			FS_NODUMP_FL	/* do not dump file */
#define OCFS2_NOATIME_FL		FS_NOATIME_FL	/* do not update atime */
/* Reserved for compression usage... */
#define OCFS2_DIRTY_FL			FS_DIRTY_FL
#define OCFS2_COMPRBLK_FL		FS_COMPRBLK_FL	/* One or more compressed clusters */
#define OCFS2_NOCOMP_FL			FS_NOCOMP_FL	/* Don't compress */
#define OCFS2_ECOMPR_FL			FS_ECOMPR_FL	/* Compression error */
/* End compression flags --- maybe not all used */
#define OCFS2_BTREE_FL			FS_BTREE_FL	/* btree format dir */
#define OCFS2_INDEX_FL			FS_INDEX_FL	/* hash-indexed directory */
#define OCFS2_IMAGIC_FL			FS_IMAGIC_FL	/* AFS directory */
#define OCFS2_JOURNAL_DATA_FL		FS_JOURNAL_DATA_FL /* Reserved for ext3 */
#define OCFS2_NOTAIL_FL			FS_NOTAIL_FL	/* file tail should not be merged */
#define OCFS2_DIRSYNC_FL		FS_DIRSYNC_FL	/* dirsync behaviour (directories only) */
#define OCFS2_TOPDIR_FL			FS_TOPDIR_FL	/* Top of directory hierarchies*/
#define OCFS2_RESERVED_FL		FS_RESERVED_FL	/* reserved for ext2 lib */

#define OCFS2_FL_VISIBLE		FS_FL_USER_VISIBLE	/* User visible flags */
#define OCFS2_FL_MODIFIABLE		FS_FL_USER_MODIFIABLE	/* User modifiable flags */

/*
 * Extent record flags (e_node.leaf.flags)
 */
#define OCFS2_EXT_UNWRITTEN		(0x01)	/* Extent is allocated but
						 * unwritten */
#define OCFS2_EXT_REFCOUNTED		(0x02)  /* Extent is reference
						 * counted in an associated
						 * refcount tree */

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
#define OCFS2_INVALID_SLOT		((u16)-1)

#define OCFS2_VOL_UUID_LEN		16
#define OCFS2_MAX_VOL_LABEL_LEN		64

/* The cluster stack fields */
#define OCFS2_STACK_LABEL_LEN		4
#define OCFS2_CLUSTER_NAME_LEN		16

/* Classic (historically speaking) cluster stack */
#define OCFS2_CLASSIC_CLUSTER_STACK	"o2cb"

/* Journal limits (in bytes) */
#define OCFS2_MIN_JOURNAL_SIZE		(4 * 1024 * 1024)

/*
 * Inline extended attribute size (in bytes)
 * The value chosen should be aligned to 16 byte boundaries.
 */
#define OCFS2_MIN_XATTR_INLINE_SIZE     256

/*
 * Cluster info flags (ocfs2_cluster_info.ci_stackflags)
 */
#define OCFS2_CLUSTER_O2CB_GLOBAL_HEARTBEAT	(0x01)

struct ocfs2_system_inode_info {
	char	*si_name;
	int	si_iflags;
	int	si_mode;
};

/* System file index */
enum {
	BAD_BLOCK_SYSTEM_INODE = 0,
	GLOBAL_INODE_ALLOC_SYSTEM_INODE,
#define OCFS2_FIRST_ONLINE_SYSTEM_INODE GLOBAL_INODE_ALLOC_SYSTEM_INODE
	SLOT_MAP_SYSTEM_INODE,
	HEARTBEAT_SYSTEM_INODE,
	GLOBAL_BITMAP_SYSTEM_INODE,
	USER_QUOTA_SYSTEM_INODE,
	GROUP_QUOTA_SYSTEM_INODE,
#define OCFS2_LAST_GLOBAL_SYSTEM_INODE GROUP_QUOTA_SYSTEM_INODE
#define OCFS2_FIRST_LOCAL_SYSTEM_INODE ORPHAN_DIR_SYSTEM_INODE
	ORPHAN_DIR_SYSTEM_INODE,
	EXTENT_ALLOC_SYSTEM_INODE,
	INODE_ALLOC_SYSTEM_INODE,
	JOURNAL_SYSTEM_INODE,
	LOCAL_ALLOC_SYSTEM_INODE,
	TRUNCATE_LOG_SYSTEM_INODE,
	LOCAL_USER_QUOTA_SYSTEM_INODE,
	LOCAL_GROUP_QUOTA_SYSTEM_INODE,
#define OCFS2_LAST_LOCAL_SYSTEM_INODE LOCAL_GROUP_QUOTA_SYSTEM_INODE
	NUM_SYSTEM_INODES
};
#define NUM_GLOBAL_SYSTEM_INODES OCFS2_FIRST_LOCAL_SYSTEM_INODE
#define NUM_LOCAL_SYSTEM_INODES	\
		(NUM_SYSTEM_INODES - OCFS2_FIRST_LOCAL_SYSTEM_INODE)

static struct ocfs2_system_inode_info ocfs2_system_inodes[NUM_SYSTEM_INODES] = {
	/* Global system inodes (single copy) */
	/* The first two are only used from userspace mfks/tunefs */
	[BAD_BLOCK_SYSTEM_INODE]		= { "bad_blocks", 0, S_IFREG | 0644 },
	[GLOBAL_INODE_ALLOC_SYSTEM_INODE] 	= { "global_inode_alloc", OCFS2_BITMAP_FL | OCFS2_CHAIN_FL, S_IFREG | 0644 },

	/* These are used by the running filesystem */
	[SLOT_MAP_SYSTEM_INODE]			= { "slot_map", 0, S_IFREG | 0644 },
	[HEARTBEAT_SYSTEM_INODE]		= { "heartbeat", OCFS2_HEARTBEAT_FL, S_IFREG | 0644 },
	[GLOBAL_BITMAP_SYSTEM_INODE]		= { "global_bitmap", 0, S_IFREG | 0644 },
	[USER_QUOTA_SYSTEM_INODE]		= { "aquota.user", OCFS2_QUOTA_FL, S_IFREG | 0644 },
	[GROUP_QUOTA_SYSTEM_INODE]		= { "aquota.group", OCFS2_QUOTA_FL, S_IFREG | 0644 },

	/* Slot-specific system inodes (one copy per slot) */
	[ORPHAN_DIR_SYSTEM_INODE]		= { "orphan_dir:%04d", 0, S_IFDIR | 0755 },
	[EXTENT_ALLOC_SYSTEM_INODE]		= { "extent_alloc:%04d", OCFS2_BITMAP_FL | OCFS2_CHAIN_FL, S_IFREG | 0644 },
	[INODE_ALLOC_SYSTEM_INODE]		= { "inode_alloc:%04d", OCFS2_BITMAP_FL | OCFS2_CHAIN_FL, S_IFREG | 0644 },
	[JOURNAL_SYSTEM_INODE]			= { "journal:%04d", OCFS2_JOURNAL_FL, S_IFREG | 0644 },
	[LOCAL_ALLOC_SYSTEM_INODE]		= { "local_alloc:%04d", OCFS2_BITMAP_FL | OCFS2_LOCAL_ALLOC_FL, S_IFREG | 0644 },
	[TRUNCATE_LOG_SYSTEM_INODE]		= { "truncate_log:%04d", OCFS2_DEALLOC_FL, S_IFREG | 0644 },
	[LOCAL_USER_QUOTA_SYSTEM_INODE]		= { "aquota.user:%04d", OCFS2_QUOTA_FL, S_IFREG | 0644 },
	[LOCAL_GROUP_QUOTA_SYSTEM_INODE]	= { "aquota.group:%04d", OCFS2_QUOTA_FL, S_IFREG | 0644 },
};

/* Parameter passed from mount.ocfs2 to module */
#define OCFS2_HB_NONE			"heartbeat=none"
#define OCFS2_HB_LOCAL			"heartbeat=local"
#define OCFS2_HB_GLOBAL			"heartbeat=global"

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
#define OCFS2_DIR_MIN_REC_LEN	OCFS2_DIR_REC_LEN(1)

#define OCFS2_LINK_MAX		32000
#define	OCFS2_DX_LINK_MAX	((1U << 31) - 1U)
#define	OCFS2_LINKS_HI_SHIFT	16
#define	OCFS2_DX_ENTRIES_MAX	(0xffffffffU)


/*
 * Convenience casts
 */
#define OCFS2_RAW_SB(dinode)		(&((dinode)->id2.i_super))

/*
 * Block checking structure.  This is used in metadata to validate the
 * contents.  If OCFS2_FEATURE_INCOMPAT_META_ECC is not set, it is all
 * zeros.
 */
struct ocfs2_block_check {
/*00*/	__le32 bc_crc32e;	/* 802.3 Ethernet II CRC32 */
	__le16 bc_ecc;		/* Single-error-correction parity vector.
				   This is a simple Hamming code dependent
				   on the blocksize.  OCFS2's maximum
				   blocksize, 4K, requires 16 parity bits,
				   so we fit in __le16. */
	__le16 bc_reserved1;
/*08*/
};

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
/*10*/	struct ocfs2_extent_rec l_recs[];	/* Extent records */
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
/*10*/	struct ocfs2_chain_rec cl_recs[];	/* Chain records */
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
/*08*/	struct ocfs2_truncate_rec tl_recs[];	/* Truncate records */
};

/*
 * On disk extent block (indirect block) for OCFS2
 */
struct ocfs2_extent_block
{
/*00*/	__u8 h_signature[8];		/* Signature for verification */
	struct ocfs2_block_check h_check;	/* Error checking */
/*10*/	__le16 h_suballoc_slot;		/* Slot suballocator this
					   extent_header belongs to */
	__le16 h_suballoc_bit;		/* Bit offset in suballocator
					   block group */
	__le32 h_fs_generation;		/* Must match super block */
	__le64 h_blkno;			/* Offset on disk, in blocks */
/*20*/	__le64 h_suballoc_loc;		/* Suballocator block group this
					   eb belongs to.  Only valid
					   if allocated from a
					   discontiguous block group */
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
/*00*/	DECLARE_FLEX_ARRAY(__le16, sm_slots);
/*
 * Actual on-disk size is one block.  OCFS2_MAX_SLOTS is 255,
 * 255 * sizeof(__le16) == 512B, within the 512B block minimum blocksize.
 */
};

struct ocfs2_extended_slot {
/*00*/	__u8	es_valid;
	__u8	es_reserved1[3];
	__le32	es_node_num;
/*08*/
};

/*
 * The extended slot map, used when OCFS2_FEATURE_INCOMPAT_EXTENDED_SLOT_MAP
 * is set.  It separates out the valid marker from the node number, and
 * has room to grow.  Unlike the old slot map, this format is defined by
 * i_size.
 */
struct ocfs2_slot_map_extended {
/*00*/	DECLARE_FLEX_ARRAY(struct ocfs2_extended_slot, se_slots);
/*
 * Actual size is i_size of the slot_map system file.  It should
 * match s_max_slots * sizeof(struct ocfs2_extended_slot)
 */
};

/*
 * ci_stackflags is only valid if the incompat bit
 * OCFS2_FEATURE_INCOMPAT_CLUSTERINFO is set.
 */
struct ocfs2_cluster_info {
/*00*/	__u8   ci_stack[OCFS2_STACK_LABEL_LEN];
	union {
		__le32 ci_reserved;
		struct {
			__u8 ci_stackflags;
			__u8 ci_reserved1;
			__u8 ci_reserved2;
			__u8 ci_reserved3;
		};
	};
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
	__le32 s_uuid_hash;		/* hash value of uuid */
	__le64 s_first_cluster_group;	/* Block offset of 1st cluster
					 * group header */
/*50*/	__u8  s_label[OCFS2_MAX_VOL_LABEL_LEN];	/* Label for mounting, etc. */
/*90*/	__u8  s_uuid[OCFS2_VOL_UUID_LEN];	/* 128-bit uuid */
/*A0*/  struct ocfs2_cluster_info s_cluster_info; /* Only valid if either
						     userspace or clusterinfo
						     INCOMPAT flag set. */
/*B8*/	__le16 s_xattr_inline_size;	/* extended attribute inline size
					   for this fs*/
	__le16 s_reserved0;
	__le32 s_dx_seed[3];		/* seed[0-2] for dx dir hash.
					 * s_uuid_hash serves as seed[3]. */
/*C0*/  __le64 s_reserved2[15];		/* Fill out superblock */
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
/*10*/	__u8   la_bitmap[];
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
	__u8	id_data[];	/* Start of user data */
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
/*10*/	__le16 i_links_count_hi;	/* High 16 bits of links count */
	__le16 i_xattr_inline_size;
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
/*70*/	__le32 i_attr;
	__le16 i_orphaned_slot;		/* Only valid when OCFS2_ORPHANED_FL
					   was set in i_flags */
	__le16 i_dyn_features;
	__le64 i_xattr_loc;
/*80*/	struct ocfs2_block_check i_check;	/* Error checking */
/*88*/	__le64 i_dx_root;		/* Pointer to dir index root block */
/*90*/	__le64 i_refcount_loc;
	__le64 i_suballoc_loc;		/* Suballocator block group this
					   inode belongs to.  Only valid
					   if allocated from a
					   discontiguous block group */
/*A0*/	__le16 i_dio_orphaned_slot;	/* only used for append dio write */
	__le16 i_reserved1[3];
	__le64 i_reserved2[2];
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
			__le32 ij_recovery_generation; /* Incremented when the
							  journal is recovered
							  after an unclean
							  shutdown */
		} journal1;
	} id1;				/* Inode type dependent 1 */
/*C0*/	union {
		struct ocfs2_super_block	i_super;
		struct ocfs2_local_alloc	i_lab;
		struct ocfs2_chain_list		i_chain;
		struct ocfs2_extent_list	i_list;
		struct ocfs2_truncate_log	i_dealloc;
		struct ocfs2_inline_data	i_data;
		DECLARE_FLEX_ARRAY(__u8,	i_symlink);
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
 * Per-block record for the unindexed directory btree. This is carefully
 * crafted so that the rec_len and name_len records of an ocfs2_dir_entry are
 * mirrored. That way, the directory manipulation code needs a minimal amount
 * of update.
 *
 * NOTE: Keep this structure aligned to a multiple of 4 bytes.
 */
struct ocfs2_dir_block_trailer {
/*00*/	__le64		db_compat_inode;	/* Always zero. Was inode */

	__le16		db_compat_rec_len;	/* Backwards compatible with
						 * ocfs2_dir_entry. */
	__u8		db_compat_name_len;	/* Always zero. Was name_len */
	__u8		db_reserved0;
	__le16		db_reserved1;
	__le16		db_free_rec_len;	/* Size of largest empty hole
						 * in this block. (unused) */
/*10*/	__u8		db_signature[8];	/* Signature for verification */
	__le64		db_reserved2;
/*20*/	__le64		db_free_next;		/* Next block in list (unused) */
	__le64		db_blkno;		/* Offset on disk, in blocks */
/*30*/	__le64		db_parent_dinode;	/* dinode which owns me, in
						   blocks */
	struct ocfs2_block_check db_check;	/* Error checking */
/*40*/
};

 /*
 * A directory entry in the indexed tree. We don't store the full name here,
 * but instead provide a pointer to the full dirent in the unindexed tree.
 *
 * We also store name_len here so as to reduce the number of leaf blocks we
 * need to search in case of collisions.
 */
struct ocfs2_dx_entry {
	__le32		dx_major_hash;	/* Used to find logical
					 * cluster in index */
	__le32		dx_minor_hash;	/* Lower bits used to find
					 * block in cluster */
	__le64		dx_dirent_blk;	/* Physical block in unindexed
					 * tree holding this dirent. */
};

struct ocfs2_dx_entry_list {
	__le32		de_reserved;
	__le16		de_count;	/* Maximum number of entries
					 * possible in de_entries */
	__le16		de_num_used;	/* Current number of
					 * de_entries entries */
	struct	ocfs2_dx_entry		de_entries[];	/* Indexed dir entries
							 * in a packed array of
							 * length de_num_used */
};

#define OCFS2_DX_FLAG_INLINE	0x01

/*
 * A directory indexing block. Each indexed directory has one of these,
 * pointed to by ocfs2_dinode.
 *
 * This block stores an indexed btree root, and a set of free space
 * start-of-list pointers.
 */
struct ocfs2_dx_root_block {
	__u8		dr_signature[8];	/* Signature for verification */
	struct ocfs2_block_check dr_check;	/* Error checking */
	__le16		dr_suballoc_slot;	/* Slot suballocator this
						 * block belongs to. */
	__le16		dr_suballoc_bit;	/* Bit offset in suballocator
						 * block group */
	__le32		dr_fs_generation;	/* Must match super block */
	__le64		dr_blkno;		/* Offset on disk, in blocks */
	__le64		dr_last_eb_blk;		/* Pointer to last
						 * extent block */
	__le32		dr_clusters;		/* Clusters allocated
						 * to the indexed tree. */
	__u8		dr_flags;		/* OCFS2_DX_FLAG_* flags */
	__u8		dr_reserved0;
	__le16		dr_reserved1;
	__le64		dr_dir_blkno;		/* Pointer to parent inode */
	__le32		dr_num_entries;		/* Total number of
						 * names stored in
						 * this directory.*/
	__le32		dr_reserved2;
	__le64		dr_free_blk;		/* Pointer to head of free
						 * unindexed block list. */
	__le64		dr_suballoc_loc;	/* Suballocator block group
						   this root belongs to.
						   Only valid if allocated
						   from a discontiguous
						   block group */
	__le64		dr_reserved3[14];
	union {
		struct ocfs2_extent_list dr_list; /* Keep this aligned to 128
						   * bits for maximum space
						   * efficiency. */
		struct ocfs2_dx_entry_list dr_entries; /* In-root-block list of
							* entries. We grow out
							* to extents if this
							* gets too big. */
	};
};

/*
 * The header of a leaf block in the indexed tree.
 */
struct ocfs2_dx_leaf {
	__u8		dl_signature[8];/* Signature for verification */
	struct ocfs2_block_check dl_check;	/* Error checking */
	__le64		dl_blkno;	/* Offset on disk, in blocks */
	__le32		dl_fs_generation;/* Must match super block */
	__le32		dl_reserved0;
	__le64		dl_reserved1;
	struct ocfs2_dx_entry_list	dl_list;
};

/*
 * Largest bitmap for a block (suballocator) group in bytes.  This limit
 * does not affect cluster groups (global allocator).  Cluster group
 * bitmaps run to the end of the block.
 */
#define OCFS2_MAX_BG_BITMAP_SIZE	256

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
/*30*/	struct ocfs2_block_check bg_check;	/* Error checking */
	__le64   bg_reserved2;
/*40*/	union {
		DECLARE_FLEX_ARRAY(__u8, bg_bitmap);
		struct {
			/*
			 * Block groups may be discontiguous when
			 * OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG is set.
			 * The extents of a discontiguous block group are
			 * stored in bg_list.  It is a flat list.
			 * l_tree_depth must always be zero.  A
			 * discontiguous group is signified by a non-zero
			 * bg_list->l_next_free_rec.  Only block groups
			 * can be discontiguous; Cluster groups cannot.
			 * We've never made a block group with more than
			 * 2048 blocks (256 bytes of bg_bitmap).  This
			 * codifies that limit so that we can fit bg_list.
			 * bg_size of a discontiguous block group will
			 * be 256 to match bg_bitmap_filler.
			 */
			__u8 bg_bitmap_filler[OCFS2_MAX_BG_BITMAP_SIZE];
/*140*/			struct ocfs2_extent_list bg_list;
		};
	};
/* Actual on-disk size is one block */
};

struct ocfs2_refcount_rec {
/*00*/	__le64 r_cpos;		/* Physical offset, in clusters */
	__le32 r_clusters;	/* Clusters covered by this extent */
	__le32 r_refcount;	/* Reference count of this extent */
/*10*/
};
#define OCFS2_32BIT_POS_MASK		(0xffffffffULL)

#define OCFS2_REFCOUNT_LEAF_FL          (0x00000001)
#define OCFS2_REFCOUNT_TREE_FL          (0x00000002)

struct ocfs2_refcount_list {
/*00*/	__le16 rl_count;	/* Maximum number of entries possible
				   in rl_records */
	__le16 rl_used;		/* Current number of used records */
	__le32 rl_reserved2;
	__le64 rl_reserved1;	/* Pad to sizeof(ocfs2_refcount_record) */
/*10*/	struct ocfs2_refcount_rec rl_recs[];	/* Refcount records */
};


struct ocfs2_refcount_block {
/*00*/	__u8 rf_signature[8];		/* Signature for verification */
	__le16 rf_suballoc_slot;	/* Slot suballocator this block
					   belongs to */
	__le16 rf_suballoc_bit;		/* Bit offset in suballocator
					   block group */
	__le32 rf_fs_generation;	/* Must match superblock */
/*10*/	__le64 rf_blkno;		/* Offset on disk, in blocks */
	__le64 rf_parent;		/* Parent block, only valid if
					   OCFS2_REFCOUNT_LEAF_FL is set in
					   rf_flags */
/*20*/	struct ocfs2_block_check rf_check;	/* Error checking */
	__le64 rf_last_eb_blk;		/* Pointer to last extent block */
/*30*/	__le32 rf_count;		/* Number of inodes sharing this
					   refcount tree */
	__le32 rf_flags;		/* See the flags above */
	__le32 rf_clusters;		/* clusters covered by refcount tree. */
	__le32 rf_cpos;			/* cluster offset in refcount tree.*/
/*40*/	__le32 rf_generation;		/* generation number. all be the same
					 * for the same refcount tree. */
	__le32 rf_reserved0;
	__le64 rf_suballoc_loc;		/* Suballocator block group this
					   refcount block belongs to. Only
					   valid if allocated from a
					   discontiguous block group */
/*50*/	__le64 rf_reserved1[6];
/*80*/	union {
		struct ocfs2_refcount_list rf_records;  /* List of refcount
							  records */
		struct ocfs2_extent_list rf_list;	/* Extent record list,
							only valid if
							OCFS2_REFCOUNT_TREE_FL
							is set in rf_flags */
	};
/* Actual on-disk size is one block */
};

/*
 * On disk extended attribute structure for OCFS2.
 */

/*
 * ocfs2_xattr_entry indicates one extend attribute.
 *
 * Note that it can be stored in inode, one block or one xattr bucket.
 */
struct ocfs2_xattr_entry {
	__le32	xe_name_hash;    /* hash value of xattr prefix+suffix. */
	__le16	xe_name_offset;  /* byte offset from the 1st entry in the
				    local xattr storage(inode, xattr block or
				    xattr bucket). */
	__u8	xe_name_len;	 /* xattr name len, doesn't include prefix. */
	__u8	xe_type;         /* the low 7 bits indicate the name prefix
				  * type and the highest bit indicates whether
				  * the EA is stored in the local storage. */
	__le64	xe_value_size;	 /* real xattr value length. */
};

/*
 * On disk structure for xattr header.
 *
 * One ocfs2_xattr_header describes how many ocfs2_xattr_entry records in
 * the local xattr storage.
 */
struct ocfs2_xattr_header {
	__le16	xh_count;                       /* contains the count of how
						   many records are in the
						   local xattr storage. */
	__le16	xh_free_start;                  /* current offset for storing
						   xattr. */
	__le16	xh_name_value_len;              /* total length of name/value
						   length in this bucket. */
	__le16	xh_num_buckets;                 /* Number of xattr buckets
						   in this extent record,
						   only valid in the first
						   bucket. */
	struct ocfs2_block_check xh_check;	/* Error checking
						   (Note, this is only
						    used for xattr
						    buckets.  A block uses
						    xb_check and sets
						    this field to zero.) */
	struct ocfs2_xattr_entry xh_entries[]; /* xattr entry list. */
};

/*
 * On disk structure for xattr value root.
 *
 * When an xattr's value is large enough, it is stored in an external
 * b-tree like file data.  The xattr value root points to this structure.
 */
struct ocfs2_xattr_value_root {
/*00*/	__le32	xr_clusters;              /* clusters covered by xattr value. */
	__le32	xr_reserved0;
	__le64	xr_last_eb_blk;           /* Pointer to last extent block */
/*10*/	struct ocfs2_extent_list xr_list; /* Extent record list */
};

/*
 * On disk structure for xattr tree root.
 *
 * It is used when there are too many extended attributes for one file. These
 * attributes will be organized and stored in an indexed-btree.
 */
struct ocfs2_xattr_tree_root {
/*00*/	__le32	xt_clusters;              /* clusters covered by xattr. */
	__le32	xt_reserved0;
	__le64	xt_last_eb_blk;           /* Pointer to last extent block */
/*10*/	struct ocfs2_extent_list xt_list; /* Extent record list */
};

#define OCFS2_XATTR_INDEXED	0x1
#define OCFS2_HASH_SHIFT	5
#define OCFS2_XATTR_ROUND	3
#define OCFS2_XATTR_SIZE(size)	(((size) + OCFS2_XATTR_ROUND) & \
				~(OCFS2_XATTR_ROUND))

#define OCFS2_XATTR_BUCKET_SIZE			4096
#define OCFS2_XATTR_MAX_BLOCKS_PER_BUCKET 	(OCFS2_XATTR_BUCKET_SIZE \
						 / OCFS2_MIN_BLOCKSIZE)

/*
 * On disk structure for xattr block.
 */
struct ocfs2_xattr_block {
/*00*/	__u8	xb_signature[8];     /* Signature for verification */
	__le16	xb_suballoc_slot;    /* Slot suballocator this
					block belongs to. */
	__le16	xb_suballoc_bit;     /* Bit offset in suballocator
					block group */
	__le32	xb_fs_generation;    /* Must match super block */
/*10*/	__le64	xb_blkno;            /* Offset on disk, in blocks */
	struct ocfs2_block_check xb_check;	/* Error checking */
/*20*/	__le16	xb_flags;            /* Indicates whether this block contains
					real xattr or a xattr tree. */
	__le16	xb_reserved0;
	__le32  xb_reserved1;
	__le64	xb_suballoc_loc;	/* Suballocator block group this
					   xattr block belongs to. Only
					   valid if allocated from a
					   discontiguous block group */
/*30*/	union {
		struct ocfs2_xattr_header xb_header; /* xattr header if this
							block contains xattr */
		struct ocfs2_xattr_tree_root xb_root;/* xattr tree root if this
							block cotains xattr
							tree. */
	} xb_attrs;
};

#define OCFS2_XATTR_ENTRY_LOCAL		0x80
#define OCFS2_XATTR_TYPE_MASK		0x7F
static inline void ocfs2_xattr_set_local(struct ocfs2_xattr_entry *xe,
					 int local)
{
	if (local)
		xe->xe_type |= OCFS2_XATTR_ENTRY_LOCAL;
	else
		xe->xe_type &= ~OCFS2_XATTR_ENTRY_LOCAL;
}

static inline int ocfs2_xattr_is_local(struct ocfs2_xattr_entry *xe)
{
	return xe->xe_type & OCFS2_XATTR_ENTRY_LOCAL;
}

static inline void ocfs2_xattr_set_type(struct ocfs2_xattr_entry *xe, int type)
{
	xe->xe_type |= type & OCFS2_XATTR_TYPE_MASK;
}

static inline int ocfs2_xattr_get_type(struct ocfs2_xattr_entry *xe)
{
	return xe->xe_type & OCFS2_XATTR_TYPE_MASK;
}

/*
 *  On disk structures for global quota file
 */

/* Magic numbers and known versions for global quota files */
#define OCFS2_GLOBAL_QMAGICS {\
	0x0cf52470, /* USRQUOTA */ \
	0x0cf52471  /* GRPQUOTA */ \
}

#define OCFS2_GLOBAL_QVERSIONS {\
	0, \
	0, \
}


/* Each block of each quota file has a certain fixed number of bytes reserved
 * for OCFS2 internal use at its end. OCFS2 can use it for things like
 * checksums, etc. */
#define OCFS2_QBLK_RESERVED_SPACE 8

/* Generic header of all quota files */
struct ocfs2_disk_dqheader {
	__le32 dqh_magic;	/* Magic number identifying file */
	__le32 dqh_version;	/* Quota format version */
};

#define OCFS2_GLOBAL_INFO_OFF (sizeof(struct ocfs2_disk_dqheader))

/* Information header of global quota file (immediately follows the generic
 * header) */
struct ocfs2_global_disk_dqinfo {
/*00*/	__le32 dqi_bgrace;	/* Grace time for space softlimit excess */
	__le32 dqi_igrace;	/* Grace time for inode softlimit excess */
	__le32 dqi_syncms;	/* Time after which we sync local changes to
				 * global quota file */
	__le32 dqi_blocks;	/* Number of blocks in quota file */
/*10*/	__le32 dqi_free_blk;	/* First free block in quota file */
	__le32 dqi_free_entry;	/* First block with free dquot entry in quota
				 * file */
};

/* Structure with global user / group information. We reserve some space
 * for future use. */
struct ocfs2_global_disk_dqblk {
/*00*/	__le32 dqb_id;          /* ID the structure belongs to */
	__le32 dqb_use_count;   /* Number of nodes having reference to this structure */
	__le64 dqb_ihardlimit;  /* absolute limit on allocated inodes */
/*10*/	__le64 dqb_isoftlimit;  /* preferred inode limit */
	__le64 dqb_curinodes;   /* current # allocated inodes */
/*20*/	__le64 dqb_bhardlimit;  /* absolute limit on disk space */
	__le64 dqb_bsoftlimit;  /* preferred limit on disk space */
/*30*/	__le64 dqb_curspace;    /* current space occupied */
	__le64 dqb_btime;       /* time limit for excessive disk use */
/*40*/	__le64 dqb_itime;       /* time limit for excessive inode use */
	__le64 dqb_pad1;
/*50*/	__le64 dqb_pad2;
};

/*
 *  On-disk structures for local quota file
 */

/* Magic numbers and known versions for local quota files */
#define OCFS2_LOCAL_QMAGICS {\
	0x0cf524c0, /* USRQUOTA */ \
	0x0cf524c1  /* GRPQUOTA */ \
}

#define OCFS2_LOCAL_QVERSIONS {\
	0, \
	0, \
}

/* Quota flags in dqinfo header */
#define OLQF_CLEAN	0x0001	/* Quota file is empty (this should be after\
				 * quota has been cleanly turned off) */

#define OCFS2_LOCAL_INFO_OFF (sizeof(struct ocfs2_disk_dqheader))

/* Information header of local quota file (immediately follows the generic
 * header) */
struct ocfs2_local_disk_dqinfo {
	__le32 dqi_flags;	/* Flags for quota file */
	__le32 dqi_chunks;	/* Number of chunks of quota structures
				 * with a bitmap */
	__le32 dqi_blocks;	/* Number of blocks allocated for quota file */
};

/* Header of one chunk of a quota file */
struct ocfs2_local_disk_chunk {
	__le32 dqc_free;	/* Number of free entries in the bitmap */
	__u8 dqc_bitmap[];	/* Bitmap of entries in the corresponding
				 * chunk of quota file */
};

/* One entry in local quota file */
struct ocfs2_local_disk_dqblk {
/*00*/	__le64 dqb_id;		/* id this quota applies to */
	__le64 dqb_spacemod;	/* Change in the amount of used space */
/*10*/	__le64 dqb_inodemod;	/* Change in the amount of used inodes */
};


/*
 * The quota trailer lives at the end of each quota block.
 */

struct ocfs2_disk_dqtrailer {
/*00*/	struct ocfs2_block_check dq_check;	/* Error checking */
/*08*/	/* Cannot be larger than OCFS2_QBLK_RESERVED_SPACE */
};

static inline struct ocfs2_disk_dqtrailer *ocfs2_block_dqtrailer(int blocksize,
								 void *buf)
{
	char *ptr = buf;
	ptr += blocksize - OCFS2_QBLK_RESERVED_SPACE;

	return (struct ocfs2_disk_dqtrailer *)ptr;
}

#ifdef __KERNEL__
static inline int ocfs2_fast_symlink_chars(struct super_block *sb)
{
	return  sb->s_blocksize -
		 offsetof(struct ocfs2_dinode, id2.i_symlink);
}

static inline int ocfs2_max_inline_data_with_xattr(struct super_block *sb,
						   struct ocfs2_dinode *di)
{
	unsigned int xattrsize = le16_to_cpu(di->i_xattr_inline_size);

	if (le16_to_cpu(di->i_dyn_features) & OCFS2_INLINE_XATTR_FL)
		return sb->s_blocksize -
			offsetof(struct ocfs2_dinode, id2.i_data.id_data) -
			xattrsize;
	else
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

static inline int ocfs2_extent_recs_per_inode_with_xattr(
						struct super_block *sb,
						struct ocfs2_dinode *di)
{
	int size;
	unsigned int xattrsize = le16_to_cpu(di->i_xattr_inline_size);

	if (le16_to_cpu(di->i_dyn_features) & OCFS2_INLINE_XATTR_FL)
		size = sb->s_blocksize -
			offsetof(struct ocfs2_dinode, id2.i_list.l_recs) -
			xattrsize;
	else
		size = sb->s_blocksize -
			offsetof(struct ocfs2_dinode, id2.i_list.l_recs);

	return size / sizeof(struct ocfs2_extent_rec);
}

static inline int ocfs2_extent_recs_per_dx_root(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_dx_root_block, dr_list.l_recs);

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

static inline u16 ocfs2_extent_recs_per_gd(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_group_desc, bg_list.l_recs);

	return size / sizeof(struct ocfs2_extent_rec);
}

static inline int ocfs2_dx_entries_per_leaf(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_dx_leaf, dl_list.de_entries);

	return size / sizeof(struct ocfs2_dx_entry);
}

static inline int ocfs2_dx_entries_per_root(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_dx_root_block, dr_entries.de_entries);

	return size / sizeof(struct ocfs2_dx_entry);
}

static inline u16 ocfs2_local_alloc_size(struct super_block *sb)
{
	u16 size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_dinode, id2.i_lab.la_bitmap);

	return size;
}

static inline int ocfs2_group_bitmap_size(struct super_block *sb,
					  int suballocator,
					  u32 feature_incompat)
{
	int size = sb->s_blocksize -
		offsetof(struct ocfs2_group_desc, bg_bitmap);

	/*
	 * The cluster allocator uses the entire block.  Suballocators have
	 * never used more than OCFS2_MAX_BG_BITMAP_SIZE.  Unfortunately, older
	 * code expects bg_size set to the maximum.  Thus we must keep
	 * bg_size as-is unless discontig_bg is enabled.
	 */
	if (suballocator &&
	    (feature_incompat & OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG))
		size = OCFS2_MAX_BG_BITMAP_SIZE;

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

static inline u16 ocfs2_xattr_recs_per_xb(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_xattr_block,
			 xb_attrs.xb_root.xt_list.l_recs);

	return size / sizeof(struct ocfs2_extent_rec);
}

static inline u16 ocfs2_extent_recs_per_rb(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_refcount_block, rf_list.l_recs);

	return size / sizeof(struct ocfs2_extent_rec);
}

static inline u16 ocfs2_refcount_recs_per_rb(struct super_block *sb)
{
	int size;

	size = sb->s_blocksize -
		offsetof(struct ocfs2_refcount_block, rf_records.rl_recs);

	return size / sizeof(struct ocfs2_refcount_rec);
}

static inline u32
ocfs2_get_ref_rec_low_cpos(const struct ocfs2_refcount_rec *rec)
{
	return le64_to_cpu(rec->r_cpos) & OCFS2_32BIT_POS_MASK;
}
#else
static inline int ocfs2_fast_symlink_chars(int blocksize)
{
	return blocksize - offsetof(struct ocfs2_dinode, id2.i_symlink);
}

static inline int ocfs2_max_inline_data_with_xattr(int blocksize,
						   struct ocfs2_dinode *di)
{
	if (di && (di->i_dyn_features & OCFS2_INLINE_XATTR_FL))
		return blocksize -
			offsetof(struct ocfs2_dinode, id2.i_data.id_data) -
			di->i_xattr_inline_size;
	else
		return blocksize -
			offsetof(struct ocfs2_dinode, id2.i_data.id_data);
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

static inline int ocfs2_extent_recs_per_gd(int blocksize)
{
	int size;

	size = blocksize -
		offsetof(struct ocfs2_group_desc, bg_list.l_recs);

	return size / sizeof(struct ocfs2_extent_rec);
}

static inline int ocfs2_local_alloc_size(int blocksize)
{
	int size;

	size = blocksize -
		offsetof(struct ocfs2_dinode, id2.i_lab.la_bitmap);

	return size;
}

static inline int ocfs2_group_bitmap_size(int blocksize,
					  int suballocator,
					  uint32_t feature_incompat)
{
	int size = sb->s_blocksize -
		offsetof(struct ocfs2_group_desc, bg_bitmap);

	/*
	 * The cluster allocator uses the entire block.  Suballocators have
	 * never used more than OCFS2_MAX_BG_BITMAP_SIZE.  Unfortunately, older
	 * code expects bg_size set to the maximum.  Thus we must keep
	 * bg_size as-is unless discontig_bg is enabled.
	 */
	if (suballocator &&
	    (feature_incompat & OCFS2_FEATURE_INCOMPAT_DISCONTIG_BG))
		size = OCFS2_MAX_BG_BITMAP_SIZE;

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

static inline int ocfs2_xattr_recs_per_xb(int blocksize)
{
	int size;

	size = blocksize -
		offsetof(struct ocfs2_xattr_block,
			 xb_attrs.xb_root.xt_list.l_recs);

	return size / sizeof(struct ocfs2_extent_rec);
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
		chars = snprintf(buf, len, "%s",
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
	de->file_type = fs_umode_to_ftype(mode);
}

static inline int ocfs2_gd_is_discontig(struct ocfs2_group_desc *gd)
{
	if ((offsetof(struct ocfs2_group_desc, bg_bitmap) +
	     le16_to_cpu(gd->bg_size)) !=
	    offsetof(struct ocfs2_group_desc, bg_list))
		return 0;
	/*
	 * Only valid to check l_next_free_rec if
	 * bg_bitmap + bg_size == bg_list.
	 */
	if (!gd->bg_list.l_next_free_rec)
		return 0;
	return 1;
}
#endif  /* _OCFS2_FS_H */

