/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018 Google LLC
 */

/*
 * Overview
 * --------
 * The backbone of the incremental-fs ondisk format is an append only linked
 * list of metadata blocks. Each metadata block contains an offset of the next
 * one. These blocks describe files and directories on the
 * file system. They also represent actions of adding and removing file names
 * (hard links).
 *
 * Every time incremental-fs instance is mounted, it reads through this list
 * to recreate filesystem's state in memory. An offset of the first record in
 * the metadata list is stored in the superblock at the beginning of the backing
 * file.
 *
 * Most of the backing file is taken by data areas and blockmaps.
 * Since data blocks can be compressed and have different sizes,
 * single per-file data area can't be pre-allocated. That's why blockmaps are
 * needed in order to find a location and size of each data block in
 * the backing file. Each time a file is created, a corresponding block map is
 * allocated to store future offsets of data blocks.
 *
 * Whenever a data block is given by data loader to incremental-fs:
 *   - A data area with the given block is appended to the end of
 *     the backing file.
 *   - A record in the blockmap for the given block index is updated to reflect
 *     its location, size, and compression algorithm.

 * Metadata records
 * ----------------
 * incfs_blockmap - metadata record that specifies size and location
 *                           of a blockmap area for a given file. This area
 *                           contains an array of incfs_blockmap_entry-s.
 * incfs_file_signature - metadata record that specifies where file signature
 *                           and its hash tree can be found in the backing file.
 *
 * incfs_file_attr - metadata record that specifies where additional file
 *		        attributes blob can be found.
 *
 * Metadata header
 * ---------------
 * incfs_md_header - header of a metadata record. It's always a part
 *                   of other structures and served purpose of metadata
 *                   bookkeeping.
 *
 *              +-----------------------------------------------+       ^
 *              |            incfs_md_header                    |       |
 *              | 1. type of body(BLOCKMAP, FILE_ATTR..)        |       |
 *              | 2. size of the whole record header + body     |       |
 *              | 3. CRC the whole record header + body         |       |
 *              | 4. offset of the previous md record           |]------+
 *              | 5. offset of the next md record (md link)     |]---+
 *              +-----------------------------------------------+    |
 *              |  Metadata record body with useful data        |    |
 *              +-----------------------------------------------+    |
 *                                                                   +--->
 *
 * Other ondisk structures
 * -----------------------
 * incfs_super_block - backing file header
 * incfs_blockmap_entry - a record in a blockmap area that describes size
 *                       and location of a data block.
 * Data blocks dont have any particular structure, they are written to the
 * backing file in a raw form as they come from a data loader.
 *
 * Backing file layout
 * -------------------
 *
 *
 *              +-------------------------------------------+
 *              |            incfs_file_header              |]---+
 *              +-------------------------------------------+    |
 *              |                 metadata                  |<---+
 *              |           incfs_file_signature            |]---+
 *              +-------------------------------------------+    |
 *                        .........................              |
 *              +-------------------------------------------+    |   metadata
 *     +------->|               blockmap area               |    |  list links
 *     |        |          [incfs_blockmap_entry]           |    |
 *     |        |          [incfs_blockmap_entry]           |    |
 *     |        |          [incfs_blockmap_entry]           |    |
 *     |    +--[|          [incfs_blockmap_entry]           |    |
 *     |    |   |          [incfs_blockmap_entry]           |    |
 *     |    |   |          [incfs_blockmap_entry]           |    |
 *     |    |   +-------------------------------------------+    |
 *     |    |             .........................              |
 *     |    |   +-------------------------------------------+    |
 *     |    |   |                 metadata                  |<---+
 *     +----|--[|               incfs_blockmap              |]---+
 *          |   +-------------------------------------------+    |
 *          |             .........................              |
 *          |   +-------------------------------------------+    |
 *          +-->|                 data block                |    |
 *              +-------------------------------------------+    |
 *                        .........................              |
 *              +-------------------------------------------+    |
 *              |                 metadata                  |<---+
 *              |              incfs_file_attr              |
 *              +-------------------------------------------+
 */
