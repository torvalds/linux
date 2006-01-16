/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

/*
* Implements Extendible Hashing as described in:
*   "Extendible Hashing" by Fagin, et al in
*     __ACM Trans. on Database Systems__, Sept 1979.
*
*
* Here's the layout of dirents which is essentially the same as that of ext2
* within a single block. The field de_name_len is the number of bytes
* actually required for the name (no null terminator). The field de_rec_len
* is the number of bytes allocated to the dirent. The offset of the next
* dirent in the block is (dirent + dirent->de_rec_len). When a dirent is
* deleted, the preceding dirent inherits its allocated space, ie
* prev->de_rec_len += deleted->de_rec_len. Since the next dirent is obtained
* by adding de_rec_len to the current dirent, this essentially causes the
* deleted dirent to get jumped over when iterating through all the dirents.
*
* When deleting the first dirent in a block, there is no previous dirent so
* the field de_ino is set to zero to designate it as deleted. When allocating
* a dirent, gfs2_dirent_alloc iterates through the dirents in a block. If the
* first dirent has (de_ino == 0) and de_rec_len is large enough, this first
* dirent is allocated. Otherwise it must go through all the 'used' dirents
* searching for one in which the amount of total space minus the amount of
* used space will provide enough space for the new dirent.
*
* There are two types of blocks in which dirents reside. In a stuffed dinode,
* the dirents begin at offset sizeof(struct gfs2_dinode) from the beginning of
* the block.  In leaves, they begin at offset sizeof(struct gfs2_leaf) from the
* beginning of the leaf block. The dirents reside in leaves when
*
* dip->i_di.di_flags & GFS2_DIF_EXHASH is true
*
* Otherwise, the dirents are "linear", within a single stuffed dinode block.
*
* When the dirents are in leaves, the actual contents of the directory file are
* used as an array of 64-bit block pointers pointing to the leaf blocks. The
* dirents are NOT in the directory file itself. There can be more than one block
* pointer in the array that points to the same leaf. In fact, when a directory
* is first converted from linear to exhash, all of the pointers point to the
* same leaf.
*
* When a leaf is completely full, the size of the hash table can be
* doubled unless it is already at the maximum size which is hard coded into
* GFS2_DIR_MAX_DEPTH. After that, leaves are chained together in a linked list,
* but never before the maximum hash table size has been reached.
*/

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/sort.h>
#include <asm/semaphore.h>

#include "gfs2.h"
#include "dir.h"
#include "glock.h"
#include "inode.h"
#include "jdata.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"

#define IS_LEAF     1 /* Hashed (leaf) directory */
#define IS_DINODE   2 /* Linear (stuffed dinode block) directory */

#if 1
#define gfs2_disk_hash2offset(h) (((uint64_t)(h)) >> 1)
#define gfs2_dir_offset2hash(p) ((uint32_t)(((uint64_t)(p)) << 1))
#else
#define gfs2_disk_hash2offset(h) (((uint64_t)(h)))
#define gfs2_dir_offset2hash(p) ((uint32_t)(((uint64_t)(p))))
#endif

typedef int (*leaf_call_t) (struct gfs2_inode *dip,
			    uint32_t index, uint32_t len, uint64_t leaf_no,
			    void *data);

/**
 * int gfs2_filecmp - Compare two filenames
 * @file1: The first filename
 * @file2: The second filename
 * @len_of_file2: The length of the second file
 *
 * This routine compares two filenames and returns 1 if they are equal.
 *
 * Returns: 1 if the files are the same, otherwise 0.
 */

int gfs2_filecmp(struct qstr *file1, char *file2, int len_of_file2)
{
	if (file1->len != len_of_file2)
		return 0;
	if (memcmp(file1->name, file2, file1->len))
		return 0;
	return 1;
}

/**
 * dirent_first - Return the first dirent
 * @dip: the directory
 * @bh: The buffer
 * @dent: Pointer to list of dirents
 *
 * return first dirent whether bh points to leaf or stuffed dinode
 *
 * Returns: IS_LEAF, IS_DINODE, or -errno
 */

static int dirent_first(struct gfs2_inode *dip, struct buffer_head *bh,
			struct gfs2_dirent **dent)
{
	struct gfs2_meta_header *h = (struct gfs2_meta_header *)bh->b_data;

	if (be16_to_cpu(h->mh_type) == GFS2_METATYPE_LF) {
		if (gfs2_meta_check(dip->i_sbd, bh))
			return -EIO;
		*dent = (struct gfs2_dirent *)(bh->b_data +
					       sizeof(struct gfs2_leaf));
		return IS_LEAF;
	} else {
		if (gfs2_metatype_check(dip->i_sbd, bh, GFS2_METATYPE_DI))
			return -EIO;
		*dent = (struct gfs2_dirent *)(bh->b_data +
					       sizeof(struct gfs2_dinode));
		return IS_DINODE;
	}
}

/**
 * dirent_next - Next dirent
 * @dip: the directory
 * @bh: The buffer
 * @dent: Pointer to list of dirents
 *
 * Returns: 0 on success, error code otherwise
 */

static int dirent_next(struct gfs2_inode *dip, struct buffer_head *bh,
		       struct gfs2_dirent **dent)
{
	struct gfs2_dirent *tmp, *cur;
	char *bh_end;
	uint32_t cur_rec_len;

	cur = *dent;
	bh_end = bh->b_data + bh->b_size;
	cur_rec_len = be32_to_cpu(cur->de_rec_len);

	if ((char *)cur + cur_rec_len >= bh_end) {
		if ((char *)cur + cur_rec_len > bh_end) {
			gfs2_consist_inode(dip);
			return -EIO;
		}
		return -ENOENT;
	}

	tmp = (struct gfs2_dirent *)((char *)cur + cur_rec_len);

	if ((char *)tmp + be32_to_cpu(tmp->de_rec_len) > bh_end) {
		gfs2_consist_inode(dip);
		return -EIO;
	}
        /* Only the first dent could ever have de_inum.no_addr == 0 */
	if (!tmp->de_inum.no_addr) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	*dent = tmp;

	return 0;
}

/**
 * dirent_del - Delete a dirent
 * @dip: The GFS2 inode
 * @bh: The buffer
 * @prev: The previous dirent
 * @cur: The current dirent
 *
 */

static void dirent_del(struct gfs2_inode *dip, struct buffer_head *bh,
		       struct gfs2_dirent *prev, struct gfs2_dirent *cur)
{
	uint32_t cur_rec_len, prev_rec_len;

	if (!cur->de_inum.no_addr) {
		gfs2_consist_inode(dip);
		return;
	}

	gfs2_trans_add_bh(dip->i_gl, bh);

	/* If there is no prev entry, this is the first entry in the block.
	   The de_rec_len is already as big as it needs to be.  Just zero
	   out the inode number and return.  */

	if (!prev) {
		cur->de_inum.no_addr = 0;	/* No endianess worries */
		return;
	}

	/*  Combine this dentry with the previous one.  */

	prev_rec_len = be32_to_cpu(prev->de_rec_len);
	cur_rec_len = be32_to_cpu(cur->de_rec_len);

	if ((char *)prev + prev_rec_len != (char *)cur)
		gfs2_consist_inode(dip);
	if ((char *)cur + cur_rec_len > bh->b_data + bh->b_size)
		gfs2_consist_inode(dip);

	prev_rec_len += cur_rec_len;
	prev->de_rec_len = cpu_to_be32(prev_rec_len);
}

