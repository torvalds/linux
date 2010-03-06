/*
 * fs/logfs/logfs_abi.h
 *
 * As should be obvious for Linux kernel code, license is GPLv2
 *
 * Copyright (c) 2005-2008 Joern Engel <joern@logfs.org>
 *
 * Public header for logfs.
 */
#ifndef FS_LOGFS_LOGFS_ABI_H
#define FS_LOGFS_LOGFS_ABI_H

/* For out-of-kernel compiles */
#ifndef BUILD_BUG_ON
#define BUILD_BUG_ON(condition) /**/
#endif

#define SIZE_CHECK(type, size)					\
static inline void check_##type(void)				\
{								\
	BUILD_BUG_ON(sizeof(struct type) != (size));		\
}

/*
 * Throughout the logfs code, we're constantly dealing with blocks at
 * various positions or offsets.  To remove confusion, we stricly
 * distinguish between a "position" - the logical position within a
 * file and an "offset" - the physical location within the device.
 *
 * Any usage of the term offset for a logical location or position for
 * a physical one is a bug and should get fixed.
 */

/*
 * Block are allocated in one of several segments depending on their
 * level.  The following levels are used:
 *  0	- regular data block
 *  1	- i1 indirect blocks
 *  2	- i2 indirect blocks
 *  3	- i3 indirect blocks
 *  4	- i4 indirect blocks
 *  5	- i5 indirect blocks
 *  6	- ifile data blocks
 *  7	- ifile i1 indirect blocks
 *  8	- ifile i2 indirect blocks
 *  9	- ifile i3 indirect blocks
 * 10	- ifile i4 indirect blocks
 * 11	- ifile i5 indirect blocks
 * Potential levels to be used in the future:
 * 12	- gc recycled blocks, long-lived data
 * 13	- replacement blocks, short-lived data
 *
 * Levels 1-11 are necessary for robust gc operations and help seperate
 * short-lived metadata from longer-lived file data.  In the future,
 * file data should get seperated into several segments based on simple
 * heuristics.  Old data recycled during gc operation is expected to be
 * long-lived.  New data is of uncertain life expectancy.  New data
 * used to replace older blocks in existing files is expected to be
 * short-lived.
 */


/* Magic numbers.  64bit for superblock, 32bit for statfs f_type */
#define LOGFS_MAGIC		0x7a3a8e5cb9d5bf67ull
#define LOGFS_MAGIC_U32		0xc97e8168u

/*
 * Various blocksize related macros.  Blocksize is currently fixed at 4KiB.
 * Sooner or later that should become configurable and the macros replaced
 * by something superblock-dependent.  Pointers in indirect blocks are and
 * will remain 64bit.
 *
 * LOGFS_BLOCKSIZE	- self-explaining
 * LOGFS_BLOCK_FACTOR	- number of pointers per indirect block
 * LOGFS_BLOCK_BITS	- log2 of LOGFS_BLOCK_FACTOR, used for shifts
 */
#define LOGFS_BLOCKSIZE		(4096ull)
#define LOGFS_BLOCK_FACTOR	(LOGFS_BLOCKSIZE / sizeof(u64))
#define LOGFS_BLOCK_BITS	(9)

/*
 * Number of blocks at various levels of indirection.  There are 16 direct
 * block pointers plus a single indirect pointer.
 */
#define I0_BLOCKS		(16)
#define I1_BLOCKS		LOGFS_BLOCK_FACTOR
#define I2_BLOCKS		(LOGFS_BLOCK_FACTOR * I1_BLOCKS)
#define I3_BLOCKS		(LOGFS_BLOCK_FACTOR * I2_BLOCKS)
#define I4_BLOCKS		(LOGFS_BLOCK_FACTOR * I3_BLOCKS)
#define I5_BLOCKS		(LOGFS_BLOCK_FACTOR * I4_BLOCKS)

#define INDIRECT_INDEX		I0_BLOCKS
#define LOGFS_EMBEDDED_FIELDS	(I0_BLOCKS + 1)

/*
 * Sizes at which files require another level of indirection.  Files smaller
 * than LOGFS_EMBEDDED_SIZE can be completely stored in the inode itself,
 * similar like ext2 fast symlinks.
 *
 * Data at a position smaller than LOGFS_I0_SIZE is accessed through the
 * direct pointers, else through the 1x indirect pointer and so forth.
 */
