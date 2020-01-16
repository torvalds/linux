// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 * Copyright (C) 2006, 2007 University of Szeged, Hungary
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 *          Zoltan Sogor
 */

/*
 * This file implements UBIFS I/O subsystem which provides various I/O-related
 * helper functions (reading/writing/checking/validating yesdes) and implements
 * write-buffering support. Write buffers help to save space which otherwise
 * would have been wasted for padding to the nearest minimal I/O unit boundary.
 * Instead, data first goes to the write-buffer and is flushed when the
 * buffer is full or when it is yest used for some time (by timer). This is
 * similar to the mechanism is used by JFFS2.
 *
 * UBIFS distinguishes between minimum write size (@c->min_io_size) and maximum
 * write size (@c->max_write_size). The latter is the maximum amount of bytes
 * the underlying flash is able to program at a time, and writing in
 * @c->max_write_size units should presumably be faster. Obviously,
 * @c->min_io_size <= @c->max_write_size. Write-buffers are of
 * @c->max_write_size bytes in size for maximum performance. However, when a
 * write-buffer is flushed, only the portion of it (aligned to @c->min_io_size
 * boundary) which contains data is written, yest the whole write-buffer,
 * because this is more space-efficient.
 *
 * This optimization adds few complications to the code. Indeed, on the one
 * hand, we want to write in optimal @c->max_write_size bytes chunks, which
 * also means aligning writes at the @c->max_write_size bytes offsets. On the
 * other hand, we do yest want to waste space when synchronizing the write
 * buffer, so during synchronization we writes in smaller chunks. And this makes
 * the next write offset to be yest aligned to @c->max_write_size bytes. So the
 * have to make sure that the write-buffer offset (@wbuf->offs) becomes aligned
 * to @c->max_write_size bytes again. We do this by temporarily shrinking
 * write-buffer size (@wbuf->size).
 *
 * Write-buffers are defined by 'struct ubifs_wbuf' objects and protected by
 * mutexes defined inside these objects. Since sometimes upper-level code
 * has to lock the write-buffer (e.g. journal space reservation code), many
 * functions related to write-buffers have "yeslock" suffix which means that the
 * caller has to lock the write-buffer before calling this function.
 *
 * UBIFS stores yesdes at 64 bit-aligned addresses. If the yesde length is yest
 * aligned, UBIFS starts the next yesde from the aligned address, and the padded
 * bytes may contain any rubbish. In other words, UBIFS does yest put padding
 * bytes in those small gaps. Common headers of yesdes store real yesde lengths,
 * yest aligned lengths. Indexing yesdes also store real lengths in branches.
 *
 * UBIFS uses padding when it pads to the next min. I/O unit. In this case it
 * uses padding yesdes or padding bytes, if the padding yesde does yest fit.
 *
 * All UBIFS yesdes are protected by CRC checksums and UBIFS checks CRC when
 * they are read from the flash media.
 */

#include <linux/crc32.h>
#include <linux/slab.h>
#include "ubifs.h"

/**
 * ubifs_ro_mode - switch UBIFS to read read-only mode.
 * @c: UBIFS file-system description object
 * @err: error code which is the reason of switching to R/O mode
 */
void ubifs_ro_mode(struct ubifs_info *c, int err)
{
	if (!c->ro_error) {
		c->ro_error = 1;
		c->yes_chk_data_crc = 0;
		c->vfs_sb->s_flags |= SB_RDONLY;
		ubifs_warn(c, "switched to read-only mode, error %d", err);
		dump_stack();
	}
}

/*
 * Below are simple wrappers over UBI I/O functions which include some
 * additional checks and UBIFS debugging stuff. See corresponding UBI function
 * for more information.
 */

int ubifs_leb_read(const struct ubifs_info *c, int lnum, void *buf, int offs,
		   int len, int even_ebadmsg)
{
	int err;

	err = ubi_read(c->ubi, lnum, buf, offs, len);
	/*
	 * In case of %-EBADMSG print the error message only if the
	 * @even_ebadmsg is true.
	 */
	if (err && (err != -EBADMSG || even_ebadmsg)) {
		ubifs_err(c, "reading %d bytes from LEB %d:%d failed, error %d",
			  len, lnum, offs, err);
		dump_stack();
	}
	return err;
}

int ubifs_leb_write(struct ubifs_info *c, int lnum, const void *buf, int offs,
		    int len)
{
	int err;

	ubifs_assert(c, !c->ro_media && !c->ro_mount);
	if (c->ro_error)
		return -EROFS;
	if (!dbg_is_tst_rcvry(c))
		err = ubi_leb_write(c->ubi, lnum, buf, offs, len);
	else
		err = dbg_leb_write(c, lnum, buf, offs, len);
	if (err) {
		ubifs_err(c, "writing %d bytes to LEB %d:%d failed, error %d",
			  len, lnum, offs, err);
		ubifs_ro_mode(c, err);
		dump_stack();
	}
	return err;
}

int ubifs_leb_change(struct ubifs_info *c, int lnum, const void *buf, int len)
{
	int err;

	ubifs_assert(c, !c->ro_media && !c->ro_mount);
	if (c->ro_error)
		return -EROFS;
	if (!dbg_is_tst_rcvry(c))
		err = ubi_leb_change(c->ubi, lnum, buf, len);
	else
		err = dbg_leb_change(c, lnum, buf, len);
	if (err) {
		ubifs_err(c, "changing %d bytes in LEB %d failed, error %d",
			  len, lnum, err);
		ubifs_ro_mode(c, err);
		dump_stack();
	}
	return err;
}

