/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include "gfs2.h"
#include "bmap.h"
#include "inode.h"
#include "jdata.h"
#include "meta_io.h"
#include "trans.h"

int gfs2_jdata_get_buffer(struct gfs2_inode *ip, uint64_t block, int new,
			  struct buffer_head **bhp)
{
	struct buffer_head *bh;
	int error = 0;

	if (new) {
		bh = gfs2_meta_new(ip->i_gl, block);
		gfs2_trans_add_bh(ip->i_gl, bh);
		gfs2_metatype_set(bh, GFS2_METATYPE_JD, GFS2_FORMAT_JD);
		gfs2_buffer_clear_tail(bh, sizeof(struct gfs2_meta_header));
	} else {
		error = gfs2_meta_read(ip->i_gl, block,
				       DIO_START | DIO_WAIT, &bh);
		if (error)
			return error;
		if (gfs2_metatype_check(ip->i_sbd, bh, GFS2_METATYPE_JD)) {
			brelse(bh);
			return -EIO;
		}
	}

	*bhp = bh;

	return 0;
}

/**
 * gfs2_copy2mem - Trivial copy function for gfs2_jdata_read()
 * @bh: The buffer to copy from, or NULL meaning zero the buffer
 * @buf: The buffer to copy/zero
 * @offset: The offset in the buffer to copy from
 * @size: The amount of data to copy/zero
 *
 * Returns: errno
 */

int gfs2_copy2mem(struct buffer_head *bh, char **buf, unsigned int offset,
		  unsigned int size)
{
	if (bh)
		memcpy(*buf, bh->b_data + offset, size);
	else
		memset(*buf, 0, size);
	*buf += size;
	return 0;
}

/**
 * gfs2_copy2user - Copy bytes to user space for gfs2_jdata_read()
 * @bh: The buffer
 * @buf: The destination of the data
 * @offset: The offset into the buffer
 * @size: The amount of data to copy
 *
 * Returns: errno
 */

int gfs2_copy2user(struct buffer_head *bh, char **buf, unsigned int offset,
		   unsigned int size)
{
	int error;

	if (bh)
		error = copy_to_user(*buf, bh->b_data + offset, size);
	else
		error = clear_user(*buf, size);

	if (error)
		error = -EFAULT;
	else
		*buf += size;

	return error;
}

static int jdata_read_stuffed(struct gfs2_inode *ip, char *buf,
			      unsigned int offset, unsigned int size,
			      read_copy_fn_t copy_fn)
{
	struct buffer_head *dibh;
	int error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		error = copy_fn(dibh, &buf,
				offset + sizeof(struct gfs2_dinode), size);
		brelse(dibh);
	}

	return (error) ? error : size;
}

/**
 * gfs2_jdata_read - Read a jdata file
 * @ip: The GFS2 Inode
 * @buf: The buffer to place result into
 * @offset: File offset to begin jdata_readng from
 * @size: Amount of data to transfer
 * @copy_fn: Function to actually perform the copy
 *
 * The @copy_fn only copies a maximum of a single block at once so
 * we are safe calling it with int arguments. It is done so that
 * we don't needlessly put 64bit arguments on the stack and it
 * also makes the code in the @copy_fn nicer too.
 *
 * Returns: The amount of data actually copied or the error
 */

int gfs2_jdata_read(struct gfs2_inode *ip, char __user *buf, uint64_t offset,
		    unsigned int size, read_copy_fn_t copy_fn)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	uint64_t lblock, dblock;
	uint32_t extlen = 0;
	unsigned int o;
	int copied = 0;
	int error = 0;

	if (offset >= ip->i_di.di_size)
		return 0;

	if ((offset + size) > ip->i_di.di_size)
		size = ip->i_di.di_size - offset;

	if (!size)
		return 0;

	if (gfs2_is_stuffed(ip))
		return jdata_read_stuffed(ip, buf, (unsigned int)offset, size,
					  copy_fn);

	if (gfs2_assert_warn(sdp, gfs2_is_jdata(ip)))
		return -EINVAL;

	lblock = offset;
	o = do_div(lblock, sdp->sd_jbsize) +
		sizeof(struct gfs2_meta_header);

	while (copied < size) {
		unsigned int amount;
		struct buffer_head *bh;
		int new;

		amount = size - copied;
		if (amount > sdp->sd_sb.sb_bsize - o)
			amount = sdp->sd_sb.sb_bsize - o;

		if (!extlen) {
			new = 0;
			error = gfs2_block_map(ip, lblock, &new,
					       &dblock, &extlen);
			if (error)
				goto fail;
		}

		if (extlen > 1)
			gfs2_meta_ra(ip->i_gl, dblock, extlen);

		if (dblock) {
			error = gfs2_jdata_get_buffer(ip, dblock, new, &bh);
			if (error)
				goto fail;
			dblock++;
			extlen--;
		} else
			bh = NULL;

		error = copy_fn(bh, &buf, o, amount);
		brelse(bh);
		if (error)
			goto fail;

		copied += amount;
		lblock++;

		o = sizeof(struct gfs2_meta_header);
	}

	return copied;

 fail:
	return (copied) ? copied : error;
}