#define LOGFS_EMBEDDED_SIZE	(LOGFS_EMBEDDED_FIELDS * sizeof(u64))
#define LOGFS_I0_SIZE		(I0_BLOCKS * LOGFS_BLOCKSIZE)
#define LOGFS_I1_SIZE		(I1_BLOCKS * LOGFS_BLOCKSIZE)
#define LOGFS_I2_SIZE		(I2_BLOCKS * LOGFS_BLOCKSIZE)
#define LOGFS_I3_SIZE		(I3_BLOCKS * LOGFS_BLOCKSIZE)
#define LOGFS_I4_SIZE		(I4_BLOCKS * LOGFS_BLOCKSIZE)
#define LOGFS_I5_SIZE		(I5_BLOCKS * LOGFS_BLOCKSIZE)

/*
 * Each indirect block pointer must have this flag set, if all block pointers
 * behind it are set, i.e. there is no hole hidden in the shadow of this
 * indirect block pointer.
 */
#define LOGFS_FULLY_POPULATED (1ULL << 63)
#define pure_ofs(ofs) (ofs & ~LOGFS_FULLY_POPULATED)

/*
 * LogFS needs to seperate data into levels.  Each level is defined as the
 * maximal possible distance from the master inode (inode of the inode file).
 * Data blocks reside on level 0, 1x indirect block on level 1, etc.
 * Inodes reside on level 6, indirect blocks for the inode file on levels 7-11.
 * This effort is necessary to guarantee garbage collection to always make
 * progress.
 *
 * LOGFS_MAX_INDIRECT is the maximal indirection through indirect blocks,
 * LOGFS_MAX_LEVELS is one more for the actual data level of a file.  It is
 * the maximal number of levels for one file.
 * LOGFS_NO_AREAS is twice that, as the inode file and regular files are
 * effectively stacked on top of each other.
 */
#define LOGFS_MAX_INDIRECT	(5)
#define LOGFS_MAX_LEVELS	(LOGFS_MAX_INDIRECT + 1)
#define LOGFS_NO_AREAS		(2 * LOGFS_MAX_LEVELS)

/* Maximum size of filenames */
#define LOGFS_MAX_NAMELEN	(255)

/* Number of segments in the primary journal. */
#define LOGFS_JOURNAL_SEGS	(16)

/* Maximum number of free/erased/etc. segments in journal entries */
#define MAX_CACHED_SEGS		(64)


/*
 * LOGFS_OBJECT_HEADERSIZE is the size of a single header in the object store,
 * LOGFS_MAX_OBJECTSIZE the size of the largest possible object, including
 * its header,
 * LOGFS_SEGMENT_RESERVE is the amount of space reserved for each segment for
 * its segment header and the padded space at the end when no further objects
 * fit.
 */
#define LOGFS_OBJECT_HEADERSIZE	(0x1c)
#define LOGFS_SEGMENT_HEADERSIZE (0x18)
#define LOGFS_MAX_OBJECTSIZE	(LOGFS_OBJECT_HEADERSIZE + LOGFS_BLOCKSIZE)
#define LOGFS_SEGMENT_RESERVE	\
	(LOGFS_SEGMENT_HEADERSIZE + LOGFS_MAX_OBJECTSIZE - 1)

/*
 * Segment types:
 * SEG_SUPER	- Data or indirect block
 * SEG_JOURNAL	- Inode
 * SEG_OSTORE	- Dentry
 */
enum {
	SEG_SUPER	= 0x01,
	SEG_JOURNAL	= 0x02,
	SEG_OSTORE	= 0x03,
};

/**
 * struct logfs_segment_header - per-segment header in the ostore
 *
 * @crc:			crc32 of header (there is no data)
 * @pad:			unused, must be 0
 * @type:			segment type, see above
 * @level:			GC level for all objects in this segment
 * @segno:			segment number
 * @ec:				erase count for this segment
 * @gec:			global erase count at time of writing
 */
struct logfs_segment_header {
	__be32	crc;
	__be16	pad;
	__u8	type;
	__u8	level;
	__be32	segno;
	__be32	ec;
	__be64	gec;
};

SIZE_CHECK(logfs_segment_header, LOGFS_SEGMENT_HEADERSIZE);

#define LOGFS_FEATURES_INCOMPAT		(0ull)
#define LOGFS_FEATURES_RO_COMPAT	(0ull)
#define LOGFS_FEATURES_COMPAT		(0ull)

