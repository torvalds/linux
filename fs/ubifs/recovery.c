/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation
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
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file implements functions needed to recover from unclean un-mounts.
 * When UBIFS is mounted, it checks a flag on the master node to determine if
 * an un-mount was completed successfully. If not, the process of mounting
 * incorporates additional checking and fixing of on-flash data structures.
 * UBIFS always cleans away all remnants of an unclean un-mount, so that
 * errors do not accumulate. However UBIFS defers recovery if it is mounted
 * read-only, and the flash is not modified in that case.
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
 * first_non_ff - find offset of the first non-0xff byte.
 * @buf: buffer to search in
 * @len: length of buffer
 *
 * This function returns offset of the first non-0xff byte in @buf or %-1 if
 * the buffer contains only 0xff bytes.
 */
static int first_non_ff(void *buf, int len)
{
	uint8_t *p = buf;
	int i;

	for (i = 0; i < len; i++)
		if (*p++ != 0xff)
			return i;
	return -1;
}

/**
 * get_master_node - get the last valid master node allowing for corruption.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @pbuf: buffer containing the LEB read, is returned here
 * @mst: master node, if found, is returned here
 * @cor: corruption, if found, is returned here
 *
 * This function allocates a buffer, reads the LEB into it, and finds and
 * returns the last valid master node allowing for one area of corruption.
 * The corrupt area, if there is one, must be consistent with the assumption
 * that it is the result of an unclean unmount while the master node was being
 * written. Under those circumstances, it is valid to use the previously written
 * master node.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int get_master_node(const struct ubifs_info *c, int lnum, void **pbuf,
			   struct ubifs_mst_node **mst, void **cor)
{
	const int sz = c->mst_node_alsz;
	int err, offs, len;
	void *sbuf, *buf;

	sbuf = vmalloc(c->leb_size);
	if (!sbuf)
		return -ENOMEM;

	err = ubi_read(c->ubi, lnum, sbuf, 0, c->leb_size);
	if (err && err != -EBADMSG)
		goto out_free;

	/* Find the first position that is definitely not a node */
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
	/* See if there was a valid master node before that */
	if (offs) {
		int ret;

		offs -= sz;
		buf  -= sz;
		len  += sz;
		ret = ubifs_scan_a_node(c, buf, len, lnum, offs, 1);
		if (ret != SCANNED_A_NODE && offs) {
			/* Could have been corruption so check one place back */
			offs -= sz;
			buf  -= sz;
			len  += sz;
			ret = ubifs_scan_a_node(c, buf, len, lnum, offs, 1);
			if (ret != SCANNED_A_NODE)
				/*
				 * We accept only one area of corruption because
				 * we are assuming that it was caused while
				 * trying to write a master node.
				 */
				goto out_err;
		}
		if (ret == SCANNED_A_NODE) {
			struct ubifs_ch *ch = buf;

			if (ch->node_type != UBIFS_MST_NODE)
				goto out_err;
			dbg_rcvry("found a master node at %d:%d", lnum, offs);
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
 * write_rcvrd_mst_node - write recovered master node.
 * @c: UBIFS file-system description object
 * @mst: master node
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int write_rcvrd_mst_node(struct ubifs_info *c,
				struct ubifs_mst_node *mst)
{
	int err = 0, lnum = UBIFS_MST_LNUM, sz = c->mst_node_alsz;
	__le32 save_flags;

	dbg_rcvry("recovery");

	save_flags = mst->flags;
	mst->flags |= cpu_to_le32(UBIFS_MST_RCVRY);

	ubifs_prepare_node(c, mst, UBIFS_MST_NODE_SZ, 1);
	err = ubi_leb_change(c->ubi, lnum, mst, sz, UBI_SHORTTERM);
	if (err)
		goto out;
	err = ubi_leb_change(c->ubi, lnum + 1, mst, sz, UBI_SHORTTERM);
	if (err)
		goto out;
out:
	mst->flags = save_flags;
	return err;
}

/**
 * ubifs_recover_master_node - recover the master node.
 * @c: UBIFS file-system description object
 *
 * This function recovers the master node from corruption that may occur due to
 * an unclean unmount.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_recover_master_node(struct ubifs_info *c)
{
	void *buf1 = NULL, *buf2 = NULL, *cor1 = NULL, *cor2 = NULL;
	struct ubifs_mst_node *mst1 = NULL, *mst2 = NULL, *mst;
	const int sz = c->mst_node_alsz;
	int err, offs1, offs2;

	dbg_rcvry("recovery");

	err = get_master_node(c, UBIFS_MST_LNUM, &buf1, &mst1, &cor1);
	if (err)
		goto out_free;

	err = get_master_node(c, UBIFS_MST_LNUM + 1, &buf2, &mst2, &cor2);
	if (err)
		goto out_free;

	if (mst1) {
		offs1 = (void *)mst1 - buf1;
		if ((le32_to_cpu(mst1->flags) & UBIFS_MST_RCVRY) &&
		    (offs1 == 0 && !cor1)) {
			/*
			 * mst1 was written by recovery at offset 0 with no
			 * corruption.
			 */
			dbg_rcvry("recovery recovery");
			mst = mst1;
		} else if (mst2) {
			offs2 = (void *)mst2 - buf2;
			if (offs1 == offs2) {
				/* Same offset, so must be the same */
				if (memcmp((void *)mst1 + UBIFS_CH_SZ,
					   (void *)mst2 + UBIFS_CH_SZ,
					   UBIFS_MST_NODE_SZ - UBIFS_CH_SZ))
					goto out_err;
				mst = mst1;
			} else if (offs2 + sz == offs1) {
				/* 1st LEB was written, 2nd was not */
				if (cor1)
					goto out_err;
				mst = mst1;
			} else if (offs1 == 0 && offs2 + sz >= c->leb_size) {
				/* 1st LEB was unmapped and written, 2nd not */
				if (cor1)
					goto out_err;
				mst = mst1;
			} else
				goto out_err;
		} else {
			/*
			 * 2nd LEB was unmapped and about to be written, so
			 * there must be only one master node in the first LEB
			 * and no corruption.
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
		 * be no room left in 2nd LEB.
		 */
		offs2 = (void *)mst2 - buf2;
		if (offs2 + sz + sz <= c->leb_size)
			goto out_err;
		mst = mst2;
	}

	ubifs_msg("recovered master node from LEB %d",
		  (mst == mst1 ? UBIFS_MST_LNUM : UBIFS_MST_LNUM + 1));

	memcpy(c->mst_node, mst, UBIFS_MST_NODE_SZ);

	if ((c->vfs_sb->s_flags & MS_RDONLY)) {
		/* Read-only mode. Keep a copy for switching to rw mode */
		c->rcvrd_mst_node = kmalloc(sz, GFP_KERNEL);
		if (!c->rcvrd_mst_node) {
			err = -ENOMEM;
			goto out_free;
		}
		memcpy(c->rcvrd_mst_node, c->mst_node, UBIFS_MST_NODE_SZ);
	} else {
		/* Write the recovered master node */
		c->max_sqnum = le64_to_cpu(mst->ch.sqnum) - 1;
		err = write_rcvrd_mst_node(c, c->mst_node);
		if (err)
			goto out_free;
	}

	vfree(buf2);
	vfree(buf1);

	return 0;

out_err:
	err = -EINVAL;
out_free:
	ubifs_err("failed to recover master node");
	if (mst1) {
		dbg_err("dumping first master node");
		dbg_dump_node(c, mst1);
	}
	if (mst2) {
		dbg_err("dumping second master node");
		dbg_dump_node(c, mst2);
	}
	vfree(buf2);
	vfree(buf1);
	return err;
}

