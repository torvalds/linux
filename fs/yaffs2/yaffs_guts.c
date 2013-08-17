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

#include "yportenv.h"
#include "yaffs_trace.h"

#include "yaffs_guts.h"
#include "yaffs_tagsvalidity.h"
#include "yaffs_getblockinfo.h"

#include "yaffs_tagscompat.h"

#include "yaffs_nand.h"

#include "yaffs_yaffs1.h"
#include "yaffs_yaffs2.h"
#include "yaffs_bitmap.h"
#include "yaffs_verify.h"

#include "yaffs_nand.h"
#include "yaffs_packedtags2.h"

#include "yaffs_nameval.h"
#include "yaffs_allocator.h"

#include "yaffs_attribs.h"

/* Note YAFFS_GC_GOOD_ENOUGH must be <= YAFFS_GC_PASSIVE_THRESHOLD */
#define YAFFS_GC_GOOD_ENOUGH 2
#define YAFFS_GC_PASSIVE_THRESHOLD 4

#include "yaffs_ecc.h"

/* Forward declarations */

static int yaffs_wr_data_obj(struct yaffs_obj *in, int inode_chunk,
			     const u8 * buffer, int n_bytes, int use_reserve);



/* Function to calculate chunk and offset */

static void yaffs_addr_to_chunk(struct yaffs_dev *dev, loff_t addr,
				int *chunk_out, u32 * offset_out)
{
	int chunk;
	u32 offset;

	chunk = (u32) (addr >> dev->chunk_shift);

	if (dev->chunk_div == 1) {
		/* easy power of 2 case */
		offset = (u32) (addr & dev->chunk_mask);
	} else {
		/* Non power-of-2 case */

		loff_t chunk_base;

		chunk /= dev->chunk_div;

		chunk_base = ((loff_t) chunk) * dev->data_bytes_per_chunk;
		offset = (u32) (addr - chunk_base);
	}

	*chunk_out = chunk;
	*offset_out = offset;
}

/* Function to return the number of shifts for a power of 2 greater than or
 * equal to the given number
 * Note we don't try to cater for all possible numbers and this does not have to
 * be hellishly efficient.
 */

static u32 calc_shifts_ceiling(u32 x)
{
	int extra_bits;
	int shifts;

	shifts = extra_bits = 0;

	while (x > 1) {
		if (x & 1)
			extra_bits++;
		x >>= 1;
		shifts++;
	}

	if (extra_bits)
		shifts++;

	return shifts;
}

/* Function to return the number of shifts to get a 1 in bit 0
 */

static u32 calc_shifts(u32 x)
{
	u32 shifts;

	shifts = 0;

	if (!x)
		return 0;

	while (!(x & 1)) {
		x >>= 1;
		shifts++;
	}

	return shifts;
}

/*
 * Temporary buffer manipulations.
 */

static int yaffs_init_tmp_buffers(struct yaffs_dev *dev)
{
	int i;
	u8 *buf = (u8 *) 1;

	memset(dev->temp_buffer, 0, sizeof(dev->temp_buffer));

	for (i = 0; buf && i < YAFFS_N_TEMP_BUFFERS; i++) {
		dev->temp_buffer[i].line = 0;	/* not in use */
		dev->temp_buffer[i].buffer = buf =
		    kmalloc(dev->param.total_bytes_per_chunk, GFP_NOFS);
	}

	return buf ? YAFFS_OK : YAFFS_FAIL;
}

u8 *yaffs_get_temp_buffer(struct yaffs_dev * dev, int line_no)
{
	int i, j;

	dev->temp_in_use++;
	if (dev->temp_in_use > dev->max_temp)
		dev->max_temp = dev->temp_in_use;

	for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++) {
		if (dev->temp_buffer[i].line == 0) {
			dev->temp_buffer[i].line = line_no;
			if ((i + 1) > dev->max_temp) {
				dev->max_temp = i + 1;
				for (j = 0; j <= i; j++)
					dev->temp_buffer[j].max_line =
					    dev->temp_buffer[j].line;
			}

			return dev->temp_buffer[i].buffer;
		}
	}

	yaffs_trace(YAFFS_TRACE_BUFFERS,
		"Out of temp buffers at line %d, other held by lines:",
		line_no);
	for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++)
		yaffs_trace(YAFFS_TRACE_BUFFERS," %d", dev->temp_buffer[i].line);

	/*
	 * If we got here then we have to allocate an unmanaged one
	 * This is not good.
	 */

	dev->unmanaged_buffer_allocs++;
	return kmalloc(dev->data_bytes_per_chunk, GFP_NOFS);

}

void yaffs_release_temp_buffer(struct yaffs_dev *dev, u8 * buffer, int line_no)
{
	int i;

	dev->temp_in_use--;

	for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++) {
		if (dev->temp_buffer[i].buffer == buffer) {
			dev->temp_buffer[i].line = 0;
			return;
		}
	}

	if (buffer) {
		/* assume it is an unmanaged one. */
		yaffs_trace(YAFFS_TRACE_BUFFERS,
		  "Releasing unmanaged temp buffer in line %d",
		   line_no);
		kfree(buffer);
		dev->unmanaged_buffer_deallocs++;
	}

}

/*
 * Determine if we have a managed buffer.
 */
int yaffs_is_managed_tmp_buffer(struct yaffs_dev *dev, const u8 * buffer)
{
	int i;

	for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++) {
		if (dev->temp_buffer[i].buffer == buffer)
			return 1;
	}

	for (i = 0; i < dev->param.n_caches; i++) {
		if (dev->cache[i].data == buffer)
			return 1;
	}

	if (buffer == dev->checkpt_buffer)
		return 1;

	yaffs_trace(YAFFS_TRACE_ALWAYS,
	  "yaffs: unmaged buffer detected.");
	return 0;
}

/*
 * Functions for robustisizing TODO
 *
 */

static void yaffs_handle_chunk_wr_ok(struct yaffs_dev *dev, int nand_chunk,
				     const u8 * data,
				     const struct yaffs_ext_tags *tags)
{
	dev = dev;
	nand_chunk = nand_chunk;
	data = data;
	tags = tags;
}

static void yaffs_handle_chunk_update(struct yaffs_dev *dev, int nand_chunk,
				      const struct yaffs_ext_tags *tags)
{
	dev = dev;
	nand_chunk = nand_chunk;
	tags = tags;
}

void yaffs_handle_chunk_error(struct yaffs_dev *dev,
			      struct yaffs_block_info *bi)
{
	if (!bi->gc_prioritise) {
		bi->gc_prioritise = 1;
		dev->has_pending_prioritised_gc = 1;
		bi->chunk_error_strikes++;

		if (bi->chunk_error_strikes > 3) {
			bi->needs_retiring = 1;	/* Too many stikes, so retire this */
			yaffs_trace(YAFFS_TRACE_ALWAYS, "yaffs: Block struck out");

		}
	}
}

static void yaffs_handle_chunk_wr_error(struct yaffs_dev *dev, int nand_chunk,
					int erased_ok)
{
	int flash_block = nand_chunk / dev->param.chunks_per_block;
	struct yaffs_block_info *bi = yaffs_get_block_info(dev, flash_block);

	yaffs_handle_chunk_error(dev, bi);

	if (erased_ok) {
		/* Was an actual write failure, so mark the block for retirement  */
		bi->needs_retiring = 1;
		yaffs_trace(YAFFS_TRACE_ERROR | YAFFS_TRACE_BAD_BLOCKS,
		  "**>> Block %d needs retiring", flash_block);
	}

	/* Delete the chunk */
	yaffs_chunk_del(dev, nand_chunk, 1, __LINE__);
	yaffs_skip_rest_of_block(dev);
}

/*
 * Verification code
 */

/*
 *  Simple hash function. Needs to have a reasonable spread
 */

static inline int yaffs_hash_fn(int n)
{
	n = abs(n);
	return n % YAFFS_NOBJECT_BUCKETS;
}

/*
 * Access functions to useful fake objects.
 * Note that root might have a presence in NAND if permissions are set.
 */

struct yaffs_obj *yaffs_root(struct yaffs_dev *dev)
{
	return dev->root_dir;
}

struct yaffs_obj *yaffs_lost_n_found(struct yaffs_dev *dev)
{
	return dev->lost_n_found;
}

/*
 *  Erased NAND checking functions
 */

int yaffs_check_ff(u8 * buffer, int n_bytes)
{
	/* Horrible, slow implementation */
	while (n_bytes--) {
		if (*buffer != 0xFF)
			return 0;
		buffer++;
	}
	return 1;
}

static int yaffs_check_chunk_erased(struct yaffs_dev *dev, int nand_chunk)
{
	int retval = YAFFS_OK;
	u8 *data = yaffs_get_temp_buffer(dev, __LINE__);
	struct yaffs_ext_tags tags;
	int result;

	result = yaffs_rd_chunk_tags_nand(dev, nand_chunk, data, &tags);

	if (tags.ecc_result > YAFFS_ECC_RESULT_NO_ERROR)
		retval = YAFFS_FAIL;

	if (!yaffs_check_ff(data, dev->data_bytes_per_chunk) ||
		tags.chunk_used) {
		yaffs_trace(YAFFS_TRACE_NANDACCESS, "Chunk %d not erased", nand_chunk);
		retval = YAFFS_FAIL;
	}

	yaffs_release_temp_buffer(dev, data, __LINE__);

	return retval;

}

static int yaffs_verify_chunk_written(struct yaffs_dev *dev,
				      int nand_chunk,
				      const u8 * data,
				      struct yaffs_ext_tags *tags)
{
	int retval = YAFFS_OK;
	struct yaffs_ext_tags temp_tags;
	u8 *buffer = yaffs_get_temp_buffer(dev, __LINE__);
	int result;

	result = yaffs_rd_chunk_tags_nand(dev, nand_chunk, buffer, &temp_tags);
	if (memcmp(buffer, data, dev->data_bytes_per_chunk) ||
	    temp_tags.obj_id != tags->obj_id ||
	    temp_tags.chunk_id != tags->chunk_id ||
	    temp_tags.n_bytes != tags->n_bytes)
		retval = YAFFS_FAIL;

	yaffs_release_temp_buffer(dev, buffer, __LINE__);

	return retval;
}


int yaffs_check_alloc_available(struct yaffs_dev *dev, int n_chunks)
{
	int reserved_chunks;
	int reserved_blocks = dev->param.n_reserved_blocks;
	int checkpt_blocks;

	checkpt_blocks = yaffs_calc_checkpt_blocks_required(dev);

	reserved_chunks =
	    ((reserved_blocks + checkpt_blocks) * dev->param.chunks_per_block);

	return (dev->n_free_chunks > (reserved_chunks + n_chunks));
}

static int yaffs_find_alloc_block(struct yaffs_dev *dev)
{
	int i;

	struct yaffs_block_info *bi;

	if (dev->n_erased_blocks < 1) {
		/* Hoosterman we've got a problem.
		 * Can't get space to gc
		 */
		yaffs_trace(YAFFS_TRACE_ERROR,
		  "yaffs tragedy: no more erased blocks" );

		return -1;
	}

	/* Find an empty block. */

	for (i = dev->internal_start_block; i <= dev->internal_end_block; i++) {
		dev->alloc_block_finder++;
		if (dev->alloc_block_finder < dev->internal_start_block
		    || dev->alloc_block_finder > dev->internal_end_block) {
			dev->alloc_block_finder = dev->internal_start_block;
		}

		bi = yaffs_get_block_info(dev, dev->alloc_block_finder);

		if (bi->block_state == YAFFS_BLOCK_STATE_EMPTY) {
			bi->block_state = YAFFS_BLOCK_STATE_ALLOCATING;
			dev->seq_number++;
			bi->seq_number = dev->seq_number;
			dev->n_erased_blocks--;
			yaffs_trace(YAFFS_TRACE_ALLOCATE,
			  "Allocated block %d, seq  %d, %d left" ,
			   dev->alloc_block_finder, dev->seq_number,
			   dev->n_erased_blocks);
			return dev->alloc_block_finder;
		}
	}

	yaffs_trace(YAFFS_TRACE_ALWAYS,
		"yaffs tragedy: no more erased blocks, but there should have been %d",
		dev->n_erased_blocks);

	return -1;
}

static int yaffs_alloc_chunk(struct yaffs_dev *dev, int use_reserver,
			     struct yaffs_block_info **block_ptr)
{
	int ret_val;
	struct yaffs_block_info *bi;

	if (dev->alloc_block < 0) {
		/* Get next block to allocate off */
		dev->alloc_block = yaffs_find_alloc_block(dev);
		dev->alloc_page = 0;
	}

	if (!use_reserver && !yaffs_check_alloc_available(dev, 1)) {
		/* Not enough space to allocate unless we're allowed to use the reserve. */
		return -1;
	}

	if (dev->n_erased_blocks < dev->param.n_reserved_blocks
	    && dev->alloc_page == 0)
		yaffs_trace(YAFFS_TRACE_ALLOCATE, "Allocating reserve");

	/* Next page please.... */
	if (dev->alloc_block >= 0) {
		bi = yaffs_get_block_info(dev, dev->alloc_block);

		ret_val = (dev->alloc_block * dev->param.chunks_per_block) +
		    dev->alloc_page;
		bi->pages_in_use++;
		yaffs_set_chunk_bit(dev, dev->alloc_block, dev->alloc_page);

		dev->alloc_page++;

		dev->n_free_chunks--;

		/* If the block is full set the state to full */
		if (dev->alloc_page >= dev->param.chunks_per_block) {
			bi->block_state = YAFFS_BLOCK_STATE_FULL;
			dev->alloc_block = -1;
		}

		if (block_ptr)
			*block_ptr = bi;

		return ret_val;
	}

	yaffs_trace(YAFFS_TRACE_ERROR, "!!!!!!!!! Allocator out !!!!!!!!!!!!!!!!!" );

	return -1;
}

static int yaffs_get_erased_chunks(struct yaffs_dev *dev)
{
	int n;

	n = dev->n_erased_blocks * dev->param.chunks_per_block;

	if (dev->alloc_block > 0)
		n += (dev->param.chunks_per_block - dev->alloc_page);

	return n;

}

/*
 * yaffs_skip_rest_of_block() skips over the rest of the allocation block
 * if we don't want to write to it.
 */
void yaffs_skip_rest_of_block(struct yaffs_dev *dev)
{
	if (dev->alloc_block > 0) {
		struct yaffs_block_info *bi =
		    yaffs_get_block_info(dev, dev->alloc_block);
		if (bi->block_state == YAFFS_BLOCK_STATE_ALLOCATING) {
			bi->block_state = YAFFS_BLOCK_STATE_FULL;
			dev->alloc_block = -1;
		}
	}
}

static int yaffs_write_new_chunk(struct yaffs_dev *dev,
				 const u8 * data,
				 struct yaffs_ext_tags *tags, int use_reserver)
{
	int attempts = 0;
	int write_ok = 0;
	int chunk;

	yaffs2_checkpt_invalidate(dev);

	do {
		struct yaffs_block_info *bi = 0;
		int erased_ok = 0;

		chunk = yaffs_alloc_chunk(dev, use_reserver, &bi);
		if (chunk < 0) {
			/* no space */
			break;
		}

		/* First check this chunk is erased, if it needs
		 * checking.  The checking policy (unless forced
		 * always on) is as follows:
		 *
		 * Check the first page we try to write in a block.
		 * If the check passes then we don't need to check any
		 * more.        If the check fails, we check again...
		 * If the block has been erased, we don't need to check.
		 *
		 * However, if the block has been prioritised for gc,
		 * then we think there might be something odd about
		 * this block and stop using it.
		 *
		 * Rationale: We should only ever see chunks that have
		 * not been erased if there was a partially written
		 * chunk due to power loss.  This checking policy should
		 * catch that case with very few checks and thus save a
		 * lot of checks that are most likely not needed.
		 *
		 * Mods to the above
		 * If an erase check fails or the write fails we skip the 
		 * rest of the block.
		 */

		/* let's give it a try */
		attempts++;

		if (dev->param.always_check_erased)
			bi->skip_erased_check = 0;

		if (!bi->skip_erased_check) {
			erased_ok = yaffs_check_chunk_erased(dev, chunk);
			if (erased_ok != YAFFS_OK) {
				yaffs_trace(YAFFS_TRACE_ERROR,
				  "**>> yaffs chunk %d was not erased",
				  chunk);

				/* If not erased, delete this one,
				 * skip rest of block and
				 * try another chunk */
				yaffs_chunk_del(dev, chunk, 1, __LINE__);
				yaffs_skip_rest_of_block(dev);
				continue;
			}
		}

		write_ok = yaffs_wr_chunk_tags_nand(dev, chunk, data, tags);

		if (!bi->skip_erased_check)
			write_ok =
			    yaffs_verify_chunk_written(dev, chunk, data, tags);

		if (write_ok != YAFFS_OK) {
			/* Clean up aborted write, skip to next block and
			 * try another chunk */
			yaffs_handle_chunk_wr_error(dev, chunk, erased_ok);
			continue;
		}

		bi->skip_erased_check = 1;

		/* Copy the data into the robustification buffer */
		yaffs_handle_chunk_wr_ok(dev, chunk, data, tags);

	} while (write_ok != YAFFS_OK &&
		 (yaffs_wr_attempts <= 0 || attempts <= yaffs_wr_attempts));

	if (!write_ok)
		chunk = -1;

	if (attempts > 1) {
		yaffs_trace(YAFFS_TRACE_ERROR,
			"**>> yaffs write required %d attempts",
			attempts);
		dev->n_retired_writes += (attempts - 1);
	}

	return chunk;
}

/*
 * Block retiring for handling a broken block.
 */

static void yaffs_retire_block(struct yaffs_dev *dev, int flash_block)
{
	struct yaffs_block_info *bi = yaffs_get_block_info(dev, flash_block);

	yaffs2_checkpt_invalidate(dev);

	yaffs2_clear_oldest_dirty_seq(dev, bi);

	if (yaffs_mark_bad(dev, flash_block) != YAFFS_OK) {
		if (yaffs_erase_block(dev, flash_block) != YAFFS_OK) {
			yaffs_trace(YAFFS_TRACE_ALWAYS,
				"yaffs: Failed to mark bad and erase block %d",
				flash_block);
		} else {
			struct yaffs_ext_tags tags;
			int chunk_id =
			    flash_block * dev->param.chunks_per_block;

			u8 *buffer = yaffs_get_temp_buffer(dev, __LINE__);

			memset(buffer, 0xff, dev->data_bytes_per_chunk);
			yaffs_init_tags(&tags);
			tags.seq_number = YAFFS_SEQUENCE_BAD_BLOCK;
			if (dev->param.write_chunk_tags_fn(dev, chunk_id -
							   dev->chunk_offset,
							   buffer,
							   &tags) != YAFFS_OK)
				yaffs_trace(YAFFS_TRACE_ALWAYS,
					"yaffs: Failed to write bad block marker to block %d",
					flash_block);

			yaffs_release_temp_buffer(dev, buffer, __LINE__);
		}
	}

	bi->block_state = YAFFS_BLOCK_STATE_DEAD;
	bi->gc_prioritise = 0;
	bi->needs_retiring = 0;

	dev->n_retired_blocks++;
}

/*---------------- Name handling functions ------------*/

static u16 yaffs_calc_name_sum(const YCHAR * name)
{
	u16 sum = 0;
	u16 i = 1;

	const YUCHAR *bname = (const YUCHAR *)name;
	if (bname) {
		while ((*bname) && (i < (YAFFS_MAX_NAME_LENGTH / 2))) {

			/* 0x1f mask is case insensitive */
			sum += ((*bname) & 0x1f) * i;
			i++;
			bname++;
		}
	}
	return sum;
}