/**
 * struct logfs_disk_super - on-medium superblock
 *
 * @ds_magic:			magic number, must equal LOGFS_MAGIC
 * @ds_crc:			crc32 of structure starting with the next field
 * @ds_ifile_levels:		maximum number of levels for ifile
 * @ds_iblock_levels:		maximum number of levels for regular files
 * @ds_data_levels:		number of seperate levels for data
 * @pad0:			reserved, must be 0
 * @ds_feature_incompat:	incompatible filesystem features
 * @ds_feature_ro_compat:	read-only compatible filesystem features
 * @ds_feature_compat:		compatible filesystem features
 * @ds_flags:			flags
 * @ds_segment_shift:		log2 of segment size
 * @ds_block_shift:		log2 of block size
 * @ds_write_shift:		log2 of write size
 * @pad1:			reserved, must be 0
 * @ds_journal_seg:		segments used by primary journal
 * @ds_root_reserve:		bytes reserved for the superuser
 * @ds_speed_reserve:		bytes reserved to speed up GC
 * @ds_bad_seg_reserve:		number of segments reserved to handle bad blocks
 * @pad2:			reserved, must be 0
 * @pad3:			reserved, must be 0
 *
 * Contains only read-only fields.  Read-write fields like the amount of used
 * space is tracked in the dynamic superblock, which is stored in the journal.
 */
struct logfs_disk_super {
	struct logfs_segment_header ds_sh;
	__be64	ds_magic;

	__be32	ds_crc;
	__u8	ds_ifile_levels;
	__u8	ds_iblock_levels;
	__u8	ds_data_levels;
	__u8	ds_segment_shift;
	__u8	ds_block_shift;
	__u8	ds_write_shift;
	__u8	pad0[6];

	__be64	ds_filesystem_size;
	__be32	ds_segment_size;
	__be32  ds_bad_seg_reserve;

	__be64	ds_feature_incompat;
	__be64	ds_feature_ro_compat;

	__be64	ds_feature_compat;
	__be64	ds_feature_flags;

	__be64	ds_root_reserve;
	__be64  ds_speed_reserve;

	__be32	ds_journal_seg[LOGFS_JOURNAL_SEGS];

	__be64	ds_super_ofs[2];
	__be64	pad3[8];
};

SIZE_CHECK(logfs_disk_super, 256);

/*
 * Object types:
 * OBJ_BLOCK	- Data or indirect block
 * OBJ_INODE	- Inode
 * OBJ_DENTRY	- Dentry
 */
enum {
	OBJ_BLOCK	= 0x04,
	OBJ_INODE	= 0x05,
	OBJ_DENTRY	= 0x06,
};

/**
 * struct logfs_object_header - per-object header in the ostore
 *
 * @crc:			crc32 of header, excluding data_crc
 * @len:			length of data
 * @type:			object type, see above
 * @compr:			compression type
 * @ino:			inode number
 * @bix:			block index
 * @data_crc:			crc32 of payload
 */
struct logfs_object_header {
	__be32	crc;
	__be16	len;
	__u8	type;
	__u8	compr;
	__be64	ino;
	__be64	bix;
	__be32	data_crc;
} __attribute__((packed));

SIZE_CHECK(logfs_object_header, LOGFS_OBJECT_HEADERSIZE);

/*
 * Reserved inode numbers:
 * LOGFS_INO_MASTER	- master inode (for inode file)
 * LOGFS_INO_ROOT	- root directory
 * LOGFS_INO_SEGFILE	- per-segment used bytes and erase count
 */
enum {
	LOGFS_INO_MAPPING	= 0x00,
	LOGFS_INO_MASTER	= 0x01,
	LOGFS_INO_ROOT		= 0x02,
	LOGFS_INO_SEGFILE	= 0x03,
	LOGFS_RESERVED_INOS	= 0x10,
};

/*
 * Inode flags.  High bits should never be written to the medium.  They are
 * reserved for in-memory usage.
 * Low bits should either remain in sync with the corresponding FS_*_FL or
 * reuse slots that obviously don't make sense for logfs.
 *
 * LOGFS_IF_DIRTY	Inode must be written back
 * LOGFS_IF_ZOMBIE	Inode has been deleted
 * LOGFS_IF_STILLBORN	-ENOSPC happened when creating inode
 */
#define LOGFS_IF_COMPRESSED	0x00000004 /* == FS_COMPR_FL */
#define LOGFS_IF_DIRTY		0x20000000
#define LOGFS_IF_ZOMBIE		0x40000000
#define LOGFS_IF_STILLBORN	0x80000000

/* Flags available to chattr */
#define LOGFS_FL_USER_VISIBLE	(LOGFS_IF_COMPRESSED)
#define LOGFS_FL_USER_MODIFIABLE (LOGFS_IF_COMPRESSED)
/* Flags inherited from parent directory on file/directory creation */
#define LOGFS_FL_INHERITED	(LOGFS_IF_COMPRESSED)

