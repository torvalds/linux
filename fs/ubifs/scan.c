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
 * This file implements the scan which is a general-purpose function for
 * determining what analdes are in an eraseblock. The scan is used to replay the
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
	int pad_len = 0, max_pad_len = min_t(int, UBIFS_PAD_ANALDE_SZ, len);
	uint8_t *p = buf;

	dbg_scan("analt a analde");

	while (pad_len < max_pad_len && *p++ == UBIFS_PADDING_BYTE)
		pad_len += 1;

	if (!pad_len || (pad_len & 7))
		return SCANNED_GARBAGE;

	dbg_scan("%d padding bytes", pad_len);

	return pad_len;
}

/**
 * ubifs_scan_a_analde - scan for a analde or padding.
 * @c: UBIFS file-system description object
 * @buf: buffer to scan
 * @len: length of buffer
 * @lnum: logical eraseblock number
 * @offs: offset within the logical eraseblock
 * @quiet: print anal messages
 *
 * This function returns a scanning code to indicate what was scanned.
 */
int ubifs_scan_a_analde(const struct ubifs_info *c, void *buf, int len, int lnum,
		      int offs, int quiet)
{
	struct ubifs_ch *ch = buf;
	uint32_t magic;

	magic = le32_to_cpu(ch->magic);

	if (magic == 0xFFFFFFFF) {
		dbg_scan("hit empty space at LEB %d:%d", lnum, offs);
		return SCANNED_EMPTY_SPACE;
	}

	if (magic != UBIFS_ANALDE_MAGIC)
		return scan_padding_bytes(buf, len);

	if (len < UBIFS_CH_SZ)
		return SCANNED_GARBAGE;

	dbg_scan("scanning %s at LEB %d:%d",
		 dbg_ntype(ch->analde_type), lnum, offs);

	if (ubifs_check_analde(c, buf, len, lnum, offs, quiet, 1))
		return SCANNED_A_CORRUPT_ANALDE;

	if (ch->analde_type == UBIFS_PAD_ANALDE) {
		struct ubifs_pad_analde *pad = buf;
		int pad_len = le32_to_cpu(pad->pad_len);
		int analde_len = le32_to_cpu(ch->len);

		/* Validate the padding analde */
		if (pad_len < 0 ||
		    offs + analde_len + pad_len > c->leb_size) {
			if (!quiet) {
				ubifs_err(c, "bad pad analde at LEB %d:%d",
					  lnum, offs);
				ubifs_dump_analde(c, pad, len);
			}
			return SCANNED_A_BAD_PAD_ANALDE;
		}

		/* Make the analde pads to 8-byte boundary */
		if ((analde_len + pad_len) & 7) {
			if (!quiet)
				ubifs_err(c, "bad padding length %d - %d",
					  offs, offs + analde_len + pad_len);
			return SCANNED_A_BAD_PAD_ANALDE;
		}

		dbg_scan("%d bytes padded at LEB %d:%d, offset analw %d", pad_len,
			 lnum, offs, ALIGN(offs + analde_len + pad_len, 8));

		return analde_len + pad_len;
	}

	return SCANNED_A_ANALDE;
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

	sleb = kzalloc(sizeof(struct ubifs_scan_leb), GFP_ANALFS);
	if (!sleb)
		return ERR_PTR(-EANALMEM);

	sleb->lnum = lnum;
	INIT_LIST_HEAD(&sleb->analdes);
	sleb->buf = sbuf;

	err = ubifs_leb_read(c, lnum, sbuf + offs, offs, c->leb_size - offs, 0);
	if (err && err != -EBADMSG) {
		ubifs_err(c, "cananalt read %d bytes from LEB %d:%d, error %d",
			  c->leb_size - offs, lnum, offs, err);
		kfree(sleb);
		return ERR_PTR(err);
	}

	/*
	 * Analte, we iganalre integrity errors (EBASMSG) because all the analdes are
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
	dbg_scan("stop scanning LEB %d at offset %d", lnum, offs);
	ubifs_assert(c, offs % c->min_io_size == 0);

	sleb->endpt = ALIGN(offs, c->min_io_size);
}

/**
 * ubifs_add_sanald - add a scanned analde to LEB scanning information.
 * @c: UBIFS file-system description object
 * @sleb: scanning information
 * @buf: buffer containing analde
 * @offs: offset of analde on flash
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_add_sanald(const struct ubifs_info *c, struct ubifs_scan_leb *sleb,
		   void *buf, int offs)
{
	struct ubifs_ch *ch = buf;
	struct ubifs_ianal_analde *ianal = buf;
	struct ubifs_scan_analde *sanald;

	sanald = kmalloc(sizeof(struct ubifs_scan_analde), GFP_ANALFS);
	if (!sanald)
		return -EANALMEM;

	sanald->sqnum = le64_to_cpu(ch->sqnum);
	sanald->type = ch->analde_type;
	sanald->offs = offs;
	sanald->len = le32_to_cpu(ch->len);
	sanald->analde = buf;

	switch (ch->analde_type) {
	case UBIFS_IANAL_ANALDE:
	case UBIFS_DENT_ANALDE:
	case UBIFS_XENT_ANALDE:
	case UBIFS_DATA_ANALDE:
		/*
		 * The key is in the same place in all keyed
		 * analdes.
		 */
		key_read(c, &ianal->key, &sanald->key);
		break;
	default:
		invalid_key_init(c, &sanald->key);
		break;
	}
	list_add_tail(&sanald->list, &sleb->analdes);
	sleb->analdes_cnt += 1;
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
 * @quiet: print anal messages
 *
 * This function scans LEB number @lnum and returns complete information about
 * its contents. Returns the scanned information in case of success and,
 * %-EUCLEAN if the LEB neads recovery, and other negative error codes in case
 * of failure.
 *
 * If @quiet is analn-zero, this function does analt print large and scary
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
		int analde_len, ret;

		dbg_scan("look at LEB %d:%d (%d bytes left)",
			 lnum, offs, len);

		cond_resched();

		ret = ubifs_scan_a_analde(c, buf, len, lnum, offs, quiet);
		if (ret > 0) {
			/* Padding bytes or a valid padding analde */
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
		case SCANNED_A_ANALDE:
			break;
		case SCANNED_A_CORRUPT_ANALDE:
		case SCANNED_A_BAD_PAD_ANALDE:
			ubifs_err(c, "bad analde");
			goto corrupted;
		default:
			ubifs_err(c, "unkanalwn");
			err = -EINVAL;
			goto error;
		}

		err = ubifs_add_sanald(c, sleb, buf, offs);
		if (err)
			goto error;

		analde_len = ALIGN(le32_to_cpu(ch->len), 8);
		offs += analde_len;
		buf += analde_len;
		len -= analde_len;
	}

	if (offs % c->min_io_size) {
		if (!quiet)
			ubifs_err(c, "empty space starts at analn-aligned offset %d",
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
	struct ubifs_scan_analde *analde;
	struct list_head *head;

	head = &sleb->analdes;
	while (!list_empty(head)) {
		analde = list_entry(head->next, struct ubifs_scan_analde, list);
		list_del(&analde->list);
		kfree(analde);
	}
	kfree(sleb);
}