/**
 * ubifs_write_rcvrd_mst_node - write the recovered master node.
 * @c: UBIFS file-system description object
 *
 * This function writes the master node that was recovered during mounting in
 * read-only mode and must now be written because we are remounting rw.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_write_rcvrd_mst_node(struct ubifs_info *c)
{
	int err;

	if (!c->rcvrd_mst_node)
		return 0;
	c->rcvrd_mst_node->flags |= cpu_to_le32(UBIFS_MST_DIRTY);
	c->mst_node->flags |= cpu_to_le32(UBIFS_MST_DIRTY);
	err = write_rcvrd_mst_node(c, c->rcvrd_mst_node);
	if (err)
		return err;
	kfree(c->rcvrd_mst_node);
	c->rcvrd_mst_node = NULL;
	return 0;
}

/**
 * is_last_write - determine if an offset was in the last write to a LEB.
 * @c: UBIFS file-system description object
 * @buf: buffer to check
 * @offs: offset to check
 *
 * This function returns %1 if @offs was in the last write to the LEB whose data
 * is in @buf, otherwise %0 is returned.  The determination is made by checking
 * for subsequent empty space starting from the next @c->min_io_size boundary.
 */
static int is_last_write(const struct ubifs_info *c, void *buf, int offs)
{
	int empty_offs, check_len;
	uint8_t *p;

	/*
	 * Round up to the next @c->min_io_size boundary i.e. @offs is in the
	 * last wbuf written. After that should be empty space.
	 */
	empty_offs = ALIGN(offs + 1, c->min_io_size);
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

	lnum = lnum;
	dbg_rcvry("cleaning corruption at %d:%d", lnum, *offs);

	ubifs_assert(!(*offs & 7));
	empty_offs = ALIGN(*offs, c->min_io_size);
	pad_len = empty_offs - *offs;
	ubifs_pad(c, *buf, pad_len);
	*offs += pad_len;
	*buf += pad_len;
	*len -= pad_len;
	memset(*buf, 0xff, c->leb_size - empty_offs);
}

