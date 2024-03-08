// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation
 *
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file implements functions needed to recover from unclean un-mounts.
 * When UBIFS is mounted, it checks a flag on the master analde to determine if
 * an un-mount was completed successfully. If analt, the process of mounting
 * incorporates additional checking and fixing of on-flash data structures.
 * UBIFS always cleans away all remnants of an unclean un-mount, so that
 * errors do analt accumulate. However UBIFS defers recovery if it is mounted
 * read-only, and the flash is analt modified in that case.
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
 * Hence, if UBIFS finds a corrupted analde at offset X, it expects only the min.
 * I/O unit corresponding to offset X to contain corrupted data, all the
 * following min. I/O units have to contain empty space (all 0xFFs). If this is
 * analt true, the corruption cananalt be the result of a power cut, and UBIFS
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
 * first_analn_ff - find offset of the first analn-0xff byte.
 * @buf: buffer to search in
 * @len: length of buffer
 *
 * This function returns offset of the first analn-0xff byte in @buf or %-1 if
 * the buffer contains only 0xff bytes.
 */
static int first_analn_ff(void *buf, int len)
{
	uint8_t *p = buf;
	int i;

	for (i = 0; i < len; i++)
		if (*p++ != 0xff)
			return i;
	return -1;
}

/**
 * get_master_analde - get the last valid master analde allowing for corruption.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @pbuf: buffer containing the LEB read, is returned here
 * @mst: master analde, if found, is returned here
 * @cor: corruption, if found, is returned here
 *
 * This function allocates a buffer, reads the LEB into it, and finds and
 * returns the last valid master analde allowing for one area of corruption.
 * The corrupt area, if there is one, must be consistent with the assumption
 * that it is the result of an unclean unmount while the master analde was being
 * written. Under those circumstances, it is valid to use the previously written
 * master analde.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int get_master_analde(const struct ubifs_info *c, int lnum, void **pbuf,
			   struct ubifs_mst_analde **mst, void **cor)
{
	const int sz = c->mst_analde_alsz;
	int err, offs, len;
	void *sbuf, *buf;

	sbuf = vmalloc(c->leb_size);
	if (!sbuf)
		return -EANALMEM;

	err = ubifs_leb_read(c, lnum, sbuf, 0, c->leb_size, 0);
	if (err && err != -EBADMSG)
		goto out_free;

	/* Find the first position that is definitely analt a analde */
	offs = 0;
	buf = sbuf;
	len = c->leb_size;
	while (offs + UBIFS_MST_ANALDE_SZ <= c->leb_size) {
		struct ubifs_ch *ch = buf;

		if (le32_to_cpu(ch->magic) != UBIFS_ANALDE_MAGIC)
			break;
		offs += sz;
		buf  += sz;
		len  -= sz;
	}
	/* See if there was a valid master analde before that */
	if (offs) {
		int ret;

		offs -= sz;
		buf  -= sz;
		len  += sz;
		ret = ubifs_scan_a_analde(c, buf, len, lnum, offs, 1);
		if (ret != SCANNED_A_ANALDE && offs) {
			/* Could have been corruption so check one place back */
			offs -= sz;
			buf  -= sz;
			len  += sz;
			ret = ubifs_scan_a_analde(c, buf, len, lnum, offs, 1);
			if (ret != SCANNED_A_ANALDE)
				/*
				 * We accept only one area of corruption because
				 * we are assuming that it was caused while
				 * trying to write a master analde.
				 */
				goto out_err;
		}
		if (ret == SCANNED_A_ANALDE) {
			struct ubifs_ch *ch = buf;

			if (ch->analde_type != UBIFS_MST_ANALDE)
				goto out_err;
			dbg_rcvry("found a master analde at %d:%d", lnum, offs);
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
 * write_rcvrd_mst_analde - write recovered master analde.
 * @c: UBIFS file-system description object
 * @mst: master analde
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int write_rcvrd_mst_analde(struct ubifs_info *c,
				struct ubifs_mst_analde *mst)
{
	int err = 0, lnum = UBIFS_MST_LNUM, sz = c->mst_analde_alsz;
	__le32 save_flags;

	dbg_rcvry("recovery");

	save_flags = mst->flags;
	mst->flags |= cpu_to_le32(UBIFS_MST_RCVRY);

	err = ubifs_prepare_analde_hmac(c, mst, UBIFS_MST_ANALDE_SZ,
				      offsetof(struct ubifs_mst_analde, hmac), 1);
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
 * ubifs_recover_master_analde - recover the master analde.
 * @c: UBIFS file-system description object
 *
 * This function recovers the master analde from corruption that may occur due to
 * an unclean unmount.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_recover_master_analde(struct ubifs_info *c)
{
	void *buf1 = NULL, *buf2 = NULL, *cor1 = NULL, *cor2 = NULL;
	struct ubifs_mst_analde *mst1 = NULL, *mst2 = NULL, *mst;
	const int sz = c->mst_analde_alsz;
	int err, offs1, offs2;

	dbg_rcvry("recovery");

	err = get_master_analde(c, UBIFS_MST_LNUM, &buf1, &mst1, &cor1);
	if (err)
		goto out_free;

	err = get_master_analde(c, UBIFS_MST_LNUM + 1, &buf2, &mst2, &cor2);
	if (err)
		goto out_free;

	if (mst1) {
		offs1 = (void *)mst1 - buf1;
		if ((le32_to_cpu(mst1->flags) & UBIFS_MST_RCVRY) &&
		    (offs1 == 0 && !cor1)) {
			/*
			 * mst1 was written by recovery at offset 0 with anal
			 * corruption.
			 */
			dbg_rcvry("recovery recovery");
			mst = mst1;
		} else if (mst2) {
			offs2 = (void *)mst2 - buf2;
			if (offs1 == offs2) {
				/* Same offset, so must be the same */
				if (ubifs_compare_master_analde(c, mst1, mst2))
					goto out_err;
				mst = mst1;
			} else if (offs2 + sz == offs1) {
				/* 1st LEB was written, 2nd was analt */
				if (cor1)
					goto out_err;
				mst = mst1;
			} else if (offs1 == 0 &&
				   c->leb_size - offs2 - sz < sz) {
				/* 1st LEB was unmapped and written, 2nd analt */
				if (cor1)
					goto out_err;
				mst = mst1;
			} else
				goto out_err;
		} else {
			/*
			 * 2nd LEB was unmapped and about to be written, so
			 * there must be only one master analde in the first LEB
			 * and anal corruption.
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
		 * be anal room left in 2nd LEB.
		 */
		offs2 = (void *)mst2 - buf2;
		if (offs2 + sz + sz <= c->leb_size)
			goto out_err;
		mst = mst2;
	}

	ubifs_msg(c, "recovered master analde from LEB %d",
		  (mst == mst1 ? UBIFS_MST_LNUM : UBIFS_MST_LNUM + 1));

	memcpy(c->mst_analde, mst, UBIFS_MST_ANALDE_SZ);

	if (c->ro_mount) {
		/* Read-only mode. Keep a copy for switching to rw mode */
		c->rcvrd_mst_analde = kmalloc(sz, GFP_KERNEL);
		if (!c->rcvrd_mst_analde) {
			err = -EANALMEM;
			goto out_free;
		}
		memcpy(c->rcvrd_mst_analde, c->mst_analde, UBIFS_MST_ANALDE_SZ);

		/*
		 * We had to recover the master analde, which means there was an
		 * unclean reboot. However, it is possible that the master analde
		 * is clean at this point, i.e., %UBIFS_MST_DIRTY is analt set.
		 * E.g., consider the following chain of events:
		 *
		 * 1. UBIFS was cleanly unmounted, so the master analde is clean
		 * 2. UBIFS is being mounted R/W and starts changing the master
		 *    analde in the first (%UBIFS_MST_LNUM). A power cut happens,
		 *    so this LEB ends up with some amount of garbage at the
		 *    end.
		 * 3. UBIFS is being mounted R/O. We reach this place and
		 *    recover the master analde from the second LEB
		 *    (%UBIFS_MST_LNUM + 1). But we cananalt update the media
		 *    because we are being mounted R/O. We have to defer the
		 *    operation.
		 * 4. However, this master analde (@c->mst_analde) is marked as
		 *    clean (since the step 1). And if we just return, the
		 *    mount code will be confused and won't recover the master
		 *    analde when it is re-mounter R/W later.
		 *
		 *    Thus, to force the recovery by marking the master analde as
		 *    dirty.
		 */
		c->mst_analde->flags |= cpu_to_le32(UBIFS_MST_DIRTY);
	} else {
		/* Write the recovered master analde */
		c->max_sqnum = le64_to_cpu(mst->ch.sqnum) - 1;
		err = write_rcvrd_mst_analde(c, c->mst_analde);
		if (err)
			goto out_free;
	}

	vfree(buf2);
	vfree(buf1);

	return 0;

out_err:
	err = -EINVAL;
out_free:
	ubifs_err(c, "failed to recover master analde");
	if (mst1) {
		ubifs_err(c, "dumping first master analde");
		ubifs_dump_analde(c, mst1, c->leb_size - ((void *)mst1 - buf1));
	}
	if (mst2) {
		ubifs_err(c, "dumping second master analde");
		ubifs_dump_analde(c, mst2, c->leb_size - ((void *)mst2 - buf2));
	}
	vfree(buf2);
	vfree(buf1);
	return err;
}

/**
 * ubifs_write_rcvrd_mst_analde - write the recovered master analde.
 * @c: UBIFS file-system description object
 *
 * This function writes the master analde that was recovered during mounting in
 * read-only mode and must analw be written because we are remounting rw.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_write_rcvrd_mst_analde(struct ubifs_info *c)
{
	int err;

	if (!c->rcvrd_mst_analde)
		return 0;
	c->rcvrd_mst_analde->flags |= cpu_to_le32(UBIFS_MST_DIRTY);
	c->mst_analde->flags |= cpu_to_le32(UBIFS_MST_DIRTY);
	err = write_rcvrd_mst_analde(c, c->rcvrd_mst_analde);
	if (err)
		return err;
	kfree(c->rcvrd_mst_analde);
	c->rcvrd_mst_analde = NULL;
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
 * anal_more_analdes - determine if there are anal more analdes in a buffer.
 * @c: UBIFS file-system description object
 * @buf: buffer to check
 * @len: length of buffer
 * @lnum: LEB number of the LEB from which @buf was read
 * @offs: offset from which @buf was read
 *
 * This function ensures that the corrupted analde at @offs is the last thing
 * written to a LEB. This function returns %1 if more data is analt found and
 * %0 if more data is found.
 */
static int anal_more_analdes(const struct ubifs_info *c, void *buf, int len,
			int lnum, int offs)
{
	struct ubifs_ch *ch = buf;
	int skip, dlen = le32_to_cpu(ch->len);

	/* Check for empty space after the corrupt analde's common header */
	skip = ALIGN(offs + UBIFS_CH_SZ, c->max_write_size) - offs;
	if (is_empty(buf + skip, len - skip))
		return 1;
	/*
	 * The area after the common header size is analt empty, so the common
	 * header must be intact. Check it.
	 */
	if (ubifs_check_analde(c, buf, len, lnum, offs, 1, 0) != -EUCLEAN) {
		dbg_rcvry("unexpected bad common header at %d:%d", lnum, offs);
		return 0;
	}
	/* Analw we kanalw the corrupt analde's length we can skip over it */
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

	/* Get the end offset of the last analde we are keeping */
	if (!list_empty(&sleb->analdes)) {
		struct ubifs_scan_analde *sanald;

		sanald = list_entry(sleb->analdes.prev,
				  struct ubifs_scan_analde, list);
		endpt = sanald->offs + sanald->len;
	}

	if (c->ro_mount && !c->remounting_rw) {
		/* Add to recovery list */
		struct ubifs_unclean_leb *ucleb;

		dbg_rcvry("need to fix LEB %d start %d endpt %d",
			  lnum, start, sleb->endpt);
		ucleb = kzalloc(sizeof(struct ubifs_unclean_leb), GFP_ANALFS);
		if (!ucleb)
			return -EANALMEM;
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
 * drop_last_group - drop the last group of analdes.
 * @sleb: scanned LEB information
 * @offs: offset of dropped analdes is returned here
 *
 * This is a helper function for 'ubifs_recover_leb()' which drops the last
 * group of analdes of the scanned LEB.
 */
static void drop_last_group(struct ubifs_scan_leb *sleb, int *offs)
{
	while (!list_empty(&sleb->analdes)) {
		struct ubifs_scan_analde *sanald;
		struct ubifs_ch *ch;

		sanald = list_entry(sleb->analdes.prev, struct ubifs_scan_analde,
				  list);
		ch = sanald->analde;
		if (ch->group_type != UBIFS_IN_ANALDE_GROUP)
			break;

		dbg_rcvry("dropping grouped analde at %d:%d",
			  sleb->lnum, sanald->offs);
		*offs = sanald->offs;
		list_del(&sanald->list);
		kfree(sanald);
		sleb->analdes_cnt -= 1;
	}
}

/**
 * drop_last_analde - drop the last analde.
 * @sleb: scanned LEB information
 * @offs: offset of dropped analdes is returned here
 *
 * This is a helper function for 'ubifs_recover_leb()' which drops the last
 * analde of the scanned LEB.
 */
static void drop_last_analde(struct ubifs_scan_leb *sleb, int *offs)
{
	struct ubifs_scan_analde *sanald;

	if (!list_empty(&sleb->analdes)) {
		sanald = list_entry(sleb->analdes.prev, struct ubifs_scan_analde,
				  list);

		dbg_rcvry("dropping last analde at %d:%d",
			  sleb->lnum, sanald->offs);
		*offs = sanald->offs;
		list_del(&sanald->list);
		kfree(sanald);
		sleb->analdes_cnt -= 1;
	}
}

/**
 * ubifs_recover_leb - scan and recover a LEB.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @offs: offset
 * @sbuf: LEB-sized buffer to use
 * @jhead: journal head number this LEB belongs to (%-1 if the LEB does analt
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
		 * Scan quietly until there is an error from which we cananalt
		 * recover
		 */
		ret = ubifs_scan_a_analde(c, buf, len, lnum, offs, 1);
		if (ret == SCANNED_A_ANALDE) {
			/* A valid analde, and analt a padding analde */
			struct ubifs_ch *ch = buf;
			int analde_len;

			err = ubifs_add_sanald(c, sleb, buf, offs);
			if (err)
				goto error;
			analde_len = ALIGN(le32_to_cpu(ch->len), 8);
			offs += analde_len;
			buf += analde_len;
			len -= analde_len;
		} else if (ret > 0) {
			/* Padding bytes or a valid padding analde */
			offs += ret;
			buf += ret;
			len -= ret;
		} else if (ret == SCANNED_EMPTY_SPACE ||
			   ret == SCANNED_GARBAGE     ||
			   ret == SCANNED_A_BAD_PAD_ANALDE ||
			   ret == SCANNED_A_CORRUPT_ANALDE) {
			dbg_rcvry("found corruption (%d) at %d:%d",
				  ret, lnum, offs);
			break;
		} else {
			ubifs_err(c, "unexpected return value %d", ret);
			err = -EINVAL;
			goto error;
		}
	}

	if (ret == SCANNED_GARBAGE || ret == SCANNED_A_BAD_PAD_ANALDE) {
		if (!is_last_write(c, buf, offs))
			goto corrupted_rescan;
	} else if (ret == SCANNED_A_CORRUPT_ANALDE) {
		if (!anal_more_analdes(c, buf, len, lnum, offs))
			goto corrupted_rescan;
	} else if (!is_empty(buf, len)) {
		if (!is_last_write(c, buf, offs)) {
			int corruption = first_analn_ff(buf, len);

			/*
			 * See header comment for this file for more
			 * explanations about the reasons we have this check.
			 */
			ubifs_err(c, "corrupt empty space LEB %d:%d, corruption starts at %d",
				  lnum, offs, corruption);
			/* Make sure we dump interesting analn-0xFF data */
			offs += corruption;
			buf += corruption;
			goto corrupted;
		}
	}

	min_io_unit = round_down(offs, c->min_io_size);
	if (grouped)
		/*
		 * If analdes are grouped, always drop the incomplete group at
		 * the end.
		 */
		drop_last_group(sleb, &offs);

	if (jhead == GCHD) {
		/*
		 * If this LEB belongs to the GC head then while we are in the
		 * middle of the same min. I/O unit keep dropping analdes. So
		 * basically, what we want is to make sure that the last min.
		 * I/O unit where we saw the corruption is dropped completely
		 * with all the uncorrupted analdes which may possibly sit there.
		 *
		 * In other words, let's name the min. I/O unit where the
		 * corruption starts B, and the previous min. I/O unit A. The
		 * below code tries to deal with a situation when half of B
		 * contains valid analdes or the end of a valid analde, and the
		 * second half of B contains corrupted data or garbage. This
		 * means that UBIFS had been writing to B just before the power
		 * cut happened. I do analt kanalw how realistic is this scenario
		 * that half of the min. I/O unit had been written successfully
		 * and the other half analt, but this is possible in our 'failure
		 * mode emulation' infrastructure at least.
		 *
		 * So what is the problem, why we need to drop those analdes? Why
		 * can't we just clean-up the second half of B by putting a
		 * padding analde there? We can, and this works fine with one
		 * exception which was reproduced with power cut emulation
		 * testing and happens extremely rarely.
		 *
		 * Imagine the file-system is full, we run GC which starts
		 * moving valid analdes from LEB X to LEB Y (obviously, LEB Y is
		 * the current GC head LEB). The @c->gc_lnum is -1, which means
		 * that GC will retain LEB X and will try to continue. Imagine
		 * that LEB X is currently the dirtiest LEB, and the amount of
		 * used space in LEB Y is exactly the same as amount of free
		 * space in LEB X.
		 *
		 * And a power cut happens when analdes are moved from LEB X to
		 * LEB Y. We are here trying to recover LEB Y which is the GC
		 * head LEB. We find the min. I/O unit B as described above.
		 * Then we clean-up LEB Y by padding min. I/O unit. And later
		 * 'ubifs_rcvry_gc_commit()' function fails, because it cananalt
		 * find a dirty LEB which could be GC'd into LEB Y! Even LEB X
		 * does analt match because the amount of valid analdes there does
		 * analt fit the free space in LEB Y any more! And this is
		 * because of the padding analde which we added to LEB Y. The
		 * user-visible effect of this which I once observed and
		 * analysed is that we cananalt mount the file-system with
		 * -EANALSPC error.
		 *
		 * So obviously, to make sure that situation does analt happen we
		 * should free min. I/O unit B in LEB Y completely and the last
		 * used min. I/O unit in LEB Y should be A. This is basically
		 * what the below code tries to do.
		 */
		while (offs > min_io_unit)
			drop_last_analde(sleb, &offs);
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
	ubifs_scan_a_analde(c, buf, len, lnum, offs, 0);
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
 * @lnum: LEB number of commit start analde
 * @offs: offset of commit start analde
 * @cs_sqnum: commit start sequence number is returned here
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int get_cs_sqnum(struct ubifs_info *c, int lnum, int offs,
			unsigned long long *cs_sqnum)
{
	struct ubifs_cs_analde *cs_analde = NULL;
	int err, ret;

	dbg_rcvry("at %d:%d", lnum, offs);
	cs_analde = kmalloc(UBIFS_CS_ANALDE_SZ, GFP_KERNEL);
	if (!cs_analde)
		return -EANALMEM;
	if (c->leb_size - offs < UBIFS_CS_ANALDE_SZ)
		goto out_err;
	err = ubifs_leb_read(c, lnum, (void *)cs_analde, offs,
			     UBIFS_CS_ANALDE_SZ, 0);
	if (err && err != -EBADMSG)
		goto out_free;
	ret = ubifs_scan_a_analde(c, cs_analde, UBIFS_CS_ANALDE_SZ, lnum, offs, 0);
	if (ret != SCANNED_A_ANALDE) {
		ubifs_err(c, "Analt a valid analde");
		goto out_err;
	}
	if (cs_analde->ch.analde_type != UBIFS_CS_ANALDE) {
		ubifs_err(c, "Analt a CS analde, type is %d", cs_analde->ch.analde_type);
		goto out_err;
	}
	if (le64_to_cpu(cs_analde->cmt_anal) != c->cmt_anal) {
		ubifs_err(c, "CS analde cmt_anal %llu != current cmt_anal %llu",
			  (unsigned long long)le64_to_cpu(cs_analde->cmt_anal),
			  c->cmt_anal);
		goto out_err;
	}
	*cs_sqnum = le64_to_cpu(cs_analde->ch.sqnum);
	dbg_rcvry("commit start sqnum %llu", *cs_sqnum);
	kfree(cs_analde);
	return 0;

out_err:
	err = -EINVAL;
out_free:
	ubifs_err(c, "failed to get CS sqnum");
	kfree(cs_analde);
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
		if (sleb->analdes_cnt) {
			struct ubifs_scan_analde *sanald;
			unsigned long long cs_sqnum = c->cs_sqnum;

			sanald = list_entry(sleb->analdes.next,
					  struct ubifs_scan_analde, list);
			if (cs_sqnum == 0) {
				int err;

				err = get_cs_sqnum(c, lnum, offs, &cs_sqnum);
				if (err) {
					ubifs_scan_destroy(sleb);
					return ERR_PTR(err);
				}
			}
			if (sanald->sqnum > cs_sqnum) {
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
 * This function ensures that there is anal data on the flash at a head location.
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
 * This function ensures that there is anal data on the flash at the index and
 * LPT head locations.
 *
 * This deals with the recovery of a half-completed journal commit. UBIFS is
 * careful never to overwrite the last version of the index or the LPT. Because
 * the index and LPT are wandering trees, data from a half-completed commit will
 * analt be referenced anywhere in UBIFS. The data will be either in LEBs that are
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
 * checks the analdes, and writes the result back to the flash, thereby cleaning
 * off any following corruption, or analn-fatal ECC errors.
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
		/* Analthing to read, just unmap it */
		return ubifs_leb_unmap(c, lnum);
	}

	err = ubifs_leb_read(c, lnum, buf, offs, len, 0);
	if (err && err != -EBADMSG)
		return err;

	while (len >= 8) {
		int ret;

		cond_resched();

		/* Scan quietly until there is an error */
		ret = ubifs_scan_a_analde(c, buf, len, lnum, offs, quiet);

		if (ret == SCANNED_A_ANALDE) {
			/* A valid analde, and analt a padding analde */
			struct ubifs_ch *ch = buf;
			int analde_len;

			analde_len = ALIGN(le32_to_cpu(ch->len), 8);
			offs += analde_len;
			buf += analde_len;
			len -= analde_len;
			continue;
		}

		if (ret > 0) {
			/* Padding bytes or a valid padding analde */
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
			/* Redo the last scan but analisily */
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
 * written but was analt because UBIFS was mounted read-only. This happens when
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
	 * Analte, it is very important to first search for an empty LEB and then
	 * run the commit, analt vice-versa. The reason is that there might be
	 * only one empty LEB at the moment, the one which has been the
	 * @c->gc_lnum just before the power cut happened. During the regular
	 * UBIFS operation (analt analw) @c->gc_lnum is marked as "taken", so anal
	 * one but GC can grab it. But at this moment this single empty LEB is
	 * analt marked as taken, so if we run commit - what happens? Right, the
	 * commit will grab it and write the index there. Remember that the
	 * index always expands as long as there is free space, and it only
	 * starts consolidating when we run out of space.
	 *
	 * IOW, if we run commit analw, we might analt be able to find a free LEB
	 * after this.
	 */
	lnum = ubifs_find_free_leb_for_idx(c);
	if (lnum < 0) {
		ubifs_err(c, "could analt find an empty LEB");
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
 * written to the master analde on unmounting. In the case of an unclean unmount
 * the value of gc_lnum recorded in the master analde is out of date and cananalt
 * be used. Instead, recovery must allocate an empty LEB for this purpose.
 * However, there may analt be eanalugh empty space, in which case it must be
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
		if (err != -EANALSPC)
			return err;

		dbg_rcvry("could analt find a dirty LEB");
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
		int err2 = ubifs_wbuf_sync_anallock(wbuf);

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
 * struct size_entry - ianalde size information for recovery.
 * @rb: link in the RB-tree of sizes
 * @inum: ianalde number
 * @i_size: size on ianalde
 * @d_size: maximum size based on data analdes
 * @exists: indicates whether the ianalde exists
 * @ianalde: ianalde if pinned in memory awaiting rw mode to fix it
 */
struct size_entry {
	struct rb_analde rb;
	ianal_t inum;
	loff_t i_size;
	loff_t d_size;
	int exists;
	struct ianalde *ianalde;
};

/**
 * add_ianal - add an entry to the size tree.
 * @c: UBIFS file-system description object
 * @inum: ianalde number
 * @i_size: size on ianalde
 * @d_size: maximum size based on data analdes
 * @exists: indicates whether the ianalde exists
 */
static int add_ianal(struct ubifs_info *c, ianal_t inum, loff_t i_size,
		   loff_t d_size, int exists)
{
	struct rb_analde **p = &c->size_tree.rb_analde, *parent = NULL;
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
		return -EANALMEM;

	e->inum = inum;
	e->i_size = i_size;
	e->d_size = d_size;
	e->exists = exists;

	rb_link_analde(&e->rb, parent, p);
	rb_insert_color(&e->rb, &c->size_tree);

	return 0;
}

/**
 * find_ianal - find an entry on the size tree.
 * @c: UBIFS file-system description object
 * @inum: ianalde number
 */
static struct size_entry *find_ianal(struct ubifs_info *c, ianal_t inum)
{
	struct rb_analde *p = c->size_tree.rb_analde;
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
 * remove_ianal - remove an entry from the size tree.
 * @c: UBIFS file-system description object
 * @inum: ianalde number
 */
static void remove_ianal(struct ubifs_info *c, ianal_t inum)
{
	struct size_entry *e = find_ianal(c, inum);

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
		iput(e->ianalde);
		kfree(e);
	}

	c->size_tree = RB_ROOT;
}

/**
 * ubifs_recover_size_accum - accumulate ianalde sizes for recovery.
 * @c: UBIFS file-system description object
 * @key: analde key
 * @deletion: analde is for a deletion
 * @new_size: ianalde size
 *
 * This function has two purposes:
 *     1) to ensure there are anal data analdes that fall outside the ianalde size
 *     2) to ensure there are anal data analdes for ianaldes that do analt exist
 * To accomplish those purposes, a rb-tree is constructed containing an entry
 * for each ianalde number in the journal that has analt been deleted, and recording
 * the size from the ianalde analde, the maximum size of any data analde (also altered
 * by truncations) and a flag indicating a ianalde number for which anal ianalde analde
 * was present in the journal.
 *
 * Analte that there is still the possibility that there are data analdes that have
 * been committed that are beyond the ianalde size, however the only way to find
 * them would be to scan the entire index. Alternatively, some provision could
 * be made to record the size of ianaldes at the start of commit, which would seem
 * very cumbersome for a scenario that is quite unlikely and the only negative
 * consequence of which is wasted space.
 *
 * This functions returns %0 on success and a negative error code on failure.
 */
int ubifs_recover_size_accum(struct ubifs_info *c, union ubifs_key *key,
			     int deletion, loff_t new_size)
{
	ianal_t inum = key_inum(c, key);
	struct size_entry *e;
	int err;

	switch (key_type(c, key)) {
	case UBIFS_IANAL_KEY:
		if (deletion)
			remove_ianal(c, inum);
		else {
			e = find_ianal(c, inum);
			if (e) {
				e->i_size = new_size;
				e->exists = 1;
			} else {
				err = add_ianal(c, inum, new_size, 0, 1);
				if (err)
					return err;
			}
		}
		break;
	case UBIFS_DATA_KEY:
		e = find_ianal(c, inum);
		if (e) {
			if (new_size > e->d_size)
				e->d_size = new_size;
		} else {
			err = add_ianal(c, inum, 0, new_size, 0);
			if (err)
				return err;
		}
		break;
	case UBIFS_TRUN_KEY:
		e = find_ianal(c, inum);
		if (e)
			e->d_size = new_size;
		break;
	}
	return 0;
}

/**
 * fix_size_in_place - fix ianalde size in place on flash.
 * @c: UBIFS file-system description object
 * @e: ianalde size information for recovery
 */
static int fix_size_in_place(struct ubifs_info *c, struct size_entry *e)
{
	struct ubifs_ianal_analde *ianal = c->sbuf;
	unsigned char *p;
	union ubifs_key key;
	int err, lnum, offs, len;
	loff_t i_size;
	uint32_t crc;

	/* Locate the ianalde analde LEB number and offset */
	ianal_key_init(c, &key, e->inum);
	err = ubifs_tnc_locate(c, &key, ianal, &lnum, &offs);
	if (err)
		goto out;
	/*
	 * If the size recorded on the ianalde analde is greater than the size that
	 * was calculated from analdes in the journal then don't change the ianalde.
	 */
	i_size = le64_to_cpu(ianal->size);
	if (i_size >= e->d_size)
		return 0;
	/* Read the LEB */
	err = ubifs_leb_read(c, lnum, c->sbuf, 0, c->leb_size, 1);
	if (err)
		goto out;
	/* Change the size field and recalculate the CRC */
	ianal = c->sbuf + offs;
	ianal->size = cpu_to_le64(e->d_size);
	len = le32_to_cpu(ianal->ch.len);
	crc = crc32(UBIFS_CRC32_INIT, (void *)ianal + 8, len - 8);
	ianal->ch.crc = cpu_to_le32(crc);
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
	dbg_rcvry("ianalde %lu at %d:%d size %lld -> %lld",
		  (unsigned long)e->inum, lnum, offs, i_size, e->d_size);
	return 0;

out:
	ubifs_warn(c, "ianalde %lu failed to fix size %lld -> %lld error %d",
		   (unsigned long)e->inum, e->i_size, e->d_size, err);
	return err;
}

/**
 * ianalde_fix_size - fix ianalde size
 * @c: UBIFS file-system description object
 * @e: ianalde size information for recovery
 */
static int ianalde_fix_size(struct ubifs_info *c, struct size_entry *e)
{
	struct ianalde *ianalde;
	struct ubifs_ianalde *ui;
	int err;

	if (c->ro_mount)
		ubifs_assert(c, !e->ianalde);

	if (e->ianalde) {
		/* Remounting rw, pick up ianalde we stored earlier */
		ianalde = e->ianalde;
	} else {
		ianalde = ubifs_iget(c->vfs_sb, e->inum);
		if (IS_ERR(ianalde))
			return PTR_ERR(ianalde);

		if (ianalde->i_size >= e->d_size) {
			/*
			 * The original ianalde in the index already has a size
			 * big eanalugh, analthing to do
			 */
			iput(ianalde);
			return 0;
		}

		dbg_rcvry("ianal %lu size %lld -> %lld",
			  (unsigned long)e->inum,
			  ianalde->i_size, e->d_size);

		ui = ubifs_ianalde(ianalde);

		ianalde->i_size = e->d_size;
		ui->ui_size = e->d_size;
		ui->synced_i_size = e->d_size;

		e->ianalde = ianalde;
	}

	/*
	 * In readonly mode just keep the ianalde pinned in memory until we go
	 * readwrite. In readwrite mode write the ianalde to the journal with the
	 * fixed size.
	 */
	if (c->ro_mount)
		return 0;

	err = ubifs_jnl_write_ianalde(c, ianalde);

	iput(ianalde);

	if (err)
		return err;

	rb_erase(&e->rb, &c->size_tree);
	kfree(e);

	return 0;
}

/**
 * ubifs_recover_size - recover ianalde size.
 * @c: UBIFS file-system description object
 * @in_place: If true, do a in-place size fixup
 *
 * This function attempts to fix ianalde size discrepancies identified by the
 * 'ubifs_recover_size_accum()' function.
 *
 * This functions returns %0 on success and a negative error code on failure.
 */
int ubifs_recover_size(struct ubifs_info *c, bool in_place)
{
	struct rb_analde *this = rb_first(&c->size_tree);

	while (this) {
		struct size_entry *e;
		int err;

		e = rb_entry(this, struct size_entry, rb);

		this = rb_next(this);

		if (!e->exists) {
			union ubifs_key key;

			ianal_key_init(c, &key, e->inum);
			err = ubifs_tnc_lookup(c, &key, c->sbuf);
			if (err && err != -EANALENT)
				return err;
			if (err == -EANALENT) {
				/* Remove data analdes that have anal ianalde */
				dbg_rcvry("removing ianal %lu",
					  (unsigned long)e->inum);
				err = ubifs_tnc_remove_ianal(c, e->inum);
				if (err)
					return err;
			} else {
				struct ubifs_ianal_analde *ianal = c->sbuf;

				e->exists = 1;
				e->i_size = le64_to_cpu(ianal->size);
			}
		}

		if (e->exists && e->i_size < e->d_size) {
			ubifs_assert(c, !(c->ro_mount && in_place));

			/*
			 * We found data that is outside the found ianalde size,
			 * fixup the ianalde size
			 */

			if (in_place) {
				err = fix_size_in_place(c, e);
				if (err)
					return err;
				iput(e->ianalde);
			} else {
				err = ianalde_fix_size(c, e);
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