/**
 * gfs2_dirent_alloc - Allocate a directory entry
 * @dip: The GFS2 inode
 * @bh: The buffer
 * @name_len: The length of the name
 * @dent_out: Pointer to list of dirents
 *
 * Returns: 0 on success, error code otherwise
 */

int gfs2_dirent_alloc(struct gfs2_inode *dip, struct buffer_head *bh,
		      int name_len, struct gfs2_dirent **dent_out)
{
	struct gfs2_dirent *dent, *new;
	unsigned int rec_len = GFS2_DIRENT_SIZE(name_len);
	unsigned int entries = 0, offset = 0;
	int type;

	type = dirent_first(dip, bh, &dent);
	if (type < 0)
		return type;

	if (type == IS_LEAF) {
		struct gfs2_leaf *leaf = (struct gfs2_leaf *)bh->b_data;
		entries = be16_to_cpu(leaf->lf_entries);
		offset = sizeof(struct gfs2_leaf);
	} else {
		struct gfs2_dinode *dinode = (struct gfs2_dinode *)bh->b_data;
		entries = be32_to_cpu(dinode->di_entries);
		offset = sizeof(struct gfs2_dinode);
	}

	if (!entries) {
		if (dent->de_inum.no_addr) {
			gfs2_consist_inode(dip);
			return -EIO;
		}

		gfs2_trans_add_bh(dip->i_gl, bh);

		dent->de_rec_len = bh->b_size - offset;
		dent->de_rec_len = cpu_to_be32(dent->de_rec_len);
		dent->de_name_len = name_len;

		*dent_out = dent;
		return 0;
	}

	do {
		uint32_t cur_rec_len, cur_name_len;

		cur_rec_len = be32_to_cpu(dent->de_rec_len);
		cur_name_len = dent->de_name_len;

		if ((!dent->de_inum.no_addr && cur_rec_len >= rec_len) ||
		    (cur_rec_len >= GFS2_DIRENT_SIZE(cur_name_len) + rec_len)) {
			gfs2_trans_add_bh(dip->i_gl, bh);

			if (dent->de_inum.no_addr) {
				new = (struct gfs2_dirent *)((char *)dent +
							    GFS2_DIRENT_SIZE(cur_name_len));
				memset(new, 0, sizeof(struct gfs2_dirent));

				new->de_rec_len = cur_rec_len - GFS2_DIRENT_SIZE(cur_name_len);
				new->de_rec_len = cpu_to_be32(new->de_rec_len);
				new->de_name_len = name_len;

				dent->de_rec_len = cur_rec_len - be32_to_cpu(new->de_rec_len);
				dent->de_rec_len = cpu_to_be32(dent->de_rec_len);

				*dent_out = new;
				return 0;
			}

			dent->de_name_len = name_len;

			*dent_out = dent;
			return 0;
		}
	} while (dirent_next(dip, bh, &dent) == 0);

	return -ENOSPC;
}

/**
 * dirent_fits - See if we can fit a entry in this buffer
 * @dip: The GFS2 inode
 * @bh: The buffer
 * @name_len: The length of the name
 *
 * Returns: 1 if it can fit, 0 otherwise
 */

static int dirent_fits(struct gfs2_inode *dip, struct buffer_head *bh,
		       int name_len)
{
	struct gfs2_dirent *dent;
	unsigned int rec_len = GFS2_DIRENT_SIZE(name_len);
	unsigned int entries = 0;
	int type;

	type = dirent_first(dip, bh, &dent);
	if (type < 0)
		return type;

	if (type == IS_LEAF) {
		struct gfs2_leaf *leaf = (struct gfs2_leaf *)bh->b_data;
		entries = be16_to_cpu(leaf->lf_entries);
	} else {
		struct gfs2_dinode *dinode = (struct gfs2_dinode *)bh->b_data;
		entries = be32_to_cpu(dinode->di_entries);
	}

	if (!entries)
		return 1;

	do {
		uint32_t cur_rec_len, cur_name_len;

		cur_rec_len = be32_to_cpu(dent->de_rec_len);
		cur_name_len = dent->de_name_len;

		if ((!dent->de_inum.no_addr && cur_rec_len >= rec_len) ||
		    (cur_rec_len >= GFS2_DIRENT_SIZE(cur_name_len) + rec_len))
			return 1;
	} while (dirent_next(dip, bh, &dent) == 0);

	return 0;
}

static int leaf_search(struct gfs2_inode *dip, struct buffer_head *bh,
		       struct qstr *filename, struct gfs2_dirent **dent_out,
		       struct gfs2_dirent **dent_prev)
{
	uint32_t hash;
	struct gfs2_dirent *dent, *prev = NULL;
	unsigned int entries = 0;
	int type;

	type = dirent_first(dip, bh, &dent);
	if (type < 0)
		return type;

	if (type == IS_LEAF) {
		struct gfs2_leaf *leaf = (struct gfs2_leaf *)bh->b_data;
		entries = be16_to_cpu(leaf->lf_entries);
	} else if (type == IS_DINODE) {
		struct gfs2_dinode *dinode = (struct gfs2_dinode *)bh->b_data;
		entries = be32_to_cpu(dinode->di_entries);
	}

	hash = gfs2_disk_hash(filename->name, filename->len);

	do {
		if (!dent->de_inum.no_addr) {
			prev = dent;
			continue;
		}

		if (be32_to_cpu(dent->de_hash) == hash &&
		    gfs2_filecmp(filename, (char *)(dent + 1),
				 dent->de_name_len)) {
			*dent_out = dent;
			if (dent_prev)
				*dent_prev = prev;

			return 0;
		}

		prev = dent;
	} while (dirent_next(dip, bh, &dent) == 0);

	return -ENOENT;
}

static int get_leaf(struct gfs2_inode *dip, uint64_t leaf_no,
		    struct buffer_head **bhp)
{
	int error;

	error = gfs2_meta_read(dip->i_gl, leaf_no, DIO_START | DIO_WAIT, bhp);
	if (!error && gfs2_metatype_check(dip->i_sbd, *bhp, GFS2_METATYPE_LF))
		error = -EIO;

	return error;
}

/**
 * get_leaf_nr - Get a leaf number associated with the index
 * @dip: The GFS2 inode
 * @index:
 * @leaf_out:
 *
 * Returns: 0 on success, error code otherwise
 */

static int get_leaf_nr(struct gfs2_inode *dip, uint32_t index,
		       uint64_t *leaf_out)
{
	uint64_t leaf_no;
	int error;

	error = gfs2_jdata_read_mem(dip, (char *)&leaf_no,
				    index * sizeof(uint64_t),
				    sizeof(uint64_t));
	if (error != sizeof(uint64_t))
		return (error < 0) ? error : -EIO;

	*leaf_out = be64_to_cpu(leaf_no);

	return 0;
}

static int get_first_leaf(struct gfs2_inode *dip, uint32_t index,
			  struct buffer_head **bh_out)
{
	uint64_t leaf_no;
	int error;

	error = get_leaf_nr(dip, index, &leaf_no);
	if (!error)
		error = get_leaf(dip, leaf_no, bh_out);

	return error;
}

static int get_next_leaf(struct gfs2_inode *dip, struct buffer_head *bh_in,
			 struct buffer_head **bh_out)
{
	struct gfs2_leaf *leaf;
	int error;

	leaf = (struct gfs2_leaf *)bh_in->b_data;

	if (!leaf->lf_next)
		error = -ENOENT;
	else
		error = get_leaf(dip, be64_to_cpu(leaf->lf_next), bh_out);