/**
 * no_more_nodes - determine if there are no more nodes in a buffer.
 * @c: UBIFS file-system description object
 * @buf: buffer to check
 * @len: length of buffer
 * @lnum: LEB number of the LEB from which @buf was read
 * @offs: offset from which @buf was read
 *
 * This function ensures that the corrupted node at @offs is the last thing
 * written to a LEB. This function returns %1 if more data is not found and
 * %0 if more data is found.
 */
static int no_more_nodes(const struct ubifs_info *c, void *buf, int len,
			int lnum, int offs)
{
	struct ubifs_ch *ch = buf;
	int skip, dlen = le32_to_cpu(ch->len);

	/* Check for empty space after the corrupt node's common header */
	skip = ALIGN(offs + UBIFS_CH_SZ, c->min_io_size) - offs;
	if (is_empty(buf + skip, len - skip))
		return 1;
	/*
	 * The area after the common header size is not empty, so the common
	 * header must be intact. Check it.
	 */
	if (ubifs_check_node(c, buf, lnum, offs, 1, 0) != -EUCLEAN) {
		dbg_rcvry("unexpected bad common header at %d:%d", lnum, offs);
		return 0;
	}
	/* Now we know the corrupt node's length we can skip over it */
	skip = ALIGN(offs + dlen, c->min_io_size) - offs;
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

	/* Get the end offset of the last node we are keeping */
	if (!list_empty(&sleb->nodes)) {
		struct ubifs_scan_node *snod;

		snod = list_entry(sleb->nodes.prev,
				  struct ubifs_scan_node, list);
		endpt = snod->offs + snod->len;
	}

	if ((c->vfs_sb->s_flags & MS_RDONLY) && !c->remounting_rw) {
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
				err = ubi_read(c->ubi, lnum, sleb->buf, 0,
					       start);
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
			err = ubi_leb_change(c->ubi, lnum, sleb->buf, len,
					     UBI_UNKNOWN);
			if (err)
				return err;
		}
	}
	return 0;
}

/**
 * drop_incomplete_group - drop nodes from an incomplete group.
 * @sleb: scanned LEB information
 * @offs: offset of dropped nodes is returned here
 *
 * This function returns %1 if nodes are dropped and %0 otherwise.
 */
static int drop_incomplete_group(struct ubifs_scan_leb *sleb, int *offs)
{
	int dropped = 0;

	while (!list_empty(&sleb->nodes)) {
		struct ubifs_scan_node *snod;
		struct ubifs_ch *ch;

		snod = list_entry(sleb->nodes.prev, struct ubifs_scan_node,
				  list);
		ch = snod->node;
		if (ch->group_type != UBIFS_IN_NODE_GROUP)
			return dropped;
		dbg_rcvry("dropping node at %d:%d", sleb->lnum, snod->offs);
		*offs = snod->offs;
		list_del(&snod->list);
		kfree(snod);
		sleb->nodes_cnt -= 1;
		dropped = 1;
	}
	return dropped;
}

/**
 * ubifs_recover_leb - scan and recover a LEB.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @offs: offset
 * @sbuf: LEB-sized buffer to use
 * @grouped: nodes may be grouped for recovery
 *
 * This function does a scan of a LEB, but caters for errors that might have
 * been caused by the unclean unmount from which we are attempting to recover.
 * Returns %0 in case of success, %-EUCLEAN if an unrecoverable corruption is
 * found, and a negative error code in case of failure.
 */
