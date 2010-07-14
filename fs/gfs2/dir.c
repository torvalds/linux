/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
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
 * dip->i_diskflags & GFS2_DIF_EXHASH is true
 *
 * Otherwise, the dirents are "linear", within a single stuffed dinode block.
 *
 * When the dirents are in leaves, the actual contents of the directory file are
 * used as an array of 64-bit block pointers pointing to the leaf blocks. The
 * dirents are NOT in the directory file itself. There can be more than one
 * block pointer in the array that points to the same leaf. In fact, when a
 * directory is first converted from linear to exhash, all of the pointers
 * point to the same leaf.
 *
 * When a leaf is completely full, the size of the hash table can be
 * doubled unless it is already at the maximum size which is hard coded into
 * GFS2_DIR_MAX_DEPTH. After that, leaves are chained together in a linked list,
 * but never before the maximum hash table size has been reached.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/buffer_head.h>
#include <linux/sort.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/vmalloc.h>

#include "gfs2.h"
#include "incore.h"
#include "dir.h"
#include "glock.h"
#include "inode.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "bmap.h"
#include "util.h"

#define IS_LEAF     1 /* Hashed (leaf) directory */
#define IS_DINODE   2 /* Linear (stuffed dinode block) directory */

#define gfs2_disk_hash2offset(h) (((u64)(h)) >> 1)
#define gfs2_dir_offset2hash(p) ((u32)(((u64)(p)) << 1))

typedef int (*leaf_call_t) (struct gfs2_inode *dip, u32 index, u32 len,
			    u64 leaf_no, void *data);
typedef int (*gfs2_dscan_t)(const struct gfs2_dirent *dent,
			    const struct qstr *name, void *opaque);


int gfs2_dir_get_new_buffer(struct gfs2_inode *ip, u64 block,
			    struct buffer_head **bhp)
{
	struct buffer_head *bh;

	bh = gfs2_meta_new(ip->i_gl, block);
	gfs2_trans_add_bh(ip->i_gl, bh, 1);
	gfs2_metatype_set(bh, GFS2_METATYPE_JD, GFS2_FORMAT_JD);
	gfs2_buffer_clear_tail(bh, sizeof(struct gfs2_meta_header));
	*bhp = bh;
	return 0;
}

static int gfs2_dir_get_existing_buffer(struct gfs2_inode *ip, u64 block,
					struct buffer_head **bhp)
{
	struct buffer_head *bh;
	int error;

	error = gfs2_meta_read(ip->i_gl, block, DIO_WAIT, &bh);
	if (error)
		return error;
	if (gfs2_metatype_check(GFS2_SB(&ip->i_inode), bh, GFS2_METATYPE_JD)) {
		brelse(bh);
		return -EIO;
	}
	*bhp = bh;
	return 0;
}

static int gfs2_dir_write_stuffed(struct gfs2_inode *ip, const char *buf,
				  unsigned int offset, unsigned int size)
{
	struct buffer_head *dibh;
	int error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	memcpy(dibh->b_data + offset + sizeof(struct gfs2_dinode), buf, size);
	if (ip->i_disksize < offset + size)
		ip->i_disksize = offset + size;
	ip->i_inode.i_mtime = ip->i_inode.i_ctime = CURRENT_TIME;
	gfs2_dinode_out(ip, dibh->b_data);

	brelse(dibh);

	return size;
}



/**
 * gfs2_dir_write_data - Write directory information to the inode
 * @ip: The GFS2 inode
 * @buf: The buffer containing information to be written
 * @offset: The file offset to start writing at
 * @size: The amount of data to write
 *
 * Returns: The number of bytes correctly written or error code
 */
static int gfs2_dir_write_data(struct gfs2_inode *ip, const char *buf,
			       u64 offset, unsigned int size)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head *dibh;
	u64 lblock, dblock;
	u32 extlen = 0;
	unsigned int o;
	int copied = 0;
	int error = 0;
	int new = 0;

	if (!size)
		return 0;

	if (gfs2_is_stuffed(ip) &&
	    offset + size <= sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode))
		return gfs2_dir_write_stuffed(ip, buf, (unsigned int)offset,
					      size);

	if (gfs2_assert_warn(sdp, gfs2_is_jdata(ip)))
		return -EINVAL;

	if (gfs2_is_stuffed(ip)) {
		error = gfs2_unstuff_dinode(ip, NULL);
		if (error)
			return error;
	}

	lblock = offset;
	o = do_div(lblock, sdp->sd_jbsize) + sizeof(struct gfs2_meta_header);

	while (copied < size) {
		unsigned int amount;
		struct buffer_head *bh;

		amount = size - copied;
		if (amount > sdp->sd_sb.sb_bsize - o)
			amount = sdp->sd_sb.sb_bsize - o;

		if (!extlen) {
			new = 1;
			error = gfs2_extent_map(&ip->i_inode, lblock, &new,
						&dblock, &extlen);
			if (error)
				goto fail;
			error = -EIO;
			if (gfs2_assert_withdraw(sdp, dblock))
				goto fail;
		}

		if (amount == sdp->sd_jbsize || new)
			error = gfs2_dir_get_new_buffer(ip, dblock, &bh);
		else
			error = gfs2_dir_get_existing_buffer(ip, dblock, &bh);

		if (error)
			goto fail;

		gfs2_trans_add_bh(ip->i_gl, bh, 1);
		memcpy(bh->b_data + o, buf, amount);
		brelse(bh);

		buf += amount;
		copied += amount;
		lblock++;
		dblock++;
		extlen--;

		o = sizeof(struct gfs2_meta_header);
	}

out:
	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	if (ip->i_disksize < offset + copied)
		ip->i_disksize = offset + copied;
	ip->i_inode.i_mtime = ip->i_inode.i_ctime = CURRENT_TIME;

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);

	return copied;
fail:
	if (copied)
		goto out;
	return error;
}

static int gfs2_dir_read_stuffed(struct gfs2_inode *ip, char *buf,
				 u64 offset, unsigned int size)
{
	struct buffer_head *dibh;
	int error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		offset += sizeof(struct gfs2_dinode);
		memcpy(buf, dibh->b_data + offset, size);
		brelse(dibh);
	}

	return (error) ? error : size;
}


/**
 * gfs2_dir_read_data - Read a data from a directory inode
 * @ip: The GFS2 Inode
 * @buf: The buffer to place result into
 * @offset: File offset to begin jdata_readng from
 * @size: Amount of data to transfer
 *
 * Returns: The amount of data actually copied or the error
 */
