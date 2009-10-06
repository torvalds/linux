/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 * Copyright (C) 2006, 2007 University of Szeged, Hungary
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 *          Zoltan Sogor
 */

/*
 * This file implements UBIFS I/O subsystem which provides various I/O-related
 * helper functions (reading/writing/checking/validating nodes) and implements
 * write-buffering support. Write buffers help to save space which otherwise
 * would have been wasted for padding to the nearest minimal I/O unit boundary.
 * Instead, data first goes to the write-buffer and is flushed when the
 * buffer is full or when it is not used for some time (by timer). This is
 * similar to the mechanism is used by JFFS2.
 *
 * Write-buffers are defined by 'struct ubifs_wbuf' objects and protected by
 * mutexes defined inside these objects. Since sometimes upper-level code
 * has to lock the write-buffer (e.g. journal space reservation code), many
 * functions related to write-buffers have "nolock" suffix which means that the
 * caller has to lock the write-buffer before calling this function.
 *
 * UBIFS stores nodes at 64 bit-aligned addresses. If the node length is not
 * aligned, UBIFS starts the next node from the aligned address, and the padded
 * bytes may contain any rubbish. In other words, UBIFS does not put padding
 * bytes in those small gaps. Common headers of nodes store real node lengths,
 * not aligned lengths. Indexing nodes also store real lengths in branches.
 *
 * UBIFS uses padding when it pads to the next min. I/O unit. In this case it
 * uses padding nodes or padding bytes, if the padding node does not fit.
 *
 * All UBIFS nodes are protected by CRC checksums and UBIFS checks all nodes
 * every time they are read from the flash media.
 */

#include <linux/crc32.h>
#include "ubifs.h"

/**
 * ubifs_ro_mode - switch UBIFS to read read-only mode.
 * @c: UBIFS file-system description object
 * @err: error code which is the reason of switching to R/O mode
 */
void ubifs_ro_mode(struct ubifs_info *c, int err)
{
	if (!c->ro_media) {
		c->ro_media = 1;
		c->no_chk_data_crc = 0;
		ubifs_warn("switched to read-only mode, error %d", err);
		dbg_dump_stack();
	}
}

/**
 * ubifs_check_node - check node.
 * @c: UBIFS file-system description object
 * @buf: node to check
 * @lnum: logical eraseblock number
 * @offs: offset within the logical eraseblock
 * @quiet: print no messages
 * @must_chk_crc: indicates whether to always check the CRC
 *
 * This function checks node magic number and CRC checksum. This function also
 * validates node length to prevent UBIFS from becoming crazy when an attacker
 * feeds it a file-system image with incorrect nodes. For example, too large
 * node length in the common header could cause UBIFS to read memory outside of
 * allocated buffer when checking the CRC checksum.
 *
 * This function may skip data nodes CRC checking if @c->no_chk_data_crc is
 * true, which is controlled by corresponding UBIFS mount option. However, if
 * @must_chk_crc is true, then @c->no_chk_data_crc is ignored and CRC is
 * checked. Similarly, if @c->always_chk_crc is true, @c->no_chk_data_crc is
 * ignored and CRC is checked.
 *
 * This function returns zero in case of success and %-EUCLEAN in case of bad
 * CRC or magic.
 */