void yaffs_set_obj_name(struct yaffs_obj *obj, const YCHAR * name)
{
#ifndef CONFIG_YAFFS_NO_SHORT_NAMES
	memset(obj->short_name, 0, sizeof(obj->short_name));
	if (name && 
	        strnlen(name, YAFFS_SHORT_NAME_LENGTH + 1) <=
	    YAFFS_SHORT_NAME_LENGTH)
		strcpy(obj->short_name, name);
	else
		obj->short_name[0] = _Y('\0');
#endif
	obj->sum = yaffs_calc_name_sum(name);
}

void yaffs_set_obj_name_from_oh(struct yaffs_obj *obj,
				const struct yaffs_obj_hdr *oh)
{
#ifdef CONFIG_YAFFS_AUTO_UNICODE
	YCHAR tmp_name[YAFFS_MAX_NAME_LENGTH + 1];
	memset(tmp_name, 0, sizeof(tmp_name));
	yaffs_load_name_from_oh(obj->my_dev, tmp_name, oh->name,
				YAFFS_MAX_NAME_LENGTH + 1);
	yaffs_set_obj_name(obj, tmp_name);
#else
	yaffs_set_obj_name(obj, oh->name);
#endif
}

/*-------------------- TNODES -------------------

 * List of spare tnodes
 * The list is hooked together using the first pointer
 * in the tnode.
 */

struct yaffs_tnode *yaffs_get_tnode(struct yaffs_dev *dev)
{
	struct yaffs_tnode *tn = yaffs_alloc_raw_tnode(dev);
	if (tn) {
		memset(tn, 0, dev->tnode_size);
		dev->n_tnodes++;
	}

	dev->checkpoint_blocks_required = 0;	/* force recalculation */

	return tn;
}

/* FreeTnode frees up a tnode and puts it back on the free list */
static void yaffs_free_tnode(struct yaffs_dev *dev, struct yaffs_tnode *tn)
{
	yaffs_free_raw_tnode(dev, tn);
	dev->n_tnodes--;
	dev->checkpoint_blocks_required = 0;	/* force recalculation */
}

static void yaffs_deinit_tnodes_and_objs(struct yaffs_dev *dev)
{
	yaffs_deinit_raw_tnodes_and_objs(dev);
	dev->n_obj = 0;
	dev->n_tnodes = 0;
}

void yaffs_load_tnode_0(struct yaffs_dev *dev, struct yaffs_tnode *tn,
			unsigned pos, unsigned val)
{
	u32 *map = (u32 *) tn;
	u32 bit_in_map;
	u32 bit_in_word;
	u32 word_in_map;
	u32 mask;

	pos &= YAFFS_TNODES_LEVEL0_MASK;
	val >>= dev->chunk_grp_bits;

	bit_in_map = pos * dev->tnode_width;
	word_in_map = bit_in_map / 32;
	bit_in_word = bit_in_map & (32 - 1);

	mask = dev->tnode_mask << bit_in_word;

	map[word_in_map] &= ~mask;
	map[word_in_map] |= (mask & (val << bit_in_word));

	if (dev->tnode_width > (32 - bit_in_word)) {
		bit_in_word = (32 - bit_in_word);
		word_in_map++;;
		mask =
		    dev->tnode_mask >> ( /*dev->tnode_width - */ bit_in_word);
		map[word_in_map] &= ~mask;
		map[word_in_map] |= (mask & (val >> bit_in_word));
	}
}

u32 yaffs_get_group_base(struct yaffs_dev *dev, struct yaffs_tnode *tn,
			 unsigned pos)
{
	u32 *map = (u32 *) tn;
	u32 bit_in_map;
	u32 bit_in_word;
	u32 word_in_map;
	u32 val;

	pos &= YAFFS_TNODES_LEVEL0_MASK;

	bit_in_map = pos * dev->tnode_width;
	word_in_map = bit_in_map / 32;
	bit_in_word = bit_in_map & (32 - 1);

	val = map[word_in_map] >> bit_in_word;

	if (dev->tnode_width > (32 - bit_in_word)) {
		bit_in_word = (32 - bit_in_word);
		word_in_map++;;
		val |= (map[word_in_map] << bit_in_word);
	}

	val &= dev->tnode_mask;
	val <<= dev->chunk_grp_bits;

	return val;
}

/* ------------------- End of individual tnode manipulation -----------------*/

/* ---------Functions to manipulate the look-up tree (made up of tnodes) ------
 * The look up tree is represented by the top tnode and the number of top_level
 * in the tree. 0 means only the level 0 tnode is in the tree.
 */

/* FindLevel0Tnode finds the level 0 tnode, if one exists. */
struct yaffs_tnode *yaffs_find_tnode_0(struct yaffs_dev *dev,
				       struct yaffs_file_var *file_struct,
				       u32 chunk_id)
{
	struct yaffs_tnode *tn = file_struct->top;
	u32 i;
	int required_depth;
	int level = file_struct->top_level;

	dev = dev;

	/* Check sane level and chunk Id */
	if (level < 0 || level > YAFFS_TNODES_MAX_LEVEL)
		return NULL;

	if (chunk_id > YAFFS_MAX_CHUNK_ID)
		return NULL;

	/* First check we're tall enough (ie enough top_level) */

	i = chunk_id >> YAFFS_TNODES_LEVEL0_BITS;
	required_depth = 0;
	while (i) {
		i >>= YAFFS_TNODES_INTERNAL_BITS;
		required_depth++;
	}

	if (required_depth > file_struct->top_level)
		return NULL;	/* Not tall enough, so we can't find it */

	/* Traverse down to level 0 */
	while (level > 0 && tn) {
		tn = tn->internal[(chunk_id >>
				   (YAFFS_TNODES_LEVEL0_BITS +
				    (level - 1) *
				    YAFFS_TNODES_INTERNAL_BITS)) &
				  YAFFS_TNODES_INTERNAL_MASK];
		level--;
	}

	return tn;
}

/* AddOrFindLevel0Tnode finds the level 0 tnode if it exists, otherwise first expands the tree.
 * This happens in two steps:
 *  1. If the tree isn't tall enough, then make it taller.
 *  2. Scan down the tree towards the level 0 tnode adding tnodes if required.
 *
 * Used when modifying the tree.
 *
 *  If the tn argument is NULL, then a fresh tnode will be added otherwise the specified tn will
 *  be plugged into the ttree.
 */

struct yaffs_tnode *yaffs_add_find_tnode_0(struct yaffs_dev *dev,
					   struct yaffs_file_var *file_struct,
					   u32 chunk_id,
					   struct yaffs_tnode *passed_tn)
{
	int required_depth;
	int i;
	int l;
	struct yaffs_tnode *tn;

	u32 x;

	/* Check sane level and page Id */
	if (file_struct->top_level < 0
	    || file_struct->top_level > YAFFS_TNODES_MAX_LEVEL)
		return NULL;

	if (chunk_id > YAFFS_MAX_CHUNK_ID)
		return NULL;

	/* First check we're tall enough (ie enough top_level) */

	x = chunk_id >> YAFFS_TNODES_LEVEL0_BITS;
	required_depth = 0;
	while (x) {
		x >>= YAFFS_TNODES_INTERNAL_BITS;
		required_depth++;
	}

	if (required_depth > file_struct->top_level) {
		/* Not tall enough, gotta make the tree taller */
		for (i = file_struct->top_level; i < required_depth; i++) {

			tn = yaffs_get_tnode(dev);

			if (tn) {
				tn->internal[0] = file_struct->top;
				file_struct->top = tn;
				file_struct->top_level++;
			} else {
				yaffs_trace(YAFFS_TRACE_ERROR, "yaffs: no more tnodes");
				return NULL;
			}
		}
	}

	/* Traverse down to level 0, adding anything we need */

	l = file_struct->top_level;
	tn = file_struct->top;

	if (l > 0) {
		while (l > 0 && tn) {
			x = (chunk_id >>
			     (YAFFS_TNODES_LEVEL0_BITS +
			      (l - 1) * YAFFS_TNODES_INTERNAL_BITS)) &
			    YAFFS_TNODES_INTERNAL_MASK;

			if ((l > 1) && !tn->internal[x]) {
				/* Add missing non-level-zero tnode */
				tn->internal[x] = yaffs_get_tnode(dev);
				if (!tn->internal[x])
					return NULL;
			} else if (l == 1) {
				/* Looking from level 1 at level 0 */
				if (passed_tn) {
					/* If we already have one, then release it. */
					if (tn->internal[x])
						yaffs_free_tnode(dev,
								 tn->
								 internal[x]);
					tn->internal[x] = passed_tn;

				} else if (!tn->internal[x]) {
					/* Don't have one, none passed in */
					tn->internal[x] = yaffs_get_tnode(dev);
					if (!tn->internal[x])
						return NULL;
				}
			}

			tn = tn->internal[x];
			l--;
		}
	} else {
		/* top is level 0 */
		if (passed_tn) {
			memcpy(tn, passed_tn,
			       (dev->tnode_width * YAFFS_NTNODES_LEVEL0) / 8);
			yaffs_free_tnode(dev, passed_tn);
		}
	}

	return tn;
}

static int yaffs_tags_match(const struct yaffs_ext_tags *tags, int obj_id,
			    int chunk_obj)
{
	return (tags->chunk_id == chunk_obj &&
		tags->obj_id == obj_id && !tags->is_deleted) ? 1 : 0;

}

static int yaffs_find_chunk_in_group(struct yaffs_dev *dev, int the_chunk,
				     struct yaffs_ext_tags *tags, int obj_id,
				     int inode_chunk)
{
	int j;

	for (j = 0; the_chunk && j < dev->chunk_grp_size; j++) {
		if (yaffs_check_chunk_bit
		    (dev, the_chunk / dev->param.chunks_per_block,
		     the_chunk % dev->param.chunks_per_block)) {

			if (dev->chunk_grp_size == 1)
				return the_chunk;
			else {
				yaffs_rd_chunk_tags_nand(dev, the_chunk, NULL,
							 tags);
				if (yaffs_tags_match(tags, obj_id, inode_chunk)) {
					/* found it; */
					return the_chunk;
				}
			}
		}
		the_chunk++;
	}
	return -1;
}

static int yaffs_find_chunk_in_file(struct yaffs_obj *in, int inode_chunk,
				    struct yaffs_ext_tags *tags)
{
	/*Get the Tnode, then get the level 0 offset chunk offset */
	struct yaffs_tnode *tn;
	int the_chunk = -1;
	struct yaffs_ext_tags local_tags;
	int ret_val = -1;

	struct yaffs_dev *dev = in->my_dev;

	if (!tags) {
		/* Passed a NULL, so use our own tags space */
		tags = &local_tags;
	}

	tn = yaffs_find_tnode_0(dev, &in->variant.file_variant, inode_chunk);

	if (tn) {
		the_chunk = yaffs_get_group_base(dev, tn, inode_chunk);

		ret_val =
		    yaffs_find_chunk_in_group(dev, the_chunk, tags, in->obj_id,
					      inode_chunk);
	}
	return ret_val;
}

static int yaffs_find_del_file_chunk(struct yaffs_obj *in, int inode_chunk,
				     struct yaffs_ext_tags *tags)
{
	/* Get the Tnode, then get the level 0 offset chunk offset */
	struct yaffs_tnode *tn;
	int the_chunk = -1;
	struct yaffs_ext_tags local_tags;

	struct yaffs_dev *dev = in->my_dev;
	int ret_val = -1;

	if (!tags) {
		/* Passed a NULL, so use our own tags space */
		tags = &local_tags;
	}

	tn = yaffs_find_tnode_0(dev, &in->variant.file_variant, inode_chunk);

	if (tn) {

		the_chunk = yaffs_get_group_base(dev, tn, inode_chunk);

		ret_val =
		    yaffs_find_chunk_in_group(dev, the_chunk, tags, in->obj_id,
					      inode_chunk);

		/* Delete the entry in the filestructure (if found) */
		if (ret_val != -1)
			yaffs_load_tnode_0(dev, tn, inode_chunk, 0);
	}

	return ret_val;
}

int yaffs_put_chunk_in_file(struct yaffs_obj *in, int inode_chunk,
			    int nand_chunk, int in_scan)
{
	/* NB in_scan is zero unless scanning.
	 * For forward scanning, in_scan is > 0;
	 * for backward scanning in_scan is < 0
	 *
	 * nand_chunk = 0 is a dummy insert to make sure the tnodes are there.
	 */

	struct yaffs_tnode *tn;
	struct yaffs_dev *dev = in->my_dev;
	int existing_cunk;
	struct yaffs_ext_tags existing_tags;
	struct yaffs_ext_tags new_tags;
	unsigned existing_serial, new_serial;

	if (in->variant_type != YAFFS_OBJECT_TYPE_FILE) {
		/* Just ignore an attempt at putting a chunk into a non-file during scanning
		 * If it is not during Scanning then something went wrong!
		 */
		if (!in_scan) {
			yaffs_trace(YAFFS_TRACE_ERROR,
				"yaffs tragedy:attempt to put data chunk into a non-file"
				);
			YBUG();
		}

		yaffs_chunk_del(dev, nand_chunk, 1, __LINE__);
		return YAFFS_OK;
	}

	tn = yaffs_add_find_tnode_0(dev,
				    &in->variant.file_variant,
				    inode_chunk, NULL);
	if (!tn)
		return YAFFS_FAIL;

	if (!nand_chunk)
		/* Dummy insert, bail now */
		return YAFFS_OK;

	existing_cunk = yaffs_get_group_base(dev, tn, inode_chunk);

	if (in_scan != 0) {
		/* If we're scanning then we need to test for duplicates
		 * NB This does not need to be efficient since it should only ever
		 * happen when the power fails during a write, then only one
		 * chunk should ever be affected.
		 *
		 * Correction for YAFFS2: This could happen quite a lot and we need to think about efficiency! TODO
		 * Update: For backward scanning we don't need to re-read tags so this is quite cheap.
		 */

		if (existing_cunk > 0) {
			/* NB Right now existing chunk will not be real chunk_id if the chunk group size > 1
			 *    thus we have to do a FindChunkInFile to get the real chunk id.
			 *
			 * We have a duplicate now we need to decide which one to use:
			 *
			 * Backwards scanning YAFFS2: The old one is what we use, dump the new one.
			 * Forward scanning YAFFS2: The new one is what we use, dump the old one.
			 * YAFFS1: Get both sets of tags and compare serial numbers.
			 */

			if (in_scan > 0) {
				/* Only do this for forward scanning */
				yaffs_rd_chunk_tags_nand(dev,
							 nand_chunk,
							 NULL, &new_tags);

				/* Do a proper find */
				existing_cunk =
				    yaffs_find_chunk_in_file(in, inode_chunk,
							     &existing_tags);
			}

			if (existing_cunk <= 0) {
				/*Hoosterman - how did this happen? */

				yaffs_trace(YAFFS_TRACE_ERROR,
					"yaffs tragedy: existing chunk < 0 in scan"
					);

			}

			/* NB The deleted flags should be false, otherwise the chunks will
			 * not be loaded during a scan
			 */

			if (in_scan > 0) {
				new_serial = new_tags.serial_number;
				existing_serial = existing_tags.serial_number;
			}

			if ((in_scan > 0) &&
			    (existing_cunk <= 0 ||
			     ((existing_serial + 1) & 3) == new_serial)) {
				/* Forward scanning.
				 * Use new
				 * Delete the old one and drop through to update the tnode
				 */
				yaffs_chunk_del(dev, existing_cunk, 1,
						__LINE__);
			} else {
				/* Backward scanning or we want to use the existing one
				 * Use existing.
				 * Delete the new one and return early so that the tnode isn't changed
				 */
				yaffs_chunk_del(dev, nand_chunk, 1, __LINE__);
				return YAFFS_OK;
			}
		}

	}

	if (existing_cunk == 0)
		in->n_data_chunks++;

	yaffs_load_tnode_0(dev, tn, inode_chunk, nand_chunk);

	return YAFFS_OK;
}

static void yaffs_soft_del_chunk(struct yaffs_dev *dev, int chunk)
{
	struct yaffs_block_info *the_block;
	unsigned block_no;

	yaffs_trace(YAFFS_TRACE_DELETION, "soft delete chunk %d", chunk);

	block_no = chunk / dev->param.chunks_per_block;
	the_block = yaffs_get_block_info(dev, block_no);
	if (the_block) {
		the_block->soft_del_pages++;
		dev->n_free_chunks++;
		yaffs2_update_oldest_dirty_seq(dev, block_no, the_block);
	}
}

/* SoftDeleteWorker scans backwards through the tnode tree and soft deletes all the chunks in the file.
 * All soft deleting does is increment the block's softdelete count and pulls the chunk out
 * of the tnode.
 * Thus, essentially this is the same as DeleteWorker except that the chunks are soft deleted.
 */

static int yaffs_soft_del_worker(struct yaffs_obj *in, struct yaffs_tnode *tn,
				 u32 level, int chunk_offset)
{
	int i;
	int the_chunk;
	int all_done = 1;
	struct yaffs_dev *dev = in->my_dev;

	if (tn) {
		if (level > 0) {

			for (i = YAFFS_NTNODES_INTERNAL - 1; all_done && i >= 0;
			     i--) {
				if (tn->internal[i]) {
					all_done =
					    yaffs_soft_del_worker(in,
								  tn->internal
								  [i],
								  level - 1,
								  (chunk_offset
								   <<
								   YAFFS_TNODES_INTERNAL_BITS)
								  + i);
					if (all_done) {
						yaffs_free_tnode(dev,
								 tn->internal
								 [i]);
						tn->internal[i] = NULL;
					} else {
						/* Hoosterman... how could this happen? */
					}
				}
			}
			return (all_done) ? 1 : 0;
		} else if (level == 0) {

			for (i = YAFFS_NTNODES_LEVEL0 - 1; i >= 0; i--) {
				the_chunk = yaffs_get_group_base(dev, tn, i);
				if (the_chunk) {
					/* Note this does not find the real chunk, only the chunk group.
					 * We make an assumption that a chunk group is not larger than
					 * a block.
					 */
					yaffs_soft_del_chunk(dev, the_chunk);
					yaffs_load_tnode_0(dev, tn, i, 0);
				}

			}
			return 1;

		}

	}

	return 1;

}

static void yaffs_remove_obj_from_dir(struct yaffs_obj *obj)
{
	struct yaffs_dev *dev = obj->my_dev;
	struct yaffs_obj *parent;

	yaffs_verify_obj_in_dir(obj);
	parent = obj->parent;

	yaffs_verify_dir(parent);

	if (dev && dev->param.remove_obj_fn)
		dev->param.remove_obj_fn(obj);

	list_del_init(&obj->siblings);
	obj->parent = NULL;

	yaffs_verify_dir(parent);
}

void yaffs_add_obj_to_dir(struct yaffs_obj *directory, struct yaffs_obj *obj)
{
	if (!directory) {
		yaffs_trace(YAFFS_TRACE_ALWAYS,
			"tragedy: Trying to add an object to a null pointer directory"
			);
		YBUG();
		return;
	}
	if (directory->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY) {
		yaffs_trace(YAFFS_TRACE_ALWAYS,
			"tragedy: Trying to add an object to a non-directory"
			);
		YBUG();
	}

	if (obj->siblings.prev == NULL) {
		/* Not initialised */
		YBUG();
	}

	yaffs_verify_dir(directory);

	yaffs_remove_obj_from_dir(obj);

	/* Now add it */
	list_add(&obj->siblings, &directory->variant.dir_variant.children);
	obj->parent = directory;

	if (directory == obj->my_dev->unlinked_dir
	    || directory == obj->my_dev->del_dir) {
		obj->unlinked = 1;
		obj->my_dev->n_unlinked_files++;
		obj->rename_allowed = 0;
	}

	yaffs_verify_dir(directory);
	yaffs_verify_obj_in_dir(obj);
}

