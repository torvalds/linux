/*
 * YAFFS: Yet another Flash File System . A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1 as
 * published by the Free Software Foundation.
 *
 * Note: Only YAFFS headers are LGPL, YAFFS C code is covered by GPL.
 */

#ifndef __YAFFS_GUTS_H__
#define __YAFFS_GUTS_H__

#include "yportenv.h"

#define YAFFS_OK	1
#define YAFFS_FAIL  0

/* Give us a  Y=0x59,
 * Give us an A=0x41,
 * Give us an FF=0xFF
 * Give us an S=0x53
 * And what have we got...
 */
#define YAFFS_MAGIC			0x5941FF53

#define YAFFS_NTNODES_LEVEL0	  	16
#define YAFFS_TNODES_LEVEL0_BITS	4
#define YAFFS_TNODES_LEVEL0_MASK	0xf

#define YAFFS_NTNODES_INTERNAL 		(YAFFS_NTNODES_LEVEL0 / 2)
#define YAFFS_TNODES_INTERNAL_BITS 	(YAFFS_TNODES_LEVEL0_BITS - 1)
#define YAFFS_TNODES_INTERNAL_MASK	0x7
#define YAFFS_TNODES_MAX_LEVEL		6

#ifndef CONFIG_YAFFS_NO_YAFFS1
#define YAFFS_BYTES_PER_SPARE		16
#define YAFFS_BYTES_PER_CHUNK		512
#define YAFFS_CHUNK_SIZE_SHIFT		9
#define YAFFS_CHUNKS_PER_BLOCK		32
#define YAFFS_BYTES_PER_BLOCK		(YAFFS_CHUNKS_PER_BLOCK*YAFFS_BYTES_PER_CHUNK)
#endif

#define YAFFS_MIN_YAFFS2_CHUNK_SIZE 	1024
#define YAFFS_MIN_YAFFS2_SPARE_SIZE	32

#define YAFFS_MAX_CHUNK_ID		0x000FFFFF

#define YAFFS_ALLOCATION_NOBJECTS	100
#define YAFFS_ALLOCATION_NTNODES	100
#define YAFFS_ALLOCATION_NLINKS		100

#define YAFFS_NOBJECT_BUCKETS		256

#define YAFFS_OBJECT_SPACE		0x40000
#define YAFFS_MAX_OBJECT_ID		(YAFFS_OBJECT_SPACE -1)

#define YAFFS_CHECKPOINT_VERSION 	4

#ifdef CONFIG_YAFFS_UNICODE
#define YAFFS_MAX_NAME_LENGTH		127
#define YAFFS_MAX_ALIAS_LENGTH		79
#else
#define YAFFS_MAX_NAME_LENGTH		255
#define YAFFS_MAX_ALIAS_LENGTH		159
#endif

#define YAFFS_SHORT_NAME_LENGTH		15

/* Some special object ids for pseudo objects */
#define YAFFS_OBJECTID_ROOT		1
#define YAFFS_OBJECTID_LOSTNFOUND	2
#define YAFFS_OBJECTID_UNLINKED		3
#define YAFFS_OBJECTID_DELETED		4

/* Pseudo object ids for checkpointing */
#define YAFFS_OBJECTID_SB_HEADER	0x10
#define YAFFS_OBJECTID_CHECKPOINT_DATA	0x20
#define YAFFS_SEQUENCE_CHECKPOINT_DATA  0x21

#define YAFFS_MAX_SHORT_OP_CACHES	20

#define YAFFS_N_TEMP_BUFFERS		6

/* We limit the number attempts at sucessfully saving a chunk of data.
 * Small-page devices have 32 pages per block; large-page devices have 64.
 * Default to something in the order of 5 to 10 blocks worth of chunks.
 */
#define YAFFS_WR_ATTEMPTS		(5*64)

/* Sequence numbers are used in YAFFS2 to determine block allocation order.
 * The range is limited slightly to help distinguish bad numbers from good.
 * This also allows us to perhaps in the future use special numbers for
 * special purposes.
 * EFFFFF00 allows the allocation of 8 blocks per second (~1Mbytes) for 15 years,
 * and is a larger number than the lifetime of a 2GB device.
 */
#define YAFFS_LOWEST_SEQUENCE_NUMBER	0x00001000
#define YAFFS_HIGHEST_SEQUENCE_NUMBER	0xEFFFFF00