int ubifs_check_node(const struct ubifs_info *c, const void *buf, int lnum,
		     int offs, int quiet, int must_chk_crc)
{
	int err = -EINVAL, type, node_len;
	uint32_t crc, node_crc, magic;
	const struct ubifs_ch *ch = buf;

	ubifs_assert(lnum >= 0 && lnum < c->leb_cnt && offs >= 0);
	ubifs_assert(!(offs & 7) && offs < c->leb_size);

	magic = le32_to_cpu(ch->magic);
	if (magic != UBIFS_NODE_MAGIC) {
		if (!quiet)
			ubifs_err("bad magic %#08x, expected %#08x",
				  magic, UBIFS_NODE_MAGIC);
		err = -EUCLEAN;
		goto out;
	}

	type = ch->node_type;
	if (type < 0 || type >= UBIFS_NODE_TYPES_CNT) {
		if (!quiet)
			ubifs_err("bad node type %d", type);
		goto out;
	}

	node_len = le32_to_cpu(ch->len);
	if (node_len + offs > c->leb_size)
		goto out_len;

	if (c->ranges[type].max_len == 0) {
		if (node_len != c->ranges[type].len)
			goto out_len;
	} else if (node_len < c->ranges[type].min_len ||
		   node_len > c->ranges[type].max_len)
		goto out_len;

	if (!must_chk_crc && type == UBIFS_DATA_NODE && !c->always_chk_crc &&
	     c->no_chk_data_crc)
		return 0;

	crc = crc32(UBIFS_CRC32_INIT, buf + 8, node_len - 8);
	node_crc = le32_to_cpu(ch->crc);
	if (crc != node_crc) {
		if (!quiet)
			ubifs_err("bad CRC: calculated %#08x, read %#08x",
				  crc, node_crc);
		err = -EUCLEAN;
		goto out;
	}

	return 0;

out_len:
	if (!quiet)
		ubifs_err("bad node length %d", node_len);
out:
	if (!quiet) {
		ubifs_err("bad node at LEB %d:%d", lnum, offs);
		dbg_dump_node(c, buf);
		dbg_dump_stack();
	}
	return err;
}

/**
 * ubifs_pad - pad flash space.
 * @c: UBIFS file-system description object
 * @buf: buffer to put padding to
 * @pad: how many bytes to pad
 *
 * The flash media obliges us to write only in chunks of %c->min_io_size and
 * when we have to write less data we add padding node to the write-buffer and
 * pad it to the next minimal I/O unit's boundary. Padding nodes help when the
 * media is being scanned. If the amount of wasted space is not enough to fit a
 * padding node which takes %UBIFS_PAD_NODE_SZ bytes, we write padding bytes
 * pattern (%UBIFS_PADDING_BYTE).
 *
 * Padding nodes are also used to fill gaps when the "commit-in-gaps" method is
 * used.
 */
void ubifs_pad(const struct ubifs_info *c, void *buf, int pad)
{
	uint32_t crc;

	ubifs_assert(pad >= 0 && !(pad & 7));

	if (pad >= UBIFS_PAD_NODE_SZ) {
		struct ubifs_ch *ch = buf;
		struct ubifs_pad_node *pad_node = buf;

		ch->magic = cpu_to_le32(UBIFS_NODE_MAGIC);
		ch->node_type = UBIFS_PAD_NODE;
		ch->group_type = UBIFS_NO_NODE_GROUP;
		ch->padding[0] = ch->padding[1] = 0;
		ch->sqnum = 0;
		ch->len = cpu_to_le32(UBIFS_PAD_NODE_SZ);
		pad -= UBIFS_PAD_NODE_SZ;
		pad_node->pad_len = cpu_to_le32(pad);
		crc = crc32(UBIFS_CRC32_INIT, buf + 8, UBIFS_PAD_NODE_SZ - 8);
		ch->crc = cpu_to_le32(crc);
		memset(buf + UBIFS_PAD_NODE_SZ, 0, pad);
	} else if (pad > 0)
		/* Too little space, padding node won't fit */
		memset(buf, UBIFS_PADDING_BYTE, pad);
}

/**
 * next_sqnum - get next sequence number.
 * @c: UBIFS file-system description object
 */
static unsigned long long next_sqnum(struct ubifs_info *c)
{
	unsigned long long sqnum;

	spin_lock(&c->cnt_lock);
	sqnum = ++c->max_sqnum;
	spin_unlock(&c->cnt_lock);

	if (unlikely(sqnum >= SQNUM_WARN_WATERMARK)) {
		if (sqnum >= SQNUM_WATERMARK) {
			ubifs_err("sequence number overflow %llu, end of life",
				  sqnum);
			ubifs_ro_mode(c, -EINVAL);
		}
		ubifs_warn("running out of sequence numbers, end of life soon");
	}

	return sqnum;
}

/**
 * ubifs_prepare_node - prepare node to be written to flash.
 * @c: UBIFS file-system description object
 * @node: the node to pad
 * @len: node length
 * @pad: if the buffer has to be padded
 *
 * This function prepares node at @node to be written to the media - it
 * calculates node CRC, fills the common header, and adds proper padding up to
 * the next minimum I/O unit if @pad is not zero.
 */
