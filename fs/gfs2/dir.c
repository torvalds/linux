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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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

#define MAX_RA_BLOCKS 32 /* max read-ahead blocks */

#define gfs2_disk_hash2offset(h) (((u64)(h)) >> 1)
#define gfs2_dir_offset2hash(p) ((u32)(((u64)(p)) << 1))

struct qstr gfs2_qdot __read_mostly;
struct qstr gfs2_qdotdot __read_mostly;

typedef int (*gfs2_dscan_t)(const struct gfs2_dirent *dent,
			    const struct qstr *name, void *opaque);

int gfs2_dir_get_new_buffer(struct gfs2_inode *ip, u64 block,
			    struct buffer_head **bhp)
{
	struct buffer_head *bh;

	bh = gfs2_meta_new(ip->i_gl, block);
	gfs2_trans_add_meta(ip->i_gl, bh);
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

	gfs2_trans_add_meta(ip->i_gl, dibh);
	memcpy(dibh->b_data + offset + sizeof(struct gfs2_dinode), buf, size);
	if (ip->i_inode.i_size < offset + size)
		i_size_write(&ip->i_inode, offset + size);
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

		gfs2_trans_add_meta(ip->i_gl, bh);
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

	if (ip->i_inode.i_size < offset + copied)
		i_size_write(&ip->i_inode, offset + copied);
	ip->i_inode.i_mtime = ip->i_inode.i_ctime = CURRENT_TIME;

	gfs2_trans_add_meta(ip->i_gl, dibh);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);

	return copied;
fail:
	if (copied)
		goto out;
	return error;
}

static int gfs2_dir_read_stuffed(struct gfs2_inode *ip, __be64 *buf,
				 unsigned int size)
{
	struct buffer_head *dibh;
	int error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		memcpy(buf, dibh->b_data + sizeof(struct gfs2_dinode), size);
		brelse(dibh);
	}

	return (error) ? error : size;
}


/**
 * gfs2_dir_read_data - Read a data from a directory inode
 * @ip: The GFS2 Inode
 * @buf: The buffer to place result into
 * @size: Amount of data to transfer
 *
 * Returns: The amount of data actually copied or the error
 */
static int gfs2_dir_read_data(struct gfs2_inode *ip, __be64 *buf,
			      unsigned int size)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	u64 lblock, dblock;
	u32 extlen = 0;
	unsigned int o;
	int copied = 0;
	int error = 0;

	if (gfs2_is_stuffed(ip))
		return gfs2_dir_read_stuffed(ip, buf, size);

	if (gfs2_assert_warn(sdp, gfs2_is_jdata(ip)))
		return -EINVAL;

	lblock = 0;
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
		buf += (amount/sizeof(__be64));
		copied += amount;
		lblock++;
		o = sizeof(struct gfs2_meta_header);
	}

	return copied;
fail:
	return (copied) ? copied : error;
}

/**
 * gfs2_dir_get_hash_table - Get pointer to the dir hash table
 * @ip: The inode in question
 *
 * Returns: The hash table or an error
 */

static __be64 *gfs2_dir_get_hash_table(struct gfs2_inode *ip)
{
	struct inode *inode = &ip->i_inode;
	int ret;
	u32 hsize;
	__be64 *hc;

	BUG_ON(!(ip->i_diskflags & GFS2_DIF_EXHASH));

	hc = ip->i_hash_cache;
	if (hc)
		return hc;

	hsize = 1 << ip->i_depth;
	hsize *= sizeof(__be64);
	if (hsize != i_size_read(&ip->i_inode)) {
		gfs2_consist_inode(ip);
		return ERR_PTR(-EIO);
	}

	hc = kmalloc(hsize, GFP_NOFS | __GFP_NOWARN);
	if (hc == NULL)
		hc = __vmalloc(hsize, GFP_NOFS, PAGE_KERNEL);

	if (hc == NULL)
		return ERR_PTR(-ENOMEM);

	ret = gfs2_dir_read_data(ip, hc, hsize);
	if (ret < 0) {
		kvfree(hc);
		return ERR_PTR(ret);
	}

	spin_lock(&inode->i_lock);
	if (likely(!ip->i_hash_cache)) {
		ip->i_hash_cache = hc;
		hc = NULL;
	}
	spin_unlock(&inode->i_lock);
	kvfree(hc);

	return ip->i_hash_cache;
}