/* Special sequence number for bad block that failed to be marked bad */
#define YAFFS_SEQUENCE_BAD_BLOCK	0xFFFF0000

/* ChunkCache is used for short read/write operations.*/
struct yaffs_cache {
	struct yaffs_obj *object;
	int chunk_id;
	int last_use;
	int dirty;
	int n_bytes;		/* Only valid if the cache is dirty */
	int locked;		/* Can't push out or flush while locked. */
	u8 *data;
};

/* Tags structures in RAM
 * NB This uses bitfield. Bitfields should not straddle a u32 boundary otherwise
 * the structure size will get blown out.
 */

#ifndef CONFIG_YAFFS_NO_YAFFS1
struct yaffs_tags {
	unsigned chunk_id:20;
	unsigned serial_number:2;
	unsigned n_bytes_lsb:10;
	unsigned obj_id:18;
	unsigned ecc:12;
	unsigned n_bytes_msb:2;
};

union yaffs_tags_union {
	struct yaffs_tags as_tags;
	u8 as_bytes[8];
};

#endif

/* Stuff used for extended tags in YAFFS2 */

enum yaffs_ecc_result {
	YAFFS_ECC_RESULT_UNKNOWN,
	YAFFS_ECC_RESULT_NO_ERROR,
	YAFFS_ECC_RESULT_FIXED,
	YAFFS_ECC_RESULT_UNFIXED
};

enum yaffs_obj_type {
	YAFFS_OBJECT_TYPE_UNKNOWN,
	YAFFS_OBJECT_TYPE_FILE,
	YAFFS_OBJECT_TYPE_SYMLINK,
	YAFFS_OBJECT_TYPE_DIRECTORY,
	YAFFS_OBJECT_TYPE_HARDLINK,
	YAFFS_OBJECT_TYPE_SPECIAL
};

#define YAFFS_OBJECT_TYPE_MAX YAFFS_OBJECT_TYPE_SPECIAL

struct yaffs_ext_tags {

	unsigned validity0;
	unsigned chunk_used;	/*  Status of the chunk: used or unused */
	unsigned obj_id;	/* If 0 then this is not part of an object (unused) */
	unsigned chunk_id;	/* If 0 then this is a header, else a data chunk */
	unsigned n_bytes;	/* Only valid for data chunks */

	/* The following stuff only has meaning when we read */
	enum yaffs_ecc_result ecc_result;
	unsigned block_bad;

	/* YAFFS 1 stuff */
	unsigned is_deleted;	/* The chunk is marked deleted */
	unsigned serial_number;	/* Yaffs1 2-bit serial number */

	/* YAFFS2 stuff */
	unsigned seq_number;	/* The sequence number of this block */

	/* Extra info if this is an object header (YAFFS2 only) */

	unsigned extra_available;	/* There is extra info available if this is not zero */
	unsigned extra_parent_id;	/* The parent object */
	unsigned extra_is_shrink;	/* Is it a shrink header? */
	unsigned extra_shadows;	/* Does this shadow another object? */

	enum yaffs_obj_type extra_obj_type;	/* What object type? */

	unsigned extra_length;	/* Length if it is a file */
	unsigned extra_equiv_id;	/* Equivalent object Id if it is a hard link */

	unsigned validity1;

};

/* Spare structure for YAFFS1 */
struct yaffs_spare {
	u8 tb0;
	u8 tb1;
	u8 tb2;
	u8 tb3;
	u8 page_status;		/* set to 0 to delete the chunk */
	u8 block_status;
	u8 tb4;
	u8 tb5;
	u8 ecc1[3];
	u8 tb6;
	u8 tb7;
	u8 ecc2[3];
};

/*Special structure for passing through to mtd */
struct yaffs_nand_spare {
	struct yaffs_spare spare;
	int eccres1;
	int eccres2;
};

/* Block data in RAM */

enum yaffs_block_state {
	YAFFS_BLOCK_STATE_UNKNOWN = 0,

	YAFFS_BLOCK_STATE_SCANNING,
	/* Being scanned */

	YAFFS_BLOCK_STATE_NEEDS_SCANNING,
	/* The block might have something on it (ie it is allocating or full, perhaps empty)
	 * but it needs to be scanned to determine its true state.
	 * This state is only valid during scanning.
	 * NB We tolerate empty because the pre-scanner might be incapable of deciding
	 * However, if this state is returned on a YAFFS2 device, then we expect a sequence number
	 */

