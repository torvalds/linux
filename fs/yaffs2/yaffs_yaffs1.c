/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "yaffs_yaffs1.h"
#include "yportenv.h"
#include "yaffs_trace.h"
#include "yaffs_bitmap.h"
#include "yaffs_getblockinfo.h"
#include "yaffs_nand.h"
#include "yaffs_attribs.h"

int yaffs1_scan(struct yaffs_dev *dev)
{
	struct yaffs_ext_tags tags;
	int blk;
	int result;

	int chunk;
	int c;
	int deleted;
	enum yaffs_block_state state;
	struct yaffs_obj *hard_list = NULL;
	struct yaffs_block_info *bi;
	u32 seq_number;
	struct yaffs_obj_hdr *oh;
	struct yaffs_obj *in;
	struct yaffs_obj *parent;

	int alloc_failed = 0;

	struct yaffs_shadow_fixer *shadow_fixers = NULL;

	u8 *chunk_data;

	yaffs_trace(YAFFS_TRACE_SCAN,
		"yaffs1_scan starts  intstartblk %d intendblk %d...",
		dev->internal_start_block, dev->internal_end_block);

	chunk_data = yaffs_get_temp_buffer(dev, __LINE__);

	dev->seq_number = YAFFS_LOWEST_SEQUENCE_NUMBER;

	/* Scan all the blocks to determine their state */
	bi = dev->block_info;
	for (blk = dev->internal_start_block; blk <= dev->internal_end_block;
	     blk++) {
		yaffs_clear_chunk_bits(dev, blk);
		bi->pages_in_use = 0;
		bi->soft_del_pages = 0;

		yaffs_query_init_block_state(dev, blk, &state, &seq_number);

		bi->block_state = state;
		bi->seq_number = seq_number;

		if (bi->seq_number == YAFFS_SEQUENCE_BAD_BLOCK)
			bi->block_state = state = YAFFS_BLOCK_STATE_DEAD;

		yaffs_trace(YAFFS_TRACE_SCAN_DEBUG,
			"Block scanning block %d state %d seq %d",
			blk, state, seq_number);

		if (state == YAFFS_BLOCK_STATE_DEAD) {
			yaffs_trace(YAFFS_TRACE_BAD_BLOCKS,
				"block %d is bad", blk);
		} else if (state == YAFFS_BLOCK_STATE_EMPTY) {
			yaffs_trace(YAFFS_TRACE_SCAN_DEBUG, "Block empty ");
			dev->n_erased_blocks++;
			dev->n_free_chunks += dev->param.chunks_per_block;
		}
		bi++;
	}

	/* For each block.... */
	for (blk = dev->internal_start_block;
	     !alloc_failed && blk <= dev->internal_end_block; blk++) {

		cond_resched();

		bi = yaffs_get_block_info(dev, blk);
		state = bi->block_state;

		deleted = 0;

		/* For each chunk in each block that needs scanning.... */
		for (c = 0; !alloc_failed && c < dev->param.chunks_per_block &&
		     state == YAFFS_BLOCK_STATE_NEEDS_SCANNING; c++) {
			/* Read the tags and decide what to do */
			chunk = blk * dev->param.chunks_per_block + c;

			result = yaffs_rd_chunk_tags_nand(dev, chunk, NULL,
							  &tags);

			/* Let's have a good look at this chunk... */

			if (tags.ecc_result == YAFFS_ECC_RESULT_UNFIXED
			    || tags.is_deleted) {
				/* YAFFS1 only...
				 * A deleted chunk
				 */
				deleted++;
				dev->n_free_chunks++;
				/*T((" %d %d deleted\n",blk,c)); */
			} else if (!tags.chunk_used) {
				/* An unassigned chunk in the block
				 * This means that either the block is empty or
				 * this is the one being allocated from
				 */

				if (c == 0) {
					/* We're looking at the first chunk in the block so the block is unused */
					state = YAFFS_BLOCK_STATE_EMPTY;
					dev->n_erased_blocks++;
				} else {
					/* this is the block being allocated from */
					yaffs_trace(YAFFS_TRACE_SCAN,
						" Allocating from %d %d",
						blk, c);
					state = YAFFS_BLOCK_STATE_ALLOCATING;
					dev->alloc_block = blk;
					dev->alloc_page = c;
					dev->alloc_block_finder = blk;
					/* Set block finder here to encourage the allocator to go forth from here. */

				}

				dev->n_free_chunks +=
				    (dev->param.chunks_per_block - c);
			} else if (tags.chunk_id > 0) {
				/* chunk_id > 0 so it is a data chunk... */
				unsigned int endpos;

				yaffs_set_chunk_bit(dev, blk, c);
				bi->pages_in_use++;

				in = yaffs_find_or_create_by_number(dev,
								    tags.obj_id,
								    YAFFS_OBJECT_TYPE_FILE);
				/* PutChunkIntoFile checks for a clash (two data chunks with
				 * the same chunk_id).
				 */

				if (!in)
					alloc_failed = 1;

				if (in) {
					if (!yaffs_put_chunk_in_file
					    (in, tags.chunk_id, chunk, 1))
						alloc_failed = 1;
				}

				endpos =
				    (tags.chunk_id -
				     1) * dev->data_bytes_per_chunk +
				    tags.n_bytes;
				if (in
				    && in->variant_type ==
				    YAFFS_OBJECT_TYPE_FILE
				    && in->variant.file_variant.scanned_size <
				    endpos) {
					in->variant.file_variant.scanned_size =
					    endpos;
					if (!dev->param.use_header_file_size) {
						in->variant.
						    file_variant.file_size =
						    in->variant.
						    file_variant.scanned_size;
					}

				}
				/* T((" %d %d data %d %d\n",blk,c,tags.obj_id,tags.chunk_id));   */
			} else {
				/* chunk_id == 0, so it is an ObjectHeader.
				 * Thus, we read in the object header and make the object
				 */
				yaffs_set_chunk_bit(dev, blk, c);
				bi->pages_in_use++;

				result = yaffs_rd_chunk_tags_nand(dev, chunk,
								  chunk_data,
								  NULL);

				oh = (struct yaffs_obj_hdr *)chunk_data;

				in = yaffs_find_by_number(dev, tags.obj_id);
				if (in && in->variant_type != oh->type) {
					/* This should not happen, but somehow
					 * Wev'e ended up with an obj_id that has been reused but not yet
					 * deleted, and worse still it has changed type. Delete the old object.
					 */

					yaffs_del_obj(in);

					in = 0;
				}

				in = yaffs_find_or_create_by_number(dev,
								    tags.obj_id,
								    oh->type);

				if (!in)
					alloc_failed = 1;

				if (in && oh->shadows_obj > 0) {

					struct yaffs_shadow_fixer *fixer;
					fixer =
						kmalloc(sizeof
						(struct yaffs_shadow_fixer),
						GFP_NOFS);
					if (fixer) {
						fixer->next = shadow_fixers;
						shadow_fixers = fixer;
						fixer->obj_id = tags.obj_id;
						fixer->shadowed_id =
						    oh->shadows_obj;
						yaffs_trace(YAFFS_TRACE_SCAN,
							" Shadow fixer: %d shadows %d",
							fixer->obj_id,
							fixer->shadowed_id);

					}

				}

				if (in && in->valid) {
					/* We have already filled this one. We have a duplicate and need to resolve it. */

					unsigned existing_serial = in->serial;
					unsigned new_serial =
					    tags.serial_number;

					if (((existing_serial + 1) & 3) ==
					    new_serial) {
						/* Use new one - destroy the exisiting one */
						yaffs_chunk_del(dev,
								in->hdr_chunk,
								1, __LINE__);
						in->valid = 0;
					} else {
						/* Use existing - destroy this one. */
						yaffs_chunk_del(dev, chunk, 1,
								__LINE__);
					}
				}

				if (in && !in->valid &&
				    (tags.obj_id == YAFFS_OBJECTID_ROOT ||
				     tags.obj_id ==
				     YAFFS_OBJECTID_LOSTNFOUND)) {
					/* We only load some info, don't fiddle with directory structure */
					in->valid = 1;
					in->variant_type = oh->type;

					in->yst_mode = oh->yst_mode;
					yaffs_load_attribs(in, oh);
					in->hdr_chunk = chunk;
					in->serial = tags.serial_number;

				} else if (in && !in->valid) {
					/* we need to load this info */

					in->valid = 1;
					in->variant_type = oh->type;

					in->yst_mode = oh->yst_mode;
					yaffs_load_attribs(in, oh);
					in->hdr_chunk = chunk;
					in->serial = tags.serial_number;

					yaffs_set_obj_name_from_oh(in, oh);
					in->dirty = 0;

					/* directory stuff...
					 * hook up to parent
					 */

					parent =
					    yaffs_find_or_create_by_number
					    (dev, oh->parent_obj_id,
					     YAFFS_OBJECT_TYPE_DIRECTORY);
					if (!parent)
						alloc_failed = 1;
					if (parent && parent->variant_type ==
					    YAFFS_OBJECT_TYPE_UNKNOWN) {
						/* Set up as a directory */
						parent->variant_type =
						    YAFFS_OBJECT_TYPE_DIRECTORY;
						INIT_LIST_HEAD(&parent->
							       variant.dir_variant.children);
					} else if (!parent
						   || parent->variant_type !=
						   YAFFS_OBJECT_TYPE_DIRECTORY) {
						/* Hoosterman, another problem....
						 * We're trying to use a non-directory as a directory
						 */

						yaffs_trace(YAFFS_TRACE_ERROR,
							"yaffs tragedy: attempting to use non-directory as a directory in scan. Put in lost+found."
							);
						parent = dev->lost_n_found;
					}

					yaffs_add_obj_to_dir(parent, in);

					if (0 && (parent == dev->del_dir ||
						  parent ==
						  dev->unlinked_dir)) {
						in->deleted = 1;	/* If it is unlinked at start up then it wants deleting */
						dev->n_deleted_files++;
					}
					/* Note re hardlinks.
					 * Since we might scan a hardlink before its equivalent object is scanned
					 * we put them all in a list.
					 * After scanning is complete, we should have all the objects, so we run through this
					 * list and fix up all the chains.
					 */

					switch (in->variant_type) {
					case YAFFS_OBJECT_TYPE_UNKNOWN:
						/* Todo got a problem */
						break;
					case YAFFS_OBJECT_TYPE_FILE:
						if (dev->param.
						    use_header_file_size)

							in->variant.
							    file_variant.file_size
							    = oh->file_size;

						break;
					case YAFFS_OBJECT_TYPE_HARDLINK:
						in->variant.
						    hardlink_variant.equiv_id =
						    oh->equiv_id;
						in->hard_links.next =
						    (struct list_head *)
						    hard_list;
						hard_list = in;
						break;
					case YAFFS_OBJECT_TYPE_DIRECTORY:
						/* Do nothing */
						break;
					case YAFFS_OBJECT_TYPE_SPECIAL:
						/* Do nothing */
						break;
					case YAFFS_OBJECT_TYPE_SYMLINK:
						in->variant.symlink_variant.
						    alias =
						    yaffs_clone_str(oh->alias);
						if (!in->variant.
						    symlink_variant.alias)
							alloc_failed = 1;
						break;
					}

				}
			}
		}

		if (state == YAFFS_BLOCK_STATE_NEEDS_SCANNING) {
			/* If we got this far while scanning, then the block is fully allocated. */
			state = YAFFS_BLOCK_STATE_FULL;
		}

		if (state == YAFFS_BLOCK_STATE_ALLOCATING) {
			/* If the block was partially allocated then treat it as fully allocated. */
			state = YAFFS_BLOCK_STATE_FULL;
			dev->alloc_block = -1;
		}

		bi->block_state = state;

		/* Now let's see if it was dirty */
		if (bi->pages_in_use == 0 &&
		    !bi->has_shrink_hdr &&
		    bi->block_state == YAFFS_BLOCK_STATE_FULL) {
			yaffs_block_became_dirty(dev, blk);
		}

	}

	/* Ok, we've done all the scanning.
	 * Fix up the hard link chains.
	 * We should now have scanned all the objects, now it's time to add these
	 * hardlinks.
	 */

	yaffs_link_fixup(dev, hard_list);

	/* Fix up any shadowed objects */
	{
		struct yaffs_shadow_fixer *fixer;
		struct yaffs_obj *obj;

		while (shadow_fixers) {
			fixer = shadow_fixers;
			shadow_fixers = fixer->next;
			/* Complete the rename transaction by deleting the shadowed object
			 * then setting the object header to unshadowed.
			 */
			obj = yaffs_find_by_number(dev, fixer->shadowed_id);
			if (obj)
				yaffs_del_obj(obj);

			obj = yaffs_find_by_number(dev, fixer->obj_id);

			if (obj)
				yaffs_update_oh(obj, NULL, 1, 0, 0, NULL);

			kfree(fixer);
		}
	}

	yaffs_release_temp_buffer(dev, chunk_data, __LINE__);

	if (alloc_failed)
		return YAFFS_FAIL;

	yaffs_trace(YAFFS_TRACE_SCAN, "yaffs1_scan ends");

	return YAFFS_OK;
}