void ubifs_prepare_node(struct ubifs_info *c, void *node, int len, int pad)
{
	uint32_t crc;
	struct ubifs_ch *ch = node;
	unsigned long long sqnum = next_sqnum(c);

	ubifs_assert(len >= UBIFS_CH_SZ);

	ch->magic = cpu_to_le32(UBIFS_NODE_MAGIC);
	ch->len = cpu_to_le32(len);
	ch->group_type = UBIFS_NO_NODE_GROUP;
	ch->sqnum = cpu_to_le64(sqnum);
	ch->padding[0] = ch->padding[1] = 0;
	crc = crc32(UBIFS_CRC32_INIT, node + 8, len - 8);
	ch->crc = cpu_to_le32(crc);

	if (pad) {
		len = ALIGN(len, 8);
		pad = ALIGN(len, c->min_io_size) - len;
		ubifs_pad(c, node + len, pad);
	}
}

/**
 * ubifs_prep_grp_node - prepare node of a group to be written to flash.
 * @c: UBIFS file-system description object
 * @node: the node to pad
 * @len: node length
 * @last: indicates the last node of the group
 *
 * This function prepares node at @node to be written to the media - it
 * calculates node CRC and fills the common header.
 */
void ubifs_prep_grp_node(struct ubifs_info *c, void *node, int len, int last)
{
	uint32_t crc;
	struct ubifs_ch *ch = node;
	unsigned long long sqnum = next_sqnum(c);

	ubifs_assert(len >= UBIFS_CH_SZ);

	ch->magic = cpu_to_le32(UBIFS_NODE_MAGIC);
	ch->len = cpu_to_le32(len);
	if (last)
		ch->group_type = UBIFS_LAST_OF_NODE_GROUP;
	else
		ch->group_type = UBIFS_IN_NODE_GROUP;
	ch->sqnum = cpu_to_le64(sqnum);
	ch->padding[0] = ch->padding[1] = 0;
	crc = crc32(UBIFS_CRC32_INIT, node + 8, len - 8);
	ch->crc = cpu_to_le32(crc);
}

/**
 * wbuf_timer_callback - write-buffer timer callback function.
 * @data: timer data (write-buffer descriptor)
 *
 * This function is called when the write-buffer timer expires.
 */
static enum hrtimer_restart wbuf_timer_callback_nolock(struct hrtimer *timer)
{
	struct ubifs_wbuf *wbuf = container_of(timer, struct ubifs_wbuf, timer);

	dbg_io("jhead %s", dbg_jhead(wbuf->jhead));
	wbuf->need_sync = 1;
	wbuf->c->need_wbuf_sync = 1;
	ubifs_wake_up_bgt(wbuf->c);
	return HRTIMER_NORESTART;
}

/**
 * new_wbuf_timer - start new write-buffer timer.
 * @wbuf: write-buffer descriptor
 */
static void new_wbuf_timer_nolock(struct ubifs_wbuf *wbuf)
{
	ubifs_assert(!hrtimer_active(&wbuf->timer));

	if (wbuf->no_timer)
		return;
	dbg_io("set timer for jhead %s, %llu-%llu millisecs",
	       dbg_jhead(wbuf->jhead),
	       div_u64(ktime_to_ns(wbuf->softlimit), USEC_PER_SEC),
	       div_u64(ktime_to_ns(wbuf->softlimit) + wbuf->delta,
		       USEC_PER_SEC));
	hrtimer_start_range_ns(&wbuf->timer, wbuf->softlimit, wbuf->delta,
			       HRTIMER_MODE_REL);
}

/**
 * cancel_wbuf_timer - cancel write-buffer timer.
 * @wbuf: write-buffer descriptor
 */
static void cancel_wbuf_timer_nolock(struct ubifs_wbuf *wbuf)
{
	if (wbuf->no_timer)
		return;
	wbuf->need_sync = 0;
	hrtimer_cancel(&wbuf->timer);
}

/**
 * ubifs_wbuf_sync_nolock - synchronize write-buffer.
 * @wbuf: write-buffer to synchronize
 *
 * This function synchronizes write-buffer @buf and returns zero in case of
 * success or a negative error code in case of failure.
 */