int ubifs_leb_unmap(struct ubifs_info *c, int lnum)
{
	int err;

	ubifs_assert(c, !c->ro_media && !c->ro_mount);
	if (c->ro_error)
		return -EROFS;
	if (!dbg_is_tst_rcvry(c))
		err = ubi_leb_unmap(c->ubi, lnum);
	else
		err = dbg_leb_unmap(c, lnum);
	if (err) {
		ubifs_err(c, "unmap LEB %d failed, error %d", lnum, err);
		ubifs_ro_mode(c, err);
		dump_stack();
	}
	return err;
}

int ubifs_leb_map(struct ubifs_info *c, int lnum)
{
	int err;

	ubifs_assert(c, !c->ro_media && !c->ro_mount);
	if (c->ro_error)
		return -EROFS;
	if (!dbg_is_tst_rcvry(c))
		err = ubi_leb_map(c->ubi, lnum);
	else
		err = dbg_leb_map(c, lnum);
	if (err) {
		ubifs_err(c, "mapping LEB %d failed, error %d", lnum, err);
		ubifs_ro_mode(c, err);
		dump_stack();
	}
	return err;
}

int ubifs_is_mapped(const struct ubifs_info *c, int lnum)
{
	int err;

	err = ubi_is_mapped(c->ubi, lnum);
	if (err < 0) {
		ubifs_err(c, "ubi_is_mapped failed for LEB %d, error %d",
			  lnum, err);
		dump_stack();
	}
	return err;
}

/**
 * ubifs_check_yesde - check yesde.
 * @c: UBIFS file-system description object
 * @buf: yesde to check
 * @lnum: logical eraseblock number
 * @offs: offset within the logical eraseblock
 * @quiet: print yes messages
 * @must_chk_crc: indicates whether to always check the CRC
 *
 * This function checks yesde magic number and CRC checksum. This function also
 * validates yesde length to prevent UBIFS from becoming crazy when an attacker
 * feeds it a file-system image with incorrect yesdes. For example, too large
 * yesde length in the common header could cause UBIFS to read memory outside of
 * allocated buffer when checking the CRC checksum.
 *
 * This function may skip data yesdes CRC checking if @c->yes_chk_data_crc is
 * true, which is controlled by corresponding UBIFS mount option. However, if
 * @must_chk_crc is true, then @c->yes_chk_data_crc is igyesred and CRC is
 * checked. Similarly, if @c->mounting or @c->remounting_rw is true (we are
 * mounting or re-mounting to R/W mode), @c->yes_chk_data_crc is igyesred and CRC
 * is checked. This is because during mounting or re-mounting from R/O mode to
 * R/W mode we may read journal yesdes (when replying the journal or doing the
 * recovery) and the journal yesdes may potentially be corrupted, so checking is
 * required.
 *
 * This function returns zero in case of success and %-EUCLEAN in case of bad
 * CRC or magic.
 */
int ubifs_check_yesde(const struct ubifs_info *c, const void *buf, int lnum,
		     int offs, int quiet, int must_chk_crc)
{
	int err = -EINVAL, type, yesde_len;
	uint32_t crc, yesde_crc, magic;
	const struct ubifs_ch *ch = buf;

	ubifs_assert(c, lnum >= 0 && lnum < c->leb_cnt && offs >= 0);
	ubifs_assert(c, !(offs & 7) && offs < c->leb_size);

	magic = le32_to_cpu(ch->magic);
	if (magic != UBIFS_NODE_MAGIC) {
		if (!quiet)
			ubifs_err(c, "bad magic %#08x, expected %#08x",
				  magic, UBIFS_NODE_MAGIC);
		err = -EUCLEAN;
		goto out;
	}

	type = ch->yesde_type;
	if (type < 0 || type >= UBIFS_NODE_TYPES_CNT) {
		if (!quiet)
			ubifs_err(c, "bad yesde type %d", type);
		goto out;
	}

	yesde_len = le32_to_cpu(ch->len);
	if (yesde_len + offs > c->leb_size)
		goto out_len;

	if (c->ranges[type].max_len == 0) {
		if (yesde_len != c->ranges[type].len)
			goto out_len;
	} else if (yesde_len < c->ranges[type].min_len ||
		   yesde_len > c->ranges[type].max_len)
		goto out_len;

	if (!must_chk_crc && type == UBIFS_DATA_NODE && !c->mounting &&
	    !c->remounting_rw && c->yes_chk_data_crc)
		return 0;

	crc = crc32(UBIFS_CRC32_INIT, buf + 8, yesde_len - 8);
	yesde_crc = le32_to_cpu(ch->crc);
	if (crc != yesde_crc) {
		if (!quiet)
			ubifs_err(c, "bad CRC: calculated %#08x, read %#08x",
				  crc, yesde_crc);
		err = -EUCLEAN;
		goto out;
	}

	return 0;

out_len:
	if (!quiet)
		ubifs_err(c, "bad yesde length %d", yesde_len);
out:
	if (!quiet) {
		ubifs_err(c, "bad yesde at LEB %d:%d", lnum, offs);
		ubifs_dump_yesde(c, buf);
		dump_stack();
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
 * when we have to write less data we add padding yesde to the write-buffer and
 * pad it to the next minimal I/O unit's boundary. Padding yesdes help when the
 * media is being scanned. If the amount of wasted space is yest eyesugh to fit a
 * padding yesde which takes %UBIFS_PAD_NODE_SZ bytes, we write padding bytes
 * pattern (%UBIFS_PADDING_BYTE).
 *
 * Padding yesdes are also used to fill gaps when the "commit-in-gaps" method is
 * used.
 */
void ubifs_pad(const struct ubifs_info *c, void *buf, int pad)
{
	uint32_t crc;

	ubifs_assert(c, pad >= 0 && !(pad & 7));

	if (pad >= UBIFS_PAD_NODE_SZ) {
		struct ubifs_ch *ch = buf;
		struct ubifs_pad_yesde *pad_yesde = buf;

		ch->magic = cpu_to_le32(UBIFS_NODE_MAGIC);
		ch->yesde_type = UBIFS_PAD_NODE;
		ch->group_type = UBIFS_NO_NODE_GROUP;
		ch->padding[0] = ch->padding[1] = 0;
		ch->sqnum = 0;
		ch->len = cpu_to_le32(UBIFS_PAD_NODE_SZ);
		pad -= UBIFS_PAD_NODE_SZ;
		pad_yesde->pad_len = cpu_to_le32(pad);
		crc = crc32(UBIFS_CRC32_INIT, buf + 8, UBIFS_PAD_NODE_SZ - 8);
		ch->crc = cpu_to_le32(crc);
		memset(buf + UBIFS_PAD_NODE_SZ, 0, pad);
	} else if (pad > 0)
		/* Too little space, padding yesde won't fit */
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
			ubifs_err(c, "sequence number overflow %llu, end of life",
				  sqnum);
			ubifs_ro_mode(c, -EINVAL);
		}
		ubifs_warn(c, "running out of sequence numbers, end of life soon");
	}

	return sqnum;
}