#ifndef _INCFS_FORMAT_H
#define _INCFS_FORMAT_H
#include <linux/types.h>
#include <linux/kernel.h>
#include <uapi/linux/incrementalfs.h>

#include "internal.h"

#define INCFS_MAX_NAME_LEN 255
#define INCFS_FORMAT_V1 1
#define INCFS_FORMAT_CURRENT_VER INCFS_FORMAT_V1

enum incfs_metadata_type {
	INCFS_MD_NONE = 0,
	INCFS_MD_BLOCK_MAP = 1,
	INCFS_MD_FILE_ATTR = 2,
	INCFS_MD_SIGNATURE = 3,
	INCFS_MD_STATUS = 4,
};

enum incfs_file_header_flags {
	INCFS_FILE_COMPLETE = 1 << 0,
	INCFS_FILE_MAPPED = 1 << 1,
};

/* Header included at the beginning of all metadata records on the disk. */
struct incfs_md_header {
	__u8 h_md_entry_type;

	/*
	 * Size of the metadata record.
	 * (e.g. inode, dir entry etc) not just this struct.
	 */
	__le16 h_record_size;

	/*
	 * Was: CRC32 of the metadata record.
	 * (e.g. inode, dir entry etc) not just this struct.
	 */
	__le32 h_unused1;

	/* Offset of the next metadata entry if any */
	__le64 h_next_md_offset;

	/* Was: Offset of the previous metadata entry if any */
	__le64 h_unused2;

} __packed;

/* Backing file header */
struct incfs_file_header {
	/* Magic number: INCFS_MAGIC_NUMBER */
	__le64 fh_magic;

	/* Format version: INCFS_FORMAT_CURRENT_VER */
	__le64 fh_version;

	/* sizeof(incfs_file_header) */
	__le16 fh_header_size;

	/* INCFS_DATA_FILE_BLOCK_SIZE */
	__le16 fh_data_block_size;

	/* File flags, from incfs_file_header_flags */
	__le32 fh_flags;

	union {
		/* Standard incfs file */
		struct {
			/* Offset of the first metadata record */
			__le64 fh_first_md_offset;

			/* Full size of the file's content */
			__le64 fh_file_size;

			/* File uuid */
			incfs_uuid_t fh_uuid;
		};

		/* Mapped file - INCFS_FILE_MAPPED set in fh_flags */
		struct {
			/* Offset in original file */
			__le64 fh_original_offset;

			/* Full size of the file's content */
			__le64 fh_mapped_file_size;

			/* Original file's uuid */
			incfs_uuid_t fh_original_uuid;
		};
	};
} __packed;

enum incfs_block_map_entry_flags {
	INCFS_BLOCK_COMPRESSED_LZ4 = (1 << 0),
};

/* Block map entry pointing to an actual location of the data block. */
struct incfs_blockmap_entry {
	/* Offset of the actual data block. Lower 32 bits */
	__le32 me_data_offset_lo;

	/* Offset of the actual data block. Higher 16 bits */
	__le16 me_data_offset_hi;

	/* How many bytes the data actually occupies in the backing file */
	__le16 me_data_size;

	/* Block flags from incfs_block_map_entry_flags */
	__le16 me_flags;
} __packed;

/* Metadata record for locations of file blocks. Type = INCFS_MD_BLOCK_MAP */
struct incfs_blockmap {
	struct incfs_md_header m_header;

	/* Base offset of the array of incfs_blockmap_entry */
	__le64 m_base_offset;

	/* Size of the map entry array in blocks */
	__le32 m_block_count;
} __packed;

/* Metadata record for file signature. Type = INCFS_MD_SIGNATURE */
struct incfs_file_signature {
	struct incfs_md_header sg_header;

	__le32 sg_sig_size; /* The size of the signature. */

	__le64 sg_sig_offset; /* Signature's offset in the backing file */

	__le32 sg_hash_tree_size; /* The size of the hash tree. */

	__le64 sg_hash_tree_offset; /* Hash tree offset in the backing file */
} __packed;

