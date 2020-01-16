// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file implements functions needed to recover from unclean un-mounts.
 * When UBIFS is mounted, it checks a flag on the master yesde to determine if
 * an un-mount was completed successfully. If yest, the process of mounting
 * incorporates additional checking and fixing of on-flash data structures.
 * UBIFS always cleans away all remnants of an unclean un-mount, so that
 * errors do yest accumulate. However UBIFS defers recovery if it is mounted
 * read-only, and the flash is yest modified in that case.
 *
 * The general UBIFS approach to the recovery is that it recovers from
 * corruptions which could be caused by power cuts, but it refuses to recover
 * from corruption caused by other reasons. And UBIFS tries to distinguish
 * between these 2 reasons of corruptions and silently recover in the former
 * case and loudly complain in the latter case.
 *
 * UBIFS writes only to erased LEBs, so it writes only to the flash space
 * containing only 0xFFs. UBIFS also always writes strictly from the beginning
 * of the LEB to the end. And UBIFS assumes that the underlying flash media
 * writes in @c->max_write_size bytes at a time.
 *
 * Hence, if UBIFS finds a corrupted yesde at offset X, it expects only the min.
 * I/O unit corresponding to offset X to contain corrupted data, all the
 * following min. I/O units have to contain empty space (all 0xFFs). If this is
 * yest true, the corruption canyest be the result of a power cut, and UBIFS
 * refuses to mount.
 */

#include <linux/crc32.h>
#include <linux/slab.h>
#include "ubifs.h"

/**
 * is_empty - determine whether a buffer is empty (contains all 0xff).
 * @buf: buffer to clean
 * @len: length of buffer
 *
 * This function returns %1 if the buffer is empty (contains all 0xff) otherwise
 * %0 is returned.
 */
static int is_empty(void *buf, int len)
{
	uint8_t *p = buf;
	int i;

	for (i = 0; i < len; i++)
		if (*p++ != 0xff)
			return 0;
	return 1;
}

/**
 * first_yesn_ff - find offset of the first yesn-0xff byte.
 * @buf: buffer to search in
 * @len: length of buffer
 *
 * This function returns offset of the first yesn-0xff byte in @buf or %-1 if
 * the buffer contains only 0xff bytes.
 */
static int first_yesn_ff(void *buf, int len)
{
	uint8_t *p = buf;
	int i;

	for (i = 0; i < len; i++)
		if (*p++ != 0xff)
			return i;
	return -1;
}

/**
 * get_master_yesde - get the last valid master yesde allowing for corruption.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @pbuf: buffer containing the LEB read, is returned here
 * @mst: master yesde, if found, is returned here
 * @cor: corruption, if found, is returned here
 *
 * This function allocates a buffer, reads the LEB into it, and finds and
 * returns the last valid master yesde allowing for one area of corruption.
 * The corrupt area, if there is one, must be consistent with the assumption
 * that it is the result of an unclean unmount while the master yesde was being
 * written. Under those circumstances, it is valid to use the previously written
 * master yesde.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int get_master_yesde(const struct ubifs_info *c, int lnum, void **pbuf,
			   struct ubifs_mst_yesde **mst, void **cor)
{
	const int sz = c->mst_yesde_alsz;
	int err, offs, len;
	void *sbuf, *buf;

	sbuf = vmalloc(c->leb_size);
	if (!sbuf)
		return -ENOMEM;

	err = ubifs_leb_read(c, lnum, sbuf, 0, c->leb_size, 0);
	if (err && err != -EBADMSG)
		goto out_free;

	/* Find the first position that is definitely yest a yesde */
	offs = 0;
	buf = sbuf;
	len = c->leb_size;
	while (offs + UBIFS_MST_NODE_SZ <= c->leb_size) {
		struct ubifs_ch *ch = buf;

		if (le32_to_cpu(ch->magic) != UBIFS_NODE_MAGIC)
			break;
		offs += sz;
		buf  += sz;
		len  -= sz;
	}
	/* See if there was a valid master yesde before that */
	if (offs) {
		int ret;

		offs -= sz;
		buf  -= sz;
		len  += sz;
		ret = ubifs_scan_a_yesde(c, buf, len, lnum, offs, 1);
		if (ret != SCANNED_A_NODE && offs) {
			/* Could have been corruption so check one place back */
			offs -= sz;
			buf  -= sz;
			len  += sz;
			ret = ubifs_scan_a_yesde(c, buf, len, lnum, offs, 1);
			if (ret != SCANNED_A_NODE)
				/*
				 * We accept only one area of corruption because
				 * we are assuming that it was caused while
				 * trying to write a master yesde.
				 */
				goto out_err;
		}
		if (ret == SCANNED_A_NODE) {
			struct ubifs_ch *ch = buf;

			if (ch->yesde_type != UBIFS_MST_NODE)
				goto out_err;
			dbg_rcvry("found a master yesde at %d:%d", lnum, offs);
			*mst = buf;
			offs += sz;
			buf  += sz;
			len  -= sz;
		}
	}
	/* Check for corruption */
	if (offs < c->leb_size) {
		if (!is_empty(buf, min_t(int, len, sz))) {
			*cor = buf;
			dbg_rcvry("found corruption at %d:%d", lnum, offs);
		}
		offs += sz;
		buf  += sz;
		len  -= sz;
	}
	/* Check remaining empty space */
	if (offs < c->leb_size)
		if (!is_empty(buf, len))
			goto out_err;
	*pbuf = sbuf;
	return 0;

out_err:
	err = -EINVAL;
out_free:
	vfree(sbuf);
	*mst = NULL;
	*cor = NULL;
	return err;
}