void ubifs_init_yesde(struct ubifs_info *c, void *yesde, int len, int pad)
{
	struct ubifs_ch *ch = yesde;
	unsigned long long sqnum = next_sqnum(c);

	ubifs_assert(c, len >= UBIFS_CH_SZ);

	ch->magic = cpu_to_le32(UBIFS_NODE_MAGIC);
	ch->len = cpu_to_le32(len);
	ch->group_type = UBIFS_NO_NODE_GROUP;
	ch->sqnum = cpu_to_le64(sqnum);
	ch->padding[0] = ch->padding[1] = 0;

	if (pad) {
		len = ALIGN(len, 8);
		pad = ALIGN(len, c->min_io_size) - len;
		ubifs_pad(c, yesde + len, pad);
	}
}

void ubifs_crc_yesde(struct ubifs_info *c, void *yesde, int len)
{
	struct ubifs_ch *ch = yesde;
	uint32_t crc;

	crc = crc32(UBIFS_CRC32_INIT, yesde + 8, len - 8);
	ch->crc = cpu_to_le32(crc);
}

/**
 * ubifs_prepare_yesde_hmac - prepare yesde to be written to flash.
 * @c: UBIFS file-system description object
 * @yesde: the yesde to pad
 * @len: yesde length
 * @hmac_offs: offset of the HMAC in the yesde
 * @pad: if the buffer has to be padded
 *
 * This function prepares yesde at @yesde to be written to the media - it
 * calculates yesde CRC, fills the common header, and adds proper padding up to
 * the next minimum I/O unit if @pad is yest zero. if @hmac_offs is positive then
 * a HMAC is inserted into the yesde at the given offset.
 *
 * This function returns 0 for success or a negative error code otherwise.
 */
int ubifs_prepare_yesde_hmac(struct ubifs_info *c, void *yesde, int len,
			    int hmac_offs, int pad)
{
	int err;

	ubifs_init_yesde(c, yesde, len, pad);

	if (hmac_offs > 0) {
		err = ubifs_yesde_insert_hmac(c, yesde, len, hmac_offs);
		if (err)
			return err;
	}

	ubifs_crc_yesde(c, yesde, len);

	return 0;
}

/**
 * ubifs_prepare_yesde - prepare yesde to be written to flash.
 * @c: UBIFS file-system description object
 * @yesde: the yesde to pad
 * @len: yesde length
 * @pad: if the buffer has to be padded
 *
 * This function prepares yesde at @yesde to be written to the media - it
 * calculates yesde CRC, fills the common header, and adds proper padding up to
 * the next minimum I/O unit if @pad is yest zero.
 */
void ubifs_prepare_yesde(struct ubifs_info *c, void *yesde, int len, int pad)
{
	/*
	 * Deliberately igyesre return value since this function can only fail
	 * when a hmac offset is given.
	 */
	ubifs_prepare_yesde_hmac(c, yesde, len, 0, pad);
}

/**
 * ubifs_prep_grp_yesde - prepare yesde of a group to be written to flash.
 * @c: UBIFS file-system description object
 * @yesde: the yesde to pad
 * @len: yesde length
 * @last: indicates the last yesde of the group
 *
 * This function prepares yesde at @yesde to be written to the media - it
 * calculates yesde CRC and fills the common header.
 */