static int gfs2_dir_read_data(struct gfs2_inode *ip, char *buf, u64 offset,
			      unsigned int size, unsigned ra)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	u64 lblock, dblock;
	u32 extlen = 0;
	unsigned int o;
	int copied = 0;
	int error = 0;

	if (offset >= ip->i_disksize)
		return 0;

	if (offset + size > ip->i_disksize)
		size = ip->i_disksize - offset;

	if (!size)
		return 0;

	if (gfs2_is_stuffed(ip))
		return gfs2_dir_read_stuffed(ip, buf, offset, size);

	if (gfs2_assert_warn(sdp, gfs2_is_jdata(ip)))
		return -EINVAL;

	lblock = offset;
	o = do_div(lblock, sdp->sd_jbsize) + sizeof(struct gfs2_meta_header);

	while (copied < size) {
		unsigned int amount;
		struct buffer_head *bh;
		int new;

		amount = size - copied;
		if (amount > sdp->sd_sb.sb_bsize - o)
			amount = sdp->sd_sb.sb_bsize - o;

		if (!extlen) {
			new = 0;
			error = gfs2_extent_map(&ip->i_inode, lblock, &new,
						&dblock, &extlen);
			if (error || !dblock)
				goto fail;
			BUG_ON(extlen < 1);
			if (!ra)
				extlen = 1;
			bh = gfs2_meta_ra(ip->i_gl, dblock, extlen);
		} else {
			error = gfs2_meta_read(ip->i_gl, dblock, DIO_WAIT, &bh);
			if (error)
				goto fail;
		}
		error = gfs2_metatype_check(sdp, bh, GFS2_METATYPE_JD);
		if (error) {
			brelse(bh);
			goto fail;
		}
		dblock++;
		extlen--;
		memcpy(buf, bh->b_data + o, amount);
		brelse(bh);
		buf += amount;
		copied += amount;
		lblock++;
		o = sizeof(struct gfs2_meta_header);
	}

	return copied;
fail:
	return (copied) ? copied : error;
}

static inline int gfs2_dirent_sentinel(const struct gfs2_dirent *dent)
{
	return dent->de_inum.no_addr == 0 || dent->de_inum.no_formal_ino == 0;
}

static inline int __gfs2_dirent_find(const struct gfs2_dirent *dent,
				     const struct qstr *name, int ret)
{
	if (!gfs2_dirent_sentinel(dent) &&
	    be32_to_cpu(dent->de_hash) == name->hash &&
	    be16_to_cpu(dent->de_name_len) == name->len &&
	    memcmp(dent+1, name->name, name->len) == 0)
		return ret;
	return 0;
}

static int gfs2_dirent_find(const struct gfs2_dirent *dent,
			    const struct qstr *name,
			    void *opaque)
{
	return __gfs2_dirent_find(dent, name, 1);
}

static int gfs2_dirent_prev(const struct gfs2_dirent *dent,
			    const struct qstr *name,
			    void *opaque)
{
	return __gfs2_dirent_find(dent, name, 2);
}

/*
 * name->name holds ptr to start of block.
 * name->len holds size of block.
 */
static int gfs2_dirent_last(const struct gfs2_dirent *dent,
			    const struct qstr *name,
			    void *opaque)
{
	const char *start = name->name;
	const char *end = (const char *)dent + be16_to_cpu(dent->de_rec_len);
	if (name->len == (end - start))
		return 1;
	return 0;
}

static int gfs2_dirent_find_space(const struct gfs2_dirent *dent,
				  const struct qstr *name,
				  void *opaque)
{
	unsigned required = GFS2_DIRENT_SIZE(name->len);
	unsigned actual = GFS2_DIRENT_SIZE(be16_to_cpu(dent->de_name_len));
	unsigned totlen = be16_to_cpu(dent->de_rec_len);

	if (gfs2_dirent_sentinel(dent))
		actual = 0;
	if (totlen - actual >= required)
		return 1;
	return 0;
}

struct dirent_gather {
	const struct gfs2_dirent **pdent;
	unsigned offset;
};

static int gfs2_dirent_gather(const struct gfs2_dirent *dent,
			      const struct qstr *name,
			      void *opaque)
{
	struct dirent_gather *g = opaque;
	if (!gfs2_dirent_sentinel(dent)) {
		g->pdent[g->offset++] = dent;
	}
	return 0;
}

/*
 * Other possible things to check:
 * - Inode located within filesystem size (and on valid block)
 * - Valid directory entry type
 * Not sure how heavy-weight we want to make this... could also check
 * hash is correct for example, but that would take a lot of extra time.
 * For now the most important thing is to check that the various sizes
 * are correct.
 */
static int gfs2_check_dirent(struct gfs2_dirent *dent, unsigned int offset,
			     unsigned int size, unsigned int len, int first)
{
	const char *msg = "gfs2_dirent too small";
	if (unlikely(size < sizeof(struct gfs2_dirent)))
		goto error;
	msg = "gfs2_dirent misaligned";
	if (unlikely(offset & 0x7))
		goto error;
	msg = "gfs2_dirent points beyond end of block";
	if (unlikely(offset + size > len))
		goto error;
	msg = "zero inode number";
	if (unlikely(!first && gfs2_dirent_sentinel(dent)))
		goto error;
	msg = "name length is greater than space in dirent";
	if (!gfs2_dirent_sentinel(dent) &&
	    unlikely(sizeof(struct gfs2_dirent)+be16_to_cpu(dent->de_name_len) >
		     size))
		goto error;
	return 0;
error:
	printk(KERN_WARNING "gfs2_check_dirent: %s (%s)\n", msg,
	       first ? "first in block" : "not first in block");
	return -EIO;
}

static int gfs2_dirent_offset(const void *buf)
{
	const struct gfs2_meta_header *h = buf;
	int offset;

	BUG_ON(buf == NULL);

	switch(be32_to_cpu(h->mh_type)) {
	case GFS2_METATYPE_LF:
		offset = sizeof(struct gfs2_leaf);
		break;
	case GFS2_METATYPE_DI:
		offset = sizeof(struct gfs2_dinode);
		break;
	default:
		goto wrong_type;
	}
	return offset;
wrong_type:
	printk(KERN_WARNING "gfs2_scan_dirent: wrong block type %u\n",
	       be32_to_cpu(h->mh_type));
	return -1;
}