	return error;
}

static int linked_leaf_search(struct gfs2_inode *dip, struct qstr *filename,
			      struct gfs2_dirent **dent_out,
			      struct gfs2_dirent **dent_prev,
			      struct buffer_head **bh_out)
{
	struct buffer_head *bh = NULL, *bh_next;
	uint32_t hsize, index;
	uint32_t hash;
	int error;

	hsize = 1 << dip->i_di.di_depth;
	if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	/*  Figure out the address of the leaf node.  */

	hash = gfs2_disk_hash(filename->name, filename->len);
	index = hash >> (32 - dip->i_di.di_depth);

	error = get_first_leaf(dip, index, &bh_next);
	if (error)
		return error;

	/*  Find the entry  */

	do {
		brelse(bh);

		bh = bh_next;

		error = leaf_search(dip, bh, filename, dent_out, dent_prev);
		switch (error) {
		case 0:
			*bh_out = bh;
			return 0;

		case -ENOENT:
			break;

		default:
			brelse(bh);
			return error;
		}

		error = get_next_leaf(dip, bh, &bh_next);
	}
	while (!error);

	brelse(bh);

	return error;
}

/**
 * dir_make_exhash - Convert a stuffed directory into an ExHash directory
 * @dip: The GFS2 inode
 *
 * Returns: 0 on success, error code otherwise
 */

static int dir_make_exhash(struct gfs2_inode *dip)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_dirent *dent;
	struct buffer_head *bh, *dibh;
	struct gfs2_leaf *leaf;
	int y;
	uint32_t x;
	uint64_t *lp, bn;
	int error;

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		return error;

	/*  Allocate a new block for the first leaf node  */

	bn = gfs2_alloc_meta(dip);

	/*  Turn over a new leaf  */

	bh = gfs2_meta_new(dip->i_gl, bn);
	gfs2_trans_add_bh(dip->i_gl, bh);
	gfs2_metatype_set(bh, GFS2_METATYPE_LF, GFS2_FORMAT_LF);
	gfs2_buffer_clear_tail(bh, sizeof(struct gfs2_meta_header));

	/*  Fill in the leaf structure  */

	leaf = (struct gfs2_leaf *)bh->b_data;

	gfs2_assert(sdp, dip->i_di.di_entries < (1 << 16));

	leaf->lf_dirent_format = cpu_to_be32(GFS2_FORMAT_DE);
	leaf->lf_entries = cpu_to_be16(dip->i_di.di_entries);

	/*  Copy dirents  */

	gfs2_buffer_copy_tail(bh, sizeof(struct gfs2_leaf), dibh,
			     sizeof(struct gfs2_dinode));

	/*  Find last entry  */

	x = 0;
	dirent_first(dip, bh, &dent);

	do {
		if (!dent->de_inum.no_addr)
			continue;
		if (++x == dip->i_di.di_entries)
			break;
	}
	while (dirent_next(dip, bh, &dent) == 0);

	/*  Adjust the last dirent's record length
	   (Remember that dent still points to the last entry.)  */

	dent->de_rec_len = be32_to_cpu(dent->de_rec_len) +
		sizeof(struct gfs2_dinode) -
		sizeof(struct gfs2_leaf);
	dent->de_rec_len = cpu_to_be32(dent->de_rec_len);

	brelse(bh);

	/*  We're done with the new leaf block, now setup the new
	    hash table.  */

	gfs2_trans_add_bh(dip->i_gl, dibh);
	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));

	lp = (uint64_t *)(dibh->b_data + sizeof(struct gfs2_dinode));

	for (x = sdp->sd_hash_ptrs; x--; lp++)
		*lp = cpu_to_be64(bn);

	dip->i_di.di_size = sdp->sd_sb.sb_bsize / 2;
	dip->i_di.di_blocks++;
	dip->i_di.di_flags |= GFS2_DIF_EXHASH;
	dip->i_di.di_payload_format = 0;

	for (x = sdp->sd_hash_ptrs, y = -1; x; x >>= 1, y++) ;
	dip->i_di.di_depth = y;

	gfs2_dinode_out(&dip->i_di, dibh->b_data);

	brelse(dibh);

	return 0;
}

/**
 * dir_split_leaf - Split a leaf block into two
 * @dip: The GFS2 inode
 * @index:
 * @leaf_no:
 *
 * Returns: 0 on success, error code on failure
 */

static int dir_split_leaf(struct gfs2_inode *dip, uint32_t index,
			  uint64_t leaf_no)
{
	struct buffer_head *nbh, *obh, *dibh;
	struct gfs2_leaf *nleaf, *oleaf;
	struct gfs2_dirent *dent, *prev = NULL, *next = NULL, *new;
	uint32_t start, len, half_len, divider;
	uint64_t bn, *lp;
	uint32_t name_len;
	int x, moved = 0;
	int error;

	/*  Allocate the new leaf block  */

	bn = gfs2_alloc_meta(dip);

	/*  Get the new leaf block  */

	nbh = gfs2_meta_new(dip->i_gl, bn);
	gfs2_trans_add_bh(dip->i_gl, nbh);
	gfs2_metatype_set(nbh, GFS2_METATYPE_LF, GFS2_FORMAT_LF);
	gfs2_buffer_clear_tail(nbh, sizeof(struct gfs2_meta_header));

	nleaf = (struct gfs2_leaf *)nbh->b_data;

	nleaf->lf_dirent_format = cpu_to_be32(GFS2_FORMAT_DE);

	/*  Get the old leaf block  */

	error = get_leaf(dip, leaf_no, &obh);
	if (error)
		goto fail;

	gfs2_trans_add_bh(dip->i_gl, obh);

	oleaf = (struct gfs2_leaf *)obh->b_data;

	/*  Compute the start and len of leaf pointers in the hash table.  */

	len = 1 << (dip->i_di.di_depth - be16_to_cpu(oleaf->lf_depth));
	half_len = len >> 1;
	if (!half_len) {
		gfs2_consist_inode(dip);
		error = -EIO;
		goto fail_brelse;
	}

	start = (index & ~(len - 1));

	/* Change the pointers.
	   Don't bother distinguishing stuffed from non-stuffed.
	   This code is complicated enough already. */

	lp = kcalloc(half_len, sizeof(uint64_t), GFP_KERNEL | __GFP_NOFAIL);

	error = gfs2_jdata_read_mem(dip, (char *)lp, start * sizeof(uint64_t),
				    half_len * sizeof(uint64_t));
	if (error != half_len * sizeof(uint64_t)) {
		if (error >= 0)
			error = -EIO;
		goto fail_lpfree;
	}

	/*  Change the pointers  */

	for (x = 0; x < half_len; x++)
		lp[x] = cpu_to_be64(bn);

	error = gfs2_jdata_write_mem(dip, (char *)lp, start * sizeof(uint64_t),
				     half_len * sizeof(uint64_t));
	if (error != half_len * sizeof(uint64_t)) {
		if (error >= 0)
			error = -EIO;
		goto fail_lpfree;
	}

	kfree(lp);

	/*  Compute the divider  */

	divider = (start + half_len) << (32 - dip->i_di.di_depth);

	/*  Copy the entries  */

	dirent_first(dip, obh, &dent);

	do {
		next = dent;
		if (dirent_next(dip, obh, &next))
			next = NULL;

		if (dent->de_inum.no_addr &&
		    be32_to_cpu(dent->de_hash) < divider) {
			name_len = dent->de_name_len;

			gfs2_dirent_alloc(dip, nbh, name_len, &new);

			new->de_inum = dent->de_inum; /* No endian worries */
			new->de_hash = dent->de_hash; /* No endian worries */
			new->de_type = dent->de_type; /* No endian worries */
			memcpy((char *)(new + 1), (char *)(dent + 1),
			       name_len);

			nleaf->lf_entries = be16_to_cpu(nleaf->lf_entries)+1;
			nleaf->lf_entries = cpu_to_be16(nleaf->lf_entries);

			dirent_del(dip, obh, prev, dent);

			if (!oleaf->lf_entries)
				gfs2_consist_inode(dip);
			oleaf->lf_entries = be16_to_cpu(oleaf->lf_entries)-1;
			oleaf->lf_entries = cpu_to_be16(oleaf->lf_entries);

			if (!prev)
				prev = dent;

			moved = 1;
		} else
			prev = dent;

		dent = next;
	}
	while (dent);

	/* If none of the entries got moved into the new leaf,
	   artificially fill in the first entry. */

	if (!moved) {
		gfs2_dirent_alloc(dip, nbh, 0, &new);
		new->de_inum.no_addr = 0;
	}

	oleaf->lf_depth = be16_to_cpu(oleaf->lf_depth) + 1;
	oleaf->lf_depth = cpu_to_be16(oleaf->lf_depth);
	nleaf->lf_depth = oleaf->lf_depth;

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (!gfs2_assert_withdraw(dip->i_sbd, !error)) {
		dip->i_di.di_blocks++;
		gfs2_dinode_out(&dip->i_di, dibh->b_data);
		brelse(dibh);
	}

	brelse(obh);
	brelse(nbh);

	return error;

 fail_lpfree:
	kfree(lp);

 fail_brelse:
	brelse(obh);

 fail:
	brelse(nbh);
	return error;
}