int ubifs_wbuf_sync_nolock(struct ubifs_wbuf *wbuf)
{
	struct ubifs_info *c = wbuf->c;
	int err, dirt;

	cancel_wbuf_timer_nolock(wbuf);
	if (!wbuf->used || wbuf->lnum == -1)
		/* Write-buffer is empty or not seeked */
		return 0;

	dbg_io("LEB %d:%d, %d bytes, jhead %s",
	       wbuf->lnum, wbuf->offs, wbuf->used, dbg_jhead(wbuf->jhead));
	ubifs_assert(!(c->vfs_sb->s_flags & MS_RDONLY));
	ubifs_assert(!(wbuf->avail & 7));
	ubifs_assert(wbuf->offs + c->min_io_size <= c->leb_size);

	if (c->ro_media)
		return -EROFS;

	ubifs_pad(c, wbuf->buf + wbuf->used, wbuf->avail);
	err = ubi_leb_write(c->ubi, wbuf->lnum, wbuf->buf, wbuf->offs,
			    c->min_io_size, wbuf->dtype);
	if (err) {
		ubifs_err("cannot write %d bytes to LEB %d:%d",
			  c->min_io_size, wbuf->lnum, wbuf->offs);
		dbg_dump_stack();
		return err;
	}

	dirt = wbuf->avail;

	spin_lock(&wbuf->lock);
	wbuf->offs += c->min_io_size;
	wbuf->avail = c->min_io_size;
	wbuf->used = 0;
	wbuf->next_ino = 0;
	spin_unlock(&wbuf->lock);

	if (wbuf->sync_callback)
		err = wbuf->sync_callback(c, wbuf->lnum,
					  c->leb_size - wbuf->offs, dirt);
	return err;
}

/**
 * ubifs_wbuf_seek_nolock - seek write-buffer.
 * @wbuf: write-buffer
 * @lnum: logical eraseblock number to seek to
 * @offs: logical eraseblock offset to seek to
 * @dtype: data type
 *
 * This function targets the write-buffer to logical eraseblock @lnum:@offs.
 * The write-buffer is synchronized if it is not empty. Returns zero in case of
 * success and a negative error code in case of failure.
 */
int ubifs_wbuf_seek_nolock(struct ubifs_wbuf *wbuf, int lnum, int offs,
			   int dtype)
{
	const struct ubifs_info *c = wbuf->c;

	dbg_io("LEB %d:%d, jhead %s", lnum, offs, dbg_jhead(wbuf->jhead));
	ubifs_assert(lnum >= 0 && lnum < c->leb_cnt);
	ubifs_assert(offs >= 0 && offs <= c->leb_size);
	ubifs_assert(offs % c->min_io_size == 0 && !(offs & 7));
	ubifs_assert(lnum != wbuf->lnum);

	if (wbuf->used > 0) {
		int err = ubifs_wbuf_sync_nolock(wbuf);

		if (err)
			return err;
	}

	spin_lock(&wbuf->lock);
	wbuf->lnum = lnum;
	wbuf->offs = offs;
	wbuf->avail = c->min_io_size;
	wbuf->used = 0;
	spin_unlock(&wbuf->lock);
	wbuf->dtype = dtype;

	return 0;
}

/**
 * ubifs_bg_wbufs_sync - synchronize write-buffers.
 * @c: UBIFS file-system description object
 *
 * This function is called by background thread to synchronize write-buffers.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 */
int ubifs_bg_wbufs_sync(struct ubifs_info *c)
{
	int err, i;

	if (!c->need_wbuf_sync)
		return 0;
	c->need_wbuf_sync = 0;

	if (c->ro_media) {
		err = -EROFS;
		goto out_timers;
	}

	dbg_io("synchronize");
	for (i = 0; i < c->jhead_cnt; i++) {
		struct ubifs_wbuf *wbuf = &c->jheads[i].wbuf;

		cond_resched();

		/*
		 * If the mutex is locked then wbuf is being changed, so
		 * synchronization is not necessary.
		 */
		if (mutex_is_locked(&wbuf->io_mutex))
			continue;

		mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
		if (!wbuf->need_sync) {
			mutex_unlock(&wbuf->io_mutex);
			continue;
		}

		err = ubifs_wbuf_sync_nolock(wbuf);
		mutex_unlock(&wbuf->io_mutex);
		if (err) {
			ubifs_err("cannot sync write-buffer, error %d", err);
			ubifs_ro_mode(c, err);
			goto out_timers;
		}
	}

	return 0;

out_timers:
	/* Cancel all timers to prevent repeated errors */
	for (i = 0; i < c->jhead_cnt; i++) {
		struct ubifs_wbuf *wbuf = &c->jheads[i].wbuf;

		mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
		cancel_wbuf_timer_nolock(wbuf);
		mutex_unlock(&wbuf->io_mutex);
	}
	return err;
}

