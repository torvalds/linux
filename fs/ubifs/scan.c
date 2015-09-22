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
 * This file implements the scan which is a general-purpose function for
 * determining what nodes are in an eraseblock. The scan is used to replay the
 * journal, to do garbage collection. for the TNC in-the-gaps method, and by
 * debugging functions.
 */

#include "ubifs.h"

/**
 * scan_padding_bytes - scan for padding bytes.
 * @buf: buffer to scan
 * @len: length of buffer
 *
 * This function returns the number of padding bytes on success and
 * %SCANNED_GARBAGE on failure.
 */
static int scan_padding_bytes(void *buf, int len)
{
	int pad_len = 0, max_pad_len = min_t(int, UBIFS_PAD_NODE_SZ, len);
	uint8_t *p = buf;

	dbg_scan("not a node");

	while (pad_len < max_pad_len && *p++ == UBIFS_PADDING_BYTE)
		pad_len += 1;

	if (!pad_len || (pad_len & 7))
		return SCANNED_GARBAGE;

	dbg_scan("%d padding bytes", pad_len);

	return pad_len;
}

/**
 * ubifs_scan_a_node - scan for a node or padding.
 * @c: UBIFS file-system description object
 * @buf: buffer to scan
 * @len: length of buffer
 * @lnum: logical eraseblock number
 * @offs: offset within the logical eraseblock
 * @quiet: print no messages
 *
 * This function returns a scanning code to indicate what was scanned.
 */
int ubifs_scan_a_node(const struct ubifs_info *c, void *buf, int len, int lnum,
		      int offs, int quiet)
{
	struct ubifs_ch *ch = buf;
	uint32_t magic;

	magic = le32_to_cpu(ch->magic);

	if (magic == 0xFFFFFFFF) {
		dbg_scan("hit empty space at LEB %d:%d", lnum, offs);
		return SCANNED_EMPTY_SPACE;
	}

	if (magic != UBIFS_NODE_MAGIC)
		return scan_padding_bytes(buf, len);

	if (len < UBIFS_CH_SZ)
		return SCANNED_GARBAGE;

	dbg_scan("scanning %s at LEB %d:%d",
		 dbg_ntype(ch->node_type), lnum, offs);

	if (ubifs_check_node(c, buf, lnum, offs, quiet, 1))
		return SCANNED_A_CORRUPT_NODE;

	if (ch->node_type == UBIFS_PAD_NODE) {
		struct ubifs_pad_node *pad = buf;
		int pad_len = le32_to_cpu(pad->pad_len);
		int node_len = le32_to_cpu(ch->len);

		/* Validate the padding node */
		if (pad_len < 0 ||
		    offs + node_len + pad_len > c->leb_size) {
			if (!quiet) {
				ubifs_err(c, "bad pad node at LEB %d:%d",
					  lnum, offs);
				ubifs_dump_node(c, pad);
			}
			return SCANNED_A_BAD_PAD_NODE;
		}

		/* Make the node pads to 8-byte boundary */
		if ((node_len + pad_len) & 7) {
			if (!quiet)
				ubifs_err(c, "bad padding length %d - %d",
					  offs, offs + node_len + pad_len);
			return SCANNED_A_BAD_PAD_NODE;
		}

		dbg_scan("%d bytes padded at LEB %d:%d, offset now %d", pad_len,
			 lnum, offs, ALIGN(offs + node_len + pad_len, 8));

		return node_len + pad_len;
	}

	return SCANNED_A_NODE;
}

/**
 * ubifs_start_scan - create LEB scanning information at start of scan.
 * @c: UBIFS file-system description object
 * @lnum: logical eraseblock number
 * @offs: offset to start at (usually zero)
 * @sbuf: scan buffer (must be c->leb_size)
 *
 * This function returns the scanned information on success and a negative error
 * code on failure.
 */
struct ubifs_scan_leb *ubifs_start_scan(const struct ubifs_info *c, int lnum,
					int offs, void *sbuf)
{
	struct ubifs_scan_leb *sleb;
	int err;

	dbg_scan("scan LEB %d:%d", lnum, offs);

	sleb = kzalloc(sizeof(struct ubifs_scan_leb), GFP_NOFS);
	if (!sleb)
		return ERR_PTR(-ENOMEM);

	sleb->lnum = lnum;
	INIT_LIST_HEAD(&sleb->nodes);
	sleb->buf = sbuf;

	err = ubifs_leb_read(c, lnum, sbuf + offs, offs, c->leb_size - offs, 0);
	if (err && err != -EBADMSG) {
		ubifs_err(c, "cannot read %d bytes from LEB %d:%d, error %d",
			  c->leb_size - offs, lnum, offs, err);
		kfree(sleb);
		return ERR_PTR(err);
	}

	/*
	 * Note, we ignore integrity errors (EBASMSG) because all the nodes are
	 * protected by CRC checksums.
	 */
	return sleb;
}

/**
 * ubifs_end_scan - update LEB scanning information at end of scan.
 * @c: UBIFS file-system description object
 * @sleb: scanning information
 * @lnum: logical eraseblock number
 * @offs: offset to start at (usually zero)
 */
void ubifs_end_scan(const struct ubifs_info *c, struct ubifs_scan_leb *sleb,
		    int lnum, int offs)
{
	lnum = lnum;
	dbg_scan("stop scanning LEB %d at offset %d", lnum, offs);
	ubifs_assert(offs % c->min_io_size == 0);

	sleb->endpt = ALIGN(offs, c->min_io_size);
}