	YAFFS_BLOCK_STATE_EMPTY,
	/* This block is empty */

	YAFFS_BLOCK_STATE_ALLOCATING,
	/* This block is partially allocated.
	 * At least one page holds valid data.
	 * This is the one currently being used for page
	 * allocation. Should never be more than one of these.
	 * If a block is only partially allocated at mount it is treated as full.
	 */

	YAFFS_BLOCK_STATE_FULL,
	/* All the pages in this block have been allocated.
	 * If a block was only partially allocated when mounted we treat
	 * it as fully allocated.
	 */

	YAFFS_BLOCK_STATE_DIRTY,
	/* The block was full and now all chunks have been deleted.
	 * Erase me, reuse me.
	 */

	YAFFS_BLOCK_STATE_CHECKPOINT,
	/* This block is assigned to holding checkpoint data. */

	YAFFS_BLOCK_STATE_COLLECTING,
	/* This block is being garbage collected */

	YAFFS_BLOCK_STATE_DEAD
	    /* This block has failed and is not in use */
};

#define	YAFFS_NUMBER_OF_BLOCK_STATES (YAFFS_BLOCK_STATE_DEAD + 1)

struct yaffs_block_info {

	int soft_del_pages:10;	/* number of soft deleted pages */
	int pages_in_use:10;	/* number of pages in use */
	unsigned block_state:4;	/* One of the above block states. NB use unsigned because enum is sometimes an int */
	u32 needs_retiring:1;	/* Data has failed on this block, need to get valid data off */
	/* and retire the block. */
	u32 skip_erased_check:1;	/* If this is set we can skip the erased check on this block */
	u32 gc_prioritise:1;	/* An ECC check or blank check has failed on this block.
				   It should be prioritised for GC */
	u32 chunk_error_strikes:3;	/* How many times we've had ecc etc failures on this block and tried to reuse it */

#ifdef CONFIG_YAFFS_YAFFS2
	u32 has_shrink_hdr:1;	/* This block has at least one shrink object header */
	u32 seq_number;		/* block sequence number for yaffs2 */
#endif

};

/* -------------------------- Object structure -------------------------------*/
/* This is the object structure as stored on NAND */

struct yaffs_obj_hdr {
	enum yaffs_obj_type type;

	/* Apply to everything  */
	int parent_obj_id;
	u16 sum_no_longer_used;	/* checksum of name. No longer used */
	YCHAR name[YAFFS_MAX_NAME_LENGTH + 1];

	/* The following apply to directories, files, symlinks - not hard links */
	u32 yst_mode;		/* protection */

	u32 yst_uid;
	u32 yst_gid;
	u32 yst_atime;
	u32 yst_mtime;
	u32 yst_ctime;

	/* File size  applies to files only */
	int file_size;

	/* Equivalent object id applies to hard links only. */
	int equiv_id;

	/* Alias is for symlinks only. */
	YCHAR alias[YAFFS_MAX_ALIAS_LENGTH + 1];

	u32 yst_rdev;		/* device stuff for block and char devices (major/min) */

	u32 win_ctime[2];
	u32 win_atime[2];
	u32 win_mtime[2];

	u32 inband_shadowed_obj_id;
	u32 inband_is_shrink;

	u32 reserved[2];
	int shadows_obj;	/* This object header shadows the specified object if > 0 */

	/* is_shrink applies to object headers written when we shrink the file (ie resize) */
	u32 is_shrink;

};

/*--------------------------- Tnode -------------------------- */

struct yaffs_tnode {
	struct yaffs_tnode *internal[YAFFS_NTNODES_INTERNAL];
};

/*------------------------  Object -----------------------------*/
/* An object can be one of:
 * - a directory (no data, has children links
 * - a regular file (data.... not prunes :->).
 * - a symlink [symbolic link] (the alias).
 * - a hard link
 */

struct yaffs_file_var {
	u32 file_size;
	u32 scanned_size;
	u32 shrink_size;
	int top_level;
	struct yaffs_tnode *top;
};

struct yaffs_dir_var {
	struct list_head children;	/* list of child links */
	struct list_head dirty;	/* Entry for list of dirty directories */
};