/**
 * ubifs_wbuf_write_nolock - write data to flash via write-buffer.
 * @wbuf: write-buffer
 * @buf: node to write
 * @len: node length
 *
 * This function writes data to flash via write-buffer @wbuf. This means that
 * the last piece of the node won't reach the flash media immediately if it
 * does not take whole minimal I/O unit. Instead, the node will sit in RAM
 * until the write-buffer is synchronized (e.g., by timer).
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure. If the node cannot be written because there is no more
 * space in this logical eraseblock, %-ENOSPC is returned.
 */
int ubifs_wbuf_write_nolock(struct ubifs_wbuf *wbuf, void *buf, int len)
{
	struct ubifs_info *c = wbuf->c;
	int err, written, n, aligned_len = ALIGN(len, 8), offs;

	dbg_io("%d bytes (%s) to jhead %s wbuf at LEB %d:%d", len,
	       dbg_ntype(((struct ubifs_ch *)buf)->node_type),
	       dbg_jhead(wbuf->jhead), wbuf->lnum, wbuf->offs + wbuf->used);
	ubifs_assert(len > 0 && wbuf->lnum >= 0 && wbuf->lnum < c->leb_cnt);
	ubifs_assert(wbuf->offs >= 0 && wbuf->offs % c->min_io_size == 0);
	ubifs_assert(!(wbuf->offs & 7) && wbuf->offs <= c->leb_size);
	ubifs_assert(wbuf->avail > 0 && wbuf->avail <= c->min_io_size);
	ubifs_assert(mutex_is_locked(&wbuf->io_mutex));

	if (c->leb_size - wbuf->offs - wbuf->used < aligned_len) {
		err = -ENOSPC;
		goto out;
	}

	cancel_wbuf_timer_nolock(wbuf);

	if (c->ro_media)
		return -EROFS;

	if (aligned_len <= wbuf->avail) {
		/*
		 * The node is not very large and fits entirely within
		 * write-buffer.
		 */
		memcpy(wbuf->buf + wbuf->used, buf, len);

		if (aligned_len == wbuf->avail) {
			dbg_io("flush jhead %s wbuf to LEB %d:%d",
			       dbg_jhead(wbuf->jhead), wbuf->lnum, wbuf->offs);
			err = ubi_leb_write(c->ubi, wbuf->lnum, wbuf->buf,
					    wbuf->offs, c->min_io_size,
					    wbuf->dtype);
			if (err)
				goto out;

			spin_lock(&wbuf->lock);
			wbuf->offs += c->min_io_size;
			wbuf->avail = c->min_io_size;
			wbuf->used = 0;
			wbuf->next_ino = 0;
			spin_unlock(&wbuf->lock);
		} else {
			spin_lock(&wbuf->lock);
			wbuf->avail -= aligned_len;
			wbuf->used += aligned_len;
			spin_unlock(&wbuf->lock);
		}

		goto exit;
	}

	/*
	 * The node is large enough and does not fit entirely within current
	 * minimal I/O unit. We have to fill and flush write-buffer and switch
	 * to the next min. I/O unit.
	 */
	dbg_io("flush jhead %s wbuf to LEB %d:%d",
	       dbg_jhead(wbuf->jhead), wbuf->lnum, wbuf->offs);
	memcpy(wbuf->buf + wbuf->used, buf, wbuf->avail);
	err = ubi_leb_write(c->ubi, wbuf->lnum, wbuf->buf, wbuf->offs,
			    c->min_io_size, wbuf->dtype);
	if (err)
		goto out;

	offs = wbuf->offs + c->min_io_size;
	len -= wbuf->avail;
	aligned_len -= wbuf->avail;
	written = wbuf->avail;

	/*
	 * The remaining data may take more whole min. I/O units, so write the
	 * remains multiple to min. I/O unit size directly to the flash media.
	 * We align node length to 8-byte boundary because we anyway flash wbuf
	 * if the remaining space is less than 8 bytes.
	 */
	n = aligned_len >> c->min_io_shift;
	if (n) {
		n <<= c->min_io_shift;
		dbg_io("write %d bytes to LEB %d:%d", n, wbuf->lnum, offs);
		err = ubi_leb_write(c->ubi, wbuf->lnum, buf + written, offs, n,
				    wbuf->dtype);
		if (err)
			goto out;
		offs += n;
		aligned_len -= n;
		len -= n;
		written += n;
	}

	spin_lock(&wbuf->lock);
	if (aligned_len)
		/*
		 * And now we have what's left and what does not take whole
		 * min. I/O unit, so write it to the write-buffer and we are
		 * done.
		 */
		memcpy(wbuf->buf, buf + written, len);

	wbuf->offs = offs;
	wbuf->used = aligned_len;
	wbuf->avail = c->min_io_size - aligned_len;
	wbuf->next_ino = 0;
	spin_unlock(&wbuf->lock);

exit:
	if (wbuf->sync_callback) {
		int free = c->leb_size - wbuf->offs - wbuf->used;

		err = wbuf->sync_callback(c, wbuf->lnum, free, 0);
		if (err)
			goto out;
	}

	if (wbuf->used)
		new_wbuf_timer_nolock(wbuf);

	return 0;

out:
	ubifs_err("cannot write %d bytes to LEB %d:%d, error %d",
		  len, wbuf->lnum, wbuf->offs, err);
	dbg_dump_node(c, buf);
	dbg_dump_stack();
	dbg_dump_leb(c, wbuf->lnum);
	return err;
}