/**
 * dir_double_exhash - Double size of ExHash table
 * @dip: The GFS2 dinode
 *
 * Returns: 0 on success, error code on failure
 */

static int dir_double_exhash(struct gfs2_inode *dip)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct buffer_head *dibh;
	uint32_t hsize;
	uint64_t *buf;
	uint64_t *from, *to;
	uint64_t block;
	int x;
	int error = 0;

	hsize = 1 << dip->i_di.di_depth;
	if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	/*  Allocate both the "from" and "to" buffers in one big chunk  */

	buf = kcalloc(3, sdp->sd_hash_bsize, GFP_KERNEL | __GFP_NOFAIL);

	for (block = dip->i_di.di_size >> sdp->sd_hash_bsize_shift; block--;) {
		error = gfs2_jdata_read_mem(dip, (char *)buf,
					    block * sdp->sd_hash_bsize,
					    sdp->sd_hash_bsize);
		if (error != sdp->sd_hash_bsize) {
			if (error >= 0)
				error = -EIO;
			goto fail;
		}

		from = buf;
		to = (uint64_t *)((char *)buf + sdp->sd_hash_bsize);

		for (x = sdp->sd_hash_ptrs; x--; from++) {
			*to++ = *from;	/*  No endianess worries  */
			*to++ = *from;
		}

		error = gfs2_jdata_write_mem(dip,
					     (char *)buf + sdp->sd_hash_bsize,
					     block * sdp->sd_sb.sb_bsize,
					     sdp->sd_sb.sb_bsize);
		if (error != sdp->sd_sb.sb_bsize) {
			if (error >= 0)
				error = -EIO;
			goto fail;
		}
	}

	kfree(buf);

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (!gfs2_assert_withdraw(sdp, !error)) {
		dip->i_di.di_depth++;
		gfs2_dinode_out(&dip->i_di, dibh->b_data);
		brelse(dibh);
	}

	return error;

 fail:
	kfree(buf);

	return error;
}

/**
 * compare_dents - compare directory entries by hash value
 * @a: first dent
 * @b: second dent
 *
 * When comparing the hash entries of @a to @b:
 *   gt: returns 1
 *   lt: returns -1
 *   eq: returns 0
 */

static int compare_dents(const void *a, const void *b)
{
	struct gfs2_dirent *dent_a, *dent_b;
	uint32_t hash_a, hash_b;
	int ret = 0;

	dent_a = *(struct gfs2_dirent **)a;
	hash_a = dent_a->de_hash;
	hash_a = be32_to_cpu(hash_a);

	dent_b = *(struct gfs2_dirent **)b;
	hash_b = dent_b->de_hash;
	hash_b = be32_to_cpu(hash_b);

	if (hash_a > hash_b)
		ret = 1;
	else if (hash_a < hash_b)
		ret = -1;
	else {
		unsigned int len_a = dent_a->de_name_len;
		unsigned int len_b = dent_b->de_name_len;

		if (len_a > len_b)
			ret = 1;
		else if (len_a < len_b)
			ret = -1;
		else
			ret = memcmp((char *)(dent_a + 1),
				     (char *)(dent_b + 1),
				     len_a);
	}

	return ret;
}

/**
 * do_filldir_main - read out directory entries
 * @dip: The GFS2 inode
 * @offset: The offset in the file to read from
 * @opaque: opaque data to pass to filldir
 * @filldir: The function to pass entries to
 * @darr: an array of struct gfs2_dirent pointers to read
 * @entries: the number of entries in darr
 * @copied: pointer to int that's non-zero if a entry has been copied out
 *
 * Jump through some hoops to make sure that if there are hash collsions,
 * they are read out at the beginning of a buffer.  We want to minimize
 * the possibility that they will fall into different readdir buffers or
 * that someone will want to seek to that location.
 *
 * Returns: errno, >0 on exception from filldir
 */

static int do_filldir_main(struct gfs2_inode *dip, uint64_t *offset,
			   void *opaque, gfs2_filldir_t filldir,
			   struct gfs2_dirent **darr, uint32_t entries,
			   int *copied)
{
	struct gfs2_dirent *dent, *dent_next;
	struct gfs2_inum inum;
	uint64_t off, off_next;
	unsigned int x, y;
	int run = 0;
	int error = 0;

	sort(darr, entries, sizeof(struct gfs2_dirent *), compare_dents, NULL);

	dent_next = darr[0];
	off_next = be32_to_cpu(dent_next->de_hash);
	off_next = gfs2_disk_hash2offset(off_next);

	for (x = 0, y = 1; x < entries; x++, y++) {
		dent = dent_next;
		off = off_next;

		if (y < entries) {
			dent_next = darr[y];
			off_next = be32_to_cpu(dent_next->de_hash);
			off_next = gfs2_disk_hash2offset(off_next);

			if (off < *offset)
				continue;
			*offset = off;

			if (off_next == off) {
				if (*copied && !run)
					return 1;
				run = 1;
			} else
				run = 0;
		} else {
			if (off < *offset)
				continue;
			*offset = off;
		}

		gfs2_inum_in(&inum, (char *)&dent->de_inum);

		error = filldir(opaque, (char *)(dent + 1),
				dent->de_name_len,
				off, &inum,
				dent->de_type);
		if (error)
			return 1;

		*copied = 1;
	}

	/* Increment the *offset by one, so the next time we come into the
	   do_filldir fxn, we get the next entry instead of the last one in the
	   current leaf */

	(*offset)++;

	return 0;
}

/**
 * do_filldir_single - Read directory entries out of a single block
 * @dip: The GFS2 inode
 * @offset: The offset in the file to read from
 * @opaque: opaque data to pass to filldir
 * @filldir: The function to pass entries to
 * @bh: the block
 * @entries: the number of entries in the block
 * @copied: pointer to int that's non-zero if a entry has been copied out
 *
 * Returns: errno, >0 on exception from filldir
 */