struct ubifs_scan_leb *ubifs_recover_leb(struct ubifs_info *c, int lnum,
					 int offs, void *sbuf, int grouped)
{
	int err, len = c->leb_size - offs, need_clean = 0, quiet = 1;
	int empty_chkd = 0, start = offs;
	struct ubifs_scan_leb *sleb;
	void *buf = sbuf + offs;

	dbg_rcvry("%d:%d", lnum, offs);

	sleb = ubifs_start_scan(c, lnum, offs, sbuf);
	if (IS_ERR(sleb))
		return sleb;

	if (sleb->ecc)
		need_clean = 1;

	while (len >= 8) {
		int ret;

		dbg_scan("look at LEB %d:%d (%d bytes left)",
			 lnum, offs, len);

		cond_resched();

		/*
		 * Scan quietly until there is an error from which we cannot
		 * recover
		 */
		ret = ubifs_scan_a_node(c, buf, len, lnum, offs, quiet);

		if (ret == SCANNED_A_NODE) {
			/* A valid node, and not a padding node */
			struct ubifs_ch *ch = buf;
			int node_len;

			err = ubifs_add_snod(c, sleb, buf, offs);
			if (err)
				goto error;
			node_len = ALIGN(le32_to_cpu(ch->len), 8);
			offs += node_len;
			buf += node_len;
			len -= node_len;
			continue;
		}

		if (ret > 0) {
			/* Padding bytes or a valid padding node */
			offs += ret;
			buf += ret;
			len -= ret;
			continue;
		}

		if (ret == SCANNED_EMPTY_SPACE) {
			if (!is_empty(buf, len)) {
				if (!is_last_write(c, buf, offs))
					break;
				clean_buf(c, &buf, lnum, &offs, &len);
				need_clean = 1;
			}
			empty_chkd = 1;
			break;
		}

		if (ret == SCANNED_GARBAGE || ret == SCANNED_A_BAD_PAD_NODE)
			if (is_last_write(c, buf, offs)) {
				clean_buf(c, &buf, lnum, &offs, &len);
				need_clean = 1;
				empty_chkd = 1;
				break;
			}

		if (ret == SCANNED_A_CORRUPT_NODE)
			if (no_more_nodes(c, buf, len, lnum, offs)) {
				clean_buf(c, &buf, lnum, &offs, &len);
				need_clean = 1;
				empty_chkd = 1;
				break;
			}

		if (quiet) {
			/* Redo the last scan but noisily */
			quiet = 0;
			continue;
		}

		switch (ret) {
		case SCANNED_GARBAGE:
			dbg_err("garbage");
			goto corrupted;
		case SCANNED_A_CORRUPT_NODE:
		case SCANNED_A_BAD_PAD_NODE:
			dbg_err("bad node");
			goto corrupted;
		default:
			dbg_err("unknown");
			err = -EINVAL;
			goto error;
		}
	}

	if (!empty_chkd && !is_empty(buf, len)) {
		if (is_last_write(c, buf, offs)) {
			clean_buf(c, &buf, lnum, &offs, &len);
			need_clean = 1;
		} else {
			int corruption = first_non_ff(buf, len);

			ubifs_err("corrupt empty space LEB %d:%d, corruption "
				  "starts at %d", lnum, offs, corruption);
			/* Make sure we dump interesting non-0xFF data */
			offs = corruption;
			buf += corruption;
			goto corrupted;
		}
	}

	/* Drop nodes from incomplete group */
	if (grouped && drop_incomplete_group(sleb, &offs)) {
		buf = sbuf + offs;
		len = c->leb_size - offs;
		clean_buf(c, &buf, lnum, &offs, &len);
		need_clean = 1;
	}

	if (offs % c->min_io_size) {
		clean_buf(c, &buf, lnum, &offs, &len);
		need_clean = 1;
	}

	ubifs_end_scan(c, sleb, lnum, offs);

	if (need_clean) {
		err = fix_unclean_leb(c, sleb, start);
		if (err)
			goto error;
	}

	return sleb;

corrupted:
	ubifs_scanned_corruption(c, lnum, offs, buf);
	err = -EUCLEAN;
error:
	ubifs_err("LEB %d scanning failed", lnum);
	ubifs_scan_destroy(sleb);
	return ERR_PTR(err);
}