struct yaffs_symlink_var {
	YCHAR *alias;
};

struct yaffs_hardlink_var {
	struct yaffs_obj *equiv_obj;
	u32 equiv_id;
};

union yaffs_obj_var {
	struct yaffs_file_var file_variant;
	struct yaffs_dir_var dir_variant;
	struct yaffs_symlink_var symlink_variant;
	struct yaffs_hardlink_var hardlink_variant;
};

struct yaffs_obj {
	u8 deleted:1;		/* This should only apply to unlinked files. */
	u8 soft_del:1;		/* it has also been soft deleted */
	u8 unlinked:1;		/* An unlinked file. The file should be in the unlinked directory. */
	u8 fake:1;		/* A fake object has no presence on NAND. */
	u8 rename_allowed:1;	/* Some objects are not allowed to be renamed. */
	u8 unlink_allowed:1;
	u8 dirty:1;		/* the object needs to be written to flash */
	u8 valid:1;		/* When the file system is being loaded up, this
				 * object might be created before the data
				 * is available (ie. file data records appear before the header).
				 */
	u8 lazy_loaded:1;	/* This object has been lazy loaded and is missing some detail */

	u8 defered_free:1;	/* For Linux kernel. Object is removed from NAND, but is
				 * still in the inode cache. Free of object is defered.
				 * until the inode is released.
				 */
	u8 being_created:1;	/* This object is still being created so skip some checks. */
	u8 is_shadowed:1;	/* This object is shadowed on the way to being renamed. */

	u8 xattr_known:1;	/* We know if this has object has xattribs or not. */
	u8 has_xattr:1;		/* This object has xattribs. Valid if xattr_known. */

	u8 serial;		/* serial number of chunk in NAND. Cached here */
	u16 sum;		/* sum of the name to speed searching */

	struct yaffs_dev *my_dev;	/* The device I'm on */

	struct list_head hash_link;	/* list of objects in this hash bucket */

	struct list_head hard_links;	/* all the equivalent hard linked objects */

	/* directory structure stuff */
	/* also used for linking up the free list */
	struct yaffs_obj *parent;
	struct list_head siblings;

	/* Where's my object header in NAND? */
	int hdr_chunk;

	int n_data_chunks;	/* Number of data chunks attached to the file. */

	u32 obj_id;		/* the object id value */

	u32 yst_mode;

#ifndef CONFIG_YAFFS_NO_SHORT_NAMES
	YCHAR short_name[YAFFS_SHORT_NAME_LENGTH + 1];
#endif

#ifdef CONFIG_YAFFS_WINCE
	u32 win_ctime[2];
	u32 win_mtime[2];
	u32 win_atime[2];
#else
	u32 yst_uid;
	u32 yst_gid;
	u32 yst_atime;
	u32 yst_mtime;
	u32 yst_ctime;
#endif

	u32 yst_rdev;

	void *my_inode;

	enum yaffs_obj_type variant_type;

	union yaffs_obj_var variant;

};

struct yaffs_obj_bucket {
	struct list_head list;
	int count;
};

/* yaffs_checkpt_obj holds the definition of an object as dumped
 * by checkpointing.
 */

struct yaffs_checkpt_obj {
	int struct_type;
	u32 obj_id;
	u32 parent_id;
	int hdr_chunk;
	enum yaffs_obj_type variant_type:3;
	u8 deleted:1;
	u8 soft_del:1;
	u8 unlinked:1;
	u8 fake:1;
	u8 rename_allowed:1;
	u8 unlink_allowed:1;
	u8 serial;
	int n_data_chunks;
	u32 size_or_equiv_obj;
};

/*--------------------- Temporary buffers ----------------
 *
 * These are chunk-sized working buffers. Each device has a few
 */

struct yaffs_buffer {
	u8 *buffer;
	int line;		/* track from whence this buffer was allocated */
	int max_line;
};

/*----------------- Device ---------------------------------*/

struct yaffs_param {
	const YCHAR *name;

	/*
	 * Entry parameters set up way early. Yaffs sets up the rest.
	 * The structure should be zeroed out before use so that unused
	 * and defualt values are zero.
	 */