static int do_filldir_single(struct gfs2_inode *dip, uint64_t *offset,
			     void *opaque, gfs2_filldir_t filldir,
			     struct buffer_head *bh, uint32_t entries,
			     int *copied)
{
	struct gfs2_dirent **darr;
	struct gfs2_dirent *de;
	unsigned int e = 0;
	int error;

	if (!entries)
		return 0;

	darr = kcalloc(entries, sizeof(struct gfs2_dirent *), GFP_KERNEL);
	if (!darr)
		return -ENOMEM;

	dirent_first(dip, bh, &de);
	do {
		if (!de->de_inum.no_addr)
			continue;
		if (e >= entries) {
			gfs2_consist_inode(dip);
			error = -EIO;
			goto out;
		}
		darr[e++] = de;
	}
	while (dirent_next(dip, bh, &de) == 0);

	if (e != entries) {
		gfs2_consist_inode(dip);
		error = -EIO;
		goto out;
	}

	error = do_filldir_main(dip, offset, opaque, filldir, darr,
				entries, copied);

 out:
	kfree(darr);

	return error;
}

/**
 * do_filldir_multi - Read directory entries out of a linked leaf list
 * @dip: The GFS2 inode
 * @offset: The offset in the file to read from
 * @opaque: opaque data to pass to filldir
 * @filldir: The function to pass entries to
 * @bh: the first leaf in the list
 * @copied: pointer to int that's non-zero if a entry has been copied out
 *
 * Returns: errno, >0 on exception from filldir
 */

static int do_filldir_multi(struct gfs2_inode *dip, uint64_t *offset,
			    void *opaque, gfs2_filldir_t filldir,
			    struct buffer_head *bh, int *copied)
{
	struct buffer_head **larr = NULL;
	struct gfs2_dirent **darr;
	struct gfs2_leaf *leaf;
	struct buffer_head *tmp_bh;
	struct gfs2_dirent *de;
	unsigned int entries, e = 0;
	unsigned int leaves = 0, l = 0;
	unsigned int x;
	uint64_t ln;
	int error = 0;

	/*  Count leaves and entries  */

	leaf = (struct gfs2_leaf *)bh->b_data;
	entries = be16_to_cpu(leaf->lf_entries);
	ln = leaf->lf_next;

	while (ln) {
		ln = be64_to_cpu(ln);

		error = get_leaf(dip, ln, &tmp_bh);
		if (error)
			return error;

		leaf = (struct gfs2_leaf *)tmp_bh->b_data;
		if (leaf->lf_entries) {
			entries += be16_to_cpu(leaf->lf_entries);
			leaves++;
		}
		ln = leaf->lf_next;

		brelse(tmp_bh);
	}

	if (!entries)
		return 0;

	if (leaves) {
		larr = kcalloc(leaves, sizeof(struct buffer_head *),GFP_KERNEL);
		if (!larr)
			return -ENOMEM;
	}

	darr = kcalloc(entries, sizeof(struct gfs2_dirent *), GFP_KERNEL);
	if (!darr) {
		kfree(larr);
		return -ENOMEM;
	}

	leaf = (struct gfs2_leaf *)bh->b_data;
	if (leaf->lf_entries) {
		dirent_first(dip, bh, &de);
		do {
			if (!de->de_inum.no_addr)
				continue;
			if (e >= entries) {
				gfs2_consist_inode(dip);
				error = -EIO;
				goto out;
			}
			darr[e++] = de;
		}
		while (dirent_next(dip, bh, &de) == 0);
	}
	ln = leaf->lf_next;

	while (ln) {
		ln = be64_to_cpu(ln);

		error = get_leaf(dip, ln, &tmp_bh);
		if (error)
			goto out;

		leaf = (struct gfs2_leaf *)tmp_bh->b_data;
		if (leaf->lf_entries) {
			dirent_first(dip, tmp_bh, &de);
			do {
				if (!de->de_inum.no_addr)
					continue;
				if (e >= entries) {
					gfs2_consist_inode(dip);
					error = -EIO;
					goto out;
				}
				darr[e++] = de;
			}
			while (dirent_next(dip, tmp_bh, &de) == 0);

			larr[l++] = tmp_bh;

			ln = leaf->lf_next;
		} else {
			ln = leaf->lf_next;
			brelse(tmp_bh);
		}
	}

	if (gfs2_assert_withdraw(dip->i_sbd, l == leaves)) {
		error = -EIO;
		goto out;
	}
	if (e != entries) {
		gfs2_consist_inode(dip);
		error = -EIO;
		goto out;
	}

	error = do_filldir_main(dip, offset, opaque, filldir, darr,
				entries, copied);

 out:
	kfree(darr);
	for (x = 0; x < l; x++)
		brelse(larr[x]);
	kfree(larr);

	return error;
}

/**
 * dir_e_search - Search exhash (leaf) dir for inode matching name
 * @dip: The GFS2 inode
 * @filename: Filename string
 * @inode: If non-NULL, function fills with formal inode # and block address
 * @type: If non-NULL, function fills with DT_... dinode type
 *
 * Returns:
 */

static int dir_e_search(struct gfs2_inode *dip, struct qstr *filename,
			struct gfs2_inum *inum, unsigned int *type)
{
	struct buffer_head *bh;
	struct gfs2_dirent *dent;
	int error;

	error = linked_leaf_search(dip, filename, &dent, NULL, &bh);
	if (error)
		return error;

	if (inum)
		gfs2_inum_in(inum, (char *)&dent->de_inum);
	if (type)
		*type = dent->de_type;

	brelse(bh);

	return 0;
}

static int dir_e_add(struct gfs2_inode *dip, struct qstr *filename,
		     struct gfs2_inum *inum, unsigned int type)
{
	struct buffer_head *bh, *nbh, *dibh;
	struct gfs2_leaf *leaf, *nleaf;
	struct gfs2_dirent *dent;
	uint32_t hsize, index;
	uint32_t hash;
	uint64_t leaf_no, bn;
	int error;

 restart:
	hsize = 1 << dip->i_di.di_depth;
	if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	/*  Figure out the address of the leaf node.  */

	hash = gfs2_disk_hash(filename->name, filename->len);
	index = hash >> (32 - dip->i_di.di_depth);

	error = get_leaf_nr(dip, index, &leaf_no);
	if (error)
		return error;

	/*  Add entry to the leaf  */