/**
 * gfs2_dir_hash_inval - Invalidate dir hash
 * @ip: The directory inode
 *
 * Must be called with an exclusive glock, or during glock invalidation.
 */
void gfs2_dir_hash_inval(struct gfs2_inode *ip)
{
	__be64 *hc = ip->i_hash_cache;
	ip->i_hash_cache = NULL;
	kvfree(hc);
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
	pr_warn("%s: %s (%s)\n",
		__func__, msg, first ? "first in block" : "not first in block");
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
	pr_warn("%s: wrong block type %u\n", __func__, be32_to_cpu(h->mh_type));
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

	gfs2_trans_add_meta(dip->i_gl, bh);

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
	gfs2_trans_add_meta(ip->i_gl, bh);
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
		/* pr_info("block num=%llu\n", leaf_no); */
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
	__be64 *hash;

	hash = gfs2_dir_get_hash_table(dip);
	if (IS_ERR(hash))
		return PTR_ERR(hash);
	*leaf_out = be64_to_cpu(*(hash + index));
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
		if (hsize * sizeof(u64) != i_size_read(inode)) {
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
	struct qstr name = { .name = "" };
	struct timespec tv = CURRENT_TIME;

	error = gfs2_alloc_blocks(ip, &bn, &n, 0, NULL);
	if (error)
		return NULL;
	bh = gfs2_meta_new(ip->i_gl, bn);
	if (!bh)
		return NULL;

	gfs2_trans_add_unrevoke(GFS2_SB(inode), bn, 1);
	gfs2_trans_add_meta(ip->i_gl, bh);
	gfs2_metatype_set(bh, GFS2_METATYPE_LF, GFS2_FORMAT_LF);
	leaf = (struct gfs2_leaf *)bh->b_data;
	leaf->lf_depth = cpu_to_be16(depth);
	leaf->lf_entries = 0;
	leaf->lf_dirent_format = cpu_to_be32(GFS2_FORMAT_DE);
	leaf->lf_next = 0;
	leaf->lf_inode = cpu_to_be64(ip->i_no_addr);
	leaf->lf_dist = cpu_to_be32(1);
	leaf->lf_nsec = cpu_to_be32(tv.tv_nsec);
	leaf->lf_sec = cpu_to_be64(tv.tv_sec);
	memset(leaf->lf_reserved2, 0, sizeof(leaf->lf_reserved2));
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

	gfs2_trans_add_meta(dip->i_gl, dibh);
	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));

	lp = (__be64 *)(dibh->b_data + sizeof(struct gfs2_dinode));

	for (x = sdp->sd_hash_ptrs; x--; lp++)
		*lp = cpu_to_be64(bn);

	i_size_write(inode, sdp->sd_sb.sb_bsize / 2);
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

	gfs2_trans_add_meta(dip->i_gl, obh);

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
		pr_warn("i_depth %u lf_depth %u index %u\n",
			dip->i_depth, be16_to_cpu(oleaf->lf_depth), index);
		gfs2_consist_inode(dip);
		error = -EIO;
		goto fail_brelse;
	}

	start = (index & ~(len - 1));

	/* Change the pointers.
	   Don't bother distinguishing stuffed from non-stuffed.
	   This code is complicated enough already. */
	lp = kmalloc(half_len * sizeof(__be64), GFP_NOFS);
	if (!lp) {
		error = -ENOMEM;
		goto fail_brelse;
	}

	/*  Change the pointers  */
	for (x = 0; x < half_len; x++)
		lp[x] = cpu_to_be64(bn);

	gfs2_dir_hash_inval(dip);

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
	dent = (struct gfs2_dirent *)(obh->b_data + sizeof(struct gfs2_leaf));

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
		gfs2_trans_add_meta(dip->i_gl, dibh);
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
	struct buffer_head *dibh;
	u32 hsize;
	u32 hsize_bytes;
	__be64 *hc;
	__be64 *hc2, *h;
	int x;
	int error = 0;

	hsize = 1 << dip->i_depth;
	hsize_bytes = hsize * sizeof(__be64);

	hc = gfs2_dir_get_hash_table(dip);
	if (IS_ERR(hc))
		return PTR_ERR(hc);

	hc2 = kmalloc(hsize_bytes * 2, GFP_NOFS | __GFP_NOWARN);
	if (hc2 == NULL)
		hc2 = __vmalloc(hsize_bytes * 2, GFP_NOFS, PAGE_KERNEL);

	if (!hc2)
		return -ENOMEM;

	h = hc2;
	error = gfs2_meta_inode_buffer(dip, &dibh);
	if (error)
		goto out_kfree;

	for (x = 0; x < hsize; x++) {
		*h++ = *hc;
		*h++ = *hc;
		hc++;
	}

	error = gfs2_dir_write_data(dip, (char *)hc2, 0, hsize_bytes * 2);
	if (error != (hsize_bytes * 2))
		goto fail;

	gfs2_dir_hash_inval(dip);
	dip->i_hash_cache = hc2;
	dip->i_depth++;
	gfs2_dinode_out(dip, dibh->b_data);
	brelse(dibh);
	return 0;