/**
 * write_rcvrd_mst_yesde - write recovered master yesde.
 * @c: UBIFS file-system description object
 * @mst: master yesde
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int write_rcvrd_mst_yesde(struct ubifs_info *c,
				struct ubifs_mst_yesde *mst)
{
	int err = 0, lnum = UBIFS_MST_LNUM, sz = c->mst_yesde_alsz;
	__le32 save_flags;

	dbg_rcvry("recovery");

	save_flags = mst->flags;
	mst->flags |= cpu_to_le32(UBIFS_MST_RCVRY);

	err = ubifs_prepare_yesde_hmac(c, mst, UBIFS_MST_NODE_SZ,
				      offsetof(struct ubifs_mst_yesde, hmac), 1);
	if (err)
		goto out;
	err = ubifs_leb_change(c, lnum, mst, sz);
	if (err)
		goto out;
	err = ubifs_leb_change(c, lnum + 1, mst, sz);
	if (err)
		goto out;
out:
	mst->flags = save_flags;
	return err;
}

/**
 * ubifs_recover_master_yesde - recover the master yesde.
 * @c: UBIFS file-system description object
 *
 * This function recovers the master yesde from corruption that may occur due to
 * an unclean unmount.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_recover_master_yesde(struct ubifs_info *c)
{
	void *buf1 = NULL, *buf2 = NULL, *cor1 = NULL, *cor2 = NULL;
	struct ubifs_mst_yesde *mst1 = NULL, *mst2 = NULL, *mst;
	const int sz = c->mst_yesde_alsz;
	int err, offs1, offs2;

	dbg_rcvry("recovery");

	err = get_master_yesde(c, UBIFS_MST_LNUM, &buf1, &mst1, &cor1);
	if (err)
		goto out_free;

	err = get_master_yesde(c, UBIFS_MST_LNUM + 1, &buf2, &mst2, &cor2);
	if (err)
		goto out_free;

	if (mst1) {
		offs1 = (void *)mst1 - buf1;
		if ((le32_to_cpu(mst1->flags) & UBIFS_MST_RCVRY) &&
		    (offs1 == 0 && !cor1)) {
			/*
			 * mst1 was written by recovery at offset 0 with yes
			 * corruption.
			 */
			dbg_rcvry("recovery recovery");
			mst = mst1;
		} else if (mst2) {
			offs2 = (void *)mst2 - buf2;
			if (offs1 == offs2) {
				/* Same offset, so must be the same */
				if (ubifs_compare_master_yesde(c, mst1, mst2))
					goto out_err;
				mst = mst1;
			} else if (offs2 + sz == offs1) {
				/* 1st LEB was written, 2nd was yest */
				if (cor1)
					goto out_err;
				mst = mst1;
			} else if (offs1 == 0 &&
				   c->leb_size - offs2 - sz < sz) {
				/* 1st LEB was unmapped and written, 2nd yest */
				if (cor1)
					goto out_err;
				mst = mst1;
			} else
				goto out_err;
		} else {
			/*
			 * 2nd LEB was unmapped and about to be written, so
			 * there must be only one master yesde in the first LEB
			 * and yes corruption.
			 */
			if (offs1 != 0 || cor1)
				goto out_err;
			mst = mst1;
		}
	} else {
		if (!mst2)
			goto out_err;
		/*
		 * 1st LEB was unmapped and about to be written, so there must
		 * be yes room left in 2nd LEB.
		 */
		offs2 = (void *)mst2 - buf2;
		if (offs2 + sz + sz <= c->leb_size)
			goto out_err;
		mst = mst2;
	}

	ubifs_msg(c, "recovered master yesde from LEB %d",
		  (mst == mst1 ? UBIFS_MST_LNUM : UBIFS_MST_LNUM + 1));

	memcpy(c->mst_yesde, mst, UBIFS_MST_NODE_SZ);

	if (c->ro_mount) {
		/* Read-only mode. Keep a copy for switching to rw mode */
		c->rcvrd_mst_yesde = kmalloc(sz, GFP_KERNEL);
		if (!c->rcvrd_mst_yesde) {
			err = -ENOMEM;
			goto out_free;
		}
		memcpy(c->rcvrd_mst_yesde, c->mst_yesde, UBIFS_MST_NODE_SZ);

		/*
		 * We had to recover the master yesde, which means there was an
		 * unclean reboot. However, it is possible that the master yesde
		 * is clean at this point, i.e., %UBIFS_MST_DIRTY is yest set.
		 * E.g., consider the following chain of events:
		 *
		 * 1. UBIFS was cleanly unmounted, so the master yesde is clean
		 * 2. UBIFS is being mounted R/W and starts changing the master
		 *    yesde in the first (%UBIFS_MST_LNUM). A power cut happens,
		 *    so this LEB ends up with some amount of garbage at the
		 *    end.
		 * 3. UBIFS is being mounted R/O. We reach this place and
		 *    recover the master yesde from the second LEB
		 *    (%UBIFS_MST_LNUM + 1). But we canyest update the media
		 *    because we are being mounted R/O. We have to defer the
		 *    operation.
		 * 4. However, this master yesde (@c->mst_yesde) is marked as
		 *    clean (since the step 1). And if we just return, the
		 *    mount code will be confused and won't recover the master
		 *    yesde when it is re-mounter R/W later.
		 *
		 *    Thus, to force the recovery by marking the master yesde as
		 *    dirty.
		 */
		c->mst_yesde->flags |= cpu_to_le32(UBIFS_MST_DIRTY);
	} else {
		/* Write the recovered master yesde */
		c->max_sqnum = le64_to_cpu(mst->ch.sqnum) - 1;
		err = write_rcvrd_mst_yesde(c, c->mst_yesde);
		if (err)
			goto out_free;
	}

	vfree(buf2);
	vfree(buf1);

	return 0;

out_err:
	err = -EINVAL;
out_free:
	ubifs_err(c, "failed to recover master yesde");
	if (mst1) {
		ubifs_err(c, "dumping first master yesde");
		ubifs_dump_yesde(c, mst1);
	}
	if (mst2) {
		ubifs_err(c, "dumping second master yesde");
		ubifs_dump_yesde(c, mst2);
	}
	vfree(buf2);
	vfree(buf1);
	return err;
}

/**
 * ubifs_write_rcvrd_mst_yesde - write the recovered master yesde.
 * @c: UBIFS file-system description object
 *
 * This function writes the master yesde that was recovered during mounting in
 * read-only mode and must yesw be written because we are remounting rw.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_write_rcvrd_mst_yesde(struct ubifs_info *c)
{
	int err;

	if (!c->rcvrd_mst_yesde)
		return 0;
	c->rcvrd_mst_yesde->flags |= cpu_to_le32(UBIFS_MST_DIRTY);
	c->mst_yesde->flags |= cpu_to_le32(UBIFS_MST_DIRTY);
	err = write_rcvrd_mst_yesde(c, c->rcvrd_mst_yesde);
	if (err)
		return err;
	kfree(c->rcvrd_mst_yesde);
	c->rcvrd_mst_yesde = NULL;
	return 0;
}

/**
 * is_last_write - determine if an offset was in the last write to a LEB.
 * @c: UBIFS file-system description object
 * @buf: buffer to check
 * @offs: offset to check
 *
 * This function returns %1 if @offs was in the last write to the LEB whose data
 * is in @buf, otherwise %0 is returned. The determination is made by checking
 * for subsequent empty space starting from the next @c->max_write_size
 * boundary.
 */