/**
 * struct logfs_disk_inode - on-medium inode
 *
 * @di_mode:			file mode
 * @di_pad:			reserved, must be 0
 * @di_flags:			inode flags, see above
 * @di_uid:			user id
 * @di_gid:			group id
 * @di_ctime:			change time
 * @di_mtime:			modify time
 * @di_refcount:		reference count (aka nlink or link count)
 * @di_generation:		inode generation, for nfs
 * @di_used_bytes:		number of bytes used
 * @di_size:			file size
 * @di_data:			data pointers
 */
struct logfs_disk_inode {
	__be16	di_mode;
	__u8	di_height;
	__u8	di_pad;
	__be32	di_flags;
	__be32	di_uid;
	__be32	di_gid;

	__be64	di_ctime;
	__be64	di_mtime;

	__be64	di_atime;
	__be32	di_refcount;
	__be32	di_generation;

	__be64	di_used_bytes;
	__be64	di_size;

	__be64	di_data[LOGFS_EMBEDDED_FIELDS];
};

SIZE_CHECK(logfs_disk_inode, 200);

#define INODE_POINTER_OFS \
	(offsetof(struct logfs_disk_inode, di_data) / sizeof(__be64))
#define INODE_USED_OFS \
	(offsetof(struct logfs_disk_inode, di_used_bytes) / sizeof(__be64))
#define INODE_SIZE_OFS \
	(offsetof(struct logfs_disk_inode, di_size) / sizeof(__be64))
#define INODE_HEIGHT_OFS	(0)

/**
 * struct logfs_disk_dentry - on-medium dentry structure
 *
 * @ino:			inode number
 * @namelen:			length of file name
 * @type:			file type, identical to bits 12..15 of mode
 * @name:			file name
 */
/* FIXME: add 6 bytes of padding to remove the __packed */
struct logfs_disk_dentry {
	__be64	ino;
	__be16	namelen;
	__u8	type;
	__u8	name[LOGFS_MAX_NAMELEN];
} __attribute__((packed));

SIZE_CHECK(logfs_disk_dentry, 266);

#define RESERVED		0xffffffff
#define BADSEG			0xffffffff
/**
 * struct logfs_segment_entry - segment file entry
 *
 * @ec_level:			erase count and level
 * @valid:			number of valid bytes
 *
 * Segment file contains one entry for every segment.  ec_level contains the
 * erasecount in the upper 28 bits and the level in the lower 4 bits.  An
 * ec_level of BADSEG (-1) identifies bad segments.  valid contains the number
 * of valid bytes or RESERVED (-1 again) if the segment is used for either the
 * superblock or the journal, or when the segment is bad.
 */
struct logfs_segment_entry {
	__be32	ec_level;
	__be32	valid;
};

SIZE_CHECK(logfs_segment_entry, 8);

/**
 * struct logfs_journal_header - header for journal entries (JEs)
 *
 * @h_crc:			crc32 of journal entry
 * @h_len:			length of compressed journal entry,
 *				not including header
 * @h_datalen:			length of uncompressed data
 * @h_type:			JE type
 * @h_compr:			compression type
 * @h_pad:			reserved
 */
struct logfs_journal_header {
	__be32	h_crc;
	__be16	h_len;
	__be16	h_datalen;
	__be16	h_type;
	__u8	h_compr;
	__u8	h_pad[5];
};

SIZE_CHECK(logfs_journal_header, 16);

/*
 * Life expectency of data.
 * VIM_DEFAULT		- default vim
 * VIM_SEGFILE		- for segment file only - very short-living
 * VIM_GC		- GC'd data - likely long-living
 */
enum logfs_vim {
	VIM_DEFAULT	= 0,
	VIM_SEGFILE	= 1,
};

/**
 * struct logfs_je_area - wbuf header
 *
 * @segno:			segment number of area
 * @used_bytes:			number of bytes already used
 * @gc_level:			GC level
 * @vim:			life expectancy of data
 *
 * "Areas" are segments currently being used for writing.  There is at least
 * one area per GC level.  Several may be used to seperate long-living from
 * short-living data.  If an area with unknown vim is encountered, it can
 * simply be closed.
 * The write buffer immediately follow this header.
 */
struct logfs_je_area {
	__be32	segno;
	__be32	used_bytes;
	__u8	gc_level;
	__u8	vim;
} __attribute__((packed));

SIZE_CHECK(logfs_je_area, 10);