	int inband_tags;	/* Use unband tags */
	u32 total_bytes_per_chunk;	/* Should be >= 512, does not need to be a power of 2 */
	int chunks_per_block;	/* does not need to be a power of 2 */
	int spare_bytes_per_chunk;	/* spare area size */
	int start_block;	/* Start block we're allowed to use */
	int end_block;		/* End block we're allowed to use */
	int n_reserved_blocks;	/* We want this tuneable so that we can reduce */
	/* reserved blocks on NOR and RAM. */

	int n_caches;		/* If <= 0, then short op caching is disabled, else
				 * the number of short op caches (don't use too many).
				 * 10 to 20 is a good bet.
				 */
	int use_nand_ecc;	/* Flag to decide whether or not to use NANDECC on data (yaffs1) */
	int no_tags_ecc;	/* Flag to decide whether or not to do ECC on packed tags (yaffs2) */

	int is_yaffs2;		/* Use yaffs2 mode on this device */

	int empty_lost_n_found;	/* Auto-empty lost+found directory on mount */

	int refresh_period;	/* How often we should check to do a block refresh */

	/* Checkpoint control. Can be set before or after initialisation */
	u8 skip_checkpt_rd;
	u8 skip_checkpt_wr;

	int enable_xattr;	/* Enable xattribs */

	/* NAND access functions (Must be set before calling YAFFS) */

	int (*write_chunk_fn) (struct yaffs_dev * dev,
			       int nand_chunk, const u8 * data,
			       const struct yaffs_spare * spare);
	int (*read_chunk_fn) (struct yaffs_dev * dev,
			      int nand_chunk, u8 * data,
			      struct yaffs_spare * spare);
	int (*erase_fn) (struct yaffs_dev * dev, int flash_block);
	int (*initialise_flash_fn) (struct yaffs_dev * dev);
	int (*deinitialise_flash_fn) (struct yaffs_dev * dev);

#ifdef CONFIG_YAFFS_YAFFS2
	int (*write_chunk_tags_fn) (struct yaffs_dev * dev,
				    int nand_chunk, const u8 * data,
				    const struct yaffs_ext_tags * tags);
	int (*read_chunk_tags_fn) (struct yaffs_dev * dev,
				   int nand_chunk, u8 * data,
				   struct yaffs_ext_tags * tags);
	int (*bad_block_fn) (struct yaffs_dev * dev, int block_no);
	int (*query_block_fn) (struct yaffs_dev * dev, int block_no,
			       enum yaffs_block_state * state,
			       u32 * seq_number);
#endif

	/* The remove_obj_fn function must be supplied by OS flavours that
	 * need it.
	 * yaffs direct uses it to implement the faster readdir.
	 * Linux uses it to protect the directory during unlocking.
	 */
	void (*remove_obj_fn) (struct yaffs_obj * obj);

	/* Callback to mark the superblock dirty */
	void (*sb_dirty_fn) (struct yaffs_dev * dev);

	/*  Callback to control garbage collection. */
	unsigned (*gc_control) (struct yaffs_dev * dev);

	/* Debug control flags. Don't use unless you know what you're doing */
	int use_header_file_size;	/* Flag to determine if we should use file sizes from the header */
	int disable_lazy_load;	/* Disable lazy loading on this device */
	int wide_tnodes_disabled;	/* Set to disable wide tnodes */
	int disable_soft_del;	/* yaffs 1 only: Set to disable the use of softdeletion. */

	int defered_dir_update;	/* Set to defer directory updates */

#ifdef CONFIG_YAFFS_AUTO_UNICODE
	int auto_unicode;
#endif
	int always_check_erased;	/* Force chunk erased check always on */
};

struct yaffs_dev {
	struct yaffs_param param;

	/* Context storage. Holds extra OS specific data for this device */

	void *os_context;
	void *driver_context;

	struct list_head dev_list;

	/* Runtime parameters. Set up by YAFFS. */
	int data_bytes_per_chunk;

	/* Non-wide tnode stuff */
	u16 chunk_grp_bits;	/* Number of bits that need to be resolved if
				 * the tnodes are not wide enough.
				 */
	u16 chunk_grp_size;	/* == 2^^chunk_grp_bits */

	/* Stuff to support wide tnodes */
	u32 tnode_width;
	u32 tnode_mask;
	u32 tnode_size;

	/* Stuff for figuring out file offset to chunk conversions */
	u32 chunk_shift;	/* Shift value */
	u32 chunk_div;		/* Divisor after shifting: 1 for power-of-2 sizes */
	u32 chunk_mask;		/* Mask to use for power-of-2 case */