/**
 * get_cs_sqnum - get commit start sequence number.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of commit start node
 * @offs: offset of commit start node
 * @cs_sqnum: commit start sequence number is returned here
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int get_cs_sqnum(struct ubifs_info *c, int lnum, int offs,
			unsigned long long *cs_sqnum)
{
	struct ubifs_cs_node *cs_node = NULL;
	int err, ret;

	dbg_rcvry("at %d:%d", lnum, offs);
	cs_node = kmalloc(UBIFS_CS_NODE_SZ, GFP_KERNEL);
	if (!cs_node)
		return -ENOMEM;
	if (c->leb_size - offs < UBIFS_CS_NODE_SZ)
		goto out_err;
	err = ubi_read(c->ubi, lnum, (void *)cs_node, offs, UBIFS_CS_NODE_SZ);
	if (err && err != -EBADMSG)
		goto out_free;
	ret = ubifs_scan_a_node(c, cs_node, UBIFS_CS_NODE_SZ, lnum, offs, 0);
	if (ret != SCANNED_A_NODE) {
		dbg_err("Not a valid node");
		goto out_err;
	}
	if (cs_node->ch.node_type != UBIFS_CS_NODE) {
		dbg_err("Node a CS node, type is %d", cs_node->ch.node_type);
		goto out_err;
	}
	if (le64_to_cpu(cs_node->cmt_no) != c->cmt_no) {
		dbg_err("CS node cmt_no %llu != current cmt_no %llu",
			(unsigned long long)le64_to_cpu(cs_node->cmt_no),
			c->cmt_no);
		goto out_err;
	}
	*cs_sqnum = le64_to_cpu(cs_node->ch.sqnum);
	dbg_rcvry("commit start sqnum %llu", *cs_sqnum);
	kfree(cs_node);
	return 0;

out_err:
	err = -EINVAL;
out_free:
	ubifs_err("failed to get CS sqnum");
	kfree(cs_node);
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
 * been caused by the unclean unmount from which we are attempting to recover.
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
		if (sleb->nodes_cnt) {
			struct ubifs_scan_node *snod;
			unsigned long long cs_sqnum = c->cs_sqnum;

			snod = list_entry(sleb->nodes.next,
					  struct ubifs_scan_node, list);
			if (cs_sqnum == 0) {
				int err;

				err = get_cs_sqnum(c, lnum, offs, &cs_sqnum);
				if (err) {
					ubifs_scan_destroy(sleb);
					return ERR_PTR(err);
				}
			}
			if (snod->sqnum > cs_sqnum) {
				ubifs_err("unrecoverable log corruption "
					  "in LEB %d", lnum);
				ubifs_scan_destroy(sleb);
				return ERR_PTR(-EUCLEAN);
			}
		}
		ubifs_scan_destroy(sleb);
	}
	return ubifs_recover_leb(c, lnum, offs, sbuf, 0);
}

/**
 * recover_head - recover a head.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of head to recover
 * @offs: offset of head to recover
 * @sbuf: LEB-sized buffer to use
 *
 * This function ensures that there is no data on the flash at a head location.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int recover_head(const struct ubifs_info *c, int lnum, int offs,
			void *sbuf)
{
	int len, err;

	if (c->min_io_size > 1)
		len = c->min_io_size;
	else
		len = 512;
	if (offs + len > c->leb_size)
		len = c->leb_size - offs;

	if (!len)
		return 0;

	/* Read at the head location and check it is empty flash */
	err = ubi_read(c->ubi, lnum, sbuf, offs, len);
	if (err || !is_empty(sbuf, len)) {
		dbg_rcvry("cleaning head at %d:%d", lnum, offs);
		if (offs == 0)
			return ubifs_leb_unmap(c, lnum);
		err = ubi_read(c->ubi, lnum, sbuf, 0, offs);
		if (err)
			return err;
		return ubi_leb_change(c->ubi, lnum, sbuf, offs, UBI_UNKNOWN);
	}

	return 0;
}