fail:
	/* Replace original hash table & size */
	gfs2_dir_write_data(dip, (char *)hc, 0, hsize_bytes);
	i_size_write(&dip->i_inode, hsize_bytes);
	gfs2_dinode_out(dip, dibh->b_data);
	brelse(dibh);
out_kfree:
	kvfree(hc2);
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
 * @ctx: what to feed the entries to
 * @darr: an array of struct gfs2_dirent pointers to read
 * @entries: the number of entries in darr
 * @copied: pointer to int that's non-zero if a entry has been copied out
 *
 * Jump through some hoops to make sure that if there are hash collsions,
 * they are read out at the beginning of a buffer.  We want to minimize
 * the possibility that they will fall into different readdir buffers or
 * that someone will want to seek to that location.
 *
 * Returns: errno, >0 if the actor tells you to stop
 */

static int do_filldir_main(struct gfs2_inode *dip, struct dir_context *ctx,
			   const struct gfs2_dirent **darr, u32 entries,
			   int *copied)
{
	const struct gfs2_dirent *dent, *dent_next;
	u64 off, off_next;
	unsigned int x, y;
	int run = 0;

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

			if (off < ctx->pos)
				continue;
			ctx->pos = off;

			if (off_next == off) {
				if (*copied && !run)
					return 1;
				run = 1;
			} else
				run = 0;
		} else {
			if (off < ctx->pos)
				continue;
			ctx->pos = off;
		}

		if (!dir_emit(ctx, (const char *)(dent + 1),
				be16_to_cpu(dent->de_name_len),
				be64_to_cpu(dent->de_inum.no_addr),
				be16_to_cpu(dent->de_type)))
			return 1;

		*copied = 1;
	}

	/* Increment the ctx->pos by one, so the next time we come into the
	   do_filldir fxn, we get the next entry instead of the last one in the
	   current leaf */

	ctx->pos++;

	return 0;
}

static void *gfs2_alloc_sort_buffer(unsigned size)
{
	void *ptr = NULL;

	if (size < KMALLOC_MAX_SIZE)
		ptr = kmalloc(size, GFP_NOFS | __GFP_NOWARN);
	if (!ptr)
		ptr = __vmalloc(size, GFP_NOFS, PAGE_KERNEL);
	return ptr;
}