static int yaffs_change_obj_name(struct yaffs_obj *obj,
				 struct yaffs_obj *new_dir,
				 const YCHAR * new_name, int force, int shadows)
{
	int unlink_op;
	int del_op;

	struct yaffs_obj *existing_target;

	if (new_dir == NULL)
		new_dir = obj->parent;	/* use the old directory */

	if (new_dir->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY) {
		yaffs_trace(YAFFS_TRACE_ALWAYS,
			"tragedy: yaffs_change_obj_name: new_dir is not a directory"
			);
		YBUG();
	}

	/* TODO: Do we need this different handling for YAFFS2 and YAFFS1?? */
	if (obj->my_dev->param.is_yaffs2)
		unlink_op = (new_dir == obj->my_dev->unlinked_dir);
	else
		unlink_op = (new_dir == obj->my_dev->unlinked_dir
			     && obj->variant_type == YAFFS_OBJECT_TYPE_FILE);

	del_op = (new_dir == obj->my_dev->del_dir);

	existing_target = yaffs_find_by_name(new_dir, new_name);

	/* If the object is a file going into the unlinked directory,
	 *   then it is OK to just stuff it in since duplicate names are allowed.
	 *   else only proceed if the new name does not exist and if we're putting
	 *   it into a directory.
	 */
	if ((unlink_op ||
	     del_op ||
	     force ||
	     (shadows > 0) ||
	     !existing_target) &&
	    new_dir->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY) {
		yaffs_set_obj_name(obj, new_name);
		obj->dirty = 1;

		yaffs_add_obj_to_dir(new_dir, obj);

		if (unlink_op)
			obj->unlinked = 1;

		/* If it is a deletion then we mark it as a shrink for gc purposes. */
		if (yaffs_update_oh(obj, new_name, 0, del_op, shadows, NULL) >=
		    0)
			return YAFFS_OK;
	}

	return YAFFS_FAIL;
}

/*------------------------ Short Operations Cache ----------------------------------------
 *   In many situations where there is no high level buffering  a lot of
 *   reads might be short sequential reads, and a lot of writes may be short
 *   sequential writes. eg. scanning/writing a jpeg file.
 *   In these cases, a short read/write cache can provide a huge perfomance
 *   benefit with dumb-as-a-rock code.
 *   In Linux, the page cache provides read buffering and the short op cache 
 *   provides write buffering.
 *
 *   There are a limited number (~10) of cache chunks per device so that we don't
 *   need a very intelligent search.
 */

static int yaffs_obj_cache_dirty(struct yaffs_obj *obj)
{
	struct yaffs_dev *dev = obj->my_dev;
	int i;
	struct yaffs_cache *cache;
	int n_caches = obj->my_dev->param.n_caches;

	for (i = 0; i < n_caches; i++) {
		cache = &dev->cache[i];
		if (cache->object == obj && cache->dirty)
			return 1;
	}

	return 0;
}

static void yaffs_flush_file_cache(struct yaffs_obj *obj)
{
	struct yaffs_dev *dev = obj->my_dev;
	int lowest = -99;	/* Stop compiler whining. */
	int i;
	struct yaffs_cache *cache;
	int chunk_written = 0;
	int n_caches = obj->my_dev->param.n_caches;

	if (n_caches > 0) {
		do {
			cache = NULL;

			/* Find the dirty cache for this object with the lowest chunk id. */
			for (i = 0; i < n_caches; i++) {
				if (dev->cache[i].object == obj &&
				    dev->cache[i].dirty) {
					if (!cache
					    || dev->cache[i].chunk_id <
					    lowest) {
						cache = &dev->cache[i];
						lowest = cache->chunk_id;
					}
				}
			}

			if (cache && !cache->locked) {
				/* Write it out and free it up */

				chunk_written =
				    yaffs_wr_data_obj(cache->object,
						      cache->chunk_id,
						      cache->data,
						      cache->n_bytes, 1);
				cache->dirty = 0;
				cache->object = NULL;
			}

		} while (cache && chunk_written > 0);

		if (cache)
			/* Hoosterman, disk full while writing cache out. */
			yaffs_trace(YAFFS_TRACE_ERROR,
				"yaffs tragedy: no space during cache write");

	}

}

/*yaffs_flush_whole_cache(dev)
 *
 *
 */

void yaffs_flush_whole_cache(struct yaffs_dev *dev)
{
	struct yaffs_obj *obj;
	int n_caches = dev->param.n_caches;
	int i;

	/* Find a dirty object in the cache and flush it...
	 * until there are no further dirty objects.
	 */
	do {
		obj = NULL;
		for (i = 0; i < n_caches && !obj; i++) {
			if (dev->cache[i].object && dev->cache[i].dirty)
				obj = dev->cache[i].object;

		}
		if (obj)
			yaffs_flush_file_cache(obj);

	} while (obj);

}

/* Grab us a cache chunk for use.
 * First look for an empty one.
 * Then look for the least recently used non-dirty one.
 * Then look for the least recently used dirty one...., flush and look again.
 */
static struct yaffs_cache *yaffs_grab_chunk_worker(struct yaffs_dev *dev)
{
	int i;

	if (dev->param.n_caches > 0) {
		for (i = 0; i < dev->param.n_caches; i++) {
			if (!dev->cache[i].object)
				return &dev->cache[i];
		}
	}

	return NULL;
}

static struct yaffs_cache *yaffs_grab_chunk_cache(struct yaffs_dev *dev)
{
	struct yaffs_cache *cache;
	struct yaffs_obj *the_obj;
	int usage;
	int i;
	int pushout;

	if (dev->param.n_caches > 0) {
		/* Try find a non-dirty one... */

		cache = yaffs_grab_chunk_worker(dev);

		if (!cache) {
			/* They were all dirty, find the last recently used object and flush
			 * its cache, then  find again.
			 * NB what's here is not very accurate, we actually flush the object
			 * the last recently used page.
			 */

			/* With locking we can't assume we can use entry zero */

			the_obj = NULL;
			usage = -1;
			cache = NULL;
			pushout = -1;

			for (i = 0; i < dev->param.n_caches; i++) {
				if (dev->cache[i].object &&
				    !dev->cache[i].locked &&
				    (dev->cache[i].last_use < usage
				     || !cache)) {
					usage = dev->cache[i].last_use;
					the_obj = dev->cache[i].object;
					cache = &dev->cache[i];
					pushout = i;
				}
			}

			if (!cache || cache->dirty) {
				/* Flush and try again */
				yaffs_flush_file_cache(the_obj);
				cache = yaffs_grab_chunk_worker(dev);
			}

		}
		return cache;
	} else {
		return NULL;
        }
}

/* Find a cached chunk */
static struct yaffs_cache *yaffs_find_chunk_cache(const struct yaffs_obj *obj,
						  int chunk_id)
{
	struct yaffs_dev *dev = obj->my_dev;
	int i;
	if (dev->param.n_caches > 0) {
		for (i = 0; i < dev->param.n_caches; i++) {
			if (dev->cache[i].object == obj &&
			    dev->cache[i].chunk_id == chunk_id) {
				dev->cache_hits++;

				return &dev->cache[i];
			}
		}
	}
	return NULL;
}

/* Mark the chunk for the least recently used algorithym */
static void yaffs_use_cache(struct yaffs_dev *dev, struct yaffs_cache *cache,
			    int is_write)
{

	if (dev->param.n_caches > 0) {
		if (dev->cache_last_use < 0 || dev->cache_last_use > 100000000) {
			/* Reset the cache usages */
			int i;
			for (i = 1; i < dev->param.n_caches; i++)
				dev->cache[i].last_use = 0;

			dev->cache_last_use = 0;
		}

		dev->cache_last_use++;

		cache->last_use = dev->cache_last_use;

		if (is_write)
			cache->dirty = 1;
	}
}

/* Invalidate a single cache page.
 * Do this when a whole page gets written,
 * ie the short cache for this page is no longer valid.
 */
static void yaffs_invalidate_chunk_cache(struct yaffs_obj *object, int chunk_id)
{
	if (object->my_dev->param.n_caches > 0) {
		struct yaffs_cache *cache =
		    yaffs_find_chunk_cache(object, chunk_id);

		if (cache)
			cache->object = NULL;
	}
}

/* Invalidate all the cache pages associated with this object
 * Do this whenever ther file is deleted or resized.
 */
static void yaffs_invalidate_whole_cache(struct yaffs_obj *in)
{
	int i;
	struct yaffs_dev *dev = in->my_dev;

	if (dev->param.n_caches > 0) {
		/* Invalidate it. */
		for (i = 0; i < dev->param.n_caches; i++) {
			if (dev->cache[i].object == in)
				dev->cache[i].object = NULL;
		}
	}
}

static void yaffs_unhash_obj(struct yaffs_obj *obj)
{
	int bucket;
	struct yaffs_dev *dev = obj->my_dev;

	/* If it is still linked into the bucket list, free from the list */
	if (!list_empty(&obj->hash_link)) {
		list_del_init(&obj->hash_link);
		bucket = yaffs_hash_fn(obj->obj_id);
		dev->obj_bucket[bucket].count--;
	}
}

/*  FreeObject frees up a Object and puts it back on the free list */
static void yaffs_free_obj(struct yaffs_obj *obj)
{
	struct yaffs_dev *dev = obj->my_dev;

	yaffs_trace(YAFFS_TRACE_OS, "FreeObject %p inode %p",
		obj, obj->my_inode);

	if (!obj)
		YBUG();
	if (obj->parent)
		YBUG();
	if (!list_empty(&obj->siblings))
		YBUG();

	if (obj->my_inode) {
		/* We're still hooked up to a cached inode.
		 * Don't delete now, but mark for later deletion
		 */
		obj->defered_free = 1;
		return;
	}

	yaffs_unhash_obj(obj);

	yaffs_free_raw_obj(dev, obj);
	dev->n_obj--;
	dev->checkpoint_blocks_required = 0;	/* force recalculation */
}

void yaffs_handle_defered_free(struct yaffs_obj *obj)
{
	if (obj->defered_free)
		yaffs_free_obj(obj);
}

static int yaffs_generic_obj_del(struct yaffs_obj *in)
{

	/* First off, invalidate the file's data in the cache, without flushing. */
	yaffs_invalidate_whole_cache(in);

	if (in->my_dev->param.is_yaffs2 && (in->parent != in->my_dev->del_dir)) {
		/* Move to the unlinked directory so we have a record that it was deleted. */
		yaffs_change_obj_name(in, in->my_dev->del_dir, _Y("deleted"), 0,
				      0);

	}

	yaffs_remove_obj_from_dir(in);
	yaffs_chunk_del(in->my_dev, in->hdr_chunk, 1, __LINE__);
	in->hdr_chunk = 0;

	yaffs_free_obj(in);
	return YAFFS_OK;

}

static void yaffs_soft_del_file(struct yaffs_obj *obj)
{
	if (obj->deleted &&
	    obj->variant_type == YAFFS_OBJECT_TYPE_FILE && !obj->soft_del) {
		if (obj->n_data_chunks <= 0) {
			/* Empty file with no duplicate object headers,
			 * just delete it immediately */
			yaffs_free_tnode(obj->my_dev,
					 obj->variant.file_variant.top);
			obj->variant.file_variant.top = NULL;
			yaffs_trace(YAFFS_TRACE_TRACING,
				"yaffs: Deleting empty file %d",
				obj->obj_id);
			yaffs_generic_obj_del(obj);
		} else {
			yaffs_soft_del_worker(obj,
					      obj->variant.file_variant.top,
					      obj->variant.
					      file_variant.top_level, 0);
			obj->soft_del = 1;
		}
	}
}

/* Pruning removes any part of the file structure tree that is beyond the
 * bounds of the file (ie that does not point to chunks).
 *
 * A file should only get pruned when its size is reduced.
 *
 * Before pruning, the chunks must be pulled from the tree and the
 * level 0 tnode entries must be zeroed out.
 * Could also use this for file deletion, but that's probably better handled
 * by a special case.
 *
 * This function is recursive. For levels > 0 the function is called again on
 * any sub-tree. For level == 0 we just check if the sub-tree has data.
 * If there is no data in a subtree then it is pruned.
 */

static struct yaffs_tnode *yaffs_prune_worker(struct yaffs_dev *dev,
					      struct yaffs_tnode *tn, u32 level,
					      int del0)
{
	int i;
	int has_data;

	if (tn) {
		has_data = 0;

		if (level > 0) {
			for (i = 0; i < YAFFS_NTNODES_INTERNAL; i++) {
				if (tn->internal[i]) {
					tn->internal[i] =
					    yaffs_prune_worker(dev,
							       tn->internal[i],
							       level - 1,
							       (i ==
								0) ? del0 : 1);
				}

				if (tn->internal[i])
					has_data++;
			}
		} else {
			int tnode_size_u32 = dev->tnode_size / sizeof(u32);
			u32 *map = (u32 *) tn;

			for (i = 0; !has_data && i < tnode_size_u32; i++) {
				if (map[i])
					has_data++;
			}
		}

		if (has_data == 0 && del0) {
			/* Free and return NULL */

			yaffs_free_tnode(dev, tn);
			tn = NULL;
		}

	}

	return tn;

}

static int yaffs_prune_tree(struct yaffs_dev *dev,
			    struct yaffs_file_var *file_struct)
{
	int i;
	int has_data;
	int done = 0;
	struct yaffs_tnode *tn;

	if (file_struct->top_level > 0) {
		file_struct->top =
		    yaffs_prune_worker(dev, file_struct->top,
				       file_struct->top_level, 0);

		/* Now we have a tree with all the non-zero branches NULL but the height
		 * is the same as it was.
		 * Let's see if we can trim internal tnodes to shorten the tree.
		 * We can do this if only the 0th element in the tnode is in use
		 * (ie all the non-zero are NULL)
		 */

		while (file_struct->top_level && !done) {
			tn = file_struct->top;

			has_data = 0;
			for (i = 1; i < YAFFS_NTNODES_INTERNAL; i++) {
				if (tn->internal[i])
					has_data++;
			}

			if (!has_data) {
				file_struct->top = tn->internal[0];
				file_struct->top_level--;
				yaffs_free_tnode(dev, tn);
			} else {
				done = 1;
			}
		}
	}

	return YAFFS_OK;
}

/*-------------------- End of File Structure functions.-------------------*/

/* AllocateEmptyObject gets us a clean Object. Tries to make allocate more if we run out */
static struct yaffs_obj *yaffs_alloc_empty_obj(struct yaffs_dev *dev)
{
	struct yaffs_obj *obj = yaffs_alloc_raw_obj(dev);

	if (obj) {
		dev->n_obj++;

		/* Now sweeten it up... */

		memset(obj, 0, sizeof(struct yaffs_obj));
		obj->being_created = 1;

		obj->my_dev = dev;
		obj->hdr_chunk = 0;
		obj->variant_type = YAFFS_OBJECT_TYPE_UNKNOWN;
		INIT_LIST_HEAD(&(obj->hard_links));
		INIT_LIST_HEAD(&(obj->hash_link));
		INIT_LIST_HEAD(&obj->siblings);

		/* Now make the directory sane */
		if (dev->root_dir) {
			obj->parent = dev->root_dir;
			list_add(&(obj->siblings),
				 &dev->root_dir->variant.dir_variant.children);
		}

		/* Add it to the lost and found directory.
		 * NB Can't put root or lost-n-found in lost-n-found so
		 * check if lost-n-found exists first
		 */
		if (dev->lost_n_found)
			yaffs_add_obj_to_dir(dev->lost_n_found, obj);

		obj->being_created = 0;
	}

	dev->checkpoint_blocks_required = 0;	/* force recalculation */

	return obj;
}

static int yaffs_find_nice_bucket(struct yaffs_dev *dev)
{
	int i;
	int l = 999;
	int lowest = 999999;

	/* Search for the shortest list or one that
	 * isn't too long.
	 */

	for (i = 0; i < 10 && lowest > 4; i++) {
		dev->bucket_finder++;
		dev->bucket_finder %= YAFFS_NOBJECT_BUCKETS;
		if (dev->obj_bucket[dev->bucket_finder].count < lowest) {
			lowest = dev->obj_bucket[dev->bucket_finder].count;
			l = dev->bucket_finder;
		}

	}

	return l;
}

static int yaffs_new_obj_id(struct yaffs_dev *dev)
{
	int bucket = yaffs_find_nice_bucket(dev);

	/* Now find an object value that has not already been taken
	 * by scanning the list.
	 */

	int found = 0;
	struct list_head *i;

	u32 n = (u32) bucket;

	/* yaffs_check_obj_hash_sane();  */

	while (!found) {
		found = 1;
		n += YAFFS_NOBJECT_BUCKETS;
		if (1 || dev->obj_bucket[bucket].count > 0) {
			list_for_each(i, &dev->obj_bucket[bucket].list) {
				/* If there is already one in the list */
				if (i && list_entry(i, struct yaffs_obj,
						    hash_link)->obj_id == n) {
					found = 0;
				}
			}
		}
	}

	return n;
}

static void yaffs_hash_obj(struct yaffs_obj *in)
{
	int bucket = yaffs_hash_fn(in->obj_id);
	struct yaffs_dev *dev = in->my_dev;

	list_add(&in->hash_link, &dev->obj_bucket[bucket].list);
	dev->obj_bucket[bucket].count++;
}

struct yaffs_obj *yaffs_find_by_number(struct yaffs_dev *dev, u32 number)
{
	int bucket = yaffs_hash_fn(number);
	struct list_head *i;
	struct yaffs_obj *in;

	list_for_each(i, &dev->obj_bucket[bucket].list) {
		/* Look if it is in the list */
		if (i) {
			in = list_entry(i, struct yaffs_obj, hash_link);
			if (in->obj_id == number) {

				/* Don't tell the VFS about this one if it is defered free */
				if (in->defered_free)
					return NULL;

				return in;
			}
		}
	}

	return NULL;
}

struct yaffs_obj *yaffs_new_obj(struct yaffs_dev *dev, int number,
				enum yaffs_obj_type type)
{
	struct yaffs_obj *the_obj = NULL;
	struct yaffs_tnode *tn = NULL;

	if (number < 0)
		number = yaffs_new_obj_id(dev);

	if (type == YAFFS_OBJECT_TYPE_FILE) {
		tn = yaffs_get_tnode(dev);
		if (!tn)
			return NULL;
	}

	the_obj = yaffs_alloc_empty_obj(dev);
	if (!the_obj) {
		if (tn)
			yaffs_free_tnode(dev, tn);
		return NULL;
	}

	if (the_obj) {
		the_obj->fake = 0;
		the_obj->rename_allowed = 1;
		the_obj->unlink_allowed = 1;
		the_obj->obj_id = number;
		yaffs_hash_obj(the_obj);
		the_obj->variant_type = type;
		yaffs_load_current_time(the_obj, 1, 1);

		switch (type) {
		case YAFFS_OBJECT_TYPE_FILE:
			the_obj->variant.file_variant.file_size = 0;
			the_obj->variant.file_variant.scanned_size = 0;
			the_obj->variant.file_variant.shrink_size = ~0;	/* max */
			the_obj->variant.file_variant.top_level = 0;
			the_obj->variant.file_variant.top = tn;
			break;
		case YAFFS_OBJECT_TYPE_DIRECTORY:
			INIT_LIST_HEAD(&the_obj->variant.dir_variant.children);
			INIT_LIST_HEAD(&the_obj->variant.dir_variant.dirty);
			break;
		case YAFFS_OBJECT_TYPE_SYMLINK:
		case YAFFS_OBJECT_TYPE_HARDLINK:
		case YAFFS_OBJECT_TYPE_SPECIAL:
			/* No action required */
			break;
		case YAFFS_OBJECT_TYPE_UNKNOWN:
			/* todo this should not happen */
			break;
		}
	}