/**
 * ubifs_write_node - write node to the media.
 * @c: UBIFS file-system description object
 * @buf: the node to write
 * @len: node length
 * @lnum: logical eraseblock number
 * @offs: offset within the logical eraseblock
 * @dtype: node life-time hint (%UBI_LONGTERM, %UBI_SHORTTERM, %UBI_UNKNOWN)
 *
 * This function automatically fills node magic number, assigns sequence
 * number, and calculates node CRC checksum. The length of the @buf buffer has
 * to be aligned to the minimal I/O unit size. This function automatically
 * appends padding node and padding bytes if needed. Returns zero in case of
 * success and a negative error code in case of failure.
 */
int ubifs_write_node(struct ubifs_info *c, void *buf, int len, int lnum,
		     int offs, int dtype)
{
	int err, buf_len = ALIGN(len, c->min_io_size);

	dbg_io("LEB %d:%d, %s, length %d (aligned %d)",
	       lnum, offs, dbg_ntype(((struct ubifs_ch *)buf)->node_type), len,
	       buf_len);
	ubifs_assert(lnum >= 0 && lnum < c->leb_cnt && offs >= 0);
	ubifs_assert(offs % c->min_io_size == 0 && offs < c->leb_size);

	if (c->ro_media)
		return -EROFS;

	ubifs_prepare_node(c, buf, len, 1);
	err = ubi_leb_write(c->ubi, lnum, buf, offs, buf_len, dtype);
	if (err) {
		ubifs_err("cannot write %d bytes to LEB %d:%d, error %d",
			  buf_len, lnum, offs, err);
		dbg_dump_node(c, buf);
		dbg_dump_stack();
	}

	return err;
}

/**
 * ubifs_read_node_wbuf - read node from the media or write-buffer.
 * @wbuf: wbuf to check for un-written data
 * @buf: buffer to read to
 * @type: node type
 * @len: node length
 * @lnum: logical eraseblock number
 * @offs: offset within the logical eraseblock
 *
 * This function reads a node of known type and length, checks it and stores
 * in @buf. If the node partially or fully sits in the write-buffer, this
 * function takes data from the buffer, otherwise it reads the flash media.
 * Returns zero in case of success, %-EUCLEAN if CRC mismatched and a negative
 * error code in case of failure.
 */