void ubifs_prep_grp_yesde(struct ubifs_info *c, void *yesde, int len, int last)
{
	uint32_t crc;
	struct ubifs_ch *ch = yesde;
	unsigned long long sqnum = next_sqnum(c);

	ubifs_assert(c, len >= UBIFS_CH_SZ);

	ch->magic = cpu_to_le32(UBIFS_NODE_MAGIC);
	ch->len = cpu_to_le32(len);
	if (last)
		ch->group_type = UBIFS_LAST_OF_NODE_GROUP;
	else
		ch->group_type = UBIFS_IN_NODE_GROUP;
	ch->sqnum = cpu_to_le64(sqnum);
	ch->padding[0] = ch->padding[1] = 0;
	crc = crc32(UBIFS_CRC32_INIT, yesde + 8, len - 8);
	ch->crc = cpu_to_le32(crc);
}

/**
 * wbuf_timer_callback - write-buffer timer callback function.
 * @timer: timer data (write-buffer descriptor)
 *
 * This function is called when the write-buffer timer expires.
 */
static enum hrtimer_restart wbuf_timer_callback_yeslock(struct hrtimer *timer)
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
 * @c: UBIFS file-system description object
 * @wbuf: write-buffer descriptor
 */
static void new_wbuf_timer_yeslock(struct ubifs_info *c, struct ubifs_wbuf *wbuf)
{
	ktime_t softlimit = ms_to_ktime(dirty_writeback_interval * 10);
	unsigned long long delta = dirty_writeback_interval;

	/* centi to milli, milli to nayes, then 10% */
	delta *= 10ULL * NSEC_PER_MSEC / 10ULL;

	ubifs_assert(c, !hrtimer_active(&wbuf->timer));
	ubifs_assert(c, delta <= ULONG_MAX);

	if (wbuf->yes_timer)
		return;
	dbg_io("set timer for jhead %s, %llu-%llu millisecs",
	       dbg_jhead(wbuf->jhead),
	       div_u64(ktime_to_ns(softlimit), USEC_PER_SEC),
	       div_u64(ktime_to_ns(softlimit) + delta, USEC_PER_SEC));
	hrtimer_start_range_ns(&wbuf->timer, softlimit, delta,
			       HRTIMER_MODE_REL);
}

/**
 * cancel_wbuf_timer - cancel write-buffer timer.
 * @wbuf: write-buffer descriptor
 */
static void cancel_wbuf_timer_yeslock(struct ubifs_wbuf *wbuf)
{
	if (wbuf->yes_timer)
		return;
	wbuf->need_sync = 0;
	hrtimer_cancel(&wbuf->timer);
}

/**
 * ubifs_wbuf_sync_yeslock - synchronize write-buffer.
 * @wbuf: write-buffer to synchronize
 *
 * This function synchronizes write-buffer @buf and returns zero in case of
 * success or a negative error code in case of failure.
 *
 * Note, although write-buffers are of @c->max_write_size, this function does
 * yest necessarily writes all @c->max_write_size bytes to the flash. Instead,
 * if the write-buffer is only partially filled with data, only the used part
 * of the write-buffer (aligned on @c->min_io_size boundary) is synchronized.
 * This way we waste less space.
 */
int ubifs_wbuf_sync_yeslock(struct ubifs_wbuf *wbuf)
{
	struct ubifs_info *c = wbuf->c;
	int err, dirt, sync_len;

	cancel_wbuf_timer_yeslock(wbuf);
	if (!wbuf->used || wbuf->lnum == -1)
		/* Write-buffer is empty or yest seeked */
		return 0;

	dbg_io("LEB %d:%d, %d bytes, jhead %s",
	       wbuf->lnum, wbuf->offs, wbuf->used, dbg_jhead(wbuf->jhead));
	ubifs_assert(c, !(wbuf->avail & 7));
	ubifs_assert(c, wbuf->offs + wbuf->size <= c->leb_size);
	ubifs_assert(c, wbuf->size >= c->min_io_size);
	ubifs_assert(c, wbuf->size <= c->max_write_size);
	ubifs_assert(c, wbuf->size % c->min_io_size == 0);
	ubifs_assert(c, !c->ro_media && !c->ro_mount);
	if (c->leb_size - wbuf->offs >= c->max_write_size)
		ubifs_assert(c, !((wbuf->offs + wbuf->size) % c->max_write_size));

	if (c->ro_error)
		return -EROFS;

	/*
	 * Do yest write whole write buffer but write only the minimum necessary
	 * amount of min. I/O units.
	 */
	sync_len = ALIGN(wbuf->used, c->min_io_size);
	dirt = sync_len - wbuf->used;
	if (dirt)
		ubifs_pad(c, wbuf->buf + wbuf->used, dirt);
	err = ubifs_leb_write(c, wbuf->lnum, wbuf->buf, wbuf->offs, sync_len);
	if (err)
		return err;

	spin_lock(&wbuf->lock);
	wbuf->offs += sync_len;
	/*
	 * Now @wbuf->offs is yest necessarily aligned to @c->max_write_size.
	 * But our goal is to optimize writes and make sure we write in
	 * @c->max_write_size chunks and to @c->max_write_size-aligned offset.
	 * Thus, if @wbuf->offs is yest aligned to @c->max_write_size yesw, make
	 * sure that @wbuf->offs + @wbuf->size is aligned to
	 * @c->max_write_size. This way we make sure that after next
	 * write-buffer flush we are again at the optimal offset (aligned to
	 * @c->max_write_size).
	 */
	if (c->leb_size - wbuf->offs < c->max_write_size)
		wbuf->size = c->leb_size - wbuf->offs;
	else if (wbuf->offs & (c->max_write_size - 1))
		wbuf->size = ALIGN(wbuf->offs, c->max_write_size) - wbuf->offs;
	else
		wbuf->size = c->max_write_size;
	wbuf->avail = wbuf->size;
	wbuf->used = 0;
	wbuf->next_iyes = 0;
	spin_unlock(&wbuf->lock);

	if (wbuf->sync_callback)
		err = wbuf->sync_callback(c, wbuf->lnum,
					  c->leb_size - wbuf->offs, dirt);
	return err;
}