	return the_obj;
}

static struct yaffs_obj *yaffs_create_fake_dir(struct yaffs_dev *dev,
					       int number, u32 mode)
{

	struct yaffs_obj *obj =
	    yaffs_new_obj(dev, number, YAFFS_OBJECT_TYPE_DIRECTORY);
	if (obj) {
		obj->fake = 1;	/* it is fake so it might have no NAND presence... */
		obj->rename_allowed = 0;	/* ... and we're not allowed to rename it... */
		obj->unlink_allowed = 0;	/* ... or unlink it */
		obj->deleted = 0;
		obj->unlinked = 0;
		obj->yst_mode = mode;
		obj->my_dev = dev;
		obj->hdr_chunk = 0;	/* Not a valid chunk. */
	}

	return obj;

}


static void yaffs_init_tnodes_and_objs(struct yaffs_dev *dev)
{
	int i;

	dev->n_obj = 0;
	dev->n_tnodes = 0;

	yaffs_init_raw_tnodes_and_objs(dev);

	for (i = 0; i < YAFFS_NOBJECT_BUCKETS; i++) {
		INIT_LIST_HEAD(&dev->obj_bucket[i].list);
		dev->obj_bucket[i].count = 0;
	}
}

struct yaffs_obj *yaffs_find_or_create_by_number(struct yaffs_dev *dev,
						 int number,
						 enum yaffs_obj_type type)
{
	struct yaffs_obj *the_obj = NULL;

	if (number > 0)
		the_obj = yaffs_find_by_number(dev, number);

	if (!the_obj)
		the_obj = yaffs_new_obj(dev, number, type);

	return the_obj;

}

YCHAR *yaffs_clone_str(const YCHAR * str)
{
	YCHAR *new_str = NULL;
	int len;

	if (!str)
		str = _Y("");

	len = strnlen(str, YAFFS_MAX_ALIAS_LENGTH);
	new_str = kmalloc((len + 1) * sizeof(YCHAR), GFP_NOFS);
	if (new_str) {
		strncpy(new_str, str, len);
		new_str[len] = 0;
	}
	return new_str;

}
/*
 *yaffs_update_parent() handles fixing a directories mtime and ctime when a new
 * link (ie. name) is created or deleted in the directory.
 *
 * ie.
 *   create dir/a : update dir's mtime/ctime
 *   rm dir/a:   update dir's mtime/ctime
 *   modify dir/a: don't update dir's mtimme/ctime
 *
 * This can be handled immediately or defered. Defering helps reduce the number
 * of updates when many files in a directory are changed within a brief period.
 *
 * If the directory updating is defered then yaffs_update_dirty_dirs must be
 * called periodically.
 */

static void yaffs_update_parent(struct yaffs_obj *obj)
{
	struct yaffs_dev *dev;
	if (!obj)
		return;
	dev = obj->my_dev;
	obj->dirty = 1;
	yaffs_load_current_time(obj, 0, 1);
	if (dev->param.defered_dir_update) {
		struct list_head *link = &obj->variant.dir_variant.dirty;

		if (list_empty(link)) {
			list_add(link, &dev->dirty_dirs);
			yaffs_trace(YAFFS_TRACE_BACKGROUND,
			  "Added object %d to dirty directories",
			   obj->obj_id);
		}

	} else {
		yaffs_update_oh(obj, NULL, 0, 0, 0, NULL);
        }
}

void yaffs_update_dirty_dirs(struct yaffs_dev *dev)
{
	struct list_head *link;
	struct yaffs_obj *obj;
	struct yaffs_dir_var *d_s;
	union yaffs_obj_var *o_v;

	yaffs_trace(YAFFS_TRACE_BACKGROUND, "Update dirty directories");

	while (!list_empty(&dev->dirty_dirs)) {
		link = dev->dirty_dirs.next;
		list_del_init(link);

		d_s = list_entry(link, struct yaffs_dir_var, dirty);
		o_v = list_entry(d_s, union yaffs_obj_var, dir_variant);
		obj = list_entry(o_v, struct yaffs_obj, variant);

		yaffs_trace(YAFFS_TRACE_BACKGROUND, "Update directory %d",
			obj->obj_id);

		if (obj->dirty)
			yaffs_update_oh(obj, NULL, 0, 0, 0, NULL);
	}
}

/*
 * Mknod (create) a new object.
 * equiv_obj only has meaning for a hard link;
 * alias_str only has meaning for a symlink.
 * rdev only has meaning for devices (a subset of special objects)
 */

static struct yaffs_obj *yaffs_create_obj(enum yaffs_obj_type type,
					  struct yaffs_obj *parent,
					  const YCHAR * name,
					  u32 mode,
					  u32 uid,
					  u32 gid,
					  struct yaffs_obj *equiv_obj,
					  const YCHAR * alias_str, u32 rdev)
{
	struct yaffs_obj *in;
	YCHAR *str = NULL;

	struct yaffs_dev *dev = parent->my_dev;

	/* Check if the entry exists. If it does then fail the call since we don't want a dup. */
	if (yaffs_find_by_name(parent, name))
		return NULL;

	if (type == YAFFS_OBJECT_TYPE_SYMLINK) {
		str = yaffs_clone_str(alias_str);
		if (!str)
			return NULL;
	}

	in = yaffs_new_obj(dev, -1, type);

	if (!in) {
		if (str)
			kfree(str);
		return NULL;
	}

	if (in) {
		in->hdr_chunk = 0;
		in->valid = 1;
		in->variant_type = type;

		in->yst_mode = mode;

		yaffs_attribs_init(in, gid, uid, rdev);

		in->n_data_chunks = 0;

		yaffs_set_obj_name(in, name);
		in->dirty = 1;

		yaffs_add_obj_to_dir(parent, in);

		in->my_dev = parent->my_dev;

		switch (type) {
		case YAFFS_OBJECT_TYPE_SYMLINK:
			in->variant.symlink_variant.alias = str;
			break;
		case YAFFS_OBJECT_TYPE_HARDLINK:
			in->variant.hardlink_variant.equiv_obj = equiv_obj;
			in->variant.hardlink_variant.equiv_id =
			    equiv_obj->obj_id;
			list_add(&in->hard_links, &equiv_obj->hard_links);
			break;
		case YAFFS_OBJECT_TYPE_FILE:
		case YAFFS_OBJECT_TYPE_DIRECTORY:
		case YAFFS_OBJECT_TYPE_SPECIAL:
		case YAFFS_OBJECT_TYPE_UNKNOWN:
			/* do nothing */
			break;
		}

		if (yaffs_update_oh(in, name, 0, 0, 0, NULL) < 0) {
			/* Could not create the object header, fail the creation */
			yaffs_del_obj(in);
			in = NULL;
		}

		yaffs_update_parent(parent);
	}

	return in;
}

struct yaffs_obj *yaffs_create_file(struct yaffs_obj *parent,
				    const YCHAR * name, u32 mode, u32 uid,
				    u32 gid)
{
	return yaffs_create_obj(YAFFS_OBJECT_TYPE_FILE, parent, name, mode,
				uid, gid, NULL, NULL, 0);
}

struct yaffs_obj *yaffs_create_dir(struct yaffs_obj *parent, const YCHAR * name,
				   u32 mode, u32 uid, u32 gid)
{
	return yaffs_create_obj(YAFFS_OBJECT_TYPE_DIRECTORY, parent, name,
				mode, uid, gid, NULL, NULL, 0);
}

struct yaffs_obj *yaffs_create_special(struct yaffs_obj *parent,
				       const YCHAR * name, u32 mode, u32 uid,
				       u32 gid, u32 rdev)
{
	return yaffs_create_obj(YAFFS_OBJECT_TYPE_SPECIAL, parent, name, mode,
				uid, gid, NULL, NULL, rdev);
}

struct yaffs_obj *yaffs_create_symlink(struct yaffs_obj *parent,
				       const YCHAR * name, u32 mode, u32 uid,
				       u32 gid, const YCHAR * alias)
{
	return yaffs_create_obj(YAFFS_OBJECT_TYPE_SYMLINK, parent, name, mode,
				uid, gid, NULL, alias, 0);
}

/* yaffs_link_obj returns the object id of the equivalent object.*/
struct yaffs_obj *yaffs_link_obj(struct yaffs_obj *parent, const YCHAR * name,
				 struct yaffs_obj *equiv_obj)
{
	/* Get the real object in case we were fed a hard link as an equivalent object */
	equiv_obj = yaffs_get_equivalent_obj(equiv_obj);

	if (yaffs_create_obj
	    (YAFFS_OBJECT_TYPE_HARDLINK, parent, name, 0, 0, 0,
	     equiv_obj, NULL, 0)) {
		return equiv_obj;
	} else {
		return NULL;
	}

}



/*------------------------- Block Management and Page Allocation ----------------*/

static int yaffs_init_blocks(struct yaffs_dev *dev)
{
	int n_blocks = dev->internal_end_block - dev->internal_start_block + 1;

	dev->block_info = NULL;
	dev->chunk_bits = NULL;

	dev->alloc_block = -1;	/* force it to get a new one */

	/* If the first allocation strategy fails, thry the alternate one */
	dev->block_info =
		kmalloc(n_blocks * sizeof(struct yaffs_block_info), GFP_NOFS);
	if (!dev->block_info) {
		dev->block_info =
		    vmalloc(n_blocks * sizeof(struct yaffs_block_info));
		dev->block_info_alt = 1;
	} else {
		dev->block_info_alt = 0;
        }

	if (dev->block_info) {
		/* Set up dynamic blockinfo stuff. Round up bytes. */
		dev->chunk_bit_stride = (dev->param.chunks_per_block + 7) / 8;
		dev->chunk_bits =
			kmalloc(dev->chunk_bit_stride * n_blocks, GFP_NOFS);
		if (!dev->chunk_bits) {
			dev->chunk_bits =
			    vmalloc(dev->chunk_bit_stride * n_blocks);
			dev->chunk_bits_alt = 1;
		} else {
			dev->chunk_bits_alt = 0;
                }
	}

	if (dev->block_info && dev->chunk_bits) {
		memset(dev->block_info, 0,
		       n_blocks * sizeof(struct yaffs_block_info));
		memset(dev->chunk_bits, 0, dev->chunk_bit_stride * n_blocks);
		return YAFFS_OK;
	}

	return YAFFS_FAIL;
}

static void yaffs_deinit_blocks(struct yaffs_dev *dev)
{
	if (dev->block_info_alt && dev->block_info)
		vfree(dev->block_info);
	else if (dev->block_info)
		kfree(dev->block_info);

	dev->block_info_alt = 0;

	dev->block_info = NULL;

	if (dev->chunk_bits_alt && dev->chunk_bits)
		vfree(dev->chunk_bits);
	else if (dev->chunk_bits)
		kfree(dev->chunk_bits);
	dev->chunk_bits_alt = 0;
	dev->chunk_bits = NULL;
}

void yaffs_block_became_dirty(struct yaffs_dev *dev, int block_no)
{
	struct yaffs_block_info *bi = yaffs_get_block_info(dev, block_no);

	int erased_ok = 0;

	/* If the block is still healthy erase it and mark as clean.
	 * If the block has had a data failure, then retire it.
	 */

	yaffs_trace(YAFFS_TRACE_GC | YAFFS_TRACE_ERASE,
		"yaffs_block_became_dirty block %d state %d %s",
		block_no, bi->block_state,
		(bi->needs_retiring) ? "needs retiring" : "");

	yaffs2_clear_oldest_dirty_seq(dev, bi);

	bi->block_state = YAFFS_BLOCK_STATE_DIRTY;

	/* If this is the block being garbage collected then stop gc'ing this block */
	if (block_no == dev->gc_block)
		dev->gc_block = 0;

	/* If this block is currently the best candidate for gc then drop as a candidate */
	if (block_no == dev->gc_dirtiest) {
		dev->gc_dirtiest = 0;
		dev->gc_pages_in_use = 0;
	}

	if (!bi->needs_retiring) {
		yaffs2_checkpt_invalidate(dev);
		erased_ok = yaffs_erase_block(dev, block_no);
		if (!erased_ok) {
			dev->n_erase_failures++;
			yaffs_trace(YAFFS_TRACE_ERROR | YAFFS_TRACE_BAD_BLOCKS,
			  "**>> Erasure failed %d", block_no);
		}
	}

	if (erased_ok &&
	    ((yaffs_trace_mask & YAFFS_TRACE_ERASE)
	     || !yaffs_skip_verification(dev))) {
		int i;
		for (i = 0; i < dev->param.chunks_per_block; i++) {
			if (!yaffs_check_chunk_erased
			    (dev, block_no * dev->param.chunks_per_block + i)) {
				yaffs_trace(YAFFS_TRACE_ERROR,
					">>Block %d erasure supposedly OK, but chunk %d not erased",
					block_no, i);
			}
		}
	}

	if (erased_ok) {
		/* Clean it up... */
		bi->block_state = YAFFS_BLOCK_STATE_EMPTY;
		bi->seq_number = 0;
		dev->n_erased_blocks++;
		bi->pages_in_use = 0;
		bi->soft_del_pages = 0;
		bi->has_shrink_hdr = 0;
		bi->skip_erased_check = 1;	/* Clean, so no need to check */
		bi->gc_prioritise = 0;
		yaffs_clear_chunk_bits(dev, block_no);

		yaffs_trace(YAFFS_TRACE_ERASE,
			"Erased block %d", block_no);
	} else {
		/* We lost a block of free space */
		dev->n_free_chunks -= dev->param.chunks_per_block;
		yaffs_retire_block(dev, block_no);
		yaffs_trace(YAFFS_TRACE_ERROR | YAFFS_TRACE_BAD_BLOCKS,
			"**>> Block %d retired", block_no);
	}
}



static int yaffs_gc_block(struct yaffs_dev *dev, int block, int whole_block)
{
	int old_chunk;
	int new_chunk;
	int mark_flash;
	int ret_val = YAFFS_OK;
	int i;
	int is_checkpt_block;
	int matching_chunk;
	int max_copies;

	int chunks_before = yaffs_get_erased_chunks(dev);
	int chunks_after;

	struct yaffs_ext_tags tags;

	struct yaffs_block_info *bi = yaffs_get_block_info(dev, block);

	struct yaffs_obj *object;

	is_checkpt_block = (bi->block_state == YAFFS_BLOCK_STATE_CHECKPOINT);

	yaffs_trace(YAFFS_TRACE_TRACING,
		"Collecting block %d, in use %d, shrink %d, whole_block %d",
		block, bi->pages_in_use, bi->has_shrink_hdr,
		whole_block);

	/*yaffs_verify_free_chunks(dev); */

	if (bi->block_state == YAFFS_BLOCK_STATE_FULL)
		bi->block_state = YAFFS_BLOCK_STATE_COLLECTING;

	bi->has_shrink_hdr = 0;	/* clear the flag so that the block can erase */

	dev->gc_disable = 1;

	if (is_checkpt_block || !yaffs_still_some_chunks(dev, block)) {
		yaffs_trace(YAFFS_TRACE_TRACING,
			"Collecting block %d that has no chunks in use",
		   	block);
		yaffs_block_became_dirty(dev, block);
	} else {

		u8 *buffer = yaffs_get_temp_buffer(dev, __LINE__);

		yaffs_verify_blk(dev, bi, block);

		max_copies = (whole_block) ? dev->param.chunks_per_block : 5;
		old_chunk = block * dev->param.chunks_per_block + dev->gc_chunk;

		for ( /* init already done */ ;
		     ret_val == YAFFS_OK &&
		     dev->gc_chunk < dev->param.chunks_per_block &&
		     (bi->block_state == YAFFS_BLOCK_STATE_COLLECTING) &&
		     max_copies > 0; dev->gc_chunk++, old_chunk++) {
			if (yaffs_check_chunk_bit(dev, block, dev->gc_chunk)) {

				/* This page is in use and might need to be copied off */

				max_copies--;

				mark_flash = 1;

				yaffs_init_tags(&tags);

				yaffs_rd_chunk_tags_nand(dev, old_chunk,
							 buffer, &tags);

				object = yaffs_find_by_number(dev, tags.obj_id);

				yaffs_trace(YAFFS_TRACE_GC_DETAIL,
					"Collecting chunk in block %d, %d %d %d ",
					dev->gc_chunk, tags.obj_id,
					tags.chunk_id, tags.n_bytes);

				if (object && !yaffs_skip_verification(dev)) {
					if (tags.chunk_id == 0)
						matching_chunk =
						    object->hdr_chunk;
					else if (object->soft_del)
						matching_chunk = old_chunk;	/* Defeat the test */
					else
						matching_chunk =
						    yaffs_find_chunk_in_file
						    (object, tags.chunk_id,
						     NULL);

					if (old_chunk != matching_chunk)
						yaffs_trace(YAFFS_TRACE_ERROR,
							"gc: page in gc mismatch: %d %d %d %d",
							old_chunk,
							matching_chunk,
							tags.obj_id,
						   	tags.chunk_id);

				}

				if (!object) {
					yaffs_trace(YAFFS_TRACE_ERROR,
						"page %d in gc has no object: %d %d %d ",
						old_chunk,
						tags.obj_id, tags.chunk_id,
						tags.n_bytes);
				}

				if (object &&
				    object->deleted &&
				    object->soft_del && tags.chunk_id != 0) {
					/* Data chunk in a soft deleted file, throw it away
					 * It's a soft deleted data chunk,
					 * No need to copy this, just forget about it and
					 * fix up the object.
					 */

					/* Free chunks already includes softdeleted chunks.
					 * How ever this chunk is going to soon be really deleted
					 * which will increment free chunks.
					 * We have to decrement free chunks so this works out properly.
					 */
					dev->n_free_chunks--;
					bi->soft_del_pages--;

					object->n_data_chunks--;

					if (object->n_data_chunks <= 0) {
						/* remeber to clean up the object */
						dev->gc_cleanup_list[dev->
								     n_clean_ups]
						    = tags.obj_id;
						dev->n_clean_ups++;
					}
					mark_flash = 0;
				} else if (0) {
					/* Todo object && object->deleted && object->n_data_chunks == 0 */
					/* Deleted object header with no data chunks.
					 * Can be discarded and the file deleted.
					 */
					object->hdr_chunk = 0;
					yaffs_free_tnode(object->my_dev,
							 object->
							 variant.file_variant.
							 top);
					object->variant.file_variant.top = NULL;
					yaffs_generic_obj_del(object);

				} else if (object) {
					/* It's either a data chunk in a live file or
					 * an ObjectHeader, so we're interested in it.
					 * NB Need to keep the ObjectHeaders of deleted files
					 * until the whole file has been deleted off
					 */
					tags.serial_number++;

					dev->n_gc_copies++;

					if (tags.chunk_id == 0) {
						/* It is an object Id,
						 * We need to nuke the shrinkheader flags first
						 * Also need to clean up shadowing.
						 * We no longer want the shrink_header flag since its work is done
						 * and if it is left in place it will mess up scanning.
						 */

						struct yaffs_obj_hdr *oh;
						oh = (struct yaffs_obj_hdr *)
						    buffer;

						oh->is_shrink = 0;
						tags.extra_is_shrink = 0;

						oh->shadows_obj = 0;
						oh->inband_shadowed_obj_id = 0;
						tags.extra_shadows = 0;

						/* Update file size */
						if (object->variant_type ==
						    YAFFS_OBJECT_TYPE_FILE) {
							oh->file_size =
							    object->variant.
							    file_variant.
							    file_size;
							tags.extra_length =
							    oh->file_size;
						}

						yaffs_verify_oh(object, oh,
								&tags, 1);
						new_chunk =
						    yaffs_write_new_chunk(dev,
									  (u8 *)
									  oh,
									  &tags,
									  1);
					} else {
						new_chunk =
						    yaffs_write_new_chunk(dev,
									  buffer,
									  &tags,
									  1);
                                        }

					if (new_chunk < 0) {
						ret_val = YAFFS_FAIL;
					} else {

						/* Ok, now fix up the Tnodes etc. */

						if (tags.chunk_id == 0) {
							/* It's a header */
							object->hdr_chunk =
							    new_chunk;
							object->serial =
							    tags.serial_number;
						} else {
							/* It's a data chunk */
							int ok;
							ok = yaffs_put_chunk_in_file(object, tags.chunk_id, new_chunk, 0);
						}
					}
				}

				if (ret_val == YAFFS_OK)
					yaffs_chunk_del(dev, old_chunk,
							mark_flash, __LINE__);

			}
		}

		yaffs_release_temp_buffer(dev, buffer, __LINE__);

	}

	yaffs_verify_collected_blk(dev, bi, block);

	if (bi->block_state == YAFFS_BLOCK_STATE_COLLECTING) {
		/*
		 * The gc did not complete. Set block state back to FULL
		 * because checkpointing does not restore gc.
		 */
		bi->block_state = YAFFS_BLOCK_STATE_FULL;
	} else {
		/* The gc completed. */
		/* Do any required cleanups */
		for (i = 0; i < dev->n_clean_ups; i++) {
			/* Time to delete the file too */
			object =
			    yaffs_find_by_number(dev, dev->gc_cleanup_list[i]);
			if (object) {
				yaffs_free_tnode(dev,
						 object->variant.
						 file_variant.top);
				object->variant.file_variant.top = NULL;
				yaffs_trace(YAFFS_TRACE_GC,
					"yaffs: About to finally delete object %d",
					object->obj_id);
				yaffs_generic_obj_del(object);
				object->my_dev->n_deleted_files--;
			}

		}

		chunks_after = yaffs_get_erased_chunks(dev);
		if (chunks_before >= chunks_after)
			yaffs_trace(YAFFS_TRACE_GC,
				"gc did not increase free chunks before %d after %d",
				chunks_before, chunks_after);
		dev->gc_block = 0;
		dev->gc_chunk = 0;
		dev->n_clean_ups = 0;
	}

	dev->gc_disable = 0;

	return ret_val;
}