#define MAX_JOURNAL_HEADER \
	(sizeof(struct logfs_journal_header) + sizeof(struct logfs_je_area))

/**
 * struct logfs_je_dynsb - dynamic superblock
 *
 * @ds_gec:			global erase count
 * @ds_sweeper:			current position of GC "sweeper"
 * @ds_rename_dir:		source directory ino (see dir.c documentation)
 * @ds_rename_pos:		position of source dd (see dir.c documentation)
 * @ds_victim_ino:		victims of incomplete dir operation (see dir.c)
 * @ds_victim_ino:		parent inode of victim (see dir.c)
 * @ds_used_bytes:		number of used bytes
 */
struct logfs_je_dynsb {
	__be64	ds_gec;
	__be64	ds_sweeper;

	__be64	ds_rename_dir;
	__be64	ds_rename_pos;

	__be64	ds_victim_ino;
	__be64	ds_victim_parent; /* XXX */

	__be64	ds_used_bytes;
	__be32	ds_generation;
	__be32	pad;
};

SIZE_CHECK(logfs_je_dynsb, 64);

/**
 * struct logfs_je_anchor - anchor of filesystem tree, aka master inode
 *
 * @da_size:			size of inode file
 * @da_last_ino:		last created inode
 * @da_used_bytes:		number of bytes used
 * @da_data:			data pointers
 */
struct logfs_je_anchor {
	__be64	da_size;
	__be64	da_last_ino;

	__be64	da_used_bytes;
	u8	da_height;
	u8	pad[7];

	__be64	da_data[LOGFS_EMBEDDED_FIELDS];
};

SIZE_CHECK(logfs_je_anchor, 168);

/**
 * struct logfs_je_spillout - spillout entry (from 1st to 2nd journal)
 *
 * @so_segment:			segments used for 2nd journal
 *
 * Length of the array is given by h_len field in the header.
 */
struct logfs_je_spillout {
	__be64	so_segment[0];
};

SIZE_CHECK(logfs_je_spillout, 0);

/**
 * struct logfs_je_journal_ec - erase counts for all journal segments
 *
 * @ec:				erase count
 *
 * Length of the array is given by h_len field in the header.
 */
struct logfs_je_journal_ec {
	__be32	ec[0];
};

SIZE_CHECK(logfs_je_journal_ec, 0);

/**
 * struct logfs_je_free_segments - list of free segmetns with erase count
 */
struct logfs_je_free_segments {
	__be32	segno;
	__be32	ec;
};

SIZE_CHECK(logfs_je_free_segments, 8);

/**
 * struct logfs_seg_alias - list of segment aliases
 */
struct logfs_seg_alias {
	__be32	old_segno;
	__be32	new_segno;
};

SIZE_CHECK(logfs_seg_alias, 8);

/**
 * struct logfs_obj_alias - list of object aliases
 */
struct logfs_obj_alias {
	__be64	ino;
	__be64	bix;
	__be64	val;
	u8	level;
	u8	pad[5];
	__be16	child_no;
};

SIZE_CHECK(logfs_obj_alias, 32);

/**
 * Compression types.
 *
 * COMPR_NONE	- uncompressed
 * COMPR_ZLIB	- compressed with zlib
 */
enum {
	COMPR_NONE	= 0,
	COMPR_ZLIB	= 1,
};

/*
 * Journal entries come in groups of 16.  First group contains unique
 * entries, next groups contain one entry per level
 *
 * JE_FIRST	- smallest possible journal entry number
 *
 * JEG_BASE	- base group, containing unique entries
 * JE_COMMIT	- commit entry, validates all previous entries
 * JE_DYNSB	- dynamic superblock, anything that ought to be in the
 *		  superblock but cannot because it is read-write data
 * JE_ANCHOR	- anchor aka master inode aka inode file's inode
 * JE_ERASECOUNT  erasecounts for all journal segments
 * JE_SPILLOUT	- unused
 * JE_SEG_ALIAS	- aliases segments
 * JE_AREA	- area description
 *
 * JE_LAST	- largest possible journal entry number
 */
enum {
	JE_FIRST	= 0x01,

	JEG_BASE	= 0x00,
	JE_COMMIT	= 0x02,
	JE_DYNSB	= 0x03,
	JE_ANCHOR	= 0x04,
	JE_ERASECOUNT	= 0x05,
	JE_SPILLOUT	= 0x06,
	JE_OBJ_ALIAS	= 0x0d,
	JE_AREA		= 0x0e,

	JE_LAST		= 0x0e,
};

#endif