	for (;;) {
		error = get_leaf(dip, leaf_no, &bh);
		if (error)
			return error;

		leaf = (struct gfs2_leaf *)bh->b_data;

		if (gfs2_dirent_alloc(dip, bh, filename->len, &dent)) {

			if (be16_to_cpu(leaf->lf_depth) < dip->i_di.di_depth) {
				/* Can we split the leaf? */

				brelse(bh);

				error = dir_split_leaf(dip, index, leaf_no);
				if (error)
					return error;

				goto restart;

			} else if (dip->i_di.di_depth < GFS2_DIR_MAX_DEPTH) {
				/* Can we double the hash table? */

				brelse(bh);

				error = dir_double_exhash(dip);
				if (error)
					return error;

				goto restart;

			} else if (leaf->lf_next) {
				/* Can we try the next leaf in the list? */
				leaf_no = be64_to_cpu(leaf->lf_next);
				brelse(bh);
				continue;

			} else {
				/* Create a new leaf and add it to the list. */

				bn = gfs2_alloc_meta(dip);

				nbh = gfs2_meta_new(dip->i_gl, bn);
				gfs2_trans_add_bh(dip->i_gl, nbh);
				gfs2_metatype_set(nbh,
						 GFS2_METATYPE_LF,
						 GFS2_FORMAT_LF);
				gfs2_buffer_clear_tail(nbh,
					sizeof(struct gfs2_meta_header));

				gfs2_trans_add_bh(dip->i_gl, bh);
				leaf->lf_next = cpu_to_be64(bn);

				nleaf = (struct gfs2_leaf *)nbh->b_data;
				nleaf->lf_depth = leaf->lf_depth;
				nleaf->lf_dirent_format = cpu_to_be32(GFS2_FORMAT_DE);

				gfs2_dirent_alloc(dip, nbh, filename->len,
						  &dent);

				dip->i_di.di_blocks++;

				brelse(bh);

				bh = nbh;
				leaf = nleaf;
			}
		}

		/* If the gfs2_dirent_alloc() succeeded, it pinned the "bh" */

		gfs2_inum_out(inum, (char *)&dent->de_inum);
		dent->de_hash = cpu_to_be32(hash);
		dent->de_type = type;
		memcpy((char *)(dent + 1), filename->name, filename->len);

		leaf->lf_entries = be16_to_cpu(leaf->lf_entries) + 1;
		leaf->lf_entries = cpu_to_be16(leaf->lf_entries);

		brelse(bh);

		error = gfs2_meta_inode_buffer(dip, &dibh);
		if (error)
			return error;

		dip->i_di.di_entries++;
		dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

		gfs2_trans_add_bh(dip->i_gl, dibh);
		gfs2_dinode_out(&dip->i_di, dibh->b_data);
		brelse(dibh);

		return 0;
	}

	return -ENOENT;
}

static int dir_e_del(struct gfs2_inode *dip, struct qstr *filename)
{
	struct buffer_head *bh, *dibh;
	struct gfs2_dirent *dent, *prev;
	struct gfs2_leaf *leaf;
	unsigned int entries;
	int error;

	error = linked_leaf_search(dip, filename, &dent, &prev, &bh);
	if (error == -ENOENT) {
		gfs2_consist_inode(dip);
		return -EIO;
	}
	if (error)
		return error;

	dirent_del(dip, bh, prev, dent); /* Pins bh */

	leaf = (struct gfs2_leaf *)bh->b_data;
	entries = be16_to_cpu(leaf->lf_entries);
	if (!entries)
		gfs2_consist_inode(dip);
	entries--;
	leaf->lf_entries = cpu_to_be16(entries);

	brelse(bh);

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		return error;

	if (!dip->i_di.di_entries)
		gfs2_consist_inode(dip);
	dip->i_di.di_entries--;
	dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

	gfs2_trans_add_bh(dip->i_gl, dibh);
	gfs2_dinode_out(&dip->i_di, dibh->b_data);
	brelse(dibh);

	return 0;
}

/**
 * dir_e_read - Reads the entries from a directory into a filldir buffer
 * @dip: dinode pointer
 * @offset: the hash of the last entry read shifted to the right once
 * @opaque: buffer for the filldir function to fill
 * @filldir: points to the filldir function to use
 *
 * Returns: errno
 */

static int dir_e_read(struct gfs2_inode *dip, uint64_t *offset, void *opaque,
		      gfs2_filldir_t filldir)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct buffer_head *bh;
	struct gfs2_leaf leaf;
	uint32_t hsize, len;
	uint32_t ht_offset, lp_offset, ht_offset_cur = -1;
	uint32_t hash, index;
	uint64_t *lp;
	int copied = 0;
	int error = 0;

	hsize = 1 << dip->i_di.di_depth;
	if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	hash = gfs2_dir_offset2hash(*offset);
	index = hash >> (32 - dip->i_di.di_depth);

	lp = kmalloc(sdp->sd_hash_bsize, GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	while (index < hsize) {
		lp_offset = index & (sdp->sd_hash_ptrs - 1);
		ht_offset = index - lp_offset;

		if (ht_offset_cur != ht_offset) {
			error = gfs2_jdata_read_mem(dip, (char *)lp,
						ht_offset * sizeof(uint64_t),
						sdp->sd_hash_bsize);
			if (error != sdp->sd_hash_bsize) {
				if (error >= 0)
					error = -EIO;
				goto out;
			}
			ht_offset_cur = ht_offset;
		}

		error = get_leaf(dip, be64_to_cpu(lp[lp_offset]), &bh);
		if (error)
			goto out;

		gfs2_leaf_in(&leaf, bh->b_data);

		if (leaf.lf_next)
			error = do_filldir_multi(dip, offset, opaque, filldir,
						 bh, &copied);
		else
			error = do_filldir_single(dip, offset, opaque, filldir,
						  bh, leaf.lf_entries, &copied);

		brelse(bh);

		if (error) {
			if (error > 0)
				error = 0;
			goto out;
		}

		len = 1 << (dip->i_di.di_depth - leaf.lf_depth);
		index = (index & ~(len - 1)) + len;
	}

 out:
	kfree(lp);

	return error;
}

static int dir_e_mvino(struct gfs2_inode *dip, struct qstr *filename,
		       struct gfs2_inum *inum, unsigned int new_type)
{
	struct buffer_head *bh, *dibh;
	struct gfs2_dirent *dent;
	int error;

	error = linked_leaf_search(dip, filename, &dent, NULL, &bh);
	if (error == -ENOENT) {
		gfs2_consist_inode(dip);
		return -EIO;
	}
	if (error)
		return error;

	gfs2_trans_add_bh(dip->i_gl, bh);

	gfs2_inum_out(inum, (char *)&dent->de_inum);
	dent->de_type = new_type;

	brelse(bh);

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		return error;

	dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

	gfs2_trans_add_bh(dip->i_gl, dibh);
	gfs2_dinode_out(&dip->i_di, dibh->b_data);
	brelse(dibh);

	return 0;
}

/**
 * dir_l_search - Search linear (stuffed dinode) dir for inode matching name
 * @dip: The GFS2 inode
 * @filename: Filename string
 * @inode: If non-NULL, function fills with formal inode # and block address
 * @type: If non-NULL, function fills with DT_... dinode type
 *
 * Returns:
 */

static int dir_l_search(struct gfs2_inode *dip, struct qstr *filename,
			struct gfs2_inum *inum, unsigned int *type)
{
	struct buffer_head *dibh;
	struct gfs2_dirent *dent;
	int error;

	if (!gfs2_is_stuffed(dip)) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		return error;

	error = leaf_search(dip, dibh, filename, &dent, NULL);
	if (!error) {
		if (inum)
			gfs2_inum_in(inum, (char *)&dent->de_inum);
		if (type)
			*type = dent->de_type;
	}

	brelse(dibh);

	return error;
}

static int dir_l_add(struct gfs2_inode *dip, struct qstr *filename,
		     struct gfs2_inum *inum, unsigned int type)
{
	struct buffer_head *dibh;
	struct gfs2_dirent *dent;
	int error;

	if (!gfs2_is_stuffed(dip)) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		return error;

	if (gfs2_dirent_alloc(dip, dibh, filename->len, &dent)) {
		brelse(dibh);

		error = dir_make_exhash(dip);
		if (!error)
			error = dir_e_add(dip, filename, inum, type);

		return error;
	}

	/*  gfs2_dirent_alloc() pins  */