static struct gfs2_dirent *gfs2_dirent_scan(struct inode *inode, void *buf,
					    unsigned int len, gfs2_dscan_t scan,
					    const struct qstr *name,
					    void *opaque)
{
	struct gfs2_dirent *dent, *prev;
	unsigned offset;
	unsigned size;
	int ret = 0;

	ret = gfs2_dirent_offset(buf);
	if (ret < 0)
		goto consist_inode;

	offset = ret;
	prev = NULL;
	dent = buf + offset;
	size = be16_to_cpu(dent->de_rec_len);
	if (gfs2_check_dirent(dent, offset, size, len, 1))
		goto consist_inode;
	do {
		ret = scan(dent, name, opaque);
		if (ret)
			break;
		offset += size;
		if (offset == len)
			break;
		prev = dent;
		dent = buf + offset;
		size = be16_to_cpu(dent->de_rec_len);
		if (gfs2_check_dirent(dent, offset, size, len, 0))
			goto consist_inode;
	} while(1);

	switch(ret) {
	case 0:
		return NULL;
	case 1:
		return dent;
	case 2:
		return prev ? prev : dent;
	default:
		BUG_ON(ret > 0);
		return ERR_PTR(ret);
	}

consist_inode:
	gfs2_consist_inode(GFS2_I(inode));
	return ERR_PTR(-EIO);
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

	if (be32_to_cpu(h->mh_type) == GFS2_METATYPE_LF) {
		if (gfs2_meta_check(GFS2_SB(&dip->i_inode), bh))
			return -EIO;
		*dent = (struct gfs2_dirent *)(bh->b_data +
					       sizeof(struct gfs2_leaf));
		return IS_LEAF;
	} else {
		if (gfs2_metatype_check(GFS2_SB(&dip->i_inode), bh, GFS2_METATYPE_DI))
			return -EIO;
		*dent = (struct gfs2_dirent *)(bh->b_data +
					       sizeof(struct gfs2_dinode));
		return IS_DINODE;
	}
}

static int dirent_check_reclen(struct gfs2_inode *dip,
			       const struct gfs2_dirent *d, const void *end_p)
{
	const void *ptr = d;
	u16 rec_len = be16_to_cpu(d->de_rec_len);

	if (unlikely(rec_len < sizeof(struct gfs2_dirent)))
		goto broken;
	ptr += rec_len;
	if (ptr < end_p)
		return rec_len;
	if (ptr == end_p)
		return -ENOENT;
broken:
	gfs2_consist_inode(dip);
	return -EIO;
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
	struct gfs2_dirent *cur = *dent, *tmp;
	char *bh_end = bh->b_data + bh->b_size;
	int ret;

	ret = dirent_check_reclen(dip, cur, bh_end);
	if (ret < 0)
		return ret;

	tmp = (void *)cur + ret;
	ret = dirent_check_reclen(dip, tmp, bh_end);
	if (ret == -EIO)
		return ret;

        /* Only the first dent could ever have de_inum.no_addr == 0 */
	if (gfs2_dirent_sentinel(tmp)) {
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
	u16 cur_rec_len, prev_rec_len;

	if (gfs2_dirent_sentinel(cur)) {
		gfs2_consist_inode(dip);
		return;
	}

	gfs2_trans_add_bh(dip->i_gl, bh, 1);

	/* If there is no prev entry, this is the first entry in the block.
	   The de_rec_len is already as big as it needs to be.  Just zero
	   out the inode number and return.  */

	if (!prev) {
		cur->de_inum.no_addr = 0;
		cur->de_inum.no_formal_ino = 0;
		return;
	}

	/*  Combine this dentry with the previous one.  */

	prev_rec_len = be16_to_cpu(prev->de_rec_len);
	cur_rec_len = be16_to_cpu(cur->de_rec_len);

	if ((char *)prev + prev_rec_len != (char *)cur)
		gfs2_consist_inode(dip);
	if ((char *)cur + cur_rec_len > bh->b_data + bh->b_size)
		gfs2_consist_inode(dip);

	prev_rec_len += cur_rec_len;
	prev->de_rec_len = cpu_to_be16(prev_rec_len);
}

/*
 * Takes a dent from which to grab space as an argument. Returns the
 * newly created dent.
 */
static struct gfs2_dirent *gfs2_init_dirent(struct inode *inode,
					    struct gfs2_dirent *dent,
					    const struct qstr *name,
					    struct buffer_head *bh)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_dirent *ndent;
	unsigned offset = 0, totlen;

	if (!gfs2_dirent_sentinel(dent))
		offset = GFS2_DIRENT_SIZE(be16_to_cpu(dent->de_name_len));
	totlen = be16_to_cpu(dent->de_rec_len);
	BUG_ON(offset + name->len > totlen);
	gfs2_trans_add_bh(ip->i_gl, bh, 1);
	ndent = (struct gfs2_dirent *)((char *)dent + offset);
	dent->de_rec_len = cpu_to_be16(offset);
	gfs2_qstr2dirent(name, totlen - offset, ndent);
	return ndent;
}

static struct gfs2_dirent *gfs2_dirent_alloc(struct inode *inode,
					     struct buffer_head *bh,
					     const struct qstr *name)
{
	struct gfs2_dirent *dent;
	dent = gfs2_dirent_scan(inode, bh->b_data, bh->b_size,
				gfs2_dirent_find_space, name, NULL);
	if (!dent || IS_ERR(dent))
		return dent;
	return gfs2_init_dirent(inode, dent, name, bh);
}

static int get_leaf(struct gfs2_inode *dip, u64 leaf_no,
		    struct buffer_head **bhp)
{
	int error;

	error = gfs2_meta_read(dip->i_gl, leaf_no, DIO_WAIT, bhp);
	if (!error && gfs2_metatype_check(GFS2_SB(&dip->i_inode), *bhp, GFS2_METATYPE_LF)) {
		/* printk(KERN_INFO "block num=%llu\n", leaf_no); */
		error = -EIO;
	}

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

static int get_leaf_nr(struct gfs2_inode *dip, u32 index,
		       u64 *leaf_out)
{
	__be64 leaf_no;
	int error;

	error = gfs2_dir_read_data(dip, (char *)&leaf_no,
				    index * sizeof(__be64),
				    sizeof(__be64), 0);
	if (error != sizeof(u64))
		return (error < 0) ? error : -EIO;

	*leaf_out = be64_to_cpu(leaf_no);

	return 0;
}

static int get_first_leaf(struct gfs2_inode *dip, u32 index,
			  struct buffer_head **bh_out)
{
	u64 leaf_no;
	int error;