/**
 * ubifs_wbuf_seek_yeslock - seek write-buffer.
 * @wbuf: write-buffer
 * @lnum: logical eraseblock number to seek to
 * @offs: logical eraseblock offset to seek to
 *
 * This function targets the write-buffer to logical eraseblock @lnum:@offs.
 * The write-buffer has to be empty. Returns zero in case of success and a
 * negative error code in case of failure.
 */
int ubifs_wbuf_seek_yeslock(struct ubifs_wbuf *wbuf, int lnum, int offs)
{
	const struct ubifs_info *c = wbuf->c;

	dbg_io("LEB %d:%d, jhead %s", lnum, offs, dbg_jhead(wbuf->jhead));
	ubifs_assert(c, lnum >= 0 && lnum < c->leb_cnt);
	ubifs_assert(c, offs >= 0 && offs <= c->leb_size);
	ubifs_assert(c, offs % c->min_io_size == 0 && !(offs & 7));
	ubifs_assert(c, lnum != wbuf->lnum);
	ubifs_assert(c, wbuf->used == 0);

	spin_lock(&wbuf->lock);
	wbuf->lnum = lnum;
	wbuf->offs = offs;
	if (c->leb_size - wbuf->offs < c->max_write_size)
		wbuf->size = c->leb_size - wbuf->offs;
	else if (wbuf->offs & (c->max_write_size - 1))
		wbuf->size = ALIGN(wbuf->offs, c->max_write_size) - wbuf->offs;
	else
		wbuf->size = c->max_write_size;
	wbuf->avail = wbuf->size;
	wbuf->used = 0;
	spin_unlock(&wbuf->lock);

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

	ubifs_assert(c, !c->ro_media && !c->ro_mount);
	if (!c->need_wbuf_sync)
		return 0;
	c->need_wbuf_sync = 0;

	if (c->ro_error) {
		err = -EROFS;
		goto out_timers;
	}

	dbg_io("synchronize");
	for (i = 0; i < c->jhead_cnt; i++) {
		struct ubifs_wbuf *wbuf = &c->jheads[i].wbuf;

		cond_resched();

		/*
		 * If the mutex is locked then wbuf is being changed, so
		 * synchronization is yest necessary.
		 */
		if (mutex_is_locked(&wbuf->io_mutex))
			continue;

		mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
		if (!wbuf->need_sync) {
			mutex_unlock(&wbuf->io_mutex);
			continue;
		}

		err = ubifs_wbuf_sync_yeslock(wbuf);
		mutex_unlock(&wbuf->io_mutex);
		if (err) {
			ubifs_err(c, "canyest sync write-buffer, error %d", err);
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
		cancel_wbuf_timer_yeslock(wbuf);
		mutex_unlock(&wbuf->io_mutex);
	}
	return err;
}

/**
 * ubifs_wbuf_write_yeslock - write data to flash via write-buffer.
 * @wbuf: write-buffer
 * @buf: yesde to write
 * @len: yesde length
 *
 * This function writes data to flash via write-buffer @wbuf. This means that
 * the last piece of the yesde won't reach the flash media immediately if it
 * does yest take whole max. write unit (@c->max_write_size). Instead, the yesde
 * will sit in RAM until the write-buffer is synchronized (e.g., by timer, or
 * because more data are appended to the write-buffer).
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure. If the yesde canyest be written because there is yes more
 * space in this logical eraseblock, %-ENOSPC is returned.
 */
int ubifs_wbuf_write_yeslock(struct ubifs_wbuf *wbuf, void *buf, int len)
{
	struct ubifs_info *c = wbuf->c;
	int err, written, n, aligned_len = ALIGN(len, 8);

	dbg_io("%d bytes (%s) to jhead %s wbuf at LEB %d:%d", len,
	       dbg_ntype(((struct ubifs_ch *)buf)->yesde_type),
	       dbg_jhead(wbuf->jhead), wbuf->lnum, wbuf->offs + wbuf->used);
	ubifs_assert(c, len > 0 && wbuf->lnum >= 0 && wbuf->lnum < c->leb_cnt);
	ubifs_assert(c, wbuf->offs >= 0 && wbuf->offs % c->min_io_size == 0);
	ubifs_assert(c, !(wbuf->offs & 7) && wbuf->offs <= c->leb_size);
	ubifs_assert(c, wbuf->avail > 0 && wbuf->avail <= wbuf->size);
	ubifs_assert(c, wbuf->size >= c->min_io_size);
	ubifs_assert(c, wbuf->size <= c->max_write_size);
	ubifs_assert(c, wbuf->size % c->min_io_size == 0);
	ubifs_assert(c, mutex_is_locked(&wbuf->io_mutex));
	ubifs_assert(c, !c->ro_media && !c->ro_mount);
	ubifs_assert(c, !c->space_fixup);
	if (c->leb_size - wbuf->offs >= c->max_write_size)
		ubifs_assert(c, !((wbuf->offs + wbuf->size) % c->max_write_size));

	if (c->leb_size - wbuf->offs - wbuf->used < aligned_len) {
		err = -ENOSPC;
		goto out;
	}

	cancel_wbuf_timer_yeslock(wbuf);

	if (c->ro_error)
		return -EROFS;

	if (aligned_len <= wbuf->avail) {
		/*
		 * The yesde is yest very large and fits entirely within
		 * write-buffer.
		 */
		memcpy(wbuf->buf + wbuf->used, buf, len);

		if (aligned_len == wbuf->avail) {
			dbg_io("flush jhead %s wbuf to LEB %d:%d",
			       dbg_jhead(wbuf->jhead), wbuf->lnum, wbuf->offs);
			err = ubifs_leb_write(c, wbuf->lnum, wbuf->buf,
					      wbuf->offs, wbuf->size);
			if (err)
				goto out;

			spin_lock(&wbuf->lock);
			wbuf->offs += wbuf->size;
			if (c->leb_size - wbuf->offs >= c->max_write_size)
				wbuf->size = c->max_write_size;
			else
				wbuf->size = c->leb_size - wbuf->offs;
			wbuf->avail = wbuf->size;
			wbuf->used = 0;
			wbuf->next_iyes = 0;
			spin_unlock(&wbuf->lock);
		} else {
			spin_lock(&wbuf->lock);
			wbuf->avail -= aligned_len;
			wbuf->used += aligned_len;
			spin_unlock(&wbuf->lock);
		}

		goto exit;
	}

	written = 0;

	if (wbuf->used) {
		/*
		 * The yesde is large eyesugh and does yest fit entirely within
		 * current available space. We have to fill and flush
		 * write-buffer and switch to the next max. write unit.
		 */
		dbg_io("flush jhead %s wbuf to LEB %d:%d",
		       dbg_jhead(wbuf->jhead), wbuf->lnum, wbuf->offs);
		memcpy(wbuf->buf + wbuf->used, buf, wbuf->avail);
		err = ubifs_leb_write(c, wbuf->lnum, wbuf->buf, wbuf->offs,
				      wbuf->size);
		if (err)
			goto out;

		wbuf->offs += wbuf->size;
		len -= wbuf->avail;
		aligned_len -= wbuf->avail;
		written += wbuf->avail;
	} else if (wbuf->offs & (c->max_write_size - 1)) {
		/*
		 * The write-buffer offset is yest aligned to
		 * @c->max_write_size and @wbuf->size is less than
		 * @c->max_write_size. Write @wbuf->size bytes to make sure the
		 * following writes are done in optimal @c->max_write_size
		 * chunks.
		 */
		dbg_io("write %d bytes to LEB %d:%d",
		       wbuf->size, wbuf->lnum, wbuf->offs);
		err = ubifs_leb_write(c, wbuf->lnum, buf, wbuf->offs,
				      wbuf->size);
		if (err)
			goto out;

		wbuf->offs += wbuf->size;
		len -= wbuf->size;
		aligned_len -= wbuf->size;
		written += wbuf->size;
	}

	/*
	 * The remaining data may take more whole max. write units, so write the
	 * remains multiple to max. write unit size directly to the flash media.
	 * We align yesde length to 8-byte boundary because we anyway flash wbuf
	 * if the remaining space is less than 8 bytes.
	 */
	n = aligned_len >> c->max_write_shift;
	if (n) {
		n <<= c->max_write_shift;
		dbg_io("write %d bytes to LEB %d:%d", n, wbuf->lnum,
		       wbuf->offs);
		err = ubifs_leb_write(c, wbuf->lnum, buf + written,
				      wbuf->offs, n);
		if (err)
			goto out;
		wbuf->offs += n;
		aligned_len -= n;
		len -= n;
		written += n;
	}

	spin_lock(&wbuf->lock);
	if (aligned_len)
		/*
		 * And yesw we have what's left and what does yest take whole
		 * max. write unit, so write it to the write-buffer and we are
		 * done.
		 */
		memcpy(wbuf->buf, buf + written, len);

	if (c->leb_size - wbuf->offs >= c->max_write_size)
		wbuf->size = c->max_write_size;
	else
		wbuf->size = c->leb_size - wbuf->offs;
	wbuf->avail = wbuf->size - aligned_len;
	wbuf->used = aligned_len;
	wbuf->next_iyes = 0;
	spin_unlock(&wbuf->lock);

exit:
	if (wbuf->sync_callback) {
		int free = c->leb_size - wbuf->offs - wbuf->used;

		err = wbuf->sync_callback(c, wbuf->lnum, free, 0);
		if (err)
			goto out;
	}

	if (wbuf->used)
		new_wbuf_timer_yeslock(c, wbuf);

	return 0;

out:
	ubifs_err(c, "canyest write %d bytes to LEB %d:%d, error %d",
		  len, wbuf->lnum, wbuf->offs, err);
	ubifs_dump_yesde(c, buf);
	dump_stack();
	ubifs_dump_leb(c, wbuf->lnum);
	return err;
}

/**
 * ubifs_write_yesde_hmac - write yesde to the media.
 * @c: UBIFS file-system description object
 * @buf: the yesde to write
 * @len: yesde length
 * @lnum: logical eraseblock number
 * @offs: offset within the logical eraseblock
 * @hmac_offs: offset of the HMAC within the yesde
 *
 * This function automatically fills yesde magic number, assigns sequence
 * number, and calculates yesde CRC checksum. The length of the @buf buffer has
 * to be aligned to the minimal I/O unit size. This function automatically
 * appends padding yesde and padding bytes if needed. Returns zero in case of
 * success and a negative error code in case of failure.
 */
int ubifs_write_yesde_hmac(struct ubifs_info *c, void *buf, int len, int lnum,
			  int offs, int hmac_offs)
{
	int err, buf_len = ALIGN(len, c->min_io_size);

	dbg_io("LEB %d:%d, %s, length %d (aligned %d)",
	       lnum, offs, dbg_ntype(((struct ubifs_ch *)buf)->yesde_type), len,
	       buf_len);
	ubifs_assert(c, lnum >= 0 && lnum < c->leb_cnt && offs >= 0);
	ubifs_assert(c, offs % c->min_io_size == 0 && offs < c->leb_size);
	ubifs_assert(c, !c->ro_media && !c->ro_mount);
	ubifs_assert(c, !c->space_fixup);

	if (c->ro_error)
		return -EROFS;

	err = ubifs_prepare_yesde_hmac(c, buf, len, hmac_offs, 1);
	if (err)
		return err;

	err = ubifs_leb_write(c, lnum, buf, offs, buf_len);
	if (err)
		ubifs_dump_yesde(c, buf);

	return err;
}

/**
 * ubifs_write_yesde - write yesde to the media.
 * @c: UBIFS file-system description object
 * @buf: the yesde to write
 * @len: yesde length
 * @lnum: logical eraseblock number
 * @offs: offset within the logical eraseblock
 *
 * This function automatically fills yesde magic number, assigns sequence
 * number, and calculates yesde CRC checksum. The length of the @buf buffer has
 * to be aligned to the minimal I/O unit size. This function automatically
 * appends padding yesde and padding bytes if needed. Returns zero in case of
 * success and a negative error code in case of failure.
 */
int ubifs_write_yesde(struct ubifs_info *c, void *buf, int len, int lnum,
		     int offs)
{
	return ubifs_write_yesde_hmac(c, buf, len, lnum, offs, -1);
}

/**
 * ubifs_read_yesde_wbuf - read yesde from the media or write-buffer.
 * @wbuf: wbuf to check for un-written data
 * @buf: buffer to read to
 * @type: yesde type
 * @len: yesde length
 * @lnum: logical eraseblock number
 * @offs: offset within the logical eraseblock
 *
 * This function reads a yesde of kyeswn type and length, checks it and stores
 * in @buf. If the yesde partially or fully sits in the write-buffer, this
 * function takes data from the buffer, otherwise it reads the flash media.
 * Returns zero in case of success, %-EUCLEAN if CRC mismatched and a negative
 * error code in case of failure.
 */
int ubifs_read_yesde_wbuf(struct ubifs_wbuf *wbuf, void *buf, int type, int len,
			 int lnum, int offs)
{
	const struct ubifs_info *c = wbuf->c;
	int err, rlen, overlap;
	struct ubifs_ch *ch = buf;

	dbg_io("LEB %d:%d, %s, length %d, jhead %s", lnum, offs,
	       dbg_ntype(type), len, dbg_jhead(wbuf->jhead));
	ubifs_assert(c, wbuf && lnum >= 0 && lnum < c->leb_cnt && offs >= 0);
	ubifs_assert(c, !(offs & 7) && offs < c->leb_size);
	ubifs_assert(c, type >= 0 && type < UBIFS_NODE_TYPES_CNT);

	spin_lock(&wbuf->lock);
	overlap = (lnum == wbuf->lnum && offs + len > wbuf->offs);
	if (!overlap) {
		/* We may safely unlock the write-buffer and read the data */
		spin_unlock(&wbuf->lock);
		return ubifs_read_yesde(c, buf, type, len, lnum, offs);
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
		err = ubifs_leb_read(c, lnum, buf, offs, rlen, 0);
		if (err && err != -EBADMSG)
			return err;
	}

	if (type != ch->yesde_type) {
		ubifs_err(c, "bad yesde type (%d but expected %d)",
			  ch->yesde_type, type);
		goto out;
	}

	err = ubifs_check_yesde(c, buf, lnum, offs, 0, 0);
	if (err) {
		ubifs_err(c, "expected yesde type %d", type);
		return err;
	}

	rlen = le32_to_cpu(ch->len);
	if (rlen != len) {
		ubifs_err(c, "bad yesde length %d, expected %d", rlen, len);
		goto out;
	}

	return 0;

out:
	ubifs_err(c, "bad yesde at LEB %d:%d", lnum, offs);
	ubifs_dump_yesde(c, buf);
	dump_stack();
	return -EINVAL;
}

/**
 * ubifs_read_yesde - read yesde.
 * @c: UBIFS file-system description object
 * @buf: buffer to read to
 * @type: yesde type
 * @len: yesde length (yest aligned)
 * @lnum: logical eraseblock number
 * @offs: offset within the logical eraseblock
 *
 * This function reads a yesde of kyeswn type and and length, checks it and
 * stores in @buf. Returns zero in case of success, %-EUCLEAN if CRC mismatched
 * and a negative error code in case of failure.
 */
int ubifs_read_yesde(const struct ubifs_info *c, void *buf, int type, int len,
		    int lnum, int offs)
{
	int err, l;
	struct ubifs_ch *ch = buf;

	dbg_io("LEB %d:%d, %s, length %d", lnum, offs, dbg_ntype(type), len);
	ubifs_assert(c, lnum >= 0 && lnum < c->leb_cnt && offs >= 0);
	ubifs_assert(c, len >= UBIFS_CH_SZ && offs + len <= c->leb_size);
	ubifs_assert(c, !(offs & 7) && offs < c->leb_size);
	ubifs_assert(c, type >= 0 && type < UBIFS_NODE_TYPES_CNT);

	err = ubifs_leb_read(c, lnum, buf, offs, len, 0);
	if (err && err != -EBADMSG)
		return err;

	if (type != ch->yesde_type) {
		ubifs_errc(c, "bad yesde type (%d but expected %d)",
			   ch->yesde_type, type);
		goto out;
	}

	err = ubifs_check_yesde(c, buf, lnum, offs, 0, 0);
	if (err) {
		ubifs_errc(c, "expected yesde type %d", type);
		return err;
	}

	l = le32_to_cpu(ch->len);
	if (l != len) {
		ubifs_errc(c, "bad yesde length %d, expected %d", l, len);
		goto out;
	}

	return 0;

out:
	ubifs_errc(c, "bad yesde at LEB %d:%d, LEB mapping status %d", lnum,
		   offs, ubi_is_mapped(c->ubi, lnum));
	if (!c->probing) {
		ubifs_dump_yesde(c, buf);
		dump_stack();
	}
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

	wbuf->buf = kmalloc(c->max_write_size, GFP_KERNEL);
	if (!wbuf->buf)
		return -ENOMEM;

	size = (c->max_write_size / UBIFS_CH_SZ + 1) * sizeof(iyes_t);
	wbuf->iyesdes = kmalloc(size, GFP_KERNEL);
	if (!wbuf->iyesdes) {
		kfree(wbuf->buf);
		wbuf->buf = NULL;
		return -ENOMEM;
	}

	wbuf->used = 0;
	wbuf->lnum = wbuf->offs = -1;
	/*
	 * If the LEB starts at the max. write size aligned address, then
	 * write-buffer size has to be set to @c->max_write_size. Otherwise,
	 * set it to something smaller so that it ends at the closest max.
	 * write size boundary.
	 */
	size = c->max_write_size - (c->leb_start % c->max_write_size);
	wbuf->avail = wbuf->size = size;
	wbuf->sync_callback = NULL;
	mutex_init(&wbuf->io_mutex);
	spin_lock_init(&wbuf->lock);
	wbuf->c = c;
	wbuf->next_iyes = 0;

	hrtimer_init(&wbuf->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	wbuf->timer.function = wbuf_timer_callback_yeslock;
	return 0;
}

/**
 * ubifs_wbuf_add_iyes_yeslock - add an iyesde number into the wbuf iyesde array.
 * @wbuf: the write-buffer where to add
 * @inum: the iyesde number
 *
 * This function adds an iyesde number to the iyesde array of the write-buffer.
 */
void ubifs_wbuf_add_iyes_yeslock(struct ubifs_wbuf *wbuf, iyes_t inum)
{
	if (!wbuf->buf)
		/* NOR flash or something similar */
		return;

	spin_lock(&wbuf->lock);
	if (wbuf->used)
		wbuf->iyesdes[wbuf->next_iyes++] = inum;
	spin_unlock(&wbuf->lock);
}

/**
 * wbuf_has_iyes - returns if the wbuf contains data from the iyesde.
 * @wbuf: the write-buffer
 * @inum: the iyesde number
 *
 * This function returns with %1 if the write-buffer contains some data from the
 * given iyesde otherwise it returns with %0.
 */
static int wbuf_has_iyes(struct ubifs_wbuf *wbuf, iyes_t inum)
{
	int i, ret = 0;

	spin_lock(&wbuf->lock);
	for (i = 0; i < wbuf->next_iyes; i++)
		if (inum == wbuf->iyesdes[i]) {
			ret = 1;
			break;
		}
	spin_unlock(&wbuf->lock);

	return ret;
}

/**
 * ubifs_sync_wbufs_by_iyesde - synchronize write-buffers for an iyesde.
 * @c: UBIFS file-system description object
 * @iyesde: iyesde to synchronize
 *
 * This function synchronizes write-buffers which contain yesdes belonging to
 * @iyesde. Returns zero in case of success and a negative error code in case of
 * failure.
 */
int ubifs_sync_wbufs_by_iyesde(struct ubifs_info *c, struct iyesde *iyesde)
{
	int i, err = 0;

	for (i = 0; i < c->jhead_cnt; i++) {
		struct ubifs_wbuf *wbuf = &c->jheads[i].wbuf;

		if (i == GCHD)
			/*
			 * GC head is special, do yest look at it. Even if the
			 * head contains something related to this iyesde, it is
			 * a _copy_ of corresponding on-flash yesde which sits
			 * somewhere else.
			 */
			continue;

		if (!wbuf_has_iyes(wbuf, iyesde->i_iyes))
			continue;

		mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
		if (wbuf_has_iyes(wbuf, iyesde->i_iyes))
			err = ubifs_wbuf_sync_yeslock(wbuf);
		mutex_unlock(&wbuf->io_mutex);

		if (err) {
			ubifs_ro_mode(c, err);
			return err;
		}
	}
	return 0;
}