	gfs2_inum_out(inum, (char *)&dent->de_inum);
	dent->de_hash = gfs2_disk_hash(filename->name, filename->len);
	dent->de_hash = cpu_to_be32(dent->de_hash);
	dent->de_type = type;
	memcpy((char *)(dent + 1), filename->name, filename->len);

	dip->i_di.di_entries++;
	dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

	gfs2_dinode_out(&dip->i_di, dibh->b_data);
	brelse(dibh);

	return 0;
}

static int dir_l_del(struct gfs2_inode *dip, struct qstr *filename)
{
	struct buffer_head *dibh;
	struct gfs2_dirent *dent, *prev;
	int error;

	if (!gfs2_is_stuffed(dip)) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		return error;

	error = leaf_search(dip, dibh, filename, &dent, &prev);
	if (error == -ENOENT) {
		gfs2_consist_inode(dip);
		error = -EIO;
		goto out;
	}
	if (error)
		goto out;

	dirent_del(dip, dibh, prev, dent);

	/*  dirent_del() pins  */

	if (!dip->i_di.di_entries)
		gfs2_consist_inode(dip);
	dip->i_di.di_entries--;

	dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

	gfs2_dinode_out(&dip->i_di, dibh->b_data);

 out:
	brelse(dibh);

	return error;
}

static int dir_l_read(struct gfs2_inode *dip, uint64_t *offset, void *opaque,
		      gfs2_filldir_t filldir)
{
	struct buffer_head *dibh;
	int copied = 0;
	int error;

	if (!gfs2_is_stuffed(dip)) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	if (!dip->i_di.di_entries)
		return 0;

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		return error;

	error = do_filldir_single(dip, offset,
				  opaque, filldir,
				  dibh, dip->i_di.di_entries,
				  &copied);
	if (error > 0)
		error = 0;

	brelse(dibh);

	return error;
}

static int dir_l_mvino(struct gfs2_inode *dip, struct qstr *filename,
		       struct gfs2_inum *inum, unsigned int new_type)
{
	struct buffer_head *dibh;
	struct gfs2_dirent *dent;
	int error;

	if (!gfs2_is_stuffed(dip)) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		return error;

	error = leaf_search(dip, dibh, filename, &dent, NULL);
	if (error == -ENOENT) {
		gfs2_consist_inode(dip);
		error = -EIO;
		goto out;
	}
	if (error)
		goto out;

	gfs2_trans_add_bh(dip->i_gl, dibh);

	gfs2_inum_out(inum, (char *)&dent->de_inum);
	dent->de_type = new_type;

	dip->i_di.di_mtime = dip->i_di.di_ctime = get_seconds();

	gfs2_dinode_out(&dip->i_di, dibh->b_data);

 out:
	brelse(dibh);

	return error;
}

/**
 * gfs2_dir_search - Search a directory
 * @dip: The GFS2 inode
 * @filename:
 * @inode:
 *
 * This routine searches a directory for a file or another directory.
 * Assumes a glock is held on dip.
 *
 * Returns: errno
 */

int gfs2_dir_search(struct gfs2_inode *dip, struct qstr *filename,
		    struct gfs2_inum *inum, unsigned int *type)
{
	int error;

	if (dip->i_di.di_flags & GFS2_DIF_EXHASH)
		error = dir_e_search(dip, filename, inum, type);
	else
		error = dir_l_search(dip, filename, inum, type);

	return error;
}

/**
 * gfs2_dir_add - Add new filename into directory
 * @dip: The GFS2 inode
 * @filename: The new name
 * @inode: The inode number of the entry
 * @type: The type of the entry
 *
 * Returns: 0 on success, error code on failure
 */

int gfs2_dir_add(struct gfs2_inode *dip, struct qstr *filename,
		 struct gfs2_inum *inum, unsigned int type)
{
	int error;

	if (dip->i_di.di_flags & GFS2_DIF_EXHASH)
		error = dir_e_add(dip, filename, inum, type);
	else
		error = dir_l_add(dip, filename, inum, type);

	return error;
}

/**
 * gfs2_dir_del - Delete a directory entry
 * @dip: The GFS2 inode
 * @filename: The filename
 *
 * Returns: 0 on success, error code on failure
 */

int gfs2_dir_del(struct gfs2_inode *dip, struct qstr *filename)
{
	int error;

	if (dip->i_di.di_flags & GFS2_DIF_EXHASH)
		error = dir_e_del(dip, filename);
	else
		error = dir_l_del(dip, filename);

	return error;
}

int gfs2_dir_read(struct gfs2_inode *dip, uint64_t *offset, void *opaque,
		  gfs2_filldir_t filldir)
{
	int error;

	if (dip->i_di.di_flags & GFS2_DIF_EXHASH)
		error = dir_e_read(dip, offset, opaque, filldir);
	else
		error = dir_l_read(dip, offset, opaque, filldir);

	return error;
}

/**
 * gfs2_dir_mvino - Change inode number of directory entry
 * @dip: The GFS2 inode
 * @filename:
 * @new_inode:
 *
 * This routine changes the inode number of a directory entry.  It's used
 * by rename to change ".." when a directory is moved.
 * Assumes a glock is held on dvp.
 *
 * Returns: errno
 */

int gfs2_dir_mvino(struct gfs2_inode *dip, struct qstr *filename,
		   struct gfs2_inum *inum, unsigned int new_type)
{
	int error;

	if (dip->i_di.di_flags & GFS2_DIF_EXHASH)
		error = dir_e_mvino(dip, filename, inum, new_type);
	else
		error = dir_l_mvino(dip, filename, inum, new_type);

	return error;
}

/**
 * foreach_leaf - call a function for each leaf in a directory
 * @dip: the directory
 * @lc: the function to call for each each
 * @data: private data to pass to it
 *
 * Returns: errno
 */

static int foreach_leaf(struct gfs2_inode *dip, leaf_call_t lc, void *data)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct buffer_head *bh;
	struct gfs2_leaf leaf;
	uint32_t hsize, len;
	uint32_t ht_offset, lp_offset, ht_offset_cur = -1;
	uint32_t index = 0;
	uint64_t *lp;
	uint64_t leaf_no;
	int error = 0;

	hsize = 1 << dip->i_di.di_depth;
	if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	lp = kmalloc(sdp->sd_hash_bsize, GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	while (index < hsize) {
		lp_offset = index & (sdp->sd_hash_ptrs - 1);
		ht_offset = index - lp_offset;

		if (ht_offset_cur != ht_offset) {
			error = gfs2_jdata_read_mem(dip, (char *)lp,
						ht_offset * sizeof(uint64_t),
						sdp->sd_hash_bsize);
			if (error != sdp->sd_hash_bsize) {
				if (error >= 0)
					error = -EIO;
				goto out;
			}
			ht_offset_cur = ht_offset;
		}

		leaf_no = be64_to_cpu(lp[lp_offset]);
		if (leaf_no) {
			error = get_leaf(dip, leaf_no, &bh);
			if (error)
				goto out;
			gfs2_leaf_in(&leaf, bh->b_data);
			brelse(bh);

			len = 1 << (dip->i_di.di_depth - leaf.lf_depth);

			error = lc(dip, index, len, leaf_no, data);
			if (error)
				goto out;

			index = (index & ~(len - 1)) + len;
		} else
			index++;
	}

	if (index != hsize) {
		gfs2_consist_inode(dip);
		error = -EIO;
	}

 out:
	kfree(lp);

	return error;
}