	error = get_leaf_nr(dip, index, &leaf_no);
	if (!error)
		error = get_leaf(dip, leaf_no, bh_out);

	return error;
}

static struct gfs2_dirent *gfs2_dirent_search(struct inode *inode,
					      const struct qstr *name,
					      gfs2_dscan_t scan,
					      struct buffer_head **pbh)
{
	struct buffer_head *bh;
	struct gfs2_dirent *dent;
	struct gfs2_inode *ip = GFS2_I(inode);
	int error;

	if (ip->i_diskflags & GFS2_DIF_EXHASH) {
		struct gfs2_leaf *leaf;
		unsigned hsize = 1 << ip->i_depth;
		unsigned index;
		u64 ln;
		if (hsize * sizeof(u64) != ip->i_disksize) {
			gfs2_consist_inode(ip);
			return ERR_PTR(-EIO);
		}

		index = name->hash >> (32 - ip->i_depth);
		error = get_first_leaf(ip, index, &bh);
		if (error)
			return ERR_PTR(error);
		do {
			dent = gfs2_dirent_scan(inode, bh->b_data, bh->b_size,
						scan, name, NULL);
			if (dent)
				goto got_dent;
			leaf = (struct gfs2_leaf *)bh->b_data;
			ln = be64_to_cpu(leaf->lf_next);
			brelse(bh);
			if (!ln)
				break;

			error = get_leaf(ip, ln, &bh);
		} while(!error);

		return error ? ERR_PTR(error) : NULL;
	}


	error = gfs2_meta_inode_buffer(ip, &bh);
	if (error)
		return ERR_PTR(error);
	dent = gfs2_dirent_scan(inode, bh->b_data, bh->b_size, scan, name, NULL);
got_dent:
	if (unlikely(dent == NULL || IS_ERR(dent))) {
		brelse(bh);
		bh = NULL;
	}
	*pbh = bh;
	return dent;
}

static struct gfs2_leaf *new_leaf(struct inode *inode, struct buffer_head **pbh, u16 depth)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	unsigned int n = 1;
	u64 bn;
	int error;
	struct buffer_head *bh;
	struct gfs2_leaf *leaf;
	struct gfs2_dirent *dent;
	struct qstr name = { .name = "", .len = 0, .hash = 0 };

	error = gfs2_alloc_block(ip, &bn, &n);
	if (error)
		return NULL;
	bh = gfs2_meta_new(ip->i_gl, bn);
	if (!bh)
		return NULL;

	gfs2_trans_add_unrevoke(GFS2_SB(inode), bn, 1);
	gfs2_trans_add_bh(ip->i_gl, bh, 1);
	gfs2_metatype_set(bh, GFS2_METATYPE_LF, GFS2_FORMAT_LF);
	leaf = (struct gfs2_leaf *)bh->b_data;
	leaf->lf_depth = cpu_to_be16(depth);
	leaf->lf_entries = 0;
	leaf->lf_dirent_format = cpu_to_be32(GFS2_FORMAT_DE);
	leaf->lf_next = 0;
	memset(leaf->lf_reserved, 0, sizeof(leaf->lf_reserved));
	dent = (struct gfs2_dirent *)(leaf+1);
	gfs2_qstr2dirent(&name, bh->b_size - sizeof(struct gfs2_leaf), dent);
	*pbh = bh;
	return leaf;
}

/**
 * dir_make_exhash - Convert a stuffed directory into an ExHash directory
 * @dip: The GFS2 inode
 *
 * Returns: 0 on success, error code otherwise
 */

static int dir_make_exhash(struct inode *inode)
{
	struct gfs2_inode *dip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct gfs2_dirent *dent;
	struct qstr args;
	struct buffer_head *bh, *dibh;
	struct gfs2_leaf *leaf;
	int y;
	u32 x;
	__be64 *lp;
	u64 bn;
	int error;

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		return error;

	/*  Turn over a new leaf  */

	leaf = new_leaf(inode, &bh, 0);
	if (!leaf)
		return -ENOSPC;
	bn = bh->b_blocknr;

	gfs2_assert(sdp, dip->i_entries < (1 << 16));
	leaf->lf_entries = cpu_to_be16(dip->i_entries);

	/*  Copy dirents  */

	gfs2_buffer_copy_tail(bh, sizeof(struct gfs2_leaf), dibh,
			     sizeof(struct gfs2_dinode));

	/*  Find last entry  */

	x = 0;
	args.len = bh->b_size - sizeof(struct gfs2_dinode) +
		   sizeof(struct gfs2_leaf);
	args.name = bh->b_data;
	dent = gfs2_dirent_scan(&dip->i_inode, bh->b_data, bh->b_size,
				gfs2_dirent_last, &args, NULL);
	if (!dent) {
		brelse(bh);
		brelse(dibh);
		return -EIO;
	}
	if (IS_ERR(dent)) {
		brelse(bh);
		brelse(dibh);
		return PTR_ERR(dent);
	}

	/*  Adjust the last dirent's record length
	   (Remember that dent still points to the last entry.)  */

	dent->de_rec_len = cpu_to_be16(be16_to_cpu(dent->de_rec_len) +
		sizeof(struct gfs2_dinode) -
		sizeof(struct gfs2_leaf));

	brelse(bh);

	/*  We're done with the new leaf block, now setup the new
	    hash table.  */

	gfs2_trans_add_bh(dip->i_gl, dibh, 1);
	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));

	lp = (__be64 *)(dibh->b_data + sizeof(struct gfs2_dinode));

	for (x = sdp->sd_hash_ptrs; x--; lp++)
		*lp = cpu_to_be64(bn);

	dip->i_disksize = sdp->sd_sb.sb_bsize / 2;
	gfs2_add_inode_blocks(&dip->i_inode, 1);
	dip->i_diskflags |= GFS2_DIF_EXHASH;

	for (x = sdp->sd_hash_ptrs, y = -1; x; x >>= 1, y++) ;
	dip->i_depth = y;

	gfs2_dinode_out(dip, dibh->b_data);

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