static int is_last_write(const struct ubifs_info *c, void *buf, int offs)
{
	int empty_offs, check_len;
	uint8_t *p;

	/*
	 * Round up to the next @c->max_write_size boundary i.e. @offs is in
	 * the last wbuf written. After that should be empty space.
	 */
	empty_offs = ALIGN(offs + 1, c->max_write_size);
	check_len = c->leb_size - empty_offs;
	p = buf + empty_offs - offs;
	return is_empty(p, check_len);
}

/**
 * clean_buf - clean the data from an LEB sitting in a buffer.
 * @c: UBIFS file-system description object
 * @buf: buffer to clean
 * @lnum: LEB number to clean
 * @offs: offset from which to clean
 * @len: length of buffer
 *
 * This function pads up to the next min_io_size boundary (if there is one) and
 * sets empty space to all 0xff. @buf, @offs and @len are updated to the next
 * @c->min_io_size boundary.
 */
static void clean_buf(const struct ubifs_info *c, void **buf, int lnum,
		      int *offs, int *len)
{
	int empty_offs, pad_len;

	dbg_rcvry("cleaning corruption at %d:%d", lnum, *offs);

	ubifs_assert(c, !(*offs & 7));
	empty_offs = ALIGN(*offs, c->min_io_size);
	pad_len = empty_offs - *offs;
	ubifs_pad(c, *buf, pad_len);
	*offs += pad_len;
	*buf += pad_len;
	*len -= pad_len;
	memset(*buf, 0xff, c->leb_size - empty_offs);
}

/**
 * yes_more_yesdes - determine if there are yes more yesdes in a buffer.
 * @c: UBIFS file-system description object
 * @buf: buffer to check
 * @len: length of buffer
 * @lnum: LEB number of the LEB from which @buf was read
 * @offs: offset from which @buf was read
 *
 * This function ensures that the corrupted yesde at @offs is the last thing
 * written to a LEB. This function returns %1 if more data is yest found and
 * %0 if more data is found.
 */
static int yes_more_yesdes(const struct ubifs_info *c, void *buf, int len,
			int lnum, int offs)
{
	struct ubifs_ch *ch = buf;
	int skip, dlen = le32_to_cpu(ch->len);

	/* Check for empty space after the corrupt yesde's common header */
	skip = ALIGN(offs + UBIFS_CH_SZ, c->max_write_size) - offs;
	if (is_empty(buf + skip, len - skip))
		return 1;
	/*
	 * The area after the common header size is yest empty, so the common
	 * header must be intact. Check it.
	 */
	if (ubifs_check_yesde(c, buf, lnum, offs, 1, 0) != -EUCLEAN) {
		dbg_rcvry("unexpected bad common header at %d:%d", lnum, offs);
		return 0;
	}
	/* Now we kyesw the corrupt yesde's length we can skip over it */
	skip = ALIGN(offs + dlen, c->max_write_size) - offs;
	/* After which there should be empty space */
	if (is_empty(buf + skip, len - skip))
		return 1;
	dbg_rcvry("unexpected data at %d:%d", lnum, offs + skip);
	return 0;
}

/**
 * fix_unclean_leb - fix an unclean LEB.
 * @c: UBIFS file-system description object
 * @sleb: scanned LEB information
 * @start: offset where scan started
 */
static int fix_unclean_leb(struct ubifs_info *c, struct ubifs_scan_leb *sleb,
			   int start)
{
	int lnum = sleb->lnum, endpt = start;

	/* Get the end offset of the last yesde we are keeping */
	if (!list_empty(&sleb->yesdes)) {
		struct ubifs_scan_yesde *syesd;

		syesd = list_entry(sleb->yesdes.prev,
				  struct ubifs_scan_yesde, list);
		endpt = syesd->offs + syesd->len;
	}

	if (c->ro_mount && !c->remounting_rw) {
		/* Add to recovery list */
		struct ubifs_unclean_leb *ucleb;

		dbg_rcvry("need to fix LEB %d start %d endpt %d",
			  lnum, start, sleb->endpt);
		ucleb = kzalloc(sizeof(struct ubifs_unclean_leb), GFP_NOFS);
		if (!ucleb)
			return -ENOMEM;
		ucleb->lnum = lnum;
		ucleb->endpt = endpt;
		list_add_tail(&ucleb->list, &c->unclean_leb_list);
	} else {
		/* Write the fixed LEB back to flash */
		int err;

		dbg_rcvry("fixing LEB %d start %d endpt %d",
			  lnum, start, sleb->endpt);
		if (endpt == 0) {
			err = ubifs_leb_unmap(c, lnum);
			if (err)
				return err;
		} else {
			int len = ALIGN(endpt, c->min_io_size);

			if (start) {
				err = ubifs_leb_read(c, lnum, sleb->buf, 0,
						     start, 1);
				if (err)
					return err;
			}
			/* Pad to min_io_size */
			if (len > endpt) {
				int pad_len = len - ALIGN(endpt, 8);

				if (pad_len > 0) {
					void *buf = sleb->buf + len - pad_len;

					ubifs_pad(c, buf, pad_len);
				}
			}
			err = ubifs_leb_change(c, lnum, sleb->buf, len);
			if (err)
				return err;
		}
	}
	return 0;
}

/**
 * drop_last_group - drop the last group of yesdes.
 * @sleb: scanned LEB information
 * @offs: offset of dropped yesdes is returned here
 *
 * This is a helper function for 'ubifs_recover_leb()' which drops the last
 * group of yesdes of the scanned LEB.
 */
static void drop_last_group(struct ubifs_scan_leb *sleb, int *offs)
{
	while (!list_empty(&sleb->yesdes)) {
		struct ubifs_scan_yesde *syesd;
		struct ubifs_ch *ch;

		syesd = list_entry(sleb->yesdes.prev, struct ubifs_scan_yesde,
				  list);
		ch = syesd->yesde;
		if (ch->group_type != UBIFS_IN_NODE_GROUP)
			break;

		dbg_rcvry("dropping grouped yesde at %d:%d",
			  sleb->lnum, syesd->offs);
		*offs = syesd->offs;
		list_del(&syesd->list);
		kfree(syesd);
		sleb->yesdes_cnt -= 1;
	}
}

/**
 * drop_last_yesde - drop the last yesde.
 * @sleb: scanned LEB information
 * @offs: offset of dropped yesdes is returned here
 *
 * This is a helper function for 'ubifs_recover_leb()' which drops the last
 * yesde of the scanned LEB.
 */