int ubifs_read_node_wbuf(struct ubifs_wbuf *wbuf, void *buf, int type, int len,
			 int lnum, int offs)
{
	const struct ubifs_info *c = wbuf->c;
	int err, rlen, overlap;
	struct ubifs_ch *ch = buf;

	dbg_io("LEB %d:%d, %s, length %d, jhead %s", lnum, offs,
	       dbg_ntype(type), len, dbg_jhead(wbuf->jhead));
	ubifs_assert(wbuf && lnum >= 0 && lnum < c->leb_cnt && offs >= 0);
	ubifs_assert(!(offs & 7) && offs < c->leb_size);
	ubifs_assert(type >= 0 && type < UBIFS_NODE_TYPES_CNT);

	spin_lock(&wbuf->lock);
	overlap = (lnum == wbuf->lnum && offs + len > wbuf->offs);
	if (!overlap) {
		/* We may safely unlock the write-buffer and read the data */
		spin_unlock(&wbuf->lock);
		return ubifs_read_node(c, buf, type, len, lnum, offs);
	}

	/* Don't read under wbuf */
	rlen = wbuf->offs - offs;
	if (rlen < 0)
		rlen = 0;

	/* Copy the rest from the write-buffer */
	memcpy(buf + rlen, wbuf->buf + offs + rlen - wbuf->offs, len - rlen);
	spin_unlock(&wbuf->lock);

	if (rlen > 0) {
		/* Read everything that goes before write-buffer */
		err = ubi_read(c->ubi, lnum, buf, offs, rlen);
		if (err && err != -EBADMSG) {
			ubifs_err("failed to read node %d from LEB %d:%d, "
				  "error %d", type, lnum, offs, err);
			dbg_dump_stack();
			return err;
		}
	}

	if (type != ch->node_type) {
		ubifs_err("bad node type (%d but expected %d)",
			  ch->node_type, type);
		goto out;
	}

	err = ubifs_check_node(c, buf, lnum, offs, 0, 0);
	if (err) {
		ubifs_err("expected node type %d", type);
		return err;
	}

	rlen = le32_to_cpu(ch->len);
	if (rlen != len) {
		ubifs_err("bad node length %d, expected %d", rlen, len);
		goto out;
	}

	return 0;

out:
	ubifs_err("bad node at LEB %d:%d", lnum, offs);
	dbg_dump_node(c, buf);
	dbg_dump_stack();
	return -EINVAL;
}

/**
 * ubifs_read_node - read node.
 * @c: UBIFS file-system description object
 * @buf: buffer to read to
 * @type: node type
 * @len: node length (not aligned)
 * @lnum: logical eraseblock number
 * @offs: offset within the logical eraseblock
 *
 * This function reads a node of known type and and length, checks it and
 * stores in @buf. Returns zero in case of success, %-EUCLEAN if CRC mismatched
 * and a negative error code in case of failure.
 */
int ubifs_read_node(const struct ubifs_info *c, void *buf, int type, int len,
		    int lnum, int offs)
{
	int err, l;
	struct ubifs_ch *ch = buf;

	dbg_io("LEB %d:%d, %s, length %d", lnum, offs, dbg_ntype(type), len);
	ubifs_assert(lnum >= 0 && lnum < c->leb_cnt && offs >= 0);
	ubifs_assert(len >= UBIFS_CH_SZ && offs + len <= c->leb_size);
	ubifs_assert(!(offs & 7) && offs < c->leb_size);
	ubifs_assert(type >= 0 && type < UBIFS_NODE_TYPES_CNT);

	err = ubi_read(c->ubi, lnum, buf, offs, len);
	if (err && err != -EBADMSG) {
		ubifs_err("cannot read node %d from LEB %d:%d, error %d",
			  type, lnum, offs, err);
		return err;
	}

	if (type != ch->node_type) {
		ubifs_err("bad node type (%d but expected %d)",
			  ch->node_type, type);
		goto out;
	}

	err = ubifs_check_node(c, buf, lnum, offs, 0, 0);
	if (err) {
		ubifs_err("expected node type %d", type);
		return err;
	}

	l = le32_to_cpu(ch->len);
	if (l != len) {
		ubifs_err("bad node length %d, expected %d", l, len);
		goto out;
	}

	return 0;

out:
	ubifs_err("bad node at LEB %d:%d", lnum, offs);
	dbg_dump_node(c, buf);
	dbg_dump_stack();
	return -EINVAL;
}