/*
 * FindBlockForgarbageCollection is used to select the dirtiest block (or close enough)
 * for garbage collection.
 */

static unsigned yaffs_find_gc_block(struct yaffs_dev *dev,
				    int aggressive, int background)
{
	int i;
	int iterations;
	unsigned selected = 0;
	int prioritised = 0;
	int prioritised_exist = 0;
	struct yaffs_block_info *bi;
	int threshold;

	/* First let's see if we need to grab a prioritised block */
	if (dev->has_pending_prioritised_gc && !aggressive) {
		dev->gc_dirtiest = 0;
		bi = dev->block_info;
		for (i = dev->internal_start_block;
		     i <= dev->internal_end_block && !selected; i++) {

			if (bi->gc_prioritise) {
				prioritised_exist = 1;
				if (bi->block_state == YAFFS_BLOCK_STATE_FULL &&
				    yaffs_block_ok_for_gc(dev, bi)) {
					selected = i;
					prioritised = 1;
				}
			}
			bi++;
		}

		/*
		 * If there is a prioritised block and none was selected then
		 * this happened because there is at least one old dirty block gumming
		 * up the works. Let's gc the oldest dirty block.
		 */

		if (prioritised_exist &&
		    !selected && dev->oldest_dirty_block > 0)
			selected = dev->oldest_dirty_block;

		if (!prioritised_exist)	/* None found, so we can clear this */
			dev->has_pending_prioritised_gc = 0;
	}

	/* If we're doing aggressive GC then we are happy to take a less-dirty block, and
	 * search harder.
	 * else (we're doing a leasurely gc), then we only bother to do this if the
	 * block has only a few pages in use.
	 */

	if (!selected) {
		int pages_used;
		int n_blocks =
		    dev->internal_end_block - dev->internal_start_block + 1;
		if (aggressive) {
			threshold = dev->param.chunks_per_block;
			iterations = n_blocks;
		} else {
			int max_threshold;

			if (background)
				max_threshold = dev->param.chunks_per_block / 2;
			else
				max_threshold = dev->param.chunks_per_block / 8;

			if (max_threshold < YAFFS_GC_PASSIVE_THRESHOLD)
				max_threshold = YAFFS_GC_PASSIVE_THRESHOLD;

			threshold = background ? (dev->gc_not_done + 2) * 2 : 0;
			if (threshold < YAFFS_GC_PASSIVE_THRESHOLD)
				threshold = YAFFS_GC_PASSIVE_THRESHOLD;
			if (threshold > max_threshold)
				threshold = max_threshold;

			iterations = n_blocks / 16 + 1;
			if (iterations > 100)
				iterations = 100;
		}

		for (i = 0;
		     i < iterations &&
		     (dev->gc_dirtiest < 1 ||
		      dev->gc_pages_in_use > YAFFS_GC_GOOD_ENOUGH); i++) {
			dev->gc_block_finder++;
			if (dev->gc_block_finder < dev->internal_start_block ||
			    dev->gc_block_finder > dev->internal_end_block)
				dev->gc_block_finder =
				    dev->internal_start_block;

			bi = yaffs_get_block_info(dev, dev->gc_block_finder);

			pages_used = bi->pages_in_use - bi->soft_del_pages;

			if (bi->block_state == YAFFS_BLOCK_STATE_FULL &&
			    pages_used < dev->param.chunks_per_block &&
			    (dev->gc_dirtiest < 1
			     || pages_used < dev->gc_pages_in_use)
			    && yaffs_block_ok_for_gc(dev, bi)) {
				dev->gc_dirtiest = dev->gc_block_finder;
				dev->gc_pages_in_use = pages_used;
			}
		}

		if (dev->gc_dirtiest > 0 && dev->gc_pages_in_use <= threshold)
			selected = dev->gc_dirtiest;
	}

	/*
	 * If nothing has been selected for a while, try selecting the oldest dirty
	 * because that's gumming up the works.
	 */

	if (!selected && dev->param.is_yaffs2 &&
	    dev->gc_not_done >= (background ? 10 : 20)) {
		yaffs2_find_oldest_dirty_seq(dev);
		if (dev->oldest_dirty_block > 0) {
			selected = dev->oldest_dirty_block;
			dev->gc_dirtiest = selected;
			dev->oldest_dirty_gc_count++;
			bi = yaffs_get_block_info(dev, selected);
			dev->gc_pages_in_use =
			    bi->pages_in_use - bi->soft_del_pages;
		} else {
			dev->gc_not_done = 0;
                }
	}

	if (selected) {
		yaffs_trace(YAFFS_TRACE_GC,
			"GC Selected block %d with %d free, prioritised:%d",
			selected,
			dev->param.chunks_per_block - dev->gc_pages_in_use,
			prioritised);

		dev->n_gc_blocks++;
		if (background)
			dev->bg_gcs++;

		dev->gc_dirtiest = 0;
		dev->gc_pages_in_use = 0;
		dev->gc_not_done = 0;
		if (dev->refresh_skip > 0)
			dev->refresh_skip--;
	} else {
		dev->gc_not_done++;
		yaffs_trace(YAFFS_TRACE_GC,
			"GC none: finder %d skip %d threshold %d dirtiest %d using %d oldest %d%s",
			dev->gc_block_finder, dev->gc_not_done, threshold,
			dev->gc_dirtiest, dev->gc_pages_in_use,
			dev->oldest_dirty_block, background ? " bg" : "");
	}

	return selected;
}

/* New garbage collector
 * If we're very low on erased blocks then we do aggressive garbage collection
 * otherwise we do "leasurely" garbage collection.
 * Aggressive gc looks further (whole array) and will accept less dirty blocks.
 * Passive gc only inspects smaller areas and will only accept more dirty blocks.
 *
 * The idea is to help clear out space in a more spread-out manner.
 * Dunno if it really does anything useful.
 */
static int yaffs_check_gc(struct yaffs_dev *dev, int background)
{
	int aggressive = 0;
	int gc_ok = YAFFS_OK;
	int max_tries = 0;
	int min_erased;
	int erased_chunks;
	int checkpt_block_adjust;

	if (dev->param.gc_control && (dev->param.gc_control(dev) & 1) == 0)
		return YAFFS_OK;

	if (dev->gc_disable) {
		/* Bail out so we don't get recursive gc */
		return YAFFS_OK;
	}

	/* This loop should pass the first time.
	 * We'll only see looping here if the collection does not increase space.
	 */

	do {
		max_tries++;

		checkpt_block_adjust = yaffs_calc_checkpt_blocks_required(dev);

		min_erased =
		    dev->param.n_reserved_blocks + checkpt_block_adjust + 1;
		erased_chunks =
		    dev->n_erased_blocks * dev->param.chunks_per_block;

		/* If we need a block soon then do aggressive gc. */
		if (dev->n_erased_blocks < min_erased)
			aggressive = 1;
		else {
			if (!background
			    && erased_chunks > (dev->n_free_chunks / 4))
				break;

			if (dev->gc_skip > 20)
				dev->gc_skip = 20;
			if (erased_chunks < dev->n_free_chunks / 2 ||
			    dev->gc_skip < 1 || background)
				aggressive = 0;
			else {
				dev->gc_skip--;
				break;
			}
		}

		dev->gc_skip = 5;

		/* If we don't already have a block being gc'd then see if we should start another */

		if (dev->gc_block < 1 && !aggressive) {
			dev->gc_block = yaffs2_find_refresh_block(dev);
			dev->gc_chunk = 0;
			dev->n_clean_ups = 0;
		}
		if (dev->gc_block < 1) {
			dev->gc_block =
			    yaffs_find_gc_block(dev, aggressive, background);
			dev->gc_chunk = 0;
			dev->n_clean_ups = 0;
		}

		if (dev->gc_block > 0) {
			dev->all_gcs++;
			if (!aggressive)
				dev->passive_gc_count++;

			yaffs_trace(YAFFS_TRACE_GC,
				"yaffs: GC n_erased_blocks %d aggressive %d",
				dev->n_erased_blocks, aggressive);

			gc_ok = yaffs_gc_block(dev, dev->gc_block, aggressive);
		}

		if (dev->n_erased_blocks < (dev->param.n_reserved_blocks)
		    && dev->gc_block > 0) {
			yaffs_trace(YAFFS_TRACE_GC,
				"yaffs: GC !!!no reclaim!!! n_erased_blocks %d after try %d block %d",
				dev->n_erased_blocks, max_tries,
				dev->gc_block);
		}
	} while ((dev->n_erased_blocks < dev->param.n_reserved_blocks) &&
		 (dev->gc_block > 0) && (max_tries < 2));

	return aggressive ? gc_ok : YAFFS_OK;
}

/*
 * yaffs_bg_gc()
 * Garbage collects. Intended to be called from a background thread.
 * Returns non-zero if at least half the free chunks are erased.
 */
int yaffs_bg_gc(struct yaffs_dev *dev, unsigned urgency)
{
	int erased_chunks = dev->n_erased_blocks * dev->param.chunks_per_block;

	yaffs_trace(YAFFS_TRACE_BACKGROUND, "Background gc %u", urgency);

	yaffs_check_gc(dev, 1);
	return erased_chunks > dev->n_free_chunks / 2;
}

/*-------------------- Data file manipulation -----------------*/

static int yaffs_rd_data_obj(struct yaffs_obj *in, int inode_chunk, u8 * buffer)
{
	int nand_chunk = yaffs_find_chunk_in_file(in, inode_chunk, NULL);

	if (nand_chunk >= 0)
		return yaffs_rd_chunk_tags_nand(in->my_dev, nand_chunk,
						buffer, NULL);
	else {
		yaffs_trace(YAFFS_TRACE_NANDACCESS,
			"Chunk %d not found zero instead",
			nand_chunk);
		/* get sane (zero) data if you read a hole */
		memset(buffer, 0, in->my_dev->data_bytes_per_chunk);
		return 0;
	}

}

void yaffs_chunk_del(struct yaffs_dev *dev, int chunk_id, int mark_flash,
		     int lyn)
{
	int block;
	int page;
	struct yaffs_ext_tags tags;
	struct yaffs_block_info *bi;

	if (chunk_id <= 0)
		return;

	dev->n_deletions++;
	block = chunk_id / dev->param.chunks_per_block;
	page = chunk_id % dev->param.chunks_per_block;

	if (!yaffs_check_chunk_bit(dev, block, page))
		yaffs_trace(YAFFS_TRACE_VERIFY,
			"Deleting invalid chunk %d", chunk_id);

	bi = yaffs_get_block_info(dev, block);

	yaffs2_update_oldest_dirty_seq(dev, block, bi);

	yaffs_trace(YAFFS_TRACE_DELETION,
		"line %d delete of chunk %d",
		lyn, chunk_id);

	if (!dev->param.is_yaffs2 && mark_flash &&
	    bi->block_state != YAFFS_BLOCK_STATE_COLLECTING) {

		yaffs_init_tags(&tags);

		tags.is_deleted = 1;

		yaffs_wr_chunk_tags_nand(dev, chunk_id, NULL, &tags);
		yaffs_handle_chunk_update(dev, chunk_id, &tags);
	} else {
		dev->n_unmarked_deletions++;
	}

	/* Pull out of the management area.
	 * If the whole block became dirty, this will kick off an erasure.
	 */
	if (bi->block_state == YAFFS_BLOCK_STATE_ALLOCATING ||
	    bi->block_state == YAFFS_BLOCK_STATE_FULL ||
	    bi->block_state == YAFFS_BLOCK_STATE_NEEDS_SCANNING ||
	    bi->block_state == YAFFS_BLOCK_STATE_COLLECTING) {
		dev->n_free_chunks++;

		yaffs_clear_chunk_bit(dev, block, page);

		bi->pages_in_use--;

		if (bi->pages_in_use == 0 &&
		    !bi->has_shrink_hdr &&
		    bi->block_state != YAFFS_BLOCK_STATE_ALLOCATING &&
		    bi->block_state != YAFFS_BLOCK_STATE_NEEDS_SCANNING) {
			yaffs_block_became_dirty(dev, block);
		}

	}

}

static int yaffs_wr_data_obj(struct yaffs_obj *in, int inode_chunk,
			     const u8 * buffer, int n_bytes, int use_reserve)
{
	/* Find old chunk Need to do this to get serial number
	 * Write new one and patch into tree.
	 * Invalidate old tags.
	 */

	int prev_chunk_id;
	struct yaffs_ext_tags prev_tags;

	int new_chunk_id;
	struct yaffs_ext_tags new_tags;

	struct yaffs_dev *dev = in->my_dev;

	yaffs_check_gc(dev, 0);

	/* Get the previous chunk at this location in the file if it exists.
	 * If it does not exist then put a zero into the tree. This creates
	 * the tnode now, rather than later when it is harder to clean up.
	 */
	prev_chunk_id = yaffs_find_chunk_in_file(in, inode_chunk, &prev_tags);
	if (prev_chunk_id < 1 &&
	    !yaffs_put_chunk_in_file(in, inode_chunk, 0, 0))
		return 0;

	/* Set up new tags */
	yaffs_init_tags(&new_tags);

	new_tags.chunk_id = inode_chunk;
	new_tags.obj_id = in->obj_id;
	new_tags.serial_number =
	    (prev_chunk_id > 0) ? prev_tags.serial_number + 1 : 1;
	new_tags.n_bytes = n_bytes;

	if (n_bytes < 1 || n_bytes > dev->param.total_bytes_per_chunk) {
		yaffs_trace(YAFFS_TRACE_ERROR,
		  "Writing %d bytes to chunk!!!!!!!!!",
		   n_bytes);
		YBUG();
	}

	new_chunk_id =
	    yaffs_write_new_chunk(dev, buffer, &new_tags, use_reserve);

	if (new_chunk_id > 0) {
		yaffs_put_chunk_in_file(in, inode_chunk, new_chunk_id, 0);

		if (prev_chunk_id > 0)
			yaffs_chunk_del(dev, prev_chunk_id, 1, __LINE__);

		yaffs_verify_file_sane(in);
	}
	return new_chunk_id;

}



static int yaffs_do_xattrib_mod(struct yaffs_obj *obj, int set,
				const YCHAR * name, const void *value, int size,
				int flags)
{
	struct yaffs_xattr_mod xmod;

	int result;

	xmod.set = set;
	xmod.name = name;
	xmod.data = value;
	xmod.size = size;
	xmod.flags = flags;
	xmod.result = -ENOSPC;

	result = yaffs_update_oh(obj, NULL, 0, 0, 0, &xmod);

	if (result > 0)
		return xmod.result;
	else
		return -ENOSPC;
}

static int yaffs_apply_xattrib_mod(struct yaffs_obj *obj, char *buffer,
				   struct yaffs_xattr_mod *xmod)
{
	int retval = 0;
	int x_offs = sizeof(struct yaffs_obj_hdr);
	struct yaffs_dev *dev = obj->my_dev;
	int x_size = dev->data_bytes_per_chunk - sizeof(struct yaffs_obj_hdr);

	char *x_buffer = buffer + x_offs;

	if (xmod->set)
		retval =
		    nval_set(x_buffer, x_size, xmod->name, xmod->data,
			     xmod->size, xmod->flags);
	else
		retval = nval_del(x_buffer, x_size, xmod->name);

	obj->has_xattr = nval_hasvalues(x_buffer, x_size);
	obj->xattr_known = 1;

	xmod->result = retval;

	return retval;
}

static int yaffs_do_xattrib_fetch(struct yaffs_obj *obj, const YCHAR * name,
				  void *value, int size)
{
	char *buffer = NULL;
	int result;
	struct yaffs_ext_tags tags;
	struct yaffs_dev *dev = obj->my_dev;
	int x_offs = sizeof(struct yaffs_obj_hdr);
	int x_size = dev->data_bytes_per_chunk - sizeof(struct yaffs_obj_hdr);

	char *x_buffer;

	int retval = 0;

	if (obj->hdr_chunk < 1)
		return -ENODATA;

	/* If we know that the object has no xattribs then don't do all the
	 * reading and parsing.
	 */
	if (obj->xattr_known && !obj->has_xattr) {
		if (name)
			return -ENODATA;
		else
			return 0;
	}

	buffer = (char *)yaffs_get_temp_buffer(dev, __LINE__);
	if (!buffer)
		return -ENOMEM;

	result =
	    yaffs_rd_chunk_tags_nand(dev, obj->hdr_chunk, (u8 *) buffer, &tags);

	if (result != YAFFS_OK)
		retval = -ENOENT;
	else {
		x_buffer = buffer + x_offs;

		if (!obj->xattr_known) {
			obj->has_xattr = nval_hasvalues(x_buffer, x_size);
			obj->xattr_known = 1;
		}

		if (name)
			retval = nval_get(x_buffer, x_size, name, value, size);
		else
			retval = nval_list(x_buffer, x_size, value, size);
	}
	yaffs_release_temp_buffer(dev, (u8 *) buffer, __LINE__);
	return retval;
}

int yaffs_set_xattrib(struct yaffs_obj *obj, const YCHAR * name,
		      const void *value, int size, int flags)
{
	return yaffs_do_xattrib_mod(obj, 1, name, value, size, flags);
}

int yaffs_remove_xattrib(struct yaffs_obj *obj, const YCHAR * name)
{
	return yaffs_do_xattrib_mod(obj, 0, name, NULL, 0, 0);
}

int yaffs_get_xattrib(struct yaffs_obj *obj, const YCHAR * name, void *value,
		      int size)
{
	return yaffs_do_xattrib_fetch(obj, name, value, size);
}

int yaffs_list_xattrib(struct yaffs_obj *obj, char *buffer, int size)
{
	return yaffs_do_xattrib_fetch(obj, NULL, buffer, size);
}