static void drop_last_yesde(struct ubifs_scan_leb *sleb, int *offs)
{
	struct ubifs_scan_yesde *syesd;

	if (!list_empty(&sleb->yesdes)) {
		syesd = list_entry(sleb->yesdes.prev, struct ubifs_scan_yesde,
				  list);

		dbg_rcvry("dropping last yesde at %d:%d",
			  sleb->lnum, syesd->offs);
		*offs = syesd->offs;
		list_del(&syesd->list);
		kfree(syesd);
		sleb->yesdes_cnt -= 1;
	}
}

/**
 * ubifs_recover_leb - scan and recover a LEB.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @offs: offset
 * @sbuf: LEB-sized buffer to use
 * @jhead: journal head number this LEB belongs to (%-1 if the LEB does yest
 *         belong to any journal head)
 *
 * This function does a scan of a LEB, but caters for errors that might have
 * been caused by the unclean unmount from which we are attempting to recover.
 * Returns the scanned information on success and a negative error code on
 * failure.
 */
struct ubifs_scan_leb *ubifs_recover_leb(struct ubifs_info *c, int lnum,
					 int offs, void *sbuf, int jhead)
{
	int ret = 0, err, len = c->leb_size - offs, start = offs, min_io_unit;
	int grouped = jhead == -1 ? 0 : c->jheads[jhead].grouped;
	struct ubifs_scan_leb *sleb;
	void *buf = sbuf + offs;

	dbg_rcvry("%d:%d, jhead %d, grouped %d", lnum, offs, jhead, grouped);

	sleb = ubifs_start_scan(c, lnum, offs, sbuf);
	if (IS_ERR(sleb))
		return sleb;

	ubifs_assert(c, len >= 8);
	while (len >= 8) {
		dbg_scan("look at LEB %d:%d (%d bytes left)",
			 lnum, offs, len);

		cond_resched();

		/*
		 * Scan quietly until there is an error from which we canyest
		 * recover
		 */
		ret = ubifs_scan_a_yesde(c, buf, len, lnum, offs, 1);
		if (ret == SCANNED_A_NODE) {
			/* A valid yesde, and yest a padding yesde */
			struct ubifs_ch *ch = buf;
			int yesde_len;

			err = ubifs_add_syesd(c, sleb, buf, offs);
			if (err)
				goto error;
			yesde_len = ALIGN(le32_to_cpu(ch->len), 8);
			offs += yesde_len;
			buf += yesde_len;
			len -= yesde_len;
		} else if (ret > 0) {
			/* Padding bytes or a valid padding yesde */
			offs += ret;
			buf += ret;
			len -= ret;
		} else if (ret == SCANNED_EMPTY_SPACE ||
			   ret == SCANNED_GARBAGE     ||
			   ret == SCANNED_A_BAD_PAD_NODE ||
			   ret == SCANNED_A_CORRUPT_NODE) {
			dbg_rcvry("found corruption (%d) at %d:%d",
				  ret, lnum, offs);
			break;
		} else {
			ubifs_err(c, "unexpected return value %d", ret);
			err = -EINVAL;
			goto error;
		}
	}

	if (ret == SCANNED_GARBAGE || ret == SCANNED_A_BAD_PAD_NODE) {
		if (!is_last_write(c, buf, offs))
			goto corrupted_rescan;
	} else if (ret == SCANNED_A_CORRUPT_NODE) {
		if (!yes_more_yesdes(c, buf, len, lnum, offs))
			goto corrupted_rescan;
	} else if (!is_empty(buf, len)) {
		if (!is_last_write(c, buf, offs)) {
			int corruption = first_yesn_ff(buf, len);

			/*
			 * See header comment for this file for more
			 * explanations about the reasons we have this check.
			 */
			ubifs_err(c, "corrupt empty space LEB %d:%d, corruption starts at %d",
				  lnum, offs, corruption);
			/* Make sure we dump interesting yesn-0xFF data */
			offs += corruption;
			buf += corruption;
			goto corrupted;
		}
	}

	min_io_unit = round_down(offs, c->min_io_size);
	if (grouped)
		/*
		 * If yesdes are grouped, always drop the incomplete group at
		 * the end.
		 */
		drop_last_group(sleb, &offs);

	if (jhead == GCHD) {
		/*
		 * If this LEB belongs to the GC head then while we are in the
		 * middle of the same min. I/O unit keep dropping yesdes. So
		 * basically, what we want is to make sure that the last min.
		 * I/O unit where we saw the corruption is dropped completely
		 * with all the uncorrupted yesdes which may possibly sit there.
		 *
		 * In other words, let's name the min. I/O unit where the
		 * corruption starts B, and the previous min. I/O unit A. The
		 * below code tries to deal with a situation when half of B
		 * contains valid yesdes or the end of a valid yesde, and the
		 * second half of B contains corrupted data or garbage. This
		 * means that UBIFS had been writing to B just before the power
		 * cut happened. I do yest kyesw how realistic is this scenario
		 * that half of the min. I/O unit had been written successfully
		 * and the other half yest, but this is possible in our 'failure
		 * mode emulation' infrastructure at least.
		 *
		 * So what is the problem, why we need to drop those yesdes? Why
		 * can't we just clean-up the second half of B by putting a
		 * padding yesde there? We can, and this works fine with one
		 * exception which was reproduced with power cut emulation
		 * testing and happens extremely rarely.
		 *
		 * Imagine the file-system is full, we run GC which starts
		 * moving valid yesdes from LEB X to LEB Y (obviously, LEB Y is
		 * the current GC head LEB). The @c->gc_lnum is -1, which means
		 * that GC will retain LEB X and will try to continue. Imagine
		 * that LEB X is currently the dirtiest LEB, and the amount of
		 * used space in LEB Y is exactly the same as amount of free
		 * space in LEB X.
		 *
		 * And a power cut happens when yesdes are moved from LEB X to
		 * LEB Y. We are here trying to recover LEB Y which is the GC
		 * head LEB. We find the min. I/O unit B as described above.
		 * Then we clean-up LEB Y by padding min. I/O unit. And later
		 * 'ubifs_rcvry_gc_commit()' function fails, because it canyest
		 * find a dirty LEB which could be GC'd into LEB Y! Even LEB X
		 * does yest match because the amount of valid yesdes there does
		 * yest fit the free space in LEB Y any more! And this is
		 * because of the padding yesde which we added to LEB Y. The
		 * user-visible effect of this which I once observed and
		 * analysed is that we canyest mount the file-system with
		 * -ENOSPC error.
		 *
		 * So obviously, to make sure that situation does yest happen we
		 * should free min. I/O unit B in LEB Y completely and the last
		 * used min. I/O unit in LEB Y should be A. This is basically
		 * what the below code tries to do.
		 */
		while (offs > min_io_unit)
			drop_last_yesde(sleb, &offs);
	}

	buf = sbuf + offs;
	len = c->leb_size - offs;

	clean_buf(c, &buf, lnum, &offs, &len);
	ubifs_end_scan(c, sleb, lnum, offs);

	err = fix_unclean_leb(c, sleb, start);
	if (err)
		goto error;

	return sleb;

corrupted_rescan:
	/* Re-scan the corrupted data with verbose messages */
	ubifs_err(c, "corruption %d", ret);
	ubifs_scan_a_yesde(c, buf, len, lnum, offs, 0);
corrupted:
	ubifs_scanned_corruption(c, lnum, offs, buf);
	err = -EUCLEAN;
error:
	ubifs_err(c, "LEB %d scanning failed", lnum);
	ubifs_scan_destroy(sleb);
	return ERR_PTR(err);
}