static int gfs2_dir_read_leaf(struct inode *inode, struct dir_context *ctx,
			      int *copied, unsigned *depth,
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
	larr = gfs2_alloc_sort_buffer((leaves + entries + 99) * sizeof(void *));
	if (!larr)
		goto out;
	darr = (const struct gfs2_dirent **)(larr + leaves);
	g.pdent = darr;
	g.offset = 0;
	lfn = leaf_no;

	do {
		error = get_leaf(ip, lfn, &bh);
		if (error)
			goto out_free;
		lf = (struct gfs2_leaf *)bh->b_data;
		lfn = be64_to_cpu(lf->lf_next);
		if (lf->lf_entries) {
			entries2 += be16_to_cpu(lf->lf_entries);
			dent = gfs2_dirent_scan(inode, bh->b_data, bh->b_size,
						gfs2_dirent_gather, NULL, &g);
			error = PTR_ERR(dent);
			if (IS_ERR(dent))
				goto out_free;
			if (entries2 != g.offset) {
				fs_warn(sdp, "Number of entries corrupt in dir "
						"leaf %llu, entries2 (%u) != "
						"g.offset (%u)\n",
					(unsigned long long)bh->b_blocknr,
					entries2, g.offset);
					
				error = -EIO;
				goto out_free;
			}
			error = 0;
			larr[leaf++] = bh;
		} else {
			brelse(bh);
		}
	} while(lfn);

	BUG_ON(entries2 != entries);
	error = do_filldir_main(ip, ctx, darr, entries, copied);
out_free:
	for(i = 0; i < leaf; i++)
		brelse(larr[i]);
	kvfree(larr);
out:
	return error;
}

/**
 * gfs2_dir_readahead - Issue read-ahead requests for leaf blocks.
 *
 * Note: we can't calculate each index like dir_e_read can because we don't
 * have the leaf, and therefore we don't have the depth, and therefore we
 * don't have the length. So we have to just read enough ahead to make up
 * for the loss of information.
 */
static void gfs2_dir_readahead(struct inode *inode, unsigned hsize, u32 index,
			       struct file_ra_state *f_ra)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_glock *gl = ip->i_gl;
	struct buffer_head *bh;
	u64 blocknr = 0, last;
	unsigned count;

	/* First check if we've already read-ahead for the whole range. */
	if (index + MAX_RA_BLOCKS < f_ra->start)
		return;

	f_ra->start = max((pgoff_t)index, f_ra->start);
	for (count = 0; count < MAX_RA_BLOCKS; count++) {
		if (f_ra->start >= hsize) /* if exceeded the hash table */
			break;

		last = blocknr;
		blocknr = be64_to_cpu(ip->i_hash_cache[f_ra->start]);
		f_ra->start++;
		if (blocknr == last)
			continue;

		bh = gfs2_getbuf(gl, blocknr, 1);
		if (trylock_buffer(bh)) {
			if (buffer_uptodate(bh)) {
				unlock_buffer(bh);
				brelse(bh);
				continue;
			}
			bh->b_end_io = end_buffer_read_sync;
			submit_bh(READA | REQ_META, bh);
			continue;
		}
		brelse(bh);
	}
}

/**
 * dir_e_read - Reads the entries from a directory into a filldir buffer
 * @dip: dinode pointer
 * @ctx: actor to feed the entries to
 *
 * Returns: errno
 */

static int dir_e_read(struct inode *inode, struct dir_context *ctx,
		      struct file_ra_state *f_ra)
{
	struct gfs2_inode *dip = GFS2_I(inode);
	u32 hsize, len = 0;
	u32 hash, index;
	__be64 *lp;
	int copied = 0;
	int error = 0;
	unsigned depth = 0;

	hsize = 1 << dip->i_depth;
	hash = gfs2_dir_offset2hash(ctx->pos);
	index = hash >> (32 - dip->i_depth);

	if (dip->i_hash_cache == NULL)
		f_ra->start = 0;
	lp = gfs2_dir_get_hash_table(dip);
	if (IS_ERR(lp))
		return PTR_ERR(lp);

	gfs2_dir_readahead(inode, hsize, index, f_ra);

	while (index < hsize) {
		error = gfs2_dir_read_leaf(inode, ctx,
					   &copied, &depth,
					   be64_to_cpu(lp[index]));
		if (error)
			break;

		len = 1 << (dip->i_depth - depth);
		index = (index & ~(len - 1)) + len;
	}

	if (error > 0)
		error = 0;
	return error;
}