static void yaffs_check_obj_details_loaded(struct yaffs_obj *in)
{
	u8 *chunk_data;
	struct yaffs_obj_hdr *oh;
	struct yaffs_dev *dev;
	struct yaffs_ext_tags tags;
	int result;
	int alloc_failed = 0;

	if (!in)
		return;

	dev = in->my_dev;

	if (in->lazy_loaded && in->hdr_chunk > 0) {
		in->lazy_loaded = 0;
		chunk_data = yaffs_get_temp_buffer(dev, __LINE__);

		result =
		    yaffs_rd_chunk_tags_nand(dev, in->hdr_chunk, chunk_data,
					     &tags);
		oh = (struct yaffs_obj_hdr *)chunk_data;

		in->yst_mode = oh->yst_mode;
		yaffs_load_attribs(in, oh);
		yaffs_set_obj_name_from_oh(in, oh);

		if (in->variant_type == YAFFS_OBJECT_TYPE_SYMLINK) {
			in->variant.symlink_variant.alias =
			    yaffs_clone_str(oh->alias);
			if (!in->variant.symlink_variant.alias)
				alloc_failed = 1;	/* Not returned to caller */
		}

		yaffs_release_temp_buffer(dev, chunk_data, __LINE__);
	}
}

static void yaffs_load_name_from_oh(struct yaffs_dev *dev, YCHAR * name,
				    const YCHAR * oh_name, int buff_size)
{
#ifdef CONFIG_YAFFS_AUTO_UNICODE
	if (dev->param.auto_unicode) {
		if (*oh_name) {
			/* It is an ASCII name, do an ASCII to
			 * unicode conversion */
			const char *ascii_oh_name = (const char *)oh_name;
			int n = buff_size - 1;
			while (n > 0 && *ascii_oh_name) {
				*name = *ascii_oh_name;
				name++;
				ascii_oh_name++;
				n--;
			}
		} else {
			strncpy(name, oh_name + 1, buff_size - 1);
                }
	} else {
#else
        {
#endif
		strncpy(name, oh_name, buff_size - 1);
        }
}

static void yaffs_load_oh_from_name(struct yaffs_dev *dev, YCHAR * oh_name,
				    const YCHAR * name)
{
#ifdef CONFIG_YAFFS_AUTO_UNICODE

	int is_ascii;
	YCHAR *w;

	if (dev->param.auto_unicode) {

		is_ascii = 1;
		w = name;

		/* Figure out if the name will fit in ascii character set */
		while (is_ascii && *w) {
			if ((*w) & 0xff00)
				is_ascii = 0;
			w++;
		}

		if (is_ascii) {
			/* It is an ASCII name, so do a unicode to ascii conversion */
			char *ascii_oh_name = (char *)oh_name;
			int n = YAFFS_MAX_NAME_LENGTH - 1;
			while (n > 0 && *name) {
				*ascii_oh_name = *name;
				name++;
				ascii_oh_name++;
				n--;
			}
		} else {
			/* It is a unicode name, so save starting at the second YCHAR */
			*oh_name = 0;
			strncpy(oh_name + 1, name,
				      YAFFS_MAX_NAME_LENGTH - 2);
		}
	} else {
#else
        {
#endif
		strncpy(oh_name, name, YAFFS_MAX_NAME_LENGTH - 1);
        }

}

/* UpdateObjectHeader updates the header on NAND for an object.
 * If name is not NULL, then that new name is used.
 */
int yaffs_update_oh(struct yaffs_obj *in, const YCHAR * name, int force,
		    int is_shrink, int shadows, struct yaffs_xattr_mod *xmod)
{

	struct yaffs_block_info *bi;

	struct yaffs_dev *dev = in->my_dev;

	int prev_chunk_id;
	int ret_val = 0;
	int result = 0;

	int new_chunk_id;
	struct yaffs_ext_tags new_tags;
	struct yaffs_ext_tags old_tags;
	const YCHAR *alias = NULL;

	u8 *buffer = NULL;
	YCHAR old_name[YAFFS_MAX_NAME_LENGTH + 1];

	struct yaffs_obj_hdr *oh = NULL;

	strcpy(old_name, _Y("silly old name"));

	if (!in->fake || in == dev->root_dir ||
	    force || xmod) {

		yaffs_check_gc(dev, 0);
		yaffs_check_obj_details_loaded(in);

		buffer = yaffs_get_temp_buffer(in->my_dev, __LINE__);
		oh = (struct yaffs_obj_hdr *)buffer;

		prev_chunk_id = in->hdr_chunk;

		if (prev_chunk_id > 0) {
			result = yaffs_rd_chunk_tags_nand(dev, prev_chunk_id,
							  buffer, &old_tags);

			yaffs_verify_oh(in, oh, &old_tags, 0);

			memcpy(old_name, oh->name, sizeof(oh->name));
			memset(buffer, 0xFF, sizeof(struct yaffs_obj_hdr));
		} else {
			memset(buffer, 0xFF, dev->data_bytes_per_chunk);
                }

		oh->type = in->variant_type;
		oh->yst_mode = in->yst_mode;
		oh->shadows_obj = oh->inband_shadowed_obj_id = shadows;

		yaffs_load_attribs_oh(oh, in);

		if (in->parent)
			oh->parent_obj_id = in->parent->obj_id;
		else
			oh->parent_obj_id = 0;

		if (name && *name) {
			memset(oh->name, 0, sizeof(oh->name));
			yaffs_load_oh_from_name(dev, oh->name, name);
		} else if (prev_chunk_id > 0) {
			memcpy(oh->name, old_name, sizeof(oh->name));
		} else {
			memset(oh->name, 0, sizeof(oh->name));
                }

		oh->is_shrink = is_shrink;

		switch (in->variant_type) {
		case YAFFS_OBJECT_TYPE_UNKNOWN:
			/* Should not happen */
			break;
		case YAFFS_OBJECT_TYPE_FILE:
			oh->file_size =
			    (oh->parent_obj_id == YAFFS_OBJECTID_DELETED
			     || oh->parent_obj_id ==
			     YAFFS_OBJECTID_UNLINKED) ? 0 : in->
			    variant.file_variant.file_size;
			break;
		case YAFFS_OBJECT_TYPE_HARDLINK:
			oh->equiv_id = in->variant.hardlink_variant.equiv_id;
			break;
		case YAFFS_OBJECT_TYPE_SPECIAL:
			/* Do nothing */
			break;
		case YAFFS_OBJECT_TYPE_DIRECTORY:
			/* Do nothing */
			break;
		case YAFFS_OBJECT_TYPE_SYMLINK:
			alias = in->variant.symlink_variant.alias;
			if (!alias)
				alias = _Y("no alias");
			strncpy(oh->alias, alias, YAFFS_MAX_ALIAS_LENGTH);
			oh->alias[YAFFS_MAX_ALIAS_LENGTH] = 0;
			break;
		}

		/* process any xattrib modifications */
		if (xmod)
			yaffs_apply_xattrib_mod(in, (char *)buffer, xmod);

		/* Tags */
		yaffs_init_tags(&new_tags);
		in->serial++;
		new_tags.chunk_id = 0;
		new_tags.obj_id = in->obj_id;
		new_tags.serial_number = in->serial;

		/* Add extra info for file header */

		new_tags.extra_available = 1;
		new_tags.extra_parent_id = oh->parent_obj_id;
		new_tags.extra_length = oh->file_size;
		new_tags.extra_is_shrink = oh->is_shrink;
		new_tags.extra_equiv_id = oh->equiv_id;
		new_tags.extra_shadows = (oh->shadows_obj > 0) ? 1 : 0;
		new_tags.extra_obj_type = in->variant_type;

		yaffs_verify_oh(in, oh, &new_tags, 1);

		/* Create new chunk in NAND */
		new_chunk_id =
		    yaffs_write_new_chunk(dev, buffer, &new_tags,
					  (prev_chunk_id > 0) ? 1 : 0);

		if (new_chunk_id >= 0) {

			in->hdr_chunk = new_chunk_id;

			if (prev_chunk_id > 0) {
				yaffs_chunk_del(dev, prev_chunk_id, 1,
						__LINE__);
			}

			if (!yaffs_obj_cache_dirty(in))
				in->dirty = 0;

			/* If this was a shrink, then mark the block that the chunk lives on */
			if (is_shrink) {
				bi = yaffs_get_block_info(in->my_dev,
							  new_chunk_id /
							  in->my_dev->param.
							  chunks_per_block);
				bi->has_shrink_hdr = 1;
			}

		}

		ret_val = new_chunk_id;

	}

	if (buffer)
		yaffs_release_temp_buffer(dev, buffer, __LINE__);

	return ret_val;
}

/*--------------------- File read/write ------------------------
 * Read and write have very similar structures.
 * In general the read/write has three parts to it
 * An incomplete chunk to start with (if the read/write is not chunk-aligned)
 * Some complete chunks
 * An incomplete chunk to end off with
 *
 * Curve-balls: the first chunk might also be the last chunk.
 */

int yaffs_file_rd(struct yaffs_obj *in, u8 * buffer, loff_t offset, int n_bytes)
{

	int chunk;
	u32 start;
	int n_copy;
	int n = n_bytes;
	int n_done = 0;
	struct yaffs_cache *cache;

	struct yaffs_dev *dev;

	dev = in->my_dev;

	while (n > 0) {
		/* chunk = offset / dev->data_bytes_per_chunk + 1; */
		/* start = offset % dev->data_bytes_per_chunk; */
		yaffs_addr_to_chunk(dev, offset, &chunk, &start);
		chunk++;

		/* OK now check for the curveball where the start and end are in
		 * the same chunk.
		 */
		if ((start + n) < dev->data_bytes_per_chunk)
			n_copy = n;
		else
			n_copy = dev->data_bytes_per_chunk - start;

		cache = yaffs_find_chunk_cache(in, chunk);

		/* If the chunk is already in the cache or it is less than a whole chunk
		 * or we're using inband tags then use the cache (if there is caching)
		 * else bypass the cache.
		 */
		if (cache || n_copy != dev->data_bytes_per_chunk
		    || dev->param.inband_tags) {
			if (dev->param.n_caches > 0) {

				/* If we can't find the data in the cache, then load it up. */

				if (!cache) {
					cache =
					    yaffs_grab_chunk_cache(in->my_dev);
					cache->object = in;
					cache->chunk_id = chunk;
					cache->dirty = 0;
					cache->locked = 0;
					yaffs_rd_data_obj(in, chunk,
							  cache->data);
					cache->n_bytes = 0;
				}

				yaffs_use_cache(dev, cache, 0);

				cache->locked = 1;

				memcpy(buffer, &cache->data[start], n_copy);

				cache->locked = 0;
			} else {
				/* Read into the local buffer then copy.. */

				u8 *local_buffer =
				    yaffs_get_temp_buffer(dev, __LINE__);
				yaffs_rd_data_obj(in, chunk, local_buffer);

				memcpy(buffer, &local_buffer[start], n_copy);

				yaffs_release_temp_buffer(dev, local_buffer,
							  __LINE__);
			}

		} else {

			/* A full chunk. Read directly into the supplied buffer. */
			yaffs_rd_data_obj(in, chunk, buffer);

		}

		n -= n_copy;
		offset += n_copy;
		buffer += n_copy;
		n_done += n_copy;

	}

	return n_done;
}

int yaffs_do_file_wr(struct yaffs_obj *in, const u8 * buffer, loff_t offset,
		     int n_bytes, int write_trhrough)
{

	int chunk;
	u32 start;
	int n_copy;
	int n = n_bytes;
	int n_done = 0;
	int n_writeback;
	int start_write = offset;
	int chunk_written = 0;
	u32 n_bytes_read;
	u32 chunk_start;

	struct yaffs_dev *dev;

	dev = in->my_dev;

	while (n > 0 && chunk_written >= 0) {
		yaffs_addr_to_chunk(dev, offset, &chunk, &start);

		if (chunk * dev->data_bytes_per_chunk + start != offset ||
		    start >= dev->data_bytes_per_chunk) {
			yaffs_trace(YAFFS_TRACE_ERROR,
				"AddrToChunk of offset %d gives chunk %d start %d",
				(int)offset, chunk, start);
		}
		chunk++;	/* File pos to chunk in file offset */

		/* OK now check for the curveball where the start and end are in
		 * the same chunk.
		 */

		if ((start + n) < dev->data_bytes_per_chunk) {
			n_copy = n;

			/* Now folks, to calculate how many bytes to write back....
			 * If we're overwriting and not writing to then end of file then
			 * we need to write back as much as was there before.
			 */

			chunk_start = ((chunk - 1) * dev->data_bytes_per_chunk);

			if (chunk_start > in->variant.file_variant.file_size)
				n_bytes_read = 0;	/* Past end of file */
			else
				n_bytes_read =
				    in->variant.file_variant.file_size -
				    chunk_start;

			if (n_bytes_read > dev->data_bytes_per_chunk)
				n_bytes_read = dev->data_bytes_per_chunk;

			n_writeback =
			    (n_bytes_read >
			     (start + n)) ? n_bytes_read : (start + n);

			if (n_writeback < 0
			    || n_writeback > dev->data_bytes_per_chunk)
				YBUG();

		} else {
			n_copy = dev->data_bytes_per_chunk - start;
			n_writeback = dev->data_bytes_per_chunk;
		}

		if (n_copy != dev->data_bytes_per_chunk
		    || dev->param.inband_tags) {
			/* An incomplete start or end chunk (or maybe both start and end chunk),
			 * or we're using inband tags, so we want to use the cache buffers.
			 */
			if (dev->param.n_caches > 0) {
				struct yaffs_cache *cache;
				/* If we can't find the data in the cache, then load the cache */
				cache = yaffs_find_chunk_cache(in, chunk);

				if (!cache
				    && yaffs_check_alloc_available(dev, 1)) {
					cache = yaffs_grab_chunk_cache(dev);
					cache->object = in;
					cache->chunk_id = chunk;
					cache->dirty = 0;
					cache->locked = 0;
					yaffs_rd_data_obj(in, chunk,
							  cache->data);
				} else if (cache &&
					   !cache->dirty &&
					   !yaffs_check_alloc_available(dev,
									1)) {
					/* Drop the cache if it was a read cache item and
					 * no space check has been made for it.
					 */
					cache = NULL;
				}

				if (cache) {
					yaffs_use_cache(dev, cache, 1);
					cache->locked = 1;

					memcpy(&cache->data[start], buffer,
					       n_copy);

					cache->locked = 0;
					cache->n_bytes = n_writeback;

					if (write_trhrough) {
						chunk_written =
						    yaffs_wr_data_obj
						    (cache->object,
						     cache->chunk_id,
						     cache->data,
						     cache->n_bytes, 1);
						cache->dirty = 0;
					}

				} else {
					chunk_written = -1;	/* fail the write */
				}
			} else {
				/* An incomplete start or end chunk (or maybe both start and end chunk)
				 * Read into the local buffer then copy, then copy over and write back.
				 */

				u8 *local_buffer =
				    yaffs_get_temp_buffer(dev, __LINE__);

				yaffs_rd_data_obj(in, chunk, local_buffer);

				memcpy(&local_buffer[start], buffer, n_copy);

				chunk_written =
				    yaffs_wr_data_obj(in, chunk,
						      local_buffer,
						      n_writeback, 0);

				yaffs_release_temp_buffer(dev, local_buffer,
							  __LINE__);

			}

		} else {
			/* A full chunk. Write directly from the supplied buffer. */

			chunk_written =
			    yaffs_wr_data_obj(in, chunk, buffer,
					      dev->data_bytes_per_chunk, 0);

			/* Since we've overwritten the cached data, we better invalidate it. */
			yaffs_invalidate_chunk_cache(in, chunk);
		}

		if (chunk_written >= 0) {
			n -= n_copy;
			offset += n_copy;
			buffer += n_copy;
			n_done += n_copy;
		}

	}

	/* Update file object */

	if ((start_write + n_done) > in->variant.file_variant.file_size)
		in->variant.file_variant.file_size = (start_write + n_done);

	in->dirty = 1;

	return n_done;
}

int yaffs_wr_file(struct yaffs_obj *in, const u8 * buffer, loff_t offset,
		  int n_bytes, int write_trhrough)
{
	yaffs2_handle_hole(in, offset);
	return yaffs_do_file_wr(in, buffer, offset, n_bytes, write_trhrough);
}

/* ---------------------- File resizing stuff ------------------ */

static void yaffs_prune_chunks(struct yaffs_obj *in, int new_size)
{

	struct yaffs_dev *dev = in->my_dev;
	int old_size = in->variant.file_variant.file_size;

	int last_del = 1 + (old_size - 1) / dev->data_bytes_per_chunk;

	int start_del = 1 + (new_size + dev->data_bytes_per_chunk - 1) /
	    dev->data_bytes_per_chunk;
	int i;
	int chunk_id;

	/* Delete backwards so that we don't end up with holes if
	 * power is lost part-way through the operation.
	 */
	for (i = last_del; i >= start_del; i--) {
		/* NB this could be optimised somewhat,
		 * eg. could retrieve the tags and write them without
		 * using yaffs_chunk_del
		 */

		chunk_id = yaffs_find_del_file_chunk(in, i, NULL);
		if (chunk_id > 0) {
			if (chunk_id <
			    (dev->internal_start_block *
			     dev->param.chunks_per_block)
			    || chunk_id >=
			    ((dev->internal_end_block +
			      1) * dev->param.chunks_per_block)) {
				yaffs_trace(YAFFS_TRACE_ALWAYS,
					"Found daft chunk_id %d for %d",
					chunk_id, i);
			} else {
				in->n_data_chunks--;
				yaffs_chunk_del(dev, chunk_id, 1, __LINE__);
			}
		}
	}

}

void yaffs_resize_file_down(struct yaffs_obj *obj, loff_t new_size)
{
	int new_full;
	u32 new_partial;
	struct yaffs_dev *dev = obj->my_dev;

	yaffs_addr_to_chunk(dev, new_size, &new_full, &new_partial);

	yaffs_prune_chunks(obj, new_size);

	if (new_partial != 0) {
		int last_chunk = 1 + new_full;
		u8 *local_buffer = yaffs_get_temp_buffer(dev, __LINE__);

		/* Rewrite the last chunk with its new size and zero pad */
		yaffs_rd_data_obj(obj, last_chunk, local_buffer);
		memset(local_buffer + new_partial, 0,
		       dev->data_bytes_per_chunk - new_partial);

		yaffs_wr_data_obj(obj, last_chunk, local_buffer,
				  new_partial, 1);

		yaffs_release_temp_buffer(dev, local_buffer, __LINE__);
	}

	obj->variant.file_variant.file_size = new_size;

	yaffs_prune_tree(dev, &obj->variant.file_variant);
}

int yaffs_resize_file(struct yaffs_obj *in, loff_t new_size)
{
	struct yaffs_dev *dev = in->my_dev;
	int old_size = in->variant.file_variant.file_size;

	yaffs_flush_file_cache(in);
	yaffs_invalidate_whole_cache(in);

	yaffs_check_gc(dev, 0);

	if (in->variant_type != YAFFS_OBJECT_TYPE_FILE)
		return YAFFS_FAIL;

	if (new_size == old_size)
		return YAFFS_OK;

	if (new_size > old_size) {
		yaffs2_handle_hole(in, new_size);
		in->variant.file_variant.file_size = new_size;
	} else {
		/* new_size < old_size */
		yaffs_resize_file_down(in, new_size);
	}

	/* Write a new object header to reflect the resize.
	 * show we've shrunk the file, if need be
	 * Do this only if the file is not in the deleted directories
	 * and is not shadowed.
	 */
	if (in->parent &&
	    !in->is_shadowed &&
	    in->parent->obj_id != YAFFS_OBJECTID_UNLINKED &&
	    in->parent->obj_id != YAFFS_OBJECTID_DELETED)
		yaffs_update_oh(in, NULL, 0, 0, 0, NULL);

	return YAFFS_OK;
}

int yaffs_flush_file(struct yaffs_obj *in, int update_time, int data_sync)
{
	int ret_val;
	if (in->dirty) {
		yaffs_flush_file_cache(in);
		if (data_sync)	/* Only sync data */
			ret_val = YAFFS_OK;
		else {
			if (update_time)
				yaffs_load_current_time(in, 0, 0);

			ret_val = (yaffs_update_oh(in, NULL, 0, 0, 0, NULL) >=
				   0) ? YAFFS_OK : YAFFS_FAIL;
		}
	} else {
		ret_val = YAFFS_OK;
	}

	return ret_val;

}


/* yaffs_del_file deletes the whole file data
 * and the inode associated with the file.
 * It does not delete the links associated with the file.
 */
static int yaffs_unlink_file_if_needed(struct yaffs_obj *in)
{

	int ret_val;
	int del_now = 0;
	struct yaffs_dev *dev = in->my_dev;

	if (!in->my_inode)
		del_now = 1;

	if (del_now) {
		ret_val =
		    yaffs_change_obj_name(in, in->my_dev->del_dir,
					  _Y("deleted"), 0, 0);
		yaffs_trace(YAFFS_TRACE_TRACING,
			"yaffs: immediate deletion of file %d",
			in->obj_id);
		in->deleted = 1;
		in->my_dev->n_deleted_files++;
		if (dev->param.disable_soft_del || dev->param.is_yaffs2)
			yaffs_resize_file(in, 0);
		yaffs_soft_del_file(in);
	} else {
		ret_val =
		    yaffs_change_obj_name(in, in->my_dev->unlinked_dir,
					  _Y("unlinked"), 0, 0);
	}

	return ret_val;
}

int yaffs_del_file(struct yaffs_obj *in)
{
	int ret_val = YAFFS_OK;
	int deleted;		/* Need to cache value on stack if in is freed */
	struct yaffs_dev *dev = in->my_dev;

	if (dev->param.disable_soft_del || dev->param.is_yaffs2)
		yaffs_resize_file(in, 0);

	if (in->n_data_chunks > 0) {
		/* Use soft deletion if there is data in the file.
		 * That won't be the case if it has been resized to zero.
		 */
		if (!in->unlinked)
			ret_val = yaffs_unlink_file_if_needed(in);

		deleted = in->deleted;

		if (ret_val == YAFFS_OK && in->unlinked && !in->deleted) {
			in->deleted = 1;
			deleted = 1;
			in->my_dev->n_deleted_files++;
			yaffs_soft_del_file(in);
		}
		return deleted ? YAFFS_OK : YAFFS_FAIL;
	} else {
		/* The file has no data chunks so we toss it immediately */
		yaffs_free_tnode(in->my_dev, in->variant.file_variant.top);
		in->variant.file_variant.top = NULL;
		yaffs_generic_obj_del(in);

		return YAFFS_OK;
	}
}

int yaffs_is_non_empty_dir(struct yaffs_obj *obj)
{
	return (obj &&
	        obj->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY) &&
	        !(list_empty(&obj->variant.dir_variant.children));
}

static int yaffs_del_dir(struct yaffs_obj *obj)
{
	/* First check that the directory is empty. */
	if (yaffs_is_non_empty_dir(obj))
		return YAFFS_FAIL;

	return yaffs_generic_obj_del(obj);
}

static int yaffs_del_symlink(struct yaffs_obj *in)
{
	if (in->variant.symlink_variant.alias)
		kfree(in->variant.symlink_variant.alias);
	in->variant.symlink_variant.alias = NULL;

	return yaffs_generic_obj_del(in);
}

static int yaffs_del_link(struct yaffs_obj *in)
{
	/* remove this hardlink from the list assocaited with the equivalent
	 * object
	 */
	list_del_init(&in->hard_links);
	return yaffs_generic_obj_del(in);
}

int yaffs_del_obj(struct yaffs_obj *obj)
{
	int ret_val = -1;
	switch (obj->variant_type) {
	case YAFFS_OBJECT_TYPE_FILE:
		ret_val = yaffs_del_file(obj);
		break;
	case YAFFS_OBJECT_TYPE_DIRECTORY:
		if (!list_empty(&obj->variant.dir_variant.dirty)) {
			yaffs_trace(YAFFS_TRACE_BACKGROUND,
				"Remove object %d from dirty directories",
				obj->obj_id);
			list_del_init(&obj->variant.dir_variant.dirty);
		}
		return yaffs_del_dir(obj);
		break;
	case YAFFS_OBJECT_TYPE_SYMLINK:
		ret_val = yaffs_del_symlink(obj);
		break;
	case YAFFS_OBJECT_TYPE_HARDLINK:
		ret_val = yaffs_del_link(obj);
		break;
	case YAFFS_OBJECT_TYPE_SPECIAL:
		ret_val = yaffs_generic_obj_del(obj);
		break;
	case YAFFS_OBJECT_TYPE_UNKNOWN:
		ret_val = 0;
		break;		/* should not happen. */
	}

	return ret_val;
}

static int yaffs_unlink_worker(struct yaffs_obj *obj)
{

	int del_now = 0;

	if (!obj->my_inode)
		del_now = 1;

	if (obj)
		yaffs_update_parent(obj->parent);

	if (obj->variant_type == YAFFS_OBJECT_TYPE_HARDLINK) {
		return yaffs_del_link(obj);
	} else if (!list_empty(&obj->hard_links)) {
		/* Curve ball: We're unlinking an object that has a hardlink.
		 *
		 * This problem arises because we are not strictly following
		 * The Linux link/inode model.
		 *
		 * We can't really delete the object.
		 * Instead, we do the following:
		 * - Select a hardlink.
		 * - Unhook it from the hard links
		 * - Move it from its parent directory (so that the rename can work)
		 * - Rename the object to the hardlink's name.
		 * - Delete the hardlink
		 */

		struct yaffs_obj *hl;
		struct yaffs_obj *parent;
		int ret_val;
		YCHAR name[YAFFS_MAX_NAME_LENGTH + 1];

		hl = list_entry(obj->hard_links.next, struct yaffs_obj,
				hard_links);

		yaffs_get_obj_name(hl, name, YAFFS_MAX_NAME_LENGTH + 1);
		parent = hl->parent;

		list_del_init(&hl->hard_links);

		yaffs_add_obj_to_dir(obj->my_dev->unlinked_dir, hl);

		ret_val = yaffs_change_obj_name(obj, parent, name, 0, 0);

		if (ret_val == YAFFS_OK)
			ret_val = yaffs_generic_obj_del(hl);

		return ret_val;

	} else if (del_now) {
		switch (obj->variant_type) {
		case YAFFS_OBJECT_TYPE_FILE:
			return yaffs_del_file(obj);
			break;
		case YAFFS_OBJECT_TYPE_DIRECTORY:
			list_del_init(&obj->variant.dir_variant.dirty);
			return yaffs_del_dir(obj);
			break;
		case YAFFS_OBJECT_TYPE_SYMLINK:
			return yaffs_del_symlink(obj);
			break;
		case YAFFS_OBJECT_TYPE_SPECIAL:
			return yaffs_generic_obj_del(obj);
			break;
		case YAFFS_OBJECT_TYPE_HARDLINK:
		case YAFFS_OBJECT_TYPE_UNKNOWN:
		default:
			return YAFFS_FAIL;
		}
	} else if (yaffs_is_non_empty_dir(obj)) {
		return YAFFS_FAIL;
	} else {
		return yaffs_change_obj_name(obj, obj->my_dev->unlinked_dir,
					     _Y("unlinked"), 0, 0);
        }
}

static int yaffs_unlink_obj(struct yaffs_obj *obj)
{

	if (obj && obj->unlink_allowed)
		return yaffs_unlink_worker(obj);

	return YAFFS_FAIL;

}

int yaffs_unlinker(struct yaffs_obj *dir, const YCHAR * name)
{
	struct yaffs_obj *obj;

	obj = yaffs_find_by_name(dir, name);
	return yaffs_unlink_obj(obj);
}

/* Note:
 * If old_name is NULL then we take old_dir as the object to be renamed.
 */
int yaffs_rename_obj(struct yaffs_obj *old_dir, const YCHAR * old_name,
		     struct yaffs_obj *new_dir, const YCHAR * new_name)
{
	struct yaffs_obj *obj = NULL;
	struct yaffs_obj *existing_target = NULL;
	int force = 0;
	int result;
	struct yaffs_dev *dev;

	if (!old_dir || old_dir->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY)
		YBUG();
	if (!new_dir || new_dir->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY)
		YBUG();

	dev = old_dir->my_dev;

#ifdef CONFIG_YAFFS_CASE_INSENSITIVE
	/* Special case for case insemsitive systems.
	 * While look-up is case insensitive, the name isn't.
	 * Therefore we might want to change x.txt to X.txt
	 */
	if (old_dir == new_dir && 
		old_name && new_name && 
		strcmp(old_name, new_name) == 0)
		force = 1;
#endif

	if (strnlen(new_name, YAFFS_MAX_NAME_LENGTH + 1) >
	    YAFFS_MAX_NAME_LENGTH)
		/* ENAMETOOLONG */
		return YAFFS_FAIL;

	if(old_name)
		obj = yaffs_find_by_name(old_dir, old_name);
	else{
		obj = old_dir;
		old_dir = obj->parent;
	}


	if (obj && obj->rename_allowed) {

		/* Now do the handling for an existing target, if there is one */

		existing_target = yaffs_find_by_name(new_dir, new_name);
		if (yaffs_is_non_empty_dir(existing_target)){
			return YAFFS_FAIL;	/* ENOTEMPTY */
		} else if (existing_target && existing_target != obj) {
			/* Nuke the target first, using shadowing,
			 * but only if it isn't the same object.
			 *
			 * Note we must disable gc otherwise it can mess up the shadowing.
			 *
			 */
			dev->gc_disable = 1;
			yaffs_change_obj_name(obj, new_dir, new_name, force,
					      existing_target->obj_id);
			existing_target->is_shadowed = 1;
			yaffs_unlink_obj(existing_target);
			dev->gc_disable = 0;
		}

		result = yaffs_change_obj_name(obj, new_dir, new_name, 1, 0);

		yaffs_update_parent(old_dir);
		if (new_dir != old_dir)
			yaffs_update_parent(new_dir);

		return result;
	}
	return YAFFS_FAIL;
}

/*----------------------- Initialisation Scanning ---------------------- */

void yaffs_handle_shadowed_obj(struct yaffs_dev *dev, int obj_id,
			       int backward_scanning)
{
	struct yaffs_obj *obj;

	if (!backward_scanning) {
		/* Handle YAFFS1 forward scanning case
		 * For YAFFS1 we always do the deletion
		 */

	} else {
		/* Handle YAFFS2 case (backward scanning)
		 * If the shadowed object exists then ignore.
		 */
		obj = yaffs_find_by_number(dev, obj_id);
		if (obj)
			return;
	}

	/* Let's create it (if it does not exist) assuming it is a file so that it can do shrinking etc.
	 * We put it in unlinked dir to be cleaned up after the scanning
	 */
	obj =
	    yaffs_find_or_create_by_number(dev, obj_id, YAFFS_OBJECT_TYPE_FILE);
	if (!obj)
		return;
	obj->is_shadowed = 1;
	yaffs_add_obj_to_dir(dev->unlinked_dir, obj);
	obj->variant.file_variant.shrink_size = 0;
	obj->valid = 1;		/* So that we don't read any other info for this file */

}

void yaffs_link_fixup(struct yaffs_dev *dev, struct yaffs_obj *hard_list)
{
	struct yaffs_obj *hl;
	struct yaffs_obj *in;

	while (hard_list) {
		hl = hard_list;
		hard_list = (struct yaffs_obj *)(hard_list->hard_links.next);

		in = yaffs_find_by_number(dev,
					  hl->variant.
					  hardlink_variant.equiv_id);

		if (in) {
			/* Add the hardlink pointers */
			hl->variant.hardlink_variant.equiv_obj = in;
			list_add(&hl->hard_links, &in->hard_links);
		} else {
			/* Todo Need to report/handle this better.
			 * Got a problem... hardlink to a non-existant object
			 */
			hl->variant.hardlink_variant.equiv_obj = NULL;
			INIT_LIST_HEAD(&hl->hard_links);

		}
	}
}

static void yaffs_strip_deleted_objs(struct yaffs_dev *dev)
{
	/*
	 *  Sort out state of unlinked and deleted objects after scanning.
	 */
	struct list_head *i;
	struct list_head *n;
	struct yaffs_obj *l;

	if (dev->read_only)
		return;

	/* Soft delete all the unlinked files */
	list_for_each_safe(i, n,
			   &dev->unlinked_dir->variant.dir_variant.children) {
		if (i) {
			l = list_entry(i, struct yaffs_obj, siblings);
			yaffs_del_obj(l);
		}
	}

	list_for_each_safe(i, n, &dev->del_dir->variant.dir_variant.children) {
		if (i) {
			l = list_entry(i, struct yaffs_obj, siblings);
			yaffs_del_obj(l);
		}
	}

}

/*
 * This code iterates through all the objects making sure that they are rooted.
 * Any unrooted objects are re-rooted in lost+found.
 * An object needs to be in one of:
 * - Directly under deleted, unlinked
 * - Directly or indirectly under root.
 *
 * Note:
 *  This code assumes that we don't ever change the current relationships between
 *  directories:
 *   root_dir->parent == unlinked_dir->parent == del_dir->parent == NULL
 *   lost-n-found->parent == root_dir
 *
 * This fixes the problem where directories might have inadvertently been deleted
 * leaving the object "hanging" without being rooted in the directory tree.
 */

static int yaffs_has_null_parent(struct yaffs_dev *dev, struct yaffs_obj *obj)
{
	return (obj == dev->del_dir ||
		obj == dev->unlinked_dir || obj == dev->root_dir);
}

static void yaffs_fix_hanging_objs(struct yaffs_dev *dev)
{
	struct yaffs_obj *obj;
	struct yaffs_obj *parent;
	int i;
	struct list_head *lh;
	struct list_head *n;
	int depth_limit;
	int hanging;

	if (dev->read_only)
		return;

	/* Iterate through the objects in each hash entry,
	 * looking at each object.
	 * Make sure it is rooted.
	 */

	for (i = 0; i < YAFFS_NOBJECT_BUCKETS; i++) {
		list_for_each_safe(lh, n, &dev->obj_bucket[i].list) {
			if (lh) {
				obj =
				    list_entry(lh, struct yaffs_obj, hash_link);
				parent = obj->parent;

				if (yaffs_has_null_parent(dev, obj)) {
					/* These directories are not hanging */
					hanging = 0;
				} else if (!parent
					   || parent->variant_type !=
					   YAFFS_OBJECT_TYPE_DIRECTORY) {
					hanging = 1;
				} else if (yaffs_has_null_parent(dev, parent)) {
					hanging = 0;
				} else {
					/*
					 * Need to follow the parent chain to see if it is hanging.
					 */
					hanging = 0;
					depth_limit = 100;

					while (parent != dev->root_dir &&
					       parent->parent &&
					       parent->parent->variant_type ==
					       YAFFS_OBJECT_TYPE_DIRECTORY
					       && depth_limit > 0) {
						parent = parent->parent;
						depth_limit--;
					}
					if (parent != dev->root_dir)
						hanging = 1;
				}
				if (hanging) {
					yaffs_trace(YAFFS_TRACE_SCAN,
						"Hanging object %d moved to lost and found",
						obj->obj_id);
					yaffs_add_obj_to_dir(dev->lost_n_found,
							     obj);
				}
			}
		}
	}
}

/*
 * Delete directory contents for cleaning up lost and found.
 */
static void yaffs_del_dir_contents(struct yaffs_obj *dir)
{
	struct yaffs_obj *obj;
	struct list_head *lh;
	struct list_head *n;

	if (dir->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY)
		YBUG();

	list_for_each_safe(lh, n, &dir->variant.dir_variant.children) {
		if (lh) {
			obj = list_entry(lh, struct yaffs_obj, siblings);
			if (obj->variant_type == YAFFS_OBJECT_TYPE_DIRECTORY)
				yaffs_del_dir_contents(obj);

			yaffs_trace(YAFFS_TRACE_SCAN,
				"Deleting lost_found object %d",
				obj->obj_id);

			/* Need to use UnlinkObject since Delete would not handle
			 * hardlinked objects correctly.
			 */
			yaffs_unlink_obj(obj);
		}
	}

}

static void yaffs_empty_l_n_f(struct yaffs_dev *dev)
{
	yaffs_del_dir_contents(dev->lost_n_found);
}


struct yaffs_obj *yaffs_find_by_name(struct yaffs_obj *directory,
				     const YCHAR * name)
{
	int sum;

	struct list_head *i;
	YCHAR buffer[YAFFS_MAX_NAME_LENGTH + 1];

	struct yaffs_obj *l;

	if (!name)
		return NULL;

	if (!directory) {
		yaffs_trace(YAFFS_TRACE_ALWAYS,
			"tragedy: yaffs_find_by_name: null pointer directory"
			);
		YBUG();
		return NULL;
	}
	if (directory->variant_type != YAFFS_OBJECT_TYPE_DIRECTORY) {
		yaffs_trace(YAFFS_TRACE_ALWAYS,
			"tragedy: yaffs_find_by_name: non-directory"
			);
		YBUG();
	}

	sum = yaffs_calc_name_sum(name);

	list_for_each(i, &directory->variant.dir_variant.children) {
		if (i) {
			l = list_entry(i, struct yaffs_obj, siblings);

			if (l->parent != directory)
				YBUG();

			yaffs_check_obj_details_loaded(l);

			/* Special case for lost-n-found */
			if (l->obj_id == YAFFS_OBJECTID_LOSTNFOUND) {
				if (!strcmp(name, YAFFS_LOSTNFOUND_NAME))
					return l;
			} else if (l->sum == sum
				   || l->hdr_chunk <= 0) {
				/* LostnFound chunk called Objxxx
				 * Do a real check
				 */
				yaffs_get_obj_name(l, buffer,
						   YAFFS_MAX_NAME_LENGTH + 1);
				if (strncmp
				    (name, buffer, YAFFS_MAX_NAME_LENGTH) == 0)
					return l;
			}
		}
	}

	return NULL;
}

/* GetEquivalentObject dereferences any hard links to get to the
 * actual object.
 */

struct yaffs_obj *yaffs_get_equivalent_obj(struct yaffs_obj *obj)
{
	if (obj && obj->variant_type == YAFFS_OBJECT_TYPE_HARDLINK) {
		/* We want the object id of the equivalent object, not this one */
		obj = obj->variant.hardlink_variant.equiv_obj;
		yaffs_check_obj_details_loaded(obj);
	}
	return obj;
}

/*
 *  A note or two on object names.
 *  * If the object name is missing, we then make one up in the form objnnn
 *
 *  * ASCII names are stored in the object header's name field from byte zero
 *  * Unicode names are historically stored starting from byte zero.
 *
 * Then there are automatic Unicode names...
 * The purpose of these is to save names in a way that can be read as
 * ASCII or Unicode names as appropriate, thus allowing a Unicode and ASCII
 * system to share files.
 *
 * These automatic unicode are stored slightly differently...
 *  - If the name can fit in the ASCII character space then they are saved as 
 *    ascii names as per above.
 *  - If the name needs Unicode then the name is saved in Unicode
 *    starting at oh->name[1].

 */
static void yaffs_fix_null_name(struct yaffs_obj *obj, YCHAR * name,
				int buffer_size)
{
	/* Create an object name if we could not find one. */
	if (strnlen(name, YAFFS_MAX_NAME_LENGTH) == 0) {
		YCHAR local_name[20];
		YCHAR num_string[20];
		YCHAR *x = &num_string[19];
		unsigned v = obj->obj_id;
		num_string[19] = 0;
		while (v > 0) {
			x--;
			*x = '0' + (v % 10);
			v /= 10;
		}
		/* make up a name */
		strcpy(local_name, YAFFS_LOSTNFOUND_PREFIX);
		strcat(local_name, x);
		strncpy(name, local_name, buffer_size - 1);
	}
}

int yaffs_get_obj_name(struct yaffs_obj *obj, YCHAR * name, int buffer_size)
{
	memset(name, 0, buffer_size * sizeof(YCHAR));

	yaffs_check_obj_details_loaded(obj);

	if (obj->obj_id == YAFFS_OBJECTID_LOSTNFOUND) {
		strncpy(name, YAFFS_LOSTNFOUND_NAME, buffer_size - 1);
	}
#ifndef CONFIG_YAFFS_NO_SHORT_NAMES
	else if (obj->short_name[0]) {
		strcpy(name, obj->short_name);
	}
#endif
	else if (obj->hdr_chunk > 0) {
		int result;
		u8 *buffer = yaffs_get_temp_buffer(obj->my_dev, __LINE__);

		struct yaffs_obj_hdr *oh = (struct yaffs_obj_hdr *)buffer;

		memset(buffer, 0, obj->my_dev->data_bytes_per_chunk);

		if (obj->hdr_chunk > 0) {
			result = yaffs_rd_chunk_tags_nand(obj->my_dev,
							  obj->hdr_chunk,
							  buffer, NULL);
		}
		yaffs_load_name_from_oh(obj->my_dev, name, oh->name,
					buffer_size);

		yaffs_release_temp_buffer(obj->my_dev, buffer, __LINE__);
	}

	yaffs_fix_null_name(obj, name, buffer_size);

	return strnlen(name, YAFFS_MAX_NAME_LENGTH);
}

int yaffs_get_obj_length(struct yaffs_obj *obj)
{
	/* Dereference any hard linking */
	obj = yaffs_get_equivalent_obj(obj);

	if (obj->variant_type == YAFFS_OBJECT_TYPE_FILE)
		return obj->variant.file_variant.file_size;
	if (obj->variant_type == YAFFS_OBJECT_TYPE_SYMLINK) {
		if (!obj->variant.symlink_variant.alias)
			return 0;
		return strnlen(obj->variant.symlink_variant.alias,
				     YAFFS_MAX_ALIAS_LENGTH);
	} else {
		/* Only a directory should drop through to here */
		return obj->my_dev->data_bytes_per_chunk;
	}
}

int yaffs_get_obj_link_count(struct yaffs_obj *obj)
{
	int count = 0;
	struct list_head *i;

	if (!obj->unlinked)
		count++;	/* the object itself */

	list_for_each(i, &obj->hard_links)
	    count++;		/* add the hard links; */

	return count;
}

int yaffs_get_obj_inode(struct yaffs_obj *obj)
{
	obj = yaffs_get_equivalent_obj(obj);

	return obj->obj_id;
}

unsigned yaffs_get_obj_type(struct yaffs_obj *obj)
{
	obj = yaffs_get_equivalent_obj(obj);

	switch (obj->variant_type) {
	case YAFFS_OBJECT_TYPE_FILE:
		return DT_REG;
		break;
	case YAFFS_OBJECT_TYPE_DIRECTORY:
		return DT_DIR;
		break;
	case YAFFS_OBJECT_TYPE_SYMLINK:
		return DT_LNK;
		break;
	case YAFFS_OBJECT_TYPE_HARDLINK:
		return DT_REG;
		break;
	case YAFFS_OBJECT_TYPE_SPECIAL:
		if (S_ISFIFO(obj->yst_mode))
			return DT_FIFO;
		if (S_ISCHR(obj->yst_mode))
			return DT_CHR;
		if (S_ISBLK(obj->yst_mode))
			return DT_BLK;
		if (S_ISSOCK(obj->yst_mode))
			return DT_SOCK;
	default:
		return DT_REG;
		break;
	}
}

YCHAR *yaffs_get_symlink_alias(struct yaffs_obj *obj)
{
	obj = yaffs_get_equivalent_obj(obj);
	if (obj->variant_type == YAFFS_OBJECT_TYPE_SYMLINK)
		return yaffs_clone_str(obj->variant.symlink_variant.alias);
	else
		return yaffs_clone_str(_Y(""));
}

/*--------------------------- Initialisation code -------------------------- */

static int yaffs_check_dev_fns(const struct yaffs_dev *dev)
{

	/* Common functions, gotta have */
	if (!dev->param.erase_fn || !dev->param.initialise_flash_fn)
		return 0;

#ifdef CONFIG_YAFFS_YAFFS2

	/* Can use the "with tags" style interface for yaffs1 or yaffs2 */
	if (dev->param.write_chunk_tags_fn &&
	    dev->param.read_chunk_tags_fn &&
	    !dev->param.write_chunk_fn &&
	    !dev->param.read_chunk_fn &&
	    dev->param.bad_block_fn && dev->param.query_block_fn)
		return 1;
#endif

	/* Can use the "spare" style interface for yaffs1 */
	if (!dev->param.is_yaffs2 &&
	    !dev->param.write_chunk_tags_fn &&
	    !dev->param.read_chunk_tags_fn &&
	    dev->param.write_chunk_fn &&
	    dev->param.read_chunk_fn &&
	    !dev->param.bad_block_fn && !dev->param.query_block_fn)
		return 1;

	return 0;		/* bad */
}

static int yaffs_create_initial_dir(struct yaffs_dev *dev)
{
	/* Initialise the unlinked, deleted, root and lost and found directories */

	dev->lost_n_found = dev->root_dir = NULL;
	dev->unlinked_dir = dev->del_dir = NULL;

	dev->unlinked_dir =
	    yaffs_create_fake_dir(dev, YAFFS_OBJECTID_UNLINKED, S_IFDIR);

	dev->del_dir =
	    yaffs_create_fake_dir(dev, YAFFS_OBJECTID_DELETED, S_IFDIR);

	dev->root_dir =
	    yaffs_create_fake_dir(dev, YAFFS_OBJECTID_ROOT,
				  YAFFS_ROOT_MODE | S_IFDIR);
	dev->lost_n_found =
	    yaffs_create_fake_dir(dev, YAFFS_OBJECTID_LOSTNFOUND,
				  YAFFS_LOSTNFOUND_MODE | S_IFDIR);

	if (dev->lost_n_found && dev->root_dir && dev->unlinked_dir
	    && dev->del_dir) {
		yaffs_add_obj_to_dir(dev->root_dir, dev->lost_n_found);
		return YAFFS_OK;
	}

	return YAFFS_FAIL;
}

int yaffs_guts_initialise(struct yaffs_dev *dev)
{
	int init_failed = 0;
	unsigned x;
	int bits;

	yaffs_trace(YAFFS_TRACE_TRACING, "yaffs: yaffs_guts_initialise()" );

	/* Check stuff that must be set */

	if (!dev) {
		yaffs_trace(YAFFS_TRACE_ALWAYS,
			"yaffs: Need a device"
			);
		return YAFFS_FAIL;
	}

	dev->internal_start_block = dev->param.start_block;
	dev->internal_end_block = dev->param.end_block;
	dev->block_offset = 0;
	dev->chunk_offset = 0;
	dev->n_free_chunks = 0;

	dev->gc_block = 0;

	if (dev->param.start_block == 0) {
		dev->internal_start_block = dev->param.start_block + 1;
		dev->internal_end_block = dev->param.end_block + 1;
		dev->block_offset = 1;
		dev->chunk_offset = dev->param.chunks_per_block;
	}

	/* Check geometry parameters. */

	if ((!dev->param.inband_tags && dev->param.is_yaffs2 &&
		dev->param.total_bytes_per_chunk < 1024) ||
		(!dev->param.is_yaffs2 &&
			dev->param.total_bytes_per_chunk < 512) ||
		(dev->param.inband_tags && !dev->param.is_yaffs2) ||
		 dev->param.chunks_per_block < 2 ||
		 dev->param.n_reserved_blocks < 2 ||
		dev->internal_start_block <= 0 || 
		dev->internal_end_block <= 0 || 
		dev->internal_end_block <= 
		(dev->internal_start_block + dev->param.n_reserved_blocks + 2)
		) {
		/* otherwise it is too small */
		yaffs_trace(YAFFS_TRACE_ALWAYS,
			"NAND geometry problems: chunk size %d, type is yaffs%s, inband_tags %d ",
			dev->param.total_bytes_per_chunk,
			dev->param.is_yaffs2 ? "2" : "",
			dev->param.inband_tags);
		return YAFFS_FAIL;
	}

	if (yaffs_init_nand(dev) != YAFFS_OK) {
		yaffs_trace(YAFFS_TRACE_ALWAYS, "InitialiseNAND failed");
		return YAFFS_FAIL;
	}

	/* Sort out space for inband tags, if required */
	if (dev->param.inband_tags)
		dev->data_bytes_per_chunk =
		    dev->param.total_bytes_per_chunk -
		    sizeof(struct yaffs_packed_tags2_tags_only);
	else
		dev->data_bytes_per_chunk = dev->param.total_bytes_per_chunk;

	/* Got the right mix of functions? */
	if (!yaffs_check_dev_fns(dev)) {
		/* Function missing */
		yaffs_trace(YAFFS_TRACE_ALWAYS,
			"device function(s) missing or wrong");

		return YAFFS_FAIL;
	}

	if (dev->is_mounted) {
		yaffs_trace(YAFFS_TRACE_ALWAYS, "device already mounted");
		return YAFFS_FAIL;
	}

	/* Finished with most checks. One or two more checks happen later on too. */

	dev->is_mounted = 1;

	/* OK now calculate a few things for the device */

	/*
	 *  Calculate all the chunk size manipulation numbers:
	 */
	x = dev->data_bytes_per_chunk;
	/* We always use dev->chunk_shift and dev->chunk_div */
	dev->chunk_shift = calc_shifts(x);
	x >>= dev->chunk_shift;
	dev->chunk_div = x;
	/* We only use chunk mask if chunk_div is 1 */
	dev->chunk_mask = (1 << dev->chunk_shift) - 1;

	/*
	 * Calculate chunk_grp_bits.
	 * We need to find the next power of 2 > than internal_end_block
	 */

	x = dev->param.chunks_per_block * (dev->internal_end_block + 1);

	bits = calc_shifts_ceiling(x);

	/* Set up tnode width if wide tnodes are enabled. */
	if (!dev->param.wide_tnodes_disabled) {
		/* bits must be even so that we end up with 32-bit words */
		if (bits & 1)
			bits++;
		if (bits < 16)
			dev->tnode_width = 16;
		else
			dev->tnode_width = bits;
	} else {
		dev->tnode_width = 16;
        }

	dev->tnode_mask = (1 << dev->tnode_width) - 1;

	/* Level0 Tnodes are 16 bits or wider (if wide tnodes are enabled),
	 * so if the bitwidth of the
	 * chunk range we're using is greater than 16 we need
	 * to figure out chunk shift and chunk_grp_size
	 */

	if (bits <= dev->tnode_width)
		dev->chunk_grp_bits = 0;
	else
		dev->chunk_grp_bits = bits - dev->tnode_width;

	dev->tnode_size = (dev->tnode_width * YAFFS_NTNODES_LEVEL0) / 8;
	if (dev->tnode_size < sizeof(struct yaffs_tnode))
		dev->tnode_size = sizeof(struct yaffs_tnode);

	dev->chunk_grp_size = 1 << dev->chunk_grp_bits;

	if (dev->param.chunks_per_block < dev->chunk_grp_size) {
		/* We have a problem because the soft delete won't work if
		 * the chunk group size > chunks per block.
		 * This can be remedied by using larger "virtual blocks".
		 */
		yaffs_trace(YAFFS_TRACE_ALWAYS, "chunk group too large");

		return YAFFS_FAIL;
	}

	/* OK, we've finished verifying the device, lets continue with initialisation */

	/* More device initialisation */
	dev->all_gcs = 0;
	dev->passive_gc_count = 0;
	dev->oldest_dirty_gc_count = 0;
	dev->bg_gcs = 0;
	dev->gc_block_finder = 0;
	dev->buffered_block = -1;
	dev->doing_buffered_block_rewrite = 0;
	dev->n_deleted_files = 0;
	dev->n_bg_deletions = 0;
	dev->n_unlinked_files = 0;
	dev->n_ecc_fixed = 0;
	dev->n_ecc_unfixed = 0;
	dev->n_tags_ecc_fixed = 0;
	dev->n_tags_ecc_unfixed = 0;
	dev->n_erase_failures = 0;
	dev->n_erased_blocks = 0;
	dev->gc_disable = 0;
	dev->has_pending_prioritised_gc = 1;	/* Assume the worst for now, will get fixed on first GC */
	INIT_LIST_HEAD(&dev->dirty_dirs);
	dev->oldest_dirty_seq = 0;
	dev->oldest_dirty_block = 0;

	/* Initialise temporary buffers and caches. */
	if (!yaffs_init_tmp_buffers(dev))
		init_failed = 1;

	dev->cache = NULL;
	dev->gc_cleanup_list = NULL;

	if (!init_failed && dev->param.n_caches > 0) {
		int i;
		void *buf;
		int cache_bytes =
		    dev->param.n_caches * sizeof(struct yaffs_cache);

		if (dev->param.n_caches > YAFFS_MAX_SHORT_OP_CACHES)
			dev->param.n_caches = YAFFS_MAX_SHORT_OP_CACHES;

		dev->cache = kmalloc(cache_bytes, GFP_NOFS);

		buf = (u8 *) dev->cache;

		if (dev->cache)
			memset(dev->cache, 0, cache_bytes);

		for (i = 0; i < dev->param.n_caches && buf; i++) {
			dev->cache[i].object = NULL;
			dev->cache[i].last_use = 0;
			dev->cache[i].dirty = 0;
			dev->cache[i].data = buf =
			    kmalloc(dev->param.total_bytes_per_chunk, GFP_NOFS);
		}
		if (!buf)
			init_failed = 1;

		dev->cache_last_use = 0;
	}

	dev->cache_hits = 0;

	if (!init_failed) {
		dev->gc_cleanup_list =
		    kmalloc(dev->param.chunks_per_block * sizeof(u32),
					GFP_NOFS);
		if (!dev->gc_cleanup_list)
			init_failed = 1;
	}

	if (dev->param.is_yaffs2)
		dev->param.use_header_file_size = 1;

	if (!init_failed && !yaffs_init_blocks(dev))
		init_failed = 1;

	yaffs_init_tnodes_and_objs(dev);

	if (!init_failed && !yaffs_create_initial_dir(dev))
		init_failed = 1;

	if (!init_failed) {
		/* Now scan the flash. */
		if (dev->param.is_yaffs2) {
			if (yaffs2_checkpt_restore(dev)) {
				yaffs_check_obj_details_loaded(dev->root_dir);
				yaffs_trace(YAFFS_TRACE_CHECKPOINT | YAFFS_TRACE_MOUNT,
					"yaffs: restored from checkpoint"
					);
			} else {

				/* Clean up the mess caused by an aborted checkpoint load
				 * and scan backwards.
				 */
				yaffs_deinit_blocks(dev);

				yaffs_deinit_tnodes_and_objs(dev);

				dev->n_erased_blocks = 0;
				dev->n_free_chunks = 0;
				dev->alloc_block = -1;
				dev->alloc_page = -1;
				dev->n_deleted_files = 0;
				dev->n_unlinked_files = 0;
				dev->n_bg_deletions = 0;

				if (!init_failed && !yaffs_init_blocks(dev))
					init_failed = 1;

				yaffs_init_tnodes_and_objs(dev);

				if (!init_failed
				    && !yaffs_create_initial_dir(dev))
					init_failed = 1;

				if (!init_failed && !yaffs2_scan_backwards(dev))
					init_failed = 1;
			}
		} else if (!yaffs1_scan(dev)) {
			init_failed = 1;
                }

		yaffs_strip_deleted_objs(dev);
		yaffs_fix_hanging_objs(dev);
		if (dev->param.empty_lost_n_found)
			yaffs_empty_l_n_f(dev);
	}

	if (init_failed) {
		/* Clean up the mess */
		yaffs_trace(YAFFS_TRACE_TRACING,
		  "yaffs: yaffs_guts_initialise() aborted.");

		yaffs_deinitialise(dev);
		return YAFFS_FAIL;
	}

	/* Zero out stats */
	dev->n_page_reads = 0;
	dev->n_page_writes = 0;
	dev->n_erasures = 0;
	dev->n_gc_copies = 0;
	dev->n_retired_writes = 0;

	dev->n_retired_blocks = 0;

	yaffs_verify_free_chunks(dev);
	yaffs_verify_blocks(dev);

	/* Clean up any aborted checkpoint data */
	if (!dev->is_checkpointed && dev->blocks_in_checkpt > 0)
		yaffs2_checkpt_invalidate(dev);

	yaffs_trace(YAFFS_TRACE_TRACING,
	  "yaffs: yaffs_guts_initialise() done.");
	return YAFFS_OK;

}

void yaffs_deinitialise(struct yaffs_dev *dev)
{
	if (dev->is_mounted) {
		int i;

		yaffs_deinit_blocks(dev);
		yaffs_deinit_tnodes_and_objs(dev);
		if (dev->param.n_caches > 0 && dev->cache) {

			for (i = 0; i < dev->param.n_caches; i++) {
				if (dev->cache[i].data)
					kfree(dev->cache[i].data);
				dev->cache[i].data = NULL;
			}

			kfree(dev->cache);
			dev->cache = NULL;
		}

		kfree(dev->gc_cleanup_list);

		for (i = 0; i < YAFFS_N_TEMP_BUFFERS; i++)
			kfree(dev->temp_buffer[i].buffer);

		dev->is_mounted = 0;

		if (dev->param.deinitialise_flash_fn)
			dev->param.deinitialise_flash_fn(dev);
	}
}

int yaffs_count_free_chunks(struct yaffs_dev *dev)
{
	int n_free = 0;
	int b;

	struct yaffs_block_info *blk;

	blk = dev->block_info;
	for (b = dev->internal_start_block; b <= dev->internal_end_block; b++) {
		switch (blk->block_state) {
		case YAFFS_BLOCK_STATE_EMPTY:
		case YAFFS_BLOCK_STATE_ALLOCATING:
		case YAFFS_BLOCK_STATE_COLLECTING:
		case YAFFS_BLOCK_STATE_FULL:
			n_free +=
			    (dev->param.chunks_per_block - blk->pages_in_use +
			     blk->soft_del_pages);
			break;
		default:
			break;
		}
		blk++;
	}

	return n_free;
}

int yaffs_get_n_free_chunks(struct yaffs_dev *dev)
{
	/* This is what we report to the outside world */

	int n_free;
	int n_dirty_caches;
	int blocks_for_checkpt;
	int i;

	n_free = dev->n_free_chunks;
	n_free += dev->n_deleted_files;

	/* Now count the number of dirty chunks in the cache and subtract those */

	for (n_dirty_caches = 0, i = 0; i < dev->param.n_caches; i++) {
		if (dev->cache[i].dirty)
			n_dirty_caches++;
	}

	n_free -= n_dirty_caches;

	n_free -=
	    ((dev->param.n_reserved_blocks + 1) * dev->param.chunks_per_block);

	/* Now we figure out how much to reserve for the checkpoint and report that... */
	blocks_for_checkpt = yaffs_calc_checkpt_blocks_required(dev);

	n_free -= (blocks_for_checkpt * dev->param.chunks_per_block);

	if (n_free < 0)
		n_free = 0;

	return n_free;

}