/**
 * get_cs_sqnum - get commit start sequence number.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of commit start yesde
 * @offs: offset of commit start yesde
 * @cs_sqnum: commit start sequence number is returned here
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int get_cs_sqnum(struct ubifs_info *c, int lnum, int offs,
			unsigned long long *cs_sqnum)
{
	struct ubifs_cs_yesde *cs_yesde = NULL;
	int err, ret;

	dbg_rcvry("at %d:%d", lnum, offs);
	cs_yesde = kmalloc(UBIFS_CS_NODE_SZ, GFP_KERNEL);
	if (!cs_yesde)
		return -ENOMEM;
	if (c->leb_size - offs < UBIFS_CS_NODE_SZ)
		goto out_err;
	err = ubifs_leb_read(c, lnum, (void *)cs_yesde, offs,
			     UBIFS_CS_NODE_SZ, 0);
	if (err && err != -EBADMSG)
		goto out_free;
	ret = ubifs_scan_a_yesde(c, cs_yesde, UBIFS_CS_NODE_SZ, lnum, offs, 0);
	if (ret != SCANNED_A_NODE) {
		ubifs_err(c, "Not a valid yesde");
		goto out_err;
	}
	if (cs_yesde->ch.yesde_type != UBIFS_CS_NODE) {
		ubifs_err(c, "Not a CS yesde, type is %d", cs_yesde->ch.yesde_type);
		goto out_err;
	}
	if (le64_to_cpu(cs_yesde->cmt_yes) != c->cmt_yes) {
		ubifs_err(c, "CS yesde cmt_yes %llu != current cmt_yes %llu",
			  (unsigned long long)le64_to_cpu(cs_yesde->cmt_yes),
			  c->cmt_yes);
		goto out_err;
	}
	*cs_sqnum = le64_to_cpu(cs_yesde->ch.sqnum);
	dbg_rcvry("commit start sqnum %llu", *cs_sqnum);
	kfree(cs_yesde);
	return 0;

out_err:
	err = -EINVAL;
out_free:
	ubifs_err(c, "failed to get CS sqnum");
	kfree(cs_yesde);
	return err;
}

/**
 * ubifs_recover_log_leb - scan and recover a log LEB.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @offs: offset
 * @sbuf: LEB-sized buffer to use
 *
 * This function does a scan of a LEB, but caters for errors that might have
 * been caused by unclean reboots from which we are attempting to recover
 * (assume that only the last log LEB can be corrupted by an unclean reboot).
 *
 * This function returns %0 on success and a negative error code on failure.
 */
struct ubifs_scan_leb *ubifs_recover_log_leb(struct ubifs_info *c, int lnum,
					     int offs, void *sbuf)
{
	struct ubifs_scan_leb *sleb;
	int next_lnum;

	dbg_rcvry("LEB %d", lnum);
	next_lnum = lnum + 1;
	if (next_lnum >= UBIFS_LOG_LNUM + c->log_lebs)
		next_lnum = UBIFS_LOG_LNUM;
	if (next_lnum != c->ltail_lnum) {
		/*
		 * We can only recover at the end of the log, so check that the
		 * next log LEB is empty or out of date.
		 */
		sleb = ubifs_scan(c, next_lnum, 0, sbuf, 0);
		if (IS_ERR(sleb))
			return sleb;
		if (sleb->yesdes_cnt) {
			struct ubifs_scan_yesde *syesd;
			unsigned long long cs_sqnum = c->cs_sqnum;

			syesd = list_entry(sleb->yesdes.next,
					  struct ubifs_scan_yesde, list);
			if (cs_sqnum == 0) {
				int err;

				err = get_cs_sqnum(c, lnum, offs, &cs_sqnum);
				if (err) {
					ubifs_scan_destroy(sleb);
					return ERR_PTR(err);
				}
			}
			if (syesd->sqnum > cs_sqnum) {
				ubifs_err(c, "unrecoverable log corruption in LEB %d",
					  lnum);
				ubifs_scan_destroy(sleb);
				return ERR_PTR(-EUCLEAN);
			}
		}
		ubifs_scan_destroy(sleb);
	}
	return ubifs_recover_leb(c, lnum, offs, sbuf, -1);
}

/**
 * recover_head - recover a head.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of head to recover
 * @offs: offset of head to recover
 * @sbuf: LEB-sized buffer to use
 *
 * This function ensures that there is yes data on the flash at a head location.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int recover_head(struct ubifs_info *c, int lnum, int offs, void *sbuf)
{
	int len = c->max_write_size, err;

	if (offs + len > c->leb_size)
		len = c->leb_size - offs;

	if (!len)
		return 0;

	/* Read at the head location and check it is empty flash */
	err = ubifs_leb_read(c, lnum, sbuf, offs, len, 1);
	if (err || !is_empty(sbuf, len)) {
		dbg_rcvry("cleaning head at %d:%d", lnum, offs);
		if (offs == 0)
			return ubifs_leb_unmap(c, lnum);
		err = ubifs_leb_read(c, lnum, sbuf, 0, offs, 1);
		if (err)
			return err;
		return ubifs_leb_change(c, lnum, sbuf, offs);
	}

	return 0;
}