/**
 * ubifs_recover_inl_heads - recover index and LPT heads.
 * @c: UBIFS file-system description object
 * @sbuf: LEB-sized buffer to use
 *
 * This function ensures that there is no data on the flash at the index and
 * LPT head locations.
 *
 * This deals with the recovery of a half-completed journal commit. UBIFS is
 * careful never to overwrite the last version of the index or the LPT. Because
 * the index and LPT are wandering trees, data from a half-completed commit will
 * not be referenced anywhere in UBIFS. The data will be either in LEBs that are
 * assumed to be empty and will be unmapped anyway before use, or in the index
 * and LPT heads.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_recover_inl_heads(const struct ubifs_info *c, void *sbuf)
{
	int err;

	ubifs_assert(!(c->vfs_sb->s_flags & MS_RDONLY) || c->remounting_rw);

	dbg_rcvry("checking index head at %d:%d", c->ihead_lnum, c->ihead_offs);
	err = recover_head(c, c->ihead_lnum, c->ihead_offs, sbuf);
	if (err)
		return err;

	dbg_rcvry("checking LPT head at %d:%d", c->nhead_lnum, c->nhead_offs);
	err = recover_head(c, c->nhead_lnum, c->nhead_offs, sbuf);
	if (err)
		return err;

	return 0;
}

/**
 *  clean_an_unclean_leb - read and write a LEB to remove corruption.
 * @c: UBIFS file-system description object
 * @ucleb: unclean LEB information
 * @sbuf: LEB-sized buffer to use
 *
 * This function reads a LEB up to a point pre-determined by the mount recovery,
 * checks the nodes, and writes the result back to the flash, thereby cleaning
 * off any following corruption, or non-fatal ECC errors.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int clean_an_unclean_leb(const struct ubifs_info *c,
				struct ubifs_unclean_leb *ucleb, void *sbuf)
{
	int err, lnum = ucleb->lnum, offs = 0, len = ucleb->endpt, quiet = 1;
	void *buf = sbuf;

	dbg_rcvry("LEB %d len %d", lnum, len);

	if (len == 0) {
		/* Nothing to read, just unmap it */
		err = ubifs_leb_unmap(c, lnum);
		if (err)
			return err;
		return 0;
	}

	err = ubi_read(c->ubi, lnum, buf, offs, len);
	if (err && err != -EBADMSG)
		return err;

	while (len >= 8) {
		int ret;

		cond_resched();

		/* Scan quietly until there is an error */
		ret = ubifs_scan_a_node(c, buf, len, lnum, offs, quiet);

		if (ret == SCANNED_A_NODE) {
			/* A valid node, and not a padding node */
			struct ubifs_ch *ch = buf;
			int node_len;

			node_len = ALIGN(le32_to_cpu(ch->len), 8);
			offs += node_len;
			buf += node_len;
			len -= node_len;
			continue;
		}

		if (ret > 0) {
			/* Padding bytes or a valid padding node */
			offs += ret;
			buf += ret;
			len -= ret;
			continue;
		}

		if (ret == SCANNED_EMPTY_SPACE) {
			ubifs_err("unexpected empty space at %d:%d",
				  lnum, offs);
			return -EUCLEAN;
		}

		if (quiet) {
			/* Redo the last scan but noisily */
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
	err = ubi_leb_change(c->ubi, lnum, sbuf, len, UBI_UNKNOWN);
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
 * written but was not because UBIFS was mounted read-only. This happens when
 * remounting to read-write mode.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_clean_lebs(const struct ubifs_info *c, void *sbuf)
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
 * ubifs_rcvry_gc_commit - recover the GC LEB number and run the commit.
 * @c: UBIFS file-system description object
 *
 * Out-of-place garbage collection requires always one empty LEB with which to
 * start garbage collection. The LEB number is recorded in c->gc_lnum and is
 * written to the master node on unmounting. In the case of an unclean unmount
 * the value of gc_lnum recorded in the master node is out of date and cannot
 * be used. Instead, recovery must allocate an empty LEB for this purpose.
 * However, there may not be enough empty space, in which case it must be
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
	int lnum, err;

	c->gc_lnum = -1;
	if (wbuf->lnum == -1) {
		dbg_rcvry("no GC head LEB");
		goto find_free;
	}
	/*
	 * See whether the used space in the dirtiest LEB fits in the GC head
	 * LEB.
	 */
	if (wbuf->offs == c->leb_size) {
		dbg_rcvry("no room in GC head LEB");
		goto find_free;
	}
	err = ubifs_find_dirty_leb(c, &lp, wbuf->offs, 2);
	if (err) {
		/*
		 * There are no dirty or empty LEBs subject to here being
		 * enough for the index. Try to use
		 * 'ubifs_find_free_leb_for_idx()', which will return any empty
		 * LEBs (ignoring index requirements). If the index then
		 * doesn't have enough LEBs the recovery commit will fail -
		 * which is the  same result anyway i.e. recovery fails. So
		 * there is no problem ignoring index  requirements and just
		 * grabbing a free LEB since we have already established there
		 * is not a dirty LEB we could have used instead.
		 */
		if (err == -ENOSPC) {
			dbg_rcvry("could not find a dirty LEB");
			goto find_free;
		}
		return err;
	}
	ubifs_assert(!(lp.flags & LPROPS_INDEX));
	lnum = lp.lnum;
	if (lp.free + lp.dirty == c->leb_size) {
		/* An empty LEB was returned */
		if (lp.free != c->leb_size) {
			err = ubifs_change_one_lp(c, lnum, c->leb_size,
						  0, 0, 0, 0);
			if (err)
				return err;
		}
		err = ubifs_leb_unmap(c, lnum);
		if (err)
			return err;
		c->gc_lnum = lnum;
		dbg_rcvry("allocated LEB %d for GC", lnum);
		/* Run the commit */
		dbg_rcvry("committing");
		return ubifs_run_commit(c);
	}
	/*
	 * There was no empty LEB so the used space in the dirtiest LEB must fit
	 * in the GC head LEB.
	 */
	if (lp.free + lp.dirty < wbuf->offs) {
		dbg_rcvry("LEB %d doesn't fit in GC head LEB %d:%d",
			  lnum, wbuf->lnum, wbuf->offs);
		err = ubifs_return_leb(c, lnum);
		if (err)
			return err;
		goto find_free;
	}
	/*
	 * We run the commit before garbage collection otherwise subsequent
	 * mounts will see the GC and orphan deletion in a different order.
	 */
	dbg_rcvry("committing");
	err = ubifs_run_commit(c);
	if (err)
		return err;
	/*
	 * The data in the dirtiest LEB fits in the GC head LEB, so do the GC
	 * - use locking to keep 'ubifs_assert()' happy.
	 */
	dbg_rcvry("GC'ing LEB %d", lnum);
	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
	err = ubifs_garbage_collect_leb(c, &lp);
	if (err >= 0) {
		int err2 = ubifs_wbuf_sync_nolock(wbuf);

		if (err2)
			err = err2;
	}
	mutex_unlock(&wbuf->io_mutex);
	if (err < 0) {
		dbg_err("GC failed, error %d", err);
		if (err == -EAGAIN)
			err = -EINVAL;
		return err;
	}
	if (err != LEB_RETAINED) {
		dbg_err("GC returned %d", err);
		return -EINVAL;
	}
	err = ubifs_leb_unmap(c, c->gc_lnum);
	if (err)
		return err;
	dbg_rcvry("allocated LEB %d for GC", lnum);
	return 0;

find_free:
	/*
	 * There is no GC head LEB or the free space in the GC head LEB is too
	 * small, or there are not dirty LEBs. Allocate gc_lnum by calling
	 * 'ubifs_find_free_leb_for_idx()' so GC is not run.
	 */
	lnum = ubifs_find_free_leb_for_idx(c);
	if (lnum < 0) {
		dbg_err("could not find an empty LEB");
		return lnum;
	}
	/* And reset the index flag */
	err = ubifs_change_one_lp(c, lnum, LPROPS_NC, LPROPS_NC, 0,
				  LPROPS_INDEX, 0);
	if (err)
		return err;
	c->gc_lnum = lnum;
	dbg_rcvry("allocated LEB %d for GC", lnum);
	/* Run the commit */
	dbg_rcvry("committing");
	return ubifs_run_commit(c);
}

/**
 * struct size_entry - inode size information for recovery.
 * @rb: link in the RB-tree of sizes
 * @inum: inode number
 * @i_size: size on inode
 * @d_size: maximum size based on data nodes
 * @exists: indicates whether the inode exists
 * @inode: inode if pinned in memory awaiting rw mode to fix it
 */
struct size_entry {
	struct rb_node rb;
	ino_t inum;
	loff_t i_size;
	loff_t d_size;
	int exists;
	struct inode *inode;
};

/**
 * add_ino - add an entry to the size tree.
 * @c: UBIFS file-system description object
 * @inum: inode number
 * @i_size: size on inode
 * @d_size: maximum size based on data nodes
 * @exists: indicates whether the inode exists
 */
static int add_ino(struct ubifs_info *c, ino_t inum, loff_t i_size,
		   loff_t d_size, int exists)
{
	struct rb_node **p = &c->size_tree.rb_node, *parent = NULL;
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

	rb_link_node(&e->rb, parent, p);
	rb_insert_color(&e->rb, &c->size_tree);

	return 0;
}

/**
 * find_ino - find an entry on the size tree.
 * @c: UBIFS file-system description object
 * @inum: inode number
 */
static struct size_entry *find_ino(struct ubifs_info *c, ino_t inum)
{
	struct rb_node *p = c->size_tree.rb_node;
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
 * remove_ino - remove an entry from the size tree.
 * @c: UBIFS file-system description object
 * @inum: inode number
 */
static void remove_ino(struct ubifs_info *c, ino_t inum)
{
	struct size_entry *e = find_ino(c, inum);

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
	struct rb_node *this = c->size_tree.rb_node;
	struct size_entry *e;

	while (this) {
		if (this->rb_left) {
			this = this->rb_left;
			continue;
		} else if (this->rb_right) {
			this = this->rb_right;
			continue;
		}
		e = rb_entry(this, struct size_entry, rb);
		if (e->inode)
			iput(e->inode);
		this = rb_parent(this);
		if (this) {
			if (this->rb_left == &e->rb)
				this->rb_left = NULL;
			else
				this->rb_right = NULL;
		}
		kfree(e);
	}
	c->size_tree = RB_ROOT;
}

/**
 * ubifs_recover_size_accum - accumulate inode sizes for recovery.
 * @c: UBIFS file-system description object
 * @key: node key
 * @deletion: node is for a deletion
 * @new_size: inode size
 *
 * This function has two purposes:
 *     1) to ensure there are no data nodes that fall outside the inode size
 *     2) to ensure there are no data nodes for inodes that do not exist
 * To accomplish those purposes, a rb-tree is constructed containing an entry
 * for each inode number in the journal that has not been deleted, and recording
 * the size from the inode node, the maximum size of any data node (also altered
 * by truncations) and a flag indicating a inode number for which no inode node
 * was present in the journal.
 *
 * Note that there is still the possibility that there are data nodes that have
 * been committed that are beyond the inode size, however the only way to find
 * them would be to scan the entire index. Alternatively, some provision could
 * be made to record the size of inodes at the start of commit, which would seem
 * very cumbersome for a scenario that is quite unlikely and the only negative
 * consequence of which is wasted space.
 *
 * This functions returns %0 on success and a negative error code on failure.
 */
int ubifs_recover_size_accum(struct ubifs_info *c, union ubifs_key *key,
			     int deletion, loff_t new_size)
{
	ino_t inum = key_inum(c, key);
	struct size_entry *e;
	int err;

	switch (key_type(c, key)) {
	case UBIFS_INO_KEY:
		if (deletion)
			remove_ino(c, inum);
		else {
			e = find_ino(c, inum);
			if (e) {
				e->i_size = new_size;
				e->exists = 1;
			} else {
				err = add_ino(c, inum, new_size, 0, 1);
				if (err)
					return err;
			}
		}
		break;
	case UBIFS_DATA_KEY:
		e = find_ino(c, inum);
		if (e) {
			if (new_size > e->d_size)
				e->d_size = new_size;
		} else {
			err = add_ino(c, inum, 0, new_size, 0);
			if (err)
				return err;
		}
		break;
	case UBIFS_TRUN_KEY:
		e = find_ino(c, inum);
		if (e)
			e->d_size = new_size;
		break;
	}
	return 0;
}

/**
 * fix_size_in_place - fix inode size in place on flash.
 * @c: UBIFS file-system description object
 * @e: inode size information for recovery
 */
static int fix_size_in_place(struct ubifs_info *c, struct size_entry *e)
{
	struct ubifs_ino_node *ino = c->sbuf;
	unsigned char *p;
	union ubifs_key key;
	int err, lnum, offs, len;
	loff_t i_size;
	uint32_t crc;

	/* Locate the inode node LEB number and offset */
	ino_key_init(c, &key, e->inum);
	err = ubifs_tnc_locate(c, &key, ino, &lnum, &offs);
	if (err)
		goto out;
	/*
	 * If the size recorded on the inode node is greater than the size that
	 * was calculated from nodes in the journal then don't change the inode.
	 */
	i_size = le64_to_cpu(ino->size);
	if (i_size >= e->d_size)
		return 0;
	/* Read the LEB */
	err = ubi_read(c->ubi, lnum, c->sbuf, 0, c->leb_size);
	if (err)
		goto out;
	/* Change the size field and recalculate the CRC */
	ino = c->sbuf + offs;
	ino->size = cpu_to_le64(e->d_size);
	len = le32_to_cpu(ino->ch.len);
	crc = crc32(UBIFS_CRC32_INIT, (void *)ino + 8, len - 8);
	ino->ch.crc = cpu_to_le32(crc);
	/* Work out where data in the LEB ends and free space begins */
	p = c->sbuf;
	len = c->leb_size - 1;
	while (p[len] == 0xff)
		len -= 1;
	len = ALIGN(len + 1, c->min_io_size);
	/* Atomically write the fixed LEB back again */
	err = ubi_leb_change(c->ubi, lnum, c->sbuf, len, UBI_UNKNOWN);
	if (err)
		goto out;
	dbg_rcvry("inode %lu at %d:%d size %lld -> %lld ",
		  (unsigned long)e->inum, lnum, offs, i_size, e->d_size);
	return 0;

out:
	ubifs_warn("inode %lu failed to fix size %lld -> %lld error %d",
		   (unsigned long)e->inum, e->i_size, e->d_size, err);
	return err;
}

/**
 * ubifs_recover_size - recover inode size.
 * @c: UBIFS file-system description object
 *
 * This function attempts to fix inode size discrepancies identified by the
 * 'ubifs_recover_size_accum()' function.
 *
 * This functions returns %0 on success and a negative error code on failure.
 */
int ubifs_recover_size(struct ubifs_info *c)
{
	struct rb_node *this = rb_first(&c->size_tree);

	while (this) {
		struct size_entry *e;
		int err;

		e = rb_entry(this, struct size_entry, rb);
		if (!e->exists) {
			union ubifs_key key;

			ino_key_init(c, &key, e->inum);
			err = ubifs_tnc_lookup(c, &key, c->sbuf);
			if (err && err != -ENOENT)
				return err;
			if (err == -ENOENT) {
				/* Remove data nodes that have no inode */
				dbg_rcvry("removing ino %lu",
					  (unsigned long)e->inum);
				err = ubifs_tnc_remove_ino(c, e->inum);
				if (err)
					return err;
			} else {
				struct ubifs_ino_node *ino = c->sbuf;

				e->exists = 1;
				e->i_size = le64_to_cpu(ino->size);
			}
		}
		if (e->exists && e->i_size < e->d_size) {
			if (!e->inode && (c->vfs_sb->s_flags & MS_RDONLY)) {
				/* Fix the inode size and pin it in memory */
				struct inode *inode;

				inode = ubifs_iget(c->vfs_sb, e->inum);
				if (IS_ERR(inode))
					return PTR_ERR(inode);
				if (inode->i_size < e->d_size) {
					dbg_rcvry("ino %lu size %lld -> %lld",
						  (unsigned long)e->inum,
						  e->d_size, inode->i_size);
					inode->i_size = e->d_size;
					ubifs_inode(inode)->ui_size = e->d_size;
					e->inode = inode;
					this = rb_next(this);
					continue;
				}
				iput(inode);
			} else {
				/* Fix the size in place */
				err = fix_size_in_place(c, e);
				if (err)
					return err;
				if (e->inode)
					iput(e->inode);
			}
		}
		this = rb_next(this);
		rb_erase(&e->rb, &c->size_tree);
		kfree(e);
	}
	return 0;
}