static int dir_split_leaf(struct inode *inode, const struct qstr *name)
{
	struct gfs2_inode *dip = GFS2_I(inode);
	struct buffer_head *nbh, *obh, *dibh;
	struct gfs2_leaf *nleaf, *oleaf;
	struct gfs2_dirent *dent = NULL, *prev = NULL, *next = NULL, *new;
	u32 start, len, half_len, divider;
	u64 bn, leaf_no;
	__be64 *lp;
	u32 index;
	int x, moved = 0;
	int error;

	index = name->hash >> (32 - dip->i_depth);
	error = get_leaf_nr(dip, index, &leaf_no);
	if (error)
		return error;

	/*  Get the old leaf block  */
	error = get_leaf(dip, leaf_no, &obh);
	if (error)
		return error;

	oleaf = (struct gfs2_leaf *)obh->b_data;
	if (dip->i_depth == be16_to_cpu(oleaf->lf_depth)) {
		brelse(obh);
		return 1; /* can't split */
	}

	gfs2_trans_add_bh(dip->i_gl, obh, 1);

	nleaf = new_leaf(inode, &nbh, be16_to_cpu(oleaf->lf_depth) + 1);
	if (!nleaf) {
		brelse(obh);
		return -ENOSPC;
	}
	bn = nbh->b_blocknr;

	/*  Compute the start and len of leaf pointers in the hash table.  */
	len = 1 << (dip->i_depth - be16_to_cpu(oleaf->lf_depth));
	half_len = len >> 1;
	if (!half_len) {
		printk(KERN_WARNING "i_depth %u lf_depth %u index %u\n", dip->i_depth, be16_to_cpu(oleaf->lf_depth), index);
		gfs2_consist_inode(dip);
		error = -EIO;
		goto fail_brelse;
	}

	start = (index & ~(len - 1));

	/* Change the pointers.
	   Don't bother distinguishing stuffed from non-stuffed.
	   This code is complicated enough already. */
	lp = kmalloc(half_len * sizeof(__be64), GFP_NOFS | __GFP_NOFAIL);
	/*  Change the pointers  */
	for (x = 0; x < half_len; x++)
		lp[x] = cpu_to_be64(bn);

	error = gfs2_dir_write_data(dip, (char *)lp, start * sizeof(u64),
				    half_len * sizeof(u64));
	if (error != half_len * sizeof(u64)) {
		if (error >= 0)
			error = -EIO;
		goto fail_lpfree;
	}

	kfree(lp);

	/*  Compute the divider  */
	divider = (start + half_len) << (32 - dip->i_depth);

	/*  Copy the entries  */
	dirent_first(dip, obh, &dent);

	do {
		next = dent;
		if (dirent_next(dip, obh, &next))
			next = NULL;

		if (!gfs2_dirent_sentinel(dent) &&
		    be32_to_cpu(dent->de_hash) < divider) {
			struct qstr str;
			str.name = (char*)(dent+1);
			str.len = be16_to_cpu(dent->de_name_len);
			str.hash = be32_to_cpu(dent->de_hash);
			new = gfs2_dirent_alloc(inode, nbh, &str);
			if (IS_ERR(new)) {
				error = PTR_ERR(new);
				break;
			}

			new->de_inum = dent->de_inum; /* No endian worries */
			new->de_type = dent->de_type; /* No endian worries */
			be16_add_cpu(&nleaf->lf_entries, 1);

			dirent_del(dip, obh, prev, dent);

			if (!oleaf->lf_entries)
				gfs2_consist_inode(dip);
			be16_add_cpu(&oleaf->lf_entries, -1);

			if (!prev)
				prev = dent;

			moved = 1;
		} else {
			prev = dent;
		}
		dent = next;
	} while (dent);

	oleaf->lf_depth = nleaf->lf_depth;

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (!gfs2_assert_withdraw(GFS2_SB(&dip->i_inode), !error)) {
		gfs2_trans_add_bh(dip->i_gl, dibh, 1);
		gfs2_add_inode_blocks(&dip->i_inode, 1);
		gfs2_dinode_out(dip, dibh->b_data);
		brelse(dibh);
	}

	brelse(obh);
	brelse(nbh);

	return error;

fail_lpfree:
	kfree(lp);

fail_brelse:
	brelse(obh);
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
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	struct buffer_head *dibh;
	u32 hsize;
	u64 *buf;
	u64 *from, *to;
	u64 block;
	int x;
	int error = 0;

	hsize = 1 << dip->i_depth;
	if (hsize * sizeof(u64) != dip->i_disksize) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	/*  Allocate both the "from" and "to" buffers in one big chunk  */

	buf = kcalloc(3, sdp->sd_hash_bsize, GFP_NOFS | __GFP_NOFAIL);

	for (block = dip->i_disksize >> sdp->sd_hash_bsize_shift; block--;) {
		error = gfs2_dir_read_data(dip, (char *)buf,
					    block * sdp->sd_hash_bsize,
					    sdp->sd_hash_bsize, 1);
		if (error != sdp->sd_hash_bsize) {
			if (error >= 0)
				error = -EIO;
			goto fail;
		}

		from = buf;
		to = (u64 *)((char *)buf + sdp->sd_hash_bsize);

		for (x = sdp->sd_hash_ptrs; x--; from++) {
			*to++ = *from;	/*  No endianess worries  */
			*to++ = *from;
		}

		error = gfs2_dir_write_data(dip,
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
		dip->i_depth++;
		gfs2_dinode_out(dip, dibh->b_data);
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
	const struct gfs2_dirent *dent_a, *dent_b;
	u32 hash_a, hash_b;
	int ret = 0;

	dent_a = *(const struct gfs2_dirent **)a;
	hash_a = be32_to_cpu(dent_a->de_hash);

	dent_b = *(const struct gfs2_dirent **)b;
	hash_b = be32_to_cpu(dent_b->de_hash);

	if (hash_a > hash_b)
		ret = 1;
	else if (hash_a < hash_b)
		ret = -1;
	else {
		unsigned int len_a = be16_to_cpu(dent_a->de_name_len);
		unsigned int len_b = be16_to_cpu(dent_b->de_name_len);

		if (len_a > len_b)
			ret = 1;
		else if (len_a < len_b)
			ret = -1;
		else
			ret = memcmp(dent_a + 1, dent_b + 1, len_a);
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

static int do_filldir_main(struct gfs2_inode *dip, u64 *offset,
			   void *opaque, filldir_t filldir,
			   const struct gfs2_dirent **darr, u32 entries,
			   int *copied)
{
	const struct gfs2_dirent *dent, *dent_next;
	u64 off, off_next;
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

		error = filldir(opaque, (const char *)(dent + 1),
				be16_to_cpu(dent->de_name_len),
				off, be64_to_cpu(dent->de_inum.no_addr),
				be16_to_cpu(dent->de_type));
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

static int gfs2_dir_read_leaf(struct inode *inode, u64 *offset, void *opaque,
			      filldir_t filldir, int *copied, unsigned *depth,
			      u64 leaf_no)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct buffer_head *bh;
	struct gfs2_leaf *lf;
	unsigned entries = 0, entries2 = 0;
	unsigned leaves = 0;
	const struct gfs2_dirent **darr, *dent;
	struct dirent_gather g;
	struct buffer_head **larr;
	int leaf = 0;
	int error, i;
	u64 lfn = leaf_no;

	do {
		error = get_leaf(ip, lfn, &bh);
		if (error)
			goto out;
		lf = (struct gfs2_leaf *)bh->b_data;
		if (leaves == 0)
			*depth = be16_to_cpu(lf->lf_depth);
		entries += be16_to_cpu(lf->lf_entries);
		leaves++;
		lfn = be64_to_cpu(lf->lf_next);
		brelse(bh);
	} while(lfn);

	if (!entries)
		return 0;

	error = -ENOMEM;
	/*
	 * The extra 99 entries are not normally used, but are a buffer
	 * zone in case the number of entries in the leaf is corrupt.
	 * 99 is the maximum number of entries that can fit in a single
	 * leaf block.
	 */
	larr = vmalloc((leaves + entries + 99) * sizeof(void *));
	if (!larr)
		goto out;
	darr = (const struct gfs2_dirent **)(larr + leaves);
	g.pdent = darr;
	g.offset = 0;
	lfn = leaf_no;

	do {
		error = get_leaf(ip, lfn, &bh);
		if (error)
			goto out_kfree;
		lf = (struct gfs2_leaf *)bh->b_data;
		lfn = be64_to_cpu(lf->lf_next);
		if (lf->lf_entries) {
			entries2 += be16_to_cpu(lf->lf_entries);
			dent = gfs2_dirent_scan(inode, bh->b_data, bh->b_size,
						gfs2_dirent_gather, NULL, &g);
			error = PTR_ERR(dent);
			if (IS_ERR(dent))
				goto out_kfree;
			if (entries2 != g.offset) {
				fs_warn(sdp, "Number of entries corrupt in dir "
						"leaf %llu, entries2 (%u) != "
						"g.offset (%u)\n",
					(unsigned long long)bh->b_blocknr,
					entries2, g.offset);
					
				error = -EIO;
				goto out_kfree;
			}
			error = 0;
			larr[leaf++] = bh;
		} else {
			brelse(bh);
		}
	} while(lfn);

	BUG_ON(entries2 != entries);
	error = do_filldir_main(ip, offset, opaque, filldir, darr,
				entries, copied);
out_kfree:
	for(i = 0; i < leaf; i++)
		brelse(larr[i]);
	vfree(larr);
out:
	return error;
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

static int dir_e_read(struct inode *inode, u64 *offset, void *opaque,
		      filldir_t filldir)
{
	struct gfs2_inode *dip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	u32 hsize, len = 0;
	u32 ht_offset, lp_offset, ht_offset_cur = -1;
	u32 hash, index;
	__be64 *lp;
	int copied = 0;
	int error = 0;
	unsigned depth = 0;

	hsize = 1 << dip->i_depth;
	if (hsize * sizeof(u64) != dip->i_disksize) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	hash = gfs2_dir_offset2hash(*offset);
	index = hash >> (32 - dip->i_depth);

	lp = kmalloc(sdp->sd_hash_bsize, GFP_NOFS);
	if (!lp)
		return -ENOMEM;

	while (index < hsize) {
		lp_offset = index & (sdp->sd_hash_ptrs - 1);
		ht_offset = index - lp_offset;

		if (ht_offset_cur != ht_offset) {
			error = gfs2_dir_read_data(dip, (char *)lp,
						ht_offset * sizeof(__be64),
						sdp->sd_hash_bsize, 1);
			if (error != sdp->sd_hash_bsize) {
				if (error >= 0)
					error = -EIO;
				goto out;
			}
			ht_offset_cur = ht_offset;
		}

		error = gfs2_dir_read_leaf(inode, offset, opaque, filldir,
					   &copied, &depth,
					   be64_to_cpu(lp[lp_offset]));
		if (error)
			break;

		len = 1 << (dip->i_depth - depth);
		index = (index & ~(len - 1)) + len;
	}

out:
	kfree(lp);
	if (error > 0)
		error = 0;
	return error;
}

int gfs2_dir_read(struct inode *inode, u64 *offset, void *opaque,
		  filldir_t filldir)
{
	struct gfs2_inode *dip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct dirent_gather g;
	const struct gfs2_dirent **darr, *dent;
	struct buffer_head *dibh;
	int copied = 0;
	int error;

	if (!dip->i_entries)
		return 0;

	if (dip->i_diskflags & GFS2_DIF_EXHASH)
		return dir_e_read(inode, offset, opaque, filldir);

	if (!gfs2_is_stuffed(dip)) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		return error;

	error = -ENOMEM;
	/* 96 is max number of dirents which can be stuffed into an inode */
	darr = kmalloc(96 * sizeof(struct gfs2_dirent *), GFP_NOFS);
	if (darr) {
		g.pdent = darr;
		g.offset = 0;
		dent = gfs2_dirent_scan(inode, dibh->b_data, dibh->b_size,
					gfs2_dirent_gather, NULL, &g);
		if (IS_ERR(dent)) {
			error = PTR_ERR(dent);
			goto out;
		}
		if (dip->i_entries != g.offset) {
			fs_warn(sdp, "Number of entries corrupt in dir %llu, "
				"ip->i_entries (%u) != g.offset (%u)\n",
				(unsigned long long)dip->i_no_addr,
				dip->i_entries,
				g.offset);
			error = -EIO;
			goto out;
		}
		error = do_filldir_main(dip, offset, opaque, filldir, darr,
					dip->i_entries, &copied);
out:
		kfree(darr);
	}

	if (error > 0)
		error = 0;

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

struct inode *gfs2_dir_search(struct inode *dir, const struct qstr *name)
{
	struct buffer_head *bh;
	struct gfs2_dirent *dent;
	struct inode *inode;

	dent = gfs2_dirent_search(dir, name, gfs2_dirent_find, &bh);
	if (dent) {
		if (IS_ERR(dent))
			return ERR_CAST(dent);
		inode = gfs2_inode_lookup(dir->i_sb, 
				be16_to_cpu(dent->de_type),
				be64_to_cpu(dent->de_inum.no_addr),
				be64_to_cpu(dent->de_inum.no_formal_ino), 0);
		brelse(bh);
		return inode;
	}
	return ERR_PTR(-ENOENT);
}

int gfs2_dir_check(struct inode *dir, const struct qstr *name,
		   const struct gfs2_inode *ip)
{
	struct buffer_head *bh;
	struct gfs2_dirent *dent;
	int ret = -ENOENT;

	dent = gfs2_dirent_search(dir, name, gfs2_dirent_find, &bh);
	if (dent) {
		if (IS_ERR(dent))
			return PTR_ERR(dent);
		if (ip) {
			if (be64_to_cpu(dent->de_inum.no_addr) != ip->i_no_addr)
				goto out;
			if (be64_to_cpu(dent->de_inum.no_formal_ino) !=
			    ip->i_no_formal_ino)
				goto out;
			if (unlikely(IF2DT(ip->i_inode.i_mode) !=
			    be16_to_cpu(dent->de_type))) {
				gfs2_consist_inode(GFS2_I(dir));
				ret = -EIO;
				goto out;
			}
		}
		ret = 0;
out:
		brelse(bh);
	}
	return ret;
}

static int dir_new_leaf(struct inode *inode, const struct qstr *name)
{
	struct buffer_head *bh, *obh;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_leaf *leaf, *oleaf;
	int error;
	u32 index;
	u64 bn;

	index = name->hash >> (32 - ip->i_depth);
	error = get_first_leaf(ip, index, &obh);
	if (error)
		return error;
	do {
		oleaf = (struct gfs2_leaf *)obh->b_data;
		bn = be64_to_cpu(oleaf->lf_next);
		if (!bn)
			break;
		brelse(obh);
		error = get_leaf(ip, bn, &obh);
		if (error)
			return error;
	} while(1);

	gfs2_trans_add_bh(ip->i_gl, obh, 1);

	leaf = new_leaf(inode, &bh, be16_to_cpu(oleaf->lf_depth));
	if (!leaf) {
		brelse(obh);
		return -ENOSPC;
	}
	oleaf->lf_next = cpu_to_be64(bh->b_blocknr);
	brelse(bh);
	brelse(obh);

	error = gfs2_meta_inode_buffer(ip, &bh);
	if (error)
		return error;
	gfs2_trans_add_bh(ip->i_gl, bh, 1);
	gfs2_add_inode_blocks(&ip->i_inode, 1);
	gfs2_dinode_out(ip, bh->b_data);
	brelse(bh);
	return 0;
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

int gfs2_dir_add(struct inode *inode, const struct qstr *name,
		 const struct gfs2_inode *nip, unsigned type)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct buffer_head *bh;
	struct gfs2_dirent *dent;
	struct gfs2_leaf *leaf;
	int error;

	while(1) {
		dent = gfs2_dirent_search(inode, name, gfs2_dirent_find_space,
					  &bh);
		if (dent) {
			if (IS_ERR(dent))
				return PTR_ERR(dent);
			dent = gfs2_init_dirent(inode, dent, name, bh);
			gfs2_inum_out(nip, dent);
			dent->de_type = cpu_to_be16(type);
			if (ip->i_diskflags & GFS2_DIF_EXHASH) {
				leaf = (struct gfs2_leaf *)bh->b_data;
				be16_add_cpu(&leaf->lf_entries, 1);
			}
			brelse(bh);
			error = gfs2_meta_inode_buffer(ip, &bh);
			if (error)
				break;
			gfs2_trans_add_bh(ip->i_gl, bh, 1);
			ip->i_entries++;
			ip->i_inode.i_mtime = ip->i_inode.i_ctime = CURRENT_TIME;
			gfs2_dinode_out(ip, bh->b_data);
			brelse(bh);
			error = 0;
			break;
		}
		if (!(ip->i_diskflags & GFS2_DIF_EXHASH)) {
			error = dir_make_exhash(inode);
			if (error)
				break;
			continue;
		}
		error = dir_split_leaf(inode, name);
		if (error == 0)
			continue;
		if (error < 0)
			break;
		if (ip->i_depth < GFS2_DIR_MAX_DEPTH) {
			error = dir_double_exhash(ip);
			if (error)
				break;
			error = dir_split_leaf(inode, name);
			if (error < 0)
				break;
			if (error == 0)
				continue;
		}
		error = dir_new_leaf(inode, name);
		if (!error)
			continue;
		error = -ENOSPC;
		break;
	}
	return error;
}


/**
 * gfs2_dir_del - Delete a directory entry
 * @dip: The GFS2 inode
 * @filename: The filename
 *
 * Returns: 0 on success, error code on failure
 */

int gfs2_dir_del(struct gfs2_inode *dip, const struct qstr *name)
{
	struct gfs2_dirent *dent, *prev = NULL;
	struct buffer_head *bh;
	int error;

	/* Returns _either_ the entry (if its first in block) or the
	   previous entry otherwise */
	dent = gfs2_dirent_search(&dip->i_inode, name, gfs2_dirent_prev, &bh);
	if (!dent) {
		gfs2_consist_inode(dip);
		return -EIO;
	}
	if (IS_ERR(dent)) {
		gfs2_consist_inode(dip);
		return PTR_ERR(dent);
	}
	/* If not first in block, adjust pointers accordingly */
	if (gfs2_dirent_find(dent, name, NULL) == 0) {
		prev = dent;
		dent = (struct gfs2_dirent *)((char *)dent + be16_to_cpu(prev->de_rec_len));
	}

	dirent_del(dip, bh, prev, dent);
	if (dip->i_diskflags & GFS2_DIF_EXHASH) {
		struct gfs2_leaf *leaf = (struct gfs2_leaf *)bh->b_data;
		u16 entries = be16_to_cpu(leaf->lf_entries);
		if (!entries)
			gfs2_consist_inode(dip);
		leaf->lf_entries = cpu_to_be16(--entries);
	}
	brelse(bh);

	error = gfs2_meta_inode_buffer(dip, &bh);
	if (error)
		return error;

	if (!dip->i_entries)
		gfs2_consist_inode(dip);
	gfs2_trans_add_bh(dip->i_gl, bh, 1);
	dip->i_entries--;
	dip->i_inode.i_mtime = dip->i_inode.i_ctime = CURRENT_TIME;
	gfs2_dinode_out(dip, bh->b_data);
	brelse(bh);
	mark_inode_dirty(&dip->i_inode);

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

int gfs2_dir_mvino(struct gfs2_inode *dip, const struct qstr *filename,
		   const struct gfs2_inode *nip, unsigned int new_type)
{
	struct buffer_head *bh;
	struct gfs2_dirent *dent;
	int error;

	dent = gfs2_dirent_search(&dip->i_inode, filename, gfs2_dirent_find, &bh);
	if (!dent) {
		gfs2_consist_inode(dip);
		return -EIO;
	}
	if (IS_ERR(dent))
		return PTR_ERR(dent);

	gfs2_trans_add_bh(dip->i_gl, bh, 1);
	gfs2_inum_out(nip, dent);
	dent->de_type = cpu_to_be16(new_type);

	if (dip->i_diskflags & GFS2_DIF_EXHASH) {
		brelse(bh);
		error = gfs2_meta_inode_buffer(dip, &bh);
		if (error)
			return error;
		gfs2_trans_add_bh(dip->i_gl, bh, 1);
	}

	dip->i_inode.i_mtime = dip->i_inode.i_ctime = CURRENT_TIME;
	gfs2_dinode_out(dip, bh->b_data);
	brelse(bh);
	return 0;
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
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	struct buffer_head *bh;
	struct gfs2_leaf *leaf;
	u32 hsize, len;
	u32 ht_offset, lp_offset, ht_offset_cur = -1;
	u32 index = 0;
	__be64 *lp;
	u64 leaf_no;
	int error = 0;

	hsize = 1 << dip->i_depth;
	if (hsize * sizeof(u64) != dip->i_disksize) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

	lp = kmalloc(sdp->sd_hash_bsize, GFP_NOFS);
	if (!lp)
		return -ENOMEM;

	while (index < hsize) {
		lp_offset = index & (sdp->sd_hash_ptrs - 1);
		ht_offset = index - lp_offset;

		if (ht_offset_cur != ht_offset) {
			error = gfs2_dir_read_data(dip, (char *)lp,
						ht_offset * sizeof(__be64),
						sdp->sd_hash_bsize, 1);
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
			leaf = (struct gfs2_leaf *)bh->b_data;
			len = 1 << (dip->i_depth - be16_to_cpu(leaf->lf_depth));
			brelse(bh);

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

static int leaf_dealloc(struct gfs2_inode *dip, u32 index, u32 len,
			u64 leaf_no, void *data)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	struct gfs2_leaf *tmp_leaf;
	struct gfs2_rgrp_list rlist;
	struct buffer_head *bh, *dibh;
	u64 blk, nblk;
	unsigned int rg_blocks = 0, l_blocks = 0;
	char *ht;
	unsigned int x, size = len * sizeof(u64);
	int error;

	memset(&rlist, 0, sizeof(struct gfs2_rgrp_list));

	ht = kzalloc(size, GFP_NOFS);
	if (!ht)
		return -ENOMEM;

	if (!gfs2_alloc_get(dip)) {
		error = -ENOMEM;
		goto out;
	}

	error = gfs2_quota_hold(dip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out_put;

	error = gfs2_rindex_hold(sdp, &dip->i_alloc->al_ri_gh);
	if (error)
		goto out_qs;

	/*  Count the number of leaves  */

	for (blk = leaf_no; blk; blk = nblk) {
		error = get_leaf(dip, blk, &bh);
		if (error)
			goto out_rlist;
		tmp_leaf = (struct gfs2_leaf *)bh->b_data;
		nblk = be64_to_cpu(tmp_leaf->lf_next);
		brelse(bh);

		gfs2_rlist_add(sdp, &rlist, blk);
		l_blocks++;
	}

	gfs2_rlist_alloc(&rlist, LM_ST_EXCLUSIVE);

	for (x = 0; x < rlist.rl_rgrps; x++) {
		struct gfs2_rgrpd *rgd;
		rgd = rlist.rl_ghs[x].gh_gl->gl_object;
		rg_blocks += rgd->rd_length;
	}

	error = gfs2_glock_nq_m(rlist.rl_rgrps, rlist.rl_ghs);
	if (error)
		goto out_rlist;

	error = gfs2_trans_begin(sdp,
			rg_blocks + (DIV_ROUND_UP(size, sdp->sd_jbsize) + 1) +
			RES_DINODE + RES_STATFS + RES_QUOTA, l_blocks);
	if (error)
		goto out_rg_gunlock;

	for (blk = leaf_no; blk; blk = nblk) {
		error = get_leaf(dip, blk, &bh);
		if (error)
			goto out_end_trans;
		tmp_leaf = (struct gfs2_leaf *)bh->b_data;
		nblk = be64_to_cpu(tmp_leaf->lf_next);
		brelse(bh);

		gfs2_free_meta(dip, blk, 1);
		gfs2_add_inode_blocks(&dip->i_inode, -1);
	}

	error = gfs2_dir_write_data(dip, ht, index * sizeof(u64), size);
	if (error != size) {
		if (error >= 0)
			error = -EIO;
		goto out_end_trans;
	}

	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		goto out_end_trans;

	gfs2_trans_add_bh(dip->i_gl, dibh, 1);
	gfs2_dinode_out(dip, dibh->b_data);
	brelse(dibh);

out_end_trans:
	gfs2_trans_end(sdp);
out_rg_gunlock:
	gfs2_glock_dq_m(rlist.rl_rgrps, rlist.rl_ghs);
out_rlist:
	gfs2_rlist_free(&rlist);
	gfs2_glock_dq_uninit(&dip->i_alloc->al_ri_gh);
out_qs:
	gfs2_quota_unhold(dip);
out_put:
	gfs2_alloc_put(dip);
out:
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
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
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
		gfs2_trans_add_bh(dip->i_gl, bh, 1);
		((struct gfs2_dinode *)bh->b_data)->di_mode =
						cpu_to_be32(S_IFREG);
		brelse(bh);
	}

	gfs2_trans_end(sdp);

	return error;
}

/**
 * gfs2_diradd_alloc_required - find if adding entry will require an allocation
 * @ip: the file being written to
 * @filname: the filename that's going to be added
 *
 * Returns: 1 if alloc required, 0 if not, -ve on error
 */

int gfs2_diradd_alloc_required(struct inode *inode, const struct qstr *name)
{
	struct gfs2_dirent *dent;
	struct buffer_head *bh;

	dent = gfs2_dirent_search(inode, name, gfs2_dirent_find_space, &bh);
	if (!dent) {
		return 1;
	}
	if (IS_ERR(dent))
		return PTR_ERR(dent);
	brelse(bh);
	return 0;
}