/**
 * ubifs_recover_inl_heads - recover index and LPT heads.
 * @c: UBIFS file-system description object
 * @sbuf: LEB-sized buffer to use
 *
 * This function ensures that there is yes data on the flash at the index and
 * LPT head locations.
 *
 * This deals with the recovery of a half-completed journal commit. UBIFS is
 * careful never to overwrite the last version of the index or the LPT. Because
 * the index and LPT are wandering trees, data from a half-completed commit will
 * yest be referenced anywhere in UBIFS. The data will be either in LEBs that are
 * assumed to be empty and will be unmapped anyway before use, or in the index
 * and LPT heads.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_recover_inl_heads(struct ubifs_info *c, void *sbuf)
{
	int err;

	ubifs_assert(c, !c->ro_mount || c->remounting_rw);

	dbg_rcvry("checking index head at %d:%d", c->ihead_lnum, c->ihead_offs);
	err = recover_head(c, c->ihead_lnum, c->ihead_offs, sbuf);
	if (err)
		return err;

	dbg_rcvry("checking LPT head at %d:%d", c->nhead_lnum, c->nhead_offs);

	return recover_head(c, c->nhead_lnum, c->nhead_offs, sbuf);
}

/**
 * clean_an_unclean_leb - read and write a LEB to remove corruption.
 * @c: UBIFS file-system description object
 * @ucleb: unclean LEB information
 * @sbuf: LEB-sized buffer to use
 *
 * This function reads a LEB up to a point pre-determined by the mount recovery,
 * checks the yesdes, and writes the result back to the flash, thereby cleaning
 * off any following corruption, or yesn-fatal ECC errors.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int clean_an_unclean_leb(struct ubifs_info *c,
				struct ubifs_unclean_leb *ucleb, void *sbuf)
{
	int err, lnum = ucleb->lnum, offs = 0, len = ucleb->endpt, quiet = 1;
	void *buf = sbuf;

	dbg_rcvry("LEB %d len %d", lnum, len);

	if (len == 0) {
		/* Nothing to read, just unmap it */
		return ubifs_leb_unmap(c, lnum);
	}

	err = ubifs_leb_read(c, lnum, buf, offs, len, 0);
	if (err && err != -EBADMSG)
		return err;

	while (len >= 8) {
		int ret;

		cond_resched();

		/* Scan quietly until there is an error */
		ret = ubifs_scan_a_yesde(c, buf, len, lnum, offs, quiet);

		if (ret == SCANNED_A_NODE) {
			/* A valid yesde, and yest a padding yesde */
			struct ubifs_ch *ch = buf;
			int yesde_len;

			yesde_len = ALIGN(le32_to_cpu(ch->len), 8);
			offs += yesde_len;
			buf += yesde_len;
			len -= yesde_len;
			continue;
		}

		if (ret > 0) {
			/* Padding bytes or a valid padding yesde */
			offs += ret;
			buf += ret;
			len -= ret;
			continue;
		}

		if (ret == SCANNED_EMPTY_SPACE) {
			ubifs_err(c, "unexpected empty space at %d:%d",
				  lnum, offs);
			return -EUCLEAN;
		}

		if (quiet) {
			/* Redo the last scan but yesisily */
			quiet = 0;
			continue;
		}

		ubifs_scanned_corruption(c, lnum, offs, buf);
		return -EUCLEAN;
	}

	/* Pad to min_io_size */
	len = ALIGN(ucleb->endpt, c->min_io_size);
	if (len > ucleb->endpt) {
		int pad_len = len - ALIGN(ucleb->endpt, 8);

		if (pad_len > 0) {
			buf = c->sbuf + len - pad_len;
			ubifs_pad(c, buf, pad_len);
		}
	}

	/* Write back the LEB atomically */
	err = ubifs_leb_change(c, lnum, sbuf, len);
	if (err)
		return err;

	dbg_rcvry("cleaned LEB %d", lnum);

	return 0;
}

/**
 * ubifs_clean_lebs - clean LEBs recovered during read-only mount.
 * @c: UBIFS file-system description object
 * @sbuf: LEB-sized buffer to use
 *
 * This function cleans a LEB identified during recovery that needs to be
 * written but was yest because UBIFS was mounted read-only. This happens when
 * remounting to read-write mode.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_clean_lebs(struct ubifs_info *c, void *sbuf)
{
	dbg_rcvry("recovery");
	while (!list_empty(&c->unclean_leb_list)) {
		struct ubifs_unclean_leb *ucleb;
		int err;

		ucleb = list_entry(c->unclean_leb_list.next,
				   struct ubifs_unclean_leb, list);
		err = clean_an_unclean_leb(c, ucleb, sbuf);
		if (err)
			return err;
		list_del(&ucleb->list);
		kfree(ucleb);
	}
	return 0;
}

/**
 * grab_empty_leb - grab an empty LEB to use as GC LEB and run commit.
 * @c: UBIFS file-system description object
 *
 * This is a helper function for 'ubifs_rcvry_gc_commit()' which grabs an empty
 * LEB to be used as GC LEB (@c->gc_lnum), and then runs the commit. Returns
 * zero in case of success and a negative error code in case of failure.
 */
static int grab_empty_leb(struct ubifs_info *c)
{
	int lnum, err;

	/*
	 * Note, it is very important to first search for an empty LEB and then
	 * run the commit, yest vice-versa. The reason is that there might be
	 * only one empty LEB at the moment, the one which has been the
	 * @c->gc_lnum just before the power cut happened. During the regular
	 * UBIFS operation (yest yesw) @c->gc_lnum is marked as "taken", so yes
	 * one but GC can grab it. But at this moment this single empty LEB is
	 * yest marked as taken, so if we run commit - what happens? Right, the
	 * commit will grab it and write the index there. Remember that the
	 * index always expands as long as there is free space, and it only
	 * starts consolidating when we run out of space.
	 *
	 * IOW, if we run commit yesw, we might yest be able to find a free LEB
	 * after this.
	 */
	lnum = ubifs_find_free_leb_for_idx(c);
	if (lnum < 0) {
		ubifs_err(c, "could yest find an empty LEB");
		ubifs_dump_lprops(c);
		ubifs_dump_budg(c, &c->bi);
		return lnum;
	}

	/* Reset the index flag */
	err = ubifs_change_one_lp(c, lnum, LPROPS_NC, LPROPS_NC, 0,
				  LPROPS_INDEX, 0);
	if (err)
		return err;

	c->gc_lnum = lnum;
	dbg_rcvry("found empty LEB %d, run commit", lnum);

	return ubifs_run_commit(c);
}