	int is_mounted;
	int read_only;
	int is_checkpointed;

	/* Stuff to support block offsetting to support start block zero */
	int internal_start_block;
	int internal_end_block;
	int block_offset;
	int chunk_offset;

	/* Runtime checkpointing stuff */
	int checkpt_page_seq;	/* running sequence number of checkpoint pages */
	int checkpt_byte_count;
	int checkpt_byte_offs;
	u8 *checkpt_buffer;
	int checkpt_open_write;
	int blocks_in_checkpt;
	int checkpt_cur_chunk;
	int checkpt_cur_block;
	int checkpt_next_block;
	int *checkpt_block_list;
	int checkpt_max_blocks;
	u32 checkpt_sum;
	u32 checkpt_xor;

	int checkpoint_blocks_required;	/* Number of blocks needed to store current checkpoint set */

	/* Block Info */
	struct yaffs_block_info *block_info;
	u8 *chunk_bits;		/* bitmap of chunks in use */
	unsigned block_info_alt:1;	/* was allocated using alternative strategy */
	unsigned chunk_bits_alt:1;	/* was allocated using alternative strategy */
	int chunk_bit_stride;	/* Number of bytes of chunk_bits per block.
				 * Must be consistent with chunks_per_block.
				 */

	int n_erased_blocks;
	int alloc_block;	/* Current block being allocated off */
	u32 alloc_page;
	int alloc_block_finder;	/* Used to search for next allocation block */

	/* Object and Tnode memory management */
	void *allocator;
	int n_obj;
	int n_tnodes;

	int n_hardlinks;

	struct yaffs_obj_bucket obj_bucket[YAFFS_NOBJECT_BUCKETS];
	u32 bucket_finder;

	int n_free_chunks;

	/* Garbage collection control */
	u32 *gc_cleanup_list;	/* objects to delete at the end of a GC. */
	u32 n_clean_ups;

	unsigned has_pending_prioritised_gc;	/* We think this device might have pending prioritised gcs */
	unsigned gc_disable;
	unsigned gc_block_finder;
	unsigned gc_dirtiest;
	unsigned gc_pages_in_use;
	unsigned gc_not_done;
	unsigned gc_block;
	unsigned gc_chunk;
	unsigned gc_skip;

	/* Special directories */
	struct yaffs_obj *root_dir;
	struct yaffs_obj *lost_n_found;

	/* Buffer areas for storing data to recover from write failures TODO
	 *      u8            buffered_data[YAFFS_CHUNKS_PER_BLOCK][YAFFS_BYTES_PER_CHUNK];
	 *      struct yaffs_spare buffered_spare[YAFFS_CHUNKS_PER_BLOCK];
	 */

	int buffered_block;	/* Which block is buffered here? */
	int doing_buffered_block_rewrite;

	struct yaffs_cache *cache;
	int cache_last_use;

	/* Stuff for background deletion and unlinked files. */
	struct yaffs_obj *unlinked_dir;	/* Directory where unlinked and deleted files live. */
	struct yaffs_obj *del_dir;	/* Directory where deleted objects are sent to disappear. */
	struct yaffs_obj *unlinked_deletion;	/* Current file being background deleted. */
	int n_deleted_files;	/* Count of files awaiting deletion; */
	int n_unlinked_files;	/* Count of unlinked files. */
	int n_bg_deletions;	/* Count of background deletions. */

	/* Temporary buffer management */
	struct yaffs_buffer temp_buffer[YAFFS_N_TEMP_BUFFERS];
	int max_temp;
	int temp_in_use;
	int unmanaged_buffer_allocs;
	int unmanaged_buffer_deallocs;

	/* yaffs2 runtime stuff */
	unsigned seq_number;	/* Sequence number of currently allocating block */
	unsigned oldest_dirty_seq;
	unsigned oldest_dirty_block;

	/* Block refreshing */
	int refresh_skip;	/* A skip down counter. Refresh happens when this gets to zero. */

	/* Dirty directory handling */
	struct list_head dirty_dirs;	/* List of dirty directories */