/**
 * ubifs_add_snod - add a scanned node to LEB scanning information.
 * @c: UBIFS file-system description object
 * @sleb: scanning information
 * @buf: buffer containing node
 * @offs: offset of node on flash
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_add_snod(const struct ubifs_info *c, struct ubifs_scan_leb *sleb,
		   void *buf, int offs)
{
	struct ubifs_ch *ch = buf;
	struct ubifs_ino_node *ino = buf;
	struct ubifs_scan_node *snod;

	snod = kmalloc(sizeof(struct ubifs_scan_node), GFP_NOFS);
	if (!snod)
		return -ENOMEM;

	snod->sqnum = le64_to_cpu(ch->sqnum);
	snod->type = ch->node_type;
	snod->offs = offs;
	snod->len = le32_to_cpu(ch->len);
	snod->node = buf;

	switch (ch->node_type) {
	case UBIFS_INO_NODE:
	case UBIFS_DENT_NODE:
	case UBIFS_XENT_NODE:
	case UBIFS_DATA_NODE:
		/*
		 * The key is in the same place in all keyed
		 * nodes.
		 */
		key_read(c, &ino->key, &snod->key);
		break;
	default:
		invalid_key_init(c, &snod->key);
		break;
	}
	list_add_tail(&snod->list, &sleb->nodes);
	sleb->nodes_cnt += 1;
	return 0;
}

/**
 * ubifs_scanned_corruption - print information after UBIFS scanned corruption.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of corruption
 * @offs: offset of corruption
 * @buf: buffer containing corruption
 */
void ubifs_scanned_corruption(const struct ubifs_info *c, int lnum, int offs,
			      void *buf)
{
	int len;

	ubifs_err(c, "corruption at LEB %d:%d", lnum, offs);
	len = c->leb_size - offs;
	if (len > 8192)
		len = 8192;
	ubifs_err(c, "first %d bytes from LEB %d:%d", len, lnum, offs);
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 4, buf, len, 1);
}

/**
 * ubifs_scan - scan a logical eraseblock.
 * @c: UBIFS file-system description object
 * @lnum: logical eraseblock number
 * @offs: offset to start at (usually zero)
 * @sbuf: scan buffer (must be of @c->leb_size bytes in size)
 * @quiet: print no messages
 *
 * This function scans LEB number @lnum and returns complete information about
 * its contents. Returns the scanned information in case of success and,
 * %-EUCLEAN if the LEB neads recovery, and other negative error codes in case
 * of failure.
 *
 * If @quiet is non-zero, this function does not print large and scary
 * error messages and flash dumps in case of errors.
 */
struct ubifs_scan_leb *ubifs_scan(const struct ubifs_info *c, int lnum,
				  int offs, void *sbuf, int quiet)
{
	void *buf = sbuf + offs;
	int err, len = c->leb_size - offs;
	struct ubifs_scan_leb *sleb;

	sleb = ubifs_start_scan(c, lnum, offs, sbuf);
	if (IS_ERR(sleb))
		return sleb;

	while (len >= 8) {
		struct ubifs_ch *ch = buf;
		int node_len, ret;

		dbg_scan("look at LEB %d:%d (%d bytes left)",
			 lnum, offs, len);

		cond_resched();

		ret = ubifs_scan_a_node(c, buf, len, lnum, offs, quiet);
		if (ret > 0) {
			/* Padding bytes or a valid padding node */
			offs += ret;
			buf += ret;
			len -= ret;
			continue;
		}

		if (ret == SCANNED_EMPTY_SPACE)
			/* Empty space is checked later */
			break;

		switch (ret) {
		case SCANNED_GARBAGE:
			ubifs_err(c, "garbage");
			goto corrupted;
		case SCANNED_A_NODE:
			break;
		case SCANNED_A_CORRUPT_NODE:
		case SCANNED_A_BAD_PAD_NODE:
			ubifs_err(c, "bad node");
			goto corrupted;
		default:
			ubifs_err(c, "unknown");
			err = -EINVAL;
			goto error;
		}

		err = ubifs_add_snod(c, sleb, buf, offs);
		if (err)
			goto error;

		node_len = ALIGN(le32_to_cpu(ch->len), 8);
		offs += node_len;
		buf += node_len;
		len -= node_len;
	}

	if (offs % c->min_io_size) {
		if (!quiet)
			ubifs_err(c, "empty space starts at non-aligned offset %d",
				  offs);
		goto corrupted;
	}

	ubifs_end_scan(c, sleb, lnum, offs);

	for (; len > 4; offs += 4, buf = buf + 4, len -= 4)
		if (*(uint32_t *)buf != 0xffffffff)
			break;
	for (; len; offs++, buf++, len--)
		if (*(uint8_t *)buf != 0xff) {
			if (!quiet)
				ubifs_err(c, "corrupt empty space at LEB %d:%d",
					  lnum, offs);
			goto corrupted;
		}

	return sleb;

corrupted:
	if (!quiet) {
		ubifs_scanned_corruption(c, lnum, offs, buf);
		ubifs_err(c, "LEB %d scanning failed", lnum);
	}
	err = -EUCLEAN;
	ubifs_scan_destroy(sleb);
	return ERR_PTR(err);

error:
	ubifs_err(c, "LEB %d scanning failed, error %d", lnum, err);
	ubifs_scan_destroy(sleb);
	return ERR_PTR(err);
}

/**
 * ubifs_scan_destroy - destroy LEB scanning information.
 * @sleb: scanning information to free
 */
void ubifs_scan_destroy(struct ubifs_scan_leb *sleb)
{
	struct ubifs_scan_node *node;
	struct list_head *head;

	head = &sleb->nodes;
	while (!list_empty(head)) {
		node = list_entry(head->next, struct ubifs_scan_node, list);
		list_del(&node->list);
		kfree(node);
	}
	kfree(sleb);
}