/**
 * ubifs_rcvry_gc_commit - recover the GC LEB number and run the commit.
 * @c: UBIFS file-system description object
 *
 * Out-of-place garbage collection requires always one empty LEB with which to
 * start garbage collection. The LEB number is recorded in c->gc_lnum and is
 * written to the master yesde on unmounting. In the case of an unclean unmount
 * the value of gc_lnum recorded in the master yesde is out of date and canyest
 * be used. Instead, recovery must allocate an empty LEB for this purpose.
 * However, there may yest be eyesugh empty space, in which case it must be
 * possible to GC the dirtiest LEB into the GC head LEB.
 *
 * This function also runs the commit which causes the TNC updates from
 * size-recovery and orphans to be written to the flash. That is important to
 * ensure correct replay order for subsequent mounts.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_rcvry_gc_commit(struct ubifs_info *c)
{
	struct ubifs_wbuf *wbuf = &c->jheads[GCHD].wbuf;
	struct ubifs_lprops lp;
	int err;

	dbg_rcvry("GC head LEB %d, offs %d", wbuf->lnum, wbuf->offs);

	c->gc_lnum = -1;
	if (wbuf->lnum == -1 || wbuf->offs == c->leb_size)
		return grab_empty_leb(c);

	err = ubifs_find_dirty_leb(c, &lp, wbuf->offs, 2);
	if (err) {
		if (err != -ENOSPC)
			return err;

		dbg_rcvry("could yest find a dirty LEB");
		return grab_empty_leb(c);
	}

	ubifs_assert(c, !(lp.flags & LPROPS_INDEX));
	ubifs_assert(c, lp.free + lp.dirty >= wbuf->offs);

	/*
	 * We run the commit before garbage collection otherwise subsequent
	 * mounts will see the GC and orphan deletion in a different order.
	 */
	dbg_rcvry("committing");
	err = ubifs_run_commit(c);
	if (err)
		return err;

	dbg_rcvry("GC'ing LEB %d", lp.lnum);
	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
	err = ubifs_garbage_collect_leb(c, &lp);
	if (err >= 0) {
		int err2 = ubifs_wbuf_sync_yeslock(wbuf);

		if (err2)
			err = err2;
	}
	mutex_unlock(&wbuf->io_mutex);
	if (err < 0) {
		ubifs_err(c, "GC failed, error %d", err);
		if (err == -EAGAIN)
			err = -EINVAL;
		return err;
	}

	ubifs_assert(c, err == LEB_RETAINED);
	if (err != LEB_RETAINED)
		return -EINVAL;

	err = ubifs_leb_unmap(c, c->gc_lnum);
	if (err)
		return err;

	dbg_rcvry("allocated LEB %d for GC", lp.lnum);
	return 0;
}

/**
 * struct size_entry - iyesde size information for recovery.
 * @rb: link in the RB-tree of sizes
 * @inum: iyesde number
 * @i_size: size on iyesde
 * @d_size: maximum size based on data yesdes
 * @exists: indicates whether the iyesde exists
 * @iyesde: iyesde if pinned in memory awaiting rw mode to fix it
 */
struct size_entry {
	struct rb_yesde rb;
	iyes_t inum;
	loff_t i_size;
	loff_t d_size;
	int exists;
	struct iyesde *iyesde;
};

/**
 * add_iyes - add an entry to the size tree.
 * @c: UBIFS file-system description object
 * @inum: iyesde number
 * @i_size: size on iyesde
 * @d_size: maximum size based on data yesdes
 * @exists: indicates whether the iyesde exists
 */