int gfs2_dir_read(struct inode *inode, struct dir_context *ctx,
		  struct file_ra_state *f_ra)
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
		return dir_e_read(inode, ctx, f_ra);

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
		error = do_filldir_main(dip, ctx, darr,
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
 * @dip: The GFS2 dir inode
 * @name: The name we are looking up
 * @fail_on_exist: Fail if the name exists rather than looking it up
 *
 * This routine searches a directory for a file or another directory.
 * Assumes a glock is held on dip.
 *
 * Returns: errno
 */

struct inode *gfs2_dir_search(struct inode *dir, const struct qstr *name,
			      bool fail_on_exist)
{
	struct buffer_head *bh;
	struct gfs2_dirent *dent;
	u64 addr, formal_ino;
	u16 dtype;

	dent = gfs2_dirent_search(dir, name, gfs2_dirent_find, &bh);
	if (dent) {
		if (IS_ERR(dent))
			return ERR_CAST(dent);
		dtype = be16_to_cpu(dent->de_type);
		addr = be64_to_cpu(dent->de_inum.no_addr);
		formal_ino = be64_to_cpu(dent->de_inum.no_formal_ino);
		brelse(bh);
		if (fail_on_exist)
			return ERR_PTR(-EEXIST);
		return gfs2_inode_lookup(dir->i_sb, dtype, addr, formal_ino, 0);
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

/**
 * dir_new_leaf - Add a new leaf onto hash chain
 * @inode: The directory
 * @name: The name we are adding
 *
 * This adds a new dir leaf onto an existing leaf when there is not
 * enough space to add a new dir entry. This is a last resort after
 * we've expanded the hash table to max size and also split existing
 * leaf blocks, so it will only occur for very large directories.
 *
 * The dist parameter is set to 1 for leaf blocks directly attached
 * to the hash table, 2 for one layer of indirection, 3 for two layers
 * etc. We are thus able to tell the difference between an old leaf
 * with dist set to zero (i.e. "don't know") and a new one where we
 * set this information for debug/fsck purposes.
 *
 * Returns: 0 on success, or -ve on error
 */

static int dir_new_leaf(struct inode *inode, const struct qstr *name)
{
	struct buffer_head *bh, *obh;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_leaf *leaf, *oleaf;
	u32 dist = 1;
	int error;
	u32 index;
	u64 bn;

	index = name->hash >> (32 - ip->i_depth);
	error = get_first_leaf(ip, index, &obh);
	if (error)
		return error;
	do {
		dist++;
		oleaf = (struct gfs2_leaf *)obh->b_data;
		bn = be64_to_cpu(oleaf->lf_next);
		if (!bn)
			break;
		brelse(obh);
		error = get_leaf(ip, bn, &obh);
		if (error)
			return error;
	} while(1);

	gfs2_trans_add_meta(ip->i_gl, obh);

	leaf = new_leaf(inode, &bh, be16_to_cpu(oleaf->lf_depth));
	if (!leaf) {
		brelse(obh);
		return -ENOSPC;
	}
	leaf->lf_dist = cpu_to_be32(dist);
	oleaf->lf_next = cpu_to_be64(bh->b_blocknr);
	brelse(bh);
	brelse(obh);

	error = gfs2_meta_inode_buffer(ip, &bh);
	if (error)
		return error;
	gfs2_trans_add_meta(ip->i_gl, bh);
	gfs2_add_inode_blocks(&ip->i_inode, 1);
	gfs2_dinode_out(ip, bh->b_data);
	brelse(bh);
	return 0;
}

static u16 gfs2_inode_ra_len(const struct gfs2_inode *ip)
{
	u64 where = ip->i_no_addr + 1;
	if (ip->i_eattr == where)
		return 1;
	return 0;
}

/**
 * gfs2_dir_add - Add new filename into directory
 * @inode: The directory inode
 * @name: The new name
 * @nip: The GFS2 inode to be linked in to the directory
 * @da: The directory addition info
 *
 * If the call to gfs2_diradd_alloc_required resulted in there being
 * no need to allocate any new directory blocks, then it will contain
 * a pointer to the directory entry and the bh in which it resides. We
 * can use that without having to repeat the search. If there was no
 * free space, then we must now create more space.
 *
 * Returns: 0 on success, error code on failure
 */

int gfs2_dir_add(struct inode *inode, const struct qstr *name,
		 const struct gfs2_inode *nip, struct gfs2_diradd *da)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct buffer_head *bh = da->bh;
	struct gfs2_dirent *dent = da->dent;
	struct timespec tv;
	struct gfs2_leaf *leaf;
	int error;

	while(1) {
		if (da->bh == NULL) {
			dent = gfs2_dirent_search(inode, name,
						  gfs2_dirent_find_space, &bh);
		}
		if (dent) {
			if (IS_ERR(dent))
				return PTR_ERR(dent);
			dent = gfs2_init_dirent(inode, dent, name, bh);
			gfs2_inum_out(nip, dent);
			dent->de_type = cpu_to_be16(IF2DT(nip->i_inode.i_mode));
			dent->de_rahead = cpu_to_be16(gfs2_inode_ra_len(nip));
			tv = CURRENT_TIME;
			if (ip->i_diskflags & GFS2_DIF_EXHASH) {
				leaf = (struct gfs2_leaf *)bh->b_data;
				be16_add_cpu(&leaf->lf_entries, 1);
				leaf->lf_nsec = cpu_to_be32(tv.tv_nsec);
				leaf->lf_sec = cpu_to_be64(tv.tv_sec);
			}
			da->dent = NULL;
			da->bh = NULL;
			brelse(bh);
			ip->i_entries++;
			ip->i_inode.i_mtime = ip->i_inode.i_ctime = tv;
			if (S_ISDIR(nip->i_inode.i_mode))
				inc_nlink(&ip->i_inode);
			mark_inode_dirty(inode);
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

int gfs2_dir_del(struct gfs2_inode *dip, const struct dentry *dentry)
{
	const struct qstr *name = &dentry->d_name;
	struct gfs2_dirent *dent, *prev = NULL;
	struct buffer_head *bh;
	struct timespec tv = CURRENT_TIME;

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
		leaf->lf_nsec = cpu_to_be32(tv.tv_nsec);
		leaf->lf_sec = cpu_to_be64(tv.tv_sec);
	}
	brelse(bh);

	if (!dip->i_entries)
		gfs2_consist_inode(dip);
	dip->i_entries--;
	dip->i_inode.i_mtime = dip->i_inode.i_ctime = tv;
	if (S_ISDIR(dentry->d_inode->i_mode))
		drop_nlink(&dip->i_inode);
	mark_inode_dirty(&dip->i_inode);

	return 0;
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

	gfs2_trans_add_meta(dip->i_gl, bh);
	gfs2_inum_out(nip, dent);
	dent->de_type = cpu_to_be16(new_type);

	if (dip->i_diskflags & GFS2_DIF_EXHASH) {
		brelse(bh);
		error = gfs2_meta_inode_buffer(dip, &bh);
		if (error)
			return error;
		gfs2_trans_add_meta(dip->i_gl, bh);
	}

	dip->i_inode.i_mtime = dip->i_inode.i_ctime = CURRENT_TIME;
	gfs2_dinode_out(dip, bh->b_data);
	brelse(bh);
	return 0;
}

/**
 * leaf_dealloc - Deallocate a directory leaf
 * @dip: the directory
 * @index: the hash table offset in the directory
 * @len: the number of pointers to this leaf
 * @leaf_no: the leaf number
 * @leaf_bh: buffer_head for the starting leaf
 * last_dealloc: 1 if this is the final dealloc for the leaf, else 0
 *
 * Returns: errno
 */

static int leaf_dealloc(struct gfs2_inode *dip, u32 index, u32 len,
			u64 leaf_no, struct buffer_head *leaf_bh,
			int last_dealloc)
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

	error = gfs2_rindex_update(sdp);
	if (error)
		return error;

	memset(&rlist, 0, sizeof(struct gfs2_rgrp_list));

	ht = kzalloc(size, GFP_NOFS | __GFP_NOWARN);
	if (ht == NULL)
		ht = vzalloc(size);
	if (!ht)
		return -ENOMEM;

	error = gfs2_quota_hold(dip, NO_UID_QUOTA_CHANGE, NO_GID_QUOTA_CHANGE);
	if (error)
		goto out;

	/*  Count the number of leaves  */
	bh = leaf_bh;

	for (blk = leaf_no; blk; blk = nblk) {
		if (blk != leaf_no) {
			error = get_leaf(dip, blk, &bh);
			if (error)
				goto out_rlist;
		}
		tmp_leaf = (struct gfs2_leaf *)bh->b_data;
		nblk = be64_to_cpu(tmp_leaf->lf_next);
		if (blk != leaf_no)
			brelse(bh);

		gfs2_rlist_add(dip, &rlist, blk);
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

	bh = leaf_bh;

	for (blk = leaf_no; blk; blk = nblk) {
		if (blk != leaf_no) {
			error = get_leaf(dip, blk, &bh);
			if (error)
				goto out_end_trans;
		}
		tmp_leaf = (struct gfs2_leaf *)bh->b_data;
		nblk = be64_to_cpu(tmp_leaf->lf_next);
		if (blk != leaf_no)
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

	gfs2_trans_add_meta(dip->i_gl, dibh);
	/* On the last dealloc, make this a regular file in case we crash.
	   (We don't want to free these blocks a second time.)  */
	if (last_dealloc)
		dip->i_inode.i_mode = S_IFREG;
	gfs2_dinode_out(dip, dibh->b_data);
	brelse(dibh);

out_end_trans:
	gfs2_trans_end(sdp);
out_rg_gunlock:
	gfs2_glock_dq_m(rlist.rl_rgrps, rlist.rl_ghs);
out_rlist:
	gfs2_rlist_free(&rlist);
	gfs2_quota_unhold(dip);
out:
	kvfree(ht);
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
	struct buffer_head *bh;
	struct gfs2_leaf *leaf;
	u32 hsize, len;
	u32 index = 0, next_index;
	__be64 *lp;
	u64 leaf_no;
	int error = 0, last;

	hsize = 1 << dip->i_depth;

	lp = gfs2_dir_get_hash_table(dip);
	if (IS_ERR(lp))
		return PTR_ERR(lp);

	while (index < hsize) {
		leaf_no = be64_to_cpu(lp[index]);
		if (leaf_no) {
			error = get_leaf(dip, leaf_no, &bh);
			if (error)
				goto out;
			leaf = (struct gfs2_leaf *)bh->b_data;
			len = 1 << (dip->i_depth - be16_to_cpu(leaf->lf_depth));

			next_index = (index & ~(len - 1)) + len;
			last = ((next_index >= hsize) ? 1 : 0);
			error = leaf_dealloc(dip, index, len, leaf_no, bh,
					     last);
			brelse(bh);
			if (error)
				goto out;
			index = next_index;
		} else
			index++;
	}

	if (index != hsize) {
		gfs2_consist_inode(dip);
		error = -EIO;
	}

out:

	return error;
}

/**
 * gfs2_diradd_alloc_required - find if adding entry will require an allocation
 * @ip: the file being written to
 * @filname: the filename that's going to be added
 * @da: The structure to return dir alloc info
 *
 * Returns: 0 if ok, -ve on error
 */

int gfs2_diradd_alloc_required(struct inode *inode, const struct qstr *name,
			       struct gfs2_diradd *da)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	const unsigned int extra = sizeof(struct gfs2_dinode) - sizeof(struct gfs2_leaf);
	struct gfs2_dirent *dent;
	struct buffer_head *bh;

	da->nr_blocks = 0;
	da->bh = NULL;
	da->dent = NULL;

	dent = gfs2_dirent_search(inode, name, gfs2_dirent_find_space, &bh);
	if (!dent) {
		da->nr_blocks = sdp->sd_max_dirres;
		if (!(ip->i_diskflags & GFS2_DIF_EXHASH) &&
		    (GFS2_DIRENT_SIZE(name->len) < extra))
			da->nr_blocks = 1;
		return 0;
	}
	if (IS_ERR(dent))
		return PTR_ERR(dent);

	if (da->save_loc) {
		da->bh = bh;
		da->dent = dent;
	} else {
		brelse(bh);
	}
	return 0;
}