	/* Statistcs */
	u32 n_page_writes;
	u32 n_page_reads;
	u32 n_erasures;
	u32 n_erase_failures;
	u32 n_gc_copies;
	u32 all_gcs;
	u32 passive_gc_count;
	u32 oldest_dirty_gc_count;
	u32 n_gc_blocks;
	u32 bg_gcs;
	u32 n_retired_writes;
	u32 n_retired_blocks;
	u32 n_ecc_fixed;
	u32 n_ecc_unfixed;
	u32 n_tags_ecc_fixed;
	u32 n_tags_ecc_unfixed;
	u32 n_deletions;
	u32 n_unmarked_deletions;
	u32 refresh_count;
	u32 cache_hits;

};

/* The CheckpointDevice structure holds the device information that changes at runtime and
 * must be preserved over unmount/mount cycles.
 */
struct yaffs_checkpt_dev {
	int struct_type;
	int n_erased_blocks;
	int alloc_block;	/* Current block being allocated off */
	u32 alloc_page;
	int n_free_chunks;

	int n_deleted_files;	/* Count of files awaiting deletion; */
	int n_unlinked_files;	/* Count of unlinked files. */
	int n_bg_deletions;	/* Count of background deletions. */

	/* yaffs2 runtime stuff */
	unsigned seq_number;	/* Sequence number of currently allocating block */

};

struct yaffs_checkpt_validity {
	int struct_type;
	u32 magic;
	u32 version;
	u32 head;
};

struct yaffs_shadow_fixer {
	int obj_id;
	int shadowed_id;
	struct yaffs_shadow_fixer *next;
};

/* Structure for doing xattr modifications */
struct yaffs_xattr_mod {
	int set;		/* If 0 then this is a deletion */
	const YCHAR *name;
	const void *data;
	int size;
	int flags;
	int result;
};

/*----------------------- YAFFS Functions -----------------------*/

int yaffs_guts_initialise(struct yaffs_dev *dev);
void yaffs_deinitialise(struct yaffs_dev *dev);

int yaffs_get_n_free_chunks(struct yaffs_dev *dev);

int yaffs_rename_obj(struct yaffs_obj *old_dir, const YCHAR * old_name,
		     struct yaffs_obj *new_dir, const YCHAR * new_name);

int yaffs_unlinker(struct yaffs_obj *dir, const YCHAR * name);
int yaffs_del_obj(struct yaffs_obj *obj);

int yaffs_get_obj_name(struct yaffs_obj *obj, YCHAR * name, int buffer_size);
int yaffs_get_obj_length(struct yaffs_obj *obj);
int yaffs_get_obj_inode(struct yaffs_obj *obj);
unsigned yaffs_get_obj_type(struct yaffs_obj *obj);
int yaffs_get_obj_link_count(struct yaffs_obj *obj);

/* File operations */
int yaffs_file_rd(struct yaffs_obj *obj, u8 * buffer, loff_t offset,
		  int n_bytes);
int yaffs_wr_file(struct yaffs_obj *obj, const u8 * buffer, loff_t offset,
		  int n_bytes, int write_trhrough);
int yaffs_resize_file(struct yaffs_obj *obj, loff_t new_size);

struct yaffs_obj *yaffs_create_file(struct yaffs_obj *parent,
				    const YCHAR * name, u32 mode, u32 uid,
				    u32 gid);

int yaffs_flush_file(struct yaffs_obj *obj, int update_time, int data_sync);

/* Flushing and checkpointing */
void yaffs_flush_whole_cache(struct yaffs_dev *dev);

int yaffs_checkpoint_save(struct yaffs_dev *dev);
int yaffs_checkpoint_restore(struct yaffs_dev *dev);

/* Directory operations */
struct yaffs_obj *yaffs_create_dir(struct yaffs_obj *parent, const YCHAR * name,
				   u32 mode, u32 uid, u32 gid);
struct yaffs_obj *yaffs_find_by_name(struct yaffs_obj *the_dir,
				     const YCHAR * name);
struct yaffs_obj *yaffs_find_by_number(struct yaffs_dev *dev, u32 number);

/* Link operations */
struct yaffs_obj *yaffs_link_obj(struct yaffs_obj *parent, const YCHAR * name,
				 struct yaffs_obj *equiv_obj);

struct yaffs_obj *yaffs_get_equivalent_obj(struct yaffs_obj *obj);

/* Symlink operations */
struct yaffs_obj *yaffs_create_symlink(struct yaffs_obj *parent,
				       const YCHAR * name, u32 mode, u32 uid,
				       u32 gid, const YCHAR * alias);