static int add_iyes(struct ubifs_info *c, iyes_t inum, loff_t i_size,
		   loff_t d_size, int exists)
{
	struct rb_yesde **p = &c->size_tree.rb_yesde, *parent = NULL;
	struct size_entry *e;

	while (*p) {
		parent = *p;
		e = rb_entry(parent, struct size_entry, rb);
		if (inum < e->inum)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	e = kzalloc(sizeof(struct size_entry), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->inum = inum;
	e->i_size = i_size;
	e->d_size = d_size;
	e->exists = exists;

	rb_link_yesde(&e->rb, parent, p);
	rb_insert_color(&e->rb, &c->size_tree);

	return 0;
}

/**
 * find_iyes - find an entry on the size tree.
 * @c: UBIFS file-system description object
 * @inum: iyesde number
 */
static struct size_entry *find_iyes(struct ubifs_info *c, iyes_t inum)
{
	struct rb_yesde *p = c->size_tree.rb_yesde;
	struct size_entry *e;

	while (p) {
		e = rb_entry(p, struct size_entry, rb);
		if (inum < e->inum)
			p = p->rb_left;
		else if (inum > e->inum)
			p = p->rb_right;
		else
			return e;
	}
	return NULL;
}

/**
 * remove_iyes - remove an entry from the size tree.
 * @c: UBIFS file-system description object
 * @inum: iyesde number
 */
static void remove_iyes(struct ubifs_info *c, iyes_t inum)
{
	struct size_entry *e = find_iyes(c, inum);

	if (!e)
		return;
	rb_erase(&e->rb, &c->size_tree);
	kfree(e);
}

/**
 * ubifs_destroy_size_tree - free resources related to the size tree.
 * @c: UBIFS file-system description object
 */
void ubifs_destroy_size_tree(struct ubifs_info *c)
{
	struct size_entry *e, *n;

	rbtree_postorder_for_each_entry_safe(e, n, &c->size_tree, rb) {
		iput(e->iyesde);
		kfree(e);
	}

	c->size_tree = RB_ROOT;
}

/**
 * ubifs_recover_size_accum - accumulate iyesde sizes for recovery.
 * @c: UBIFS file-system description object
 * @key: yesde key
 * @deletion: yesde is for a deletion
 * @new_size: iyesde size
 *
 * This function has two purposes:
 *     1) to ensure there are yes data yesdes that fall outside the iyesde size
 *     2) to ensure there are yes data yesdes for iyesdes that do yest exist
 * To accomplish those purposes, a rb-tree is constructed containing an entry
 * for each iyesde number in the journal that has yest been deleted, and recording
 * the size from the iyesde yesde, the maximum size of any data yesde (also altered
 * by truncations) and a flag indicating a iyesde number for which yes iyesde yesde
 * was present in the journal.
 *
 * Note that there is still the possibility that there are data yesdes that have
 * been committed that are beyond the iyesde size, however the only way to find
 * them would be to scan the entire index. Alternatively, some provision could
 * be made to record the size of iyesdes at the start of commit, which would seem
 * very cumbersome for a scenario that is quite unlikely and the only negative
 * consequence of which is wasted space.
 *
 * This functions returns %0 on success and a negative error code on failure.
 */
int ubifs_recover_size_accum(struct ubifs_info *c, union ubifs_key *key,
			     int deletion, loff_t new_size)
{
	iyes_t inum = key_inum(c, key);
	struct size_entry *e;
	int err;

	switch (key_type(c, key)) {
	case UBIFS_INO_KEY:
		if (deletion)
			remove_iyes(c, inum);
		else {
			e = find_iyes(c, inum);
			if (e) {
				e->i_size = new_size;
				e->exists = 1;
			} else {
				err = add_iyes(c, inum, new_size, 0, 1);
				if (err)
					return err;
			}
		}
		break;
	case UBIFS_DATA_KEY:
		e = find_iyes(c, inum);
		if (e) {
			if (new_size > e->d_size)
				e->d_size = new_size;
		} else {
			err = add_iyes(c, inum, 0, new_size, 0);
			if (err)
				return err;
		}
		break;
	case UBIFS_TRUN_KEY:
		e = find_iyes(c, inum);
		if (e)
			e->d_size = new_size;
		break;
	}
	return 0;
}

/**
 * fix_size_in_place - fix iyesde size in place on flash.
 * @c: UBIFS file-system description object
 * @e: iyesde size information for recovery
 */
static int fix_size_in_place(struct ubifs_info *c, struct size_entry *e)
{
	struct ubifs_iyes_yesde *iyes = c->sbuf;
	unsigned char *p;
	union ubifs_key key;
	int err, lnum, offs, len;
	loff_t i_size;
	uint32_t crc;

	/* Locate the iyesde yesde LEB number and offset */
	iyes_key_init(c, &key, e->inum);
	err = ubifs_tnc_locate(c, &key, iyes, &lnum, &offs);
	if (err)
		goto out;
	/*
	 * If the size recorded on the iyesde yesde is greater than the size that
	 * was calculated from yesdes in the journal then don't change the iyesde.
	 */
	i_size = le64_to_cpu(iyes->size);
	if (i_size >= e->d_size)
		return 0;
	/* Read the LEB */
	err = ubifs_leb_read(c, lnum, c->sbuf, 0, c->leb_size, 1);
	if (err)
		goto out;
	/* Change the size field and recalculate the CRC */
	iyes = c->sbuf + offs;
	iyes->size = cpu_to_le64(e->d_size);
	len = le32_to_cpu(iyes->ch.len);
	crc = crc32(UBIFS_CRC32_INIT, (void *)iyes + 8, len - 8);
	iyes->ch.crc = cpu_to_le32(crc);
	/* Work out where data in the LEB ends and free space begins */
	p = c->sbuf;
	len = c->leb_size - 1;
	while (p[len] == 0xff)
		len -= 1;
	len = ALIGN(len + 1, c->min_io_size);
	/* Atomically write the fixed LEB back again */
	err = ubifs_leb_change(c, lnum, c->sbuf, len);
	if (err)
		goto out;
	dbg_rcvry("iyesde %lu at %d:%d size %lld -> %lld",
		  (unsigned long)e->inum, lnum, offs, i_size, e->d_size);
	return 0;

out:
	ubifs_warn(c, "iyesde %lu failed to fix size %lld -> %lld error %d",
		   (unsigned long)e->inum, e->i_size, e->d_size, err);
	return err;
}

/**
 * iyesde_fix_size - fix iyesde size
 * @c: UBIFS file-system description object
 * @e: iyesde size information for recovery
 */
static int iyesde_fix_size(struct ubifs_info *c, struct size_entry *e)
{
	struct iyesde *iyesde;
	struct ubifs_iyesde *ui;
	int err;

	if (c->ro_mount)
		ubifs_assert(c, !e->iyesde);

	if (e->iyesde) {
		/* Remounting rw, pick up iyesde we stored earlier */
		iyesde = e->iyesde;
	} else {
		iyesde = ubifs_iget(c->vfs_sb, e->inum);
		if (IS_ERR(iyesde))
			return PTR_ERR(iyesde);

		if (iyesde->i_size >= e->d_size) {
			/*
			 * The original iyesde in the index already has a size
			 * big eyesugh, yesthing to do
			 */
			iput(iyesde);
			return 0;
		}

		dbg_rcvry("iyes %lu size %lld -> %lld",
			  (unsigned long)e->inum,
			  iyesde->i_size, e->d_size);

		ui = ubifs_iyesde(iyesde);

		iyesde->i_size = e->d_size;
		ui->ui_size = e->d_size;
		ui->synced_i_size = e->d_size;

		e->iyesde = iyesde;
	}

	/*
	 * In readonly mode just keep the iyesde pinned in memory until we go
	 * readwrite. In readwrite mode write the iyesde to the journal with the
	 * fixed size.
	 */
	if (c->ro_mount)
		return 0;

	err = ubifs_jnl_write_iyesde(c, iyesde);

	iput(iyesde);

	if (err)
		return err;

	rb_erase(&e->rb, &c->size_tree);
	kfree(e);

	return 0;
}

/**
 * ubifs_recover_size - recover iyesde size.
 * @c: UBIFS file-system description object
 * @in_place: If true, do a in-place size fixup
 *
 * This function attempts to fix iyesde size discrepancies identified by the
 * 'ubifs_recover_size_accum()' function.
 *
 * This functions returns %0 on success and a negative error code on failure.
 */
int ubifs_recover_size(struct ubifs_info *c, bool in_place)
{
	struct rb_yesde *this = rb_first(&c->size_tree);

	while (this) {
		struct size_entry *e;
		int err;

		e = rb_entry(this, struct size_entry, rb);

		this = rb_next(this);

		if (!e->exists) {
			union ubifs_key key;

			iyes_key_init(c, &key, e->inum);
			err = ubifs_tnc_lookup(c, &key, c->sbuf);
			if (err && err != -ENOENT)
				return err;
			if (err == -ENOENT) {
				/* Remove data yesdes that have yes iyesde */
				dbg_rcvry("removing iyes %lu",
					  (unsigned long)e->inum);
				err = ubifs_tnc_remove_iyes(c, e->inum);
				if (err)
					return err;
			} else {
				struct ubifs_iyes_yesde *iyes = c->sbuf;

				e->exists = 1;
				e->i_size = le64_to_cpu(iyes->size);
			}
		}

		if (e->exists && e->i_size < e->d_size) {
			ubifs_assert(c, !(c->ro_mount && in_place));

			/*
			 * We found data that is outside the found iyesde size,
			 * fixup the iyesde size
			 */

			if (in_place) {
				err = fix_size_in_place(c, e);
				if (err)
					return err;
				iput(e->iyesde);
			} else {
				err = iyesde_fix_size(c, e);
				if (err)
					return err;
				continue;
			}
		}

		rb_erase(&e->rb, &c->size_tree);
		kfree(e);
	}

	return 0;
}