/* In memory version of above */
struct incfs_df_signature {
	u32 sig_size;
	u64 sig_offset;
	u32 hash_size;
	u64 hash_offset;
};

struct incfs_status {
	struct incfs_md_header is_header;

	__le32 is_data_blocks_written; /* Number of data blocks written */

	__le32 is_hash_blocks_written; /* Number of hash blocks written */

	__le32 is_dummy[6]; /* Spare fields */
};

/* State of the backing file. */
struct backing_file_context {
	/* Protects writes to bc_file */
	struct mutex bc_mutex;

	/* File object to read data from */
	struct file *bc_file;

	/*
	 * Offset of the last known metadata record in the backing file.
	 * 0 means there are no metadata records.
	 */
	loff_t bc_last_md_record_offset;
};

struct metadata_handler {
	loff_t md_record_offset;
	loff_t md_prev_record_offset;
	void *context;

	union {
		struct incfs_md_header md_header;
		struct incfs_blockmap blockmap;
		struct incfs_file_signature signature;
		struct incfs_status status;
	} md_buffer;

	int (*handle_blockmap)(struct incfs_blockmap *bm,
			       struct metadata_handler *handler);
	int (*handle_signature)(struct incfs_file_signature *sig,
				 struct metadata_handler *handler);
	int (*handle_status)(struct incfs_status *sig,
				 struct metadata_handler *handler);
};
#define INCFS_MAX_METADATA_RECORD_SIZE \
	sizeof_field(struct metadata_handler, md_buffer)

loff_t incfs_get_end_offset(struct file *f);

/* Backing file context management */
struct backing_file_context *incfs_alloc_bfc(struct file *backing_file);

void incfs_free_bfc(struct backing_file_context *bfc);

/* Writing stuff */
int incfs_write_blockmap_to_backing_file(struct backing_file_context *bfc,
					 u32 block_count);

int incfs_write_fh_to_backing_file(struct backing_file_context *bfc,
				   incfs_uuid_t *uuid, u64 file_size);

int incfs_write_mapping_fh_to_backing_file(struct backing_file_context *bfc,
				incfs_uuid_t *uuid, u64 file_size, u64 offset);

int incfs_write_data_block_to_backing_file(struct backing_file_context *bfc,
					   struct mem_range block,
					   int block_index, loff_t bm_base_off,
					   u16 flags);

int incfs_write_hash_block_to_backing_file(struct backing_file_context *bfc,
					   struct mem_range block,
					   int block_index,
					   loff_t hash_area_off,
					   loff_t bm_base_off,
					   loff_t file_size);

int incfs_write_signature_to_backing_file(struct backing_file_context *bfc,
					  struct mem_range sig, u32 tree_size);

int incfs_write_status_to_backing_file(struct backing_file_context *bfc,
				       loff_t status_offset,
				       u32 data_blocks_written,
				       u32 hash_blocks_written);

int incfs_write_file_header_flags(struct backing_file_context *bfc, u32 flags);

int incfs_make_empty_backing_file(struct backing_file_context *bfc,
				  incfs_uuid_t *uuid, u64 file_size);

/* Reading stuff */
int incfs_read_file_header(struct backing_file_context *bfc,
			   loff_t *first_md_off, incfs_uuid_t *uuid,
			   u64 *file_size, u32 *flags);

int incfs_read_blockmap_entry(struct backing_file_context *bfc, int block_index,
			      loff_t bm_base_off,
			      struct incfs_blockmap_entry *bm_entry);

int incfs_read_blockmap_entries(struct backing_file_context *bfc,
		struct incfs_blockmap_entry *entries,
		int start_index, int blocks_number,
		loff_t bm_base_off);

int incfs_read_next_metadata_record(struct backing_file_context *bfc,
				    struct metadata_handler *handler);

ssize_t incfs_kread(struct file *f, void *buf, size_t size, loff_t pos);
ssize_t incfs_kwrite(struct file *f, const void *buf, size_t size, loff_t pos);

#endif /* _INCFS_FORMAT_H */