/**
 * gfs2_copy_from_mem - Trivial copy function for gfs2_jdata_write()
 * @bh: The buffer to copy to or clear
 * @buf: The buffer to copy from
 * @offset: The offset in the buffer to write to
 * @size: The amount of data to write
 *
 * Returns: errno
 */

int gfs2_copy_from_mem(struct gfs2_inode *ip, struct buffer_head *bh,
		       const char **buf, unsigned int offset, unsigned int size)
{
	gfs2_trans_add_bh(ip->i_gl, bh);
	memcpy(bh->b_data + offset, *buf, size);

	*buf += size;

	return 0;
}

/**
 * gfs2_copy_from_user - Copy bytes from user space for gfs2_jdata_write()
 * @bh: The buffer to copy to or clear
 * @buf: The buffer to copy from
 * @offset: The offset in the buffer to write to
 * @size: The amount of data to write
 *
 * Returns: errno
 */

int gfs2_copy_from_user(struct gfs2_inode *ip, struct buffer_head *bh,
			const char __user **buf, unsigned int offset, unsigned int size)
{
	int error = 0;

	gfs2_trans_add_bh(ip->i_gl, bh);
	if (copy_from_user(bh->b_data + offset, *buf, size))
		error = -EFAULT;
	else
		*buf += size;

	return error;
}

static int jdata_write_stuffed(struct gfs2_inode *ip, char *buf,
			       unsigned int offset, unsigned int size,
			       write_copy_fn_t copy_fn)
{
	struct buffer_head *dibh;
	int error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	error = copy_fn(ip,
			dibh, &buf,
			offset + sizeof(struct gfs2_dinode), size);
	if (!error) {
		if (ip->i_di.di_size < offset + size)
			ip->i_di.di_size = offset + size;
		ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();
		gfs2_dinode_out(&ip->i_di, dibh->b_data);
	}

	brelse(dibh);

	return (error) ? error : size;
}

/**
 * gfs2_jdata_write - Write bytes to a file
 * @ip: The GFS2 inode
 * @buf: The buffer containing information to be written
 * @offset: The file offset to start writing at
 * @size: The amount of data to write
 * @copy_fn: Function to do the actual copying
 *
 * Returns: The number of bytes correctly written or error code
 */

int gfs2_jdata_write(struct gfs2_inode *ip, const char __user *buf, uint64_t offset,
		     unsigned int size, write_copy_fn_t copy_fn)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct buffer_head *dibh;
	uint64_t lblock, dblock;
	uint32_t extlen = 0;
	unsigned int o;
	int copied = 0;
	int error = 0;

	if (!size)
		return 0;

	if (gfs2_is_stuffed(ip) &&
	    offset + size <= sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode))
		return jdata_write_stuffed(ip, buf, (unsigned int)offset, size,
					   copy_fn);

	if (gfs2_assert_warn(sdp, gfs2_is_jdata(ip)))
		return -EINVAL;

	if (gfs2_is_stuffed(ip)) {
		error = gfs2_unstuff_dinode(ip, NULL, NULL);
		if (error)
			return error;
	}

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
			new = 1;
			error = gfs2_block_map(ip, lblock, &new,
					       &dblock, &extlen);
			if (error)
				goto fail;
			error = -EIO;
			if (gfs2_assert_withdraw(sdp, dblock))
				goto fail;
		}

		error = gfs2_jdata_get_buffer(ip, dblock,
				(amount == sdp->sd_jbsize) ? 1 : new,
				&bh);
		if (error)
			goto fail;

		error = copy_fn(ip, bh, &buf, o, amount);
		brelse(bh);
		if (error)
			goto fail;

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

	if (ip->i_di.di_size < offset + copied)
		ip->i_di.di_size = offset + copied;
	ip->i_di.di_mtime = ip->i_di.di_ctime = get_seconds();

	gfs2_trans_add_bh(ip->i_gl, dibh);
	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	return copied;

 fail:
	if (copied)
		goto out;
	return error;
}