/**
 * ubifs_wbuf_init - initialize write-buffer.
 * @c: UBIFS file-system description object
 * @wbuf: write-buffer to initialize
 *
 * This function initializes write-buffer. Returns zero in case of success
 * %-ENOMEM in case of failure.
 */
int ubifs_wbuf_init(struct ubifs_info *c, struct ubifs_wbuf *wbuf)
{
	size_t size;

	wbuf->buf = kmalloc(c->min_io_size, GFP_KERNEL);
	if (!wbuf->buf)
		return -ENOMEM;

	size = (c->min_io_size / UBIFS_CH_SZ + 1) * sizeof(ino_t);
	wbuf->inodes = kmalloc(size, GFP_KERNEL);
	if (!wbuf->inodes) {
		kfree(wbuf->buf);
		wbuf->buf = NULL;
		return -ENOMEM;
	}

	wbuf->used = 0;
	wbuf->lnum = wbuf->offs = -1;
	wbuf->avail = c->min_io_size;
	wbuf->dtype = UBI_UNKNOWN;
	wbuf->sync_callback = NULL;
	mutex_init(&wbuf->io_mutex);
	spin_lock_init(&wbuf->lock);
	wbuf->c = c;
	wbuf->next_ino = 0;

	hrtimer_init(&wbuf->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	wbuf->timer.function = wbuf_timer_callback_nolock;
	wbuf->softlimit = ktime_set(WBUF_TIMEOUT_SOFTLIMIT, 0);
	wbuf->delta = WBUF_TIMEOUT_HARDLIMIT - WBUF_TIMEOUT_SOFTLIMIT;
	wbuf->delta *= 1000000000ULL;
	ubifs_assert(wbuf->delta <= ULONG_MAX);
	return 0;
}

/**
 * ubifs_wbuf_add_ino_nolock - add an inode number into the wbuf inode array.
 * @wbuf: the write-buffer where to add
 * @inum: the inode number
 *
 * This function adds an inode number to the inode array of the write-buffer.
 */
void ubifs_wbuf_add_ino_nolock(struct ubifs_wbuf *wbuf, ino_t inum)
{
	if (!wbuf->buf)
		/* NOR flash or something similar */
		return;

	spin_lock(&wbuf->lock);
	if (wbuf->used)
		wbuf->inodes[wbuf->next_ino++] = inum;
	spin_unlock(&wbuf->lock);
}

/**
 * wbuf_has_ino - returns if the wbuf contains data from the inode.
 * @wbuf: the write-buffer
 * @inum: the inode number
 *
 * This function returns with %1 if the write-buffer contains some data from the
 * given inode otherwise it returns with %0.
 */
static int wbuf_has_ino(struct ubifs_wbuf *wbuf, ino_t inum)
{
	int i, ret = 0;

	spin_lock(&wbuf->lock);
	for (i = 0; i < wbuf->next_ino; i++)
		if (inum == wbuf->inodes[i]) {
			ret = 1;
			break;
		}
	spin_unlock(&wbuf->lock);

	return ret;
}

/**
 * ubifs_sync_wbufs_by_inode - synchronize write-buffers for an inode.
 * @c: UBIFS file-system description object
 * @inode: inode to synchronize
 *
 * This function synchronizes write-buffers which contain nodes belonging to
 * @inode. Returns zero in case of success and a negative error code in case of
 * failure.
 */
int ubifs_sync_wbufs_by_inode(struct ubifs_info *c, struct inode *inode)
{
	int i, err = 0;

	for (i = 0; i < c->jhead_cnt; i++) {
		struct ubifs_wbuf *wbuf = &c->jheads[i].wbuf;

		if (i == GCHD)
			/*
			 * GC head is special, do not look at it. Even if the
			 * head contains something related to this inode, it is
			 * a _copy_ of corresponding on-flash node which sits
			 * somewhere else.
			 */
			continue;

		if (!wbuf_has_ino(wbuf, inode->i_ino))
			continue;

		mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
		if (wbuf_has_ino(wbuf, inode->i_ino))
			err = ubifs_wbuf_sync_nolock(wbuf);
		mutex_unlock(&wbuf->io_mutex);

		if (err) {
			ubifs_ro_mode(c, err);
			return err;
		}
	}
	return 0;
}