/**
 * leaf_dealloc - Deallocate a directory leaf
 * @dip: the directory
 * @index: the hash table offset in the directory
 * @len: the number of pointers to this leaf
 * @leaf_no: the leaf number
 * @data: not used
 *
 * Returns: errno
 */

static int leaf_dealloc(struct gfs2_inode *dip, uint32_t index, uint32_t len,
			uint64_t leaf_no, void *data)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_leaf tmp_leaf;
	struct gfs2_rgrp_list rlist;
	struct buffer_head *bh, *dibh;
	uint64_t blk;
	unsigned int rg_blocks = 0, l_blocks = 0;
	char *ht;
	unsigned int x, size = len * sizeof(uint64_t);
	int error;

	memset(&rlist, 0, sizeof(struct gfs2_rgrp_list));

	ht = kzalloc(size, GFP_KERNEL);
	if (!ht)
		return -ENOMEM;

	gfs2_alloc_get(dip);

	error = gfs2_quota_hold(dip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out;

	error = gfs2_rindex_hold(sdp, &dip->i_alloc.al_ri_gh);
	if (error)
		goto out_qs;

	/*  Count the number of leaves  */

	for (blk = leaf_no; blk; blk = tmp_leaf.lf_next) {
		error = get_leaf(dip, blk, &bh);
		if (error)
			goto out_rlist;
		gfs2_leaf_in(&tmp_leaf, (bh)->b_data);
		brelse(bh);

		gfs2_rlist_add(sdp, &rlist, blk);
		l_blocks++;
	}

	gfs2_rlist_alloc(&rlist, LM_ST_EXCLUSIVE, 0);

	for (x = 0; x < rlist.rl_rgrps; x++) {
		struct gfs2_rgrpd *rgd;
		rgd = get_gl2rgd(rlist.rl_ghs[x].gh_gl);
		rg_blocks += rgd->rd_ri.ri_length;
	}

	error = gfs2_glock_nq_m(rlist.rl_rgrps, rlist.rl_ghs);
	if (error)
		goto out_rlist;

	error = gfs2_trans_begin(sdp,
			rg_blocks + (DIV_RU(size, sdp->sd_jbsize) + 1) +
			RES_DINODE + RES_STATFS + RES_QUOTA, l_blocks);
	if (error)
		goto out_rg_gunlock;

	for (blk = leaf_no; blk; blk = tmp_leaf.lf_next) {
		error = get_leaf(dip, blk, &bh);
		if (error)
			goto out_end_trans;
		gfs2_leaf_in(&tmp_leaf, bh->b_data);
		brelse(bh);

		gfs2_free_meta(dip, blk, 1);

		if (!dip->i_di.di_blocks)
			gfs2_consist_inode(dip);
		dip->i_di.di_blocks--;
	}

	error = gfs2_jdata_write_mem(dip, ht, index * sizeof(uint64_t), size);
	if (error != size) {
		if (error >= 0)
			error = -EIO;
		goto out_end_trans;
	}

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		goto out_end_trans;

	gfs2_trans_add_bh(dip->i_gl, dibh);
	gfs2_dinode_out(&dip->i_di, dibh->b_data);
	brelse(dibh);

 out_end_trans:
	gfs2_trans_end(sdp);

 out_rg_gunlock:
	gfs2_glock_dq_m(rlist.rl_rgrps, rlist.rl_ghs);

 out_rlist:
	gfs2_rlist_free(&rlist);
	gfs2_glock_dq_uninit(&dip->i_alloc.al_ri_gh);

 out_qs:
	gfs2_quota_unhold(dip);

 out:
	gfs2_alloc_put(dip);
	kfree(ht);

	return error;
}

/**
 * gfs2_dir_exhash_dealloc - free all the leaf blocks in a directory
 * @dip: the directory
 *
 * Dealloc all on-disk directory leaves to FREEMETA state
 * Change on-disk inode type to "regular file"
 *
 * Returns: errno
 */

int gfs2_dir_exhash_dealloc(struct gfs2_inode *dip)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct buffer_head *bh;
	int error;

	/* Dealloc on-disk leaves to FREEMETA state */
	error = foreach_leaf(dip, leaf_dealloc, NULL);
	if (error)
		return error;

	/* Make this a regular file in case we crash.
	   (We don't want to free these blocks a second time.)  */

	error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		return error;

	error = gfs2_meta_inode_buffer(dip, &bh);
	if (!error) {
		gfs2_trans_add_bh(dip->i_gl, bh);
		((struct gfs2_dinode *)bh->b_data)->di_mode = cpu_to_be32(S_IFREG);
		brelse(bh);
	}

	gfs2_trans_end(sdp);

	return error;
}

/**
 * gfs2_diradd_alloc_required - find if adding entry will require an allocation
 * @ip: the file being written to
 * @filname: the filename that's going to be added
 * @alloc_required: set to 1 if an alloc is required, 0 otherwise
 *
 * Returns: errno
 */

int gfs2_diradd_alloc_required(struct gfs2_inode *dip, struct qstr *filename,
			       int *alloc_required)
{
	struct buffer_head *bh = NULL, *bh_next;
	uint32_t hsize, hash, index;
	int error = 0;

	*alloc_required = 0;

	if (dip->i_di.di_flags & GFS2_DIF_EXHASH) {
		hsize = 1 << dip->i_di.di_depth;
		if (hsize * sizeof(uint64_t) != dip->i_di.di_size) {
			gfs2_consist_inode(dip);
			return -EIO;
		}

		hash = gfs2_disk_hash(filename->name, filename->len);
		index = hash >> (32 - dip->i_di.di_depth);

		error = get_first_leaf(dip, index, &bh_next);
		if (error)
			return error;

		do {
			brelse(bh);

			bh = bh_next;

			if (dirent_fits(dip, bh, filename->len))
				break;

			error = get_next_leaf(dip, bh, &bh_next);
			if (error == -ENOENT) {
				*alloc_required = 1;
				error = 0;
				break;
			}
		}
		while (!error);

		brelse(bh);
	} else {
		error = gfs2_meta_inode_buffer(dip, &bh);
		if (error)
			return error;

		if (!dirent_fits(dip, bh, filename->len))
			*alloc_required = 1;

		brelse(bh);
	}

	return error;
}

/**
 * do_gdm - copy out one leaf (or list of leaves)
 * @dip: the directory
 * @index: the hash table offset in the directory
 * @len: the number of pointers to this leaf
 * @leaf_no: the leaf number
 * @data: a pointer to a struct gfs2_user_buffer structure
 *
 * Returns: errno
 */

static int do_gdm(struct gfs2_inode *dip, uint32_t index, uint32_t len,
		  uint64_t leaf_no, void *data)
{
	struct gfs2_user_buffer *ub = (struct gfs2_user_buffer *)data;
	struct gfs2_leaf leaf;
	struct buffer_head *bh;
	uint64_t blk;
	int error = 0;

	for (blk = leaf_no; blk; blk = leaf.lf_next) {
		error = get_leaf(dip, blk, &bh);
		if (error)
			break;

		gfs2_leaf_in(&leaf, bh->b_data);

		error = gfs2_add_bh_to_ub(ub, bh);

		brelse(bh);

		if (error)
			break;
	}

	return error;
}

/**
 * gfs2_get_dir_meta - return all the leaf blocks of a directory
 * @dip: the directory
 * @ub: the structure representing the meta
 *
 * Returns: errno
 */

int gfs2_get_dir_meta(struct gfs2_inode *dip, struct gfs2_user_buffer *ub)
{
	return foreach_leaf(dip, do_gdm, ub);
}

