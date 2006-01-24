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

#define OCFS2_FEATURE_COMPAT_SUPP	0
#define OCFS2_FEATURE_INCOMPAT_SUPP	0
#define OCFS2_FEATURE_RO_COMPAT_SUPP	0

/*
 * Heartbeat-only devices are missing journals and other files.  The
 * filesystem driver can't load them, but the library can.  Never put
 * this in OCFS2_FEATURE_INCOMPAT_SUPP, *ever*.
 */
#define OCFS2_FEATURE_INCOMPAT_HEARTBEAT_DEV	0x0002


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

/* Journal limits (in bytes) */
#define OCFS2_MIN_JOURNAL_SIZE		(4 * 1024 * 1024)
#define OCFS2_MAX_JOURNAL_SIZE		(500 * 1024 * 1024)

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
 */
struct ocfs2_extent_rec {
/*00*/	__le32 e_cpos;		/* Offset into the file, in clusters */
	__le32 e_clusters;	/* Clusters covered by this extent */
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
					   header (a leaf) */
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
	__le16 s_reserved1;
	__le32 s_reserved2;
	__le64 s_first_cluster_group;	/* Block offset of 1st cluster
					 * group header */
/*50*/	__u8  s_label[OCFS2_MAX_VOL_LABEL_LEN];	/* Label for mounting, etc. */
/*90*/	__u8  s_uuid[OCFS2_VOL_UUID_LEN];	/* 128-bit uuid */
/*A0*/
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
/*70*/	__le64 i_reserved1[9];
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
#else
static inline int ocfs2_fast_symlink_chars(int blocksize)
{
	return blocksize - offsetof(struct ocfs2_dinode, id2.i_symlink);
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