YCHAR *yaffs_get_symlink_alias(struct yaffs_obj *obj);

/* Special inodes (fifos, sockets and devices) */
struct yaffs_obj *yaffs_create_special(struct yaffs_obj *parent,
				       const YCHAR * name, u32 mode, u32 uid,
				       u32 gid, u32 rdev);

int yaffs_set_xattrib(struct yaffs_obj *obj, const YCHAR * name,
		      const void *value, int size, int flags);
int yaffs_get_xattrib(struct yaffs_obj *obj, const YCHAR * name, void *value,
		      int size);
int yaffs_list_xattrib(struct yaffs_obj *obj, char *buffer, int size);
int yaffs_remove_xattrib(struct yaffs_obj *obj, const YCHAR * name);

/* Special directories */
struct yaffs_obj *yaffs_root(struct yaffs_dev *dev);
struct yaffs_obj *yaffs_lost_n_found(struct yaffs_dev *dev);

void yaffs_handle_defered_free(struct yaffs_obj *obj);

void yaffs_update_dirty_dirs(struct yaffs_dev *dev);

int yaffs_bg_gc(struct yaffs_dev *dev, unsigned urgency);

/* Debug dump  */
int yaffs_dump_obj(struct yaffs_obj *obj);

void yaffs_guts_test(struct yaffs_dev *dev);

/* A few useful functions to be used within the core files*/
void yaffs_chunk_del(struct yaffs_dev *dev, int chunk_id, int mark_flash,
		     int lyn);
int yaffs_check_ff(u8 * buffer, int n_bytes);
void yaffs_handle_chunk_error(struct yaffs_dev *dev,
			      struct yaffs_block_info *bi);

u8 *yaffs_get_temp_buffer(struct yaffs_dev *dev, int line_no);
void yaffs_release_temp_buffer(struct yaffs_dev *dev, u8 * buffer, int line_no);

struct yaffs_obj *yaffs_find_or_create_by_number(struct yaffs_dev *dev,
						 int number,
						 enum yaffs_obj_type type);
int yaffs_put_chunk_in_file(struct yaffs_obj *in, int inode_chunk,
			    int nand_chunk, int in_scan);
void yaffs_set_obj_name(struct yaffs_obj *obj, const YCHAR * name);
void yaffs_set_obj_name_from_oh(struct yaffs_obj *obj,
				const struct yaffs_obj_hdr *oh);
void yaffs_add_obj_to_dir(struct yaffs_obj *directory, struct yaffs_obj *obj);
YCHAR *yaffs_clone_str(const YCHAR * str);
void yaffs_link_fixup(struct yaffs_dev *dev, struct yaffs_obj *hard_list);
void yaffs_block_became_dirty(struct yaffs_dev *dev, int block_no);
int yaffs_update_oh(struct yaffs_obj *in, const YCHAR * name,
		    int force, int is_shrink, int shadows,
		    struct yaffs_xattr_mod *xop);
void yaffs_handle_shadowed_obj(struct yaffs_dev *dev, int obj_id,
			       int backward_scanning);
int yaffs_check_alloc_available(struct yaffs_dev *dev, int n_chunks);
struct yaffs_tnode *yaffs_get_tnode(struct yaffs_dev *dev);
struct yaffs_tnode *yaffs_add_find_tnode_0(struct yaffs_dev *dev,
					   struct yaffs_file_var *file_struct,
					   u32 chunk_id,
					   struct yaffs_tnode *passed_tn);

int yaffs_do_file_wr(struct yaffs_obj *in, const u8 * buffer, loff_t offset,
		     int n_bytes, int write_trhrough);
void yaffs_resize_file_down(struct yaffs_obj *obj, loff_t new_size);
void yaffs_skip_rest_of_block(struct yaffs_dev *dev);

int yaffs_count_free_chunks(struct yaffs_dev *dev);

struct yaffs_tnode *yaffs_find_tnode_0(struct yaffs_dev *dev,
				       struct yaffs_file_var *file_struct,
				       u32 chunk_id);

u32 yaffs_get_group_base(struct yaffs_dev *dev, struct yaffs_tnode *tn,
			 unsigned pos);

int yaffs_is_non_empty_dir(struct yaffs_obj *obj);
#endif
