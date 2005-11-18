/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: writev.c,v 1.8 2005/09/09 15:11:58 havasi Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/mtd/mtd.h>
#include "nodelist.h"

/* This ought to be in core MTD code. All registered MTD devices
   without writev should have this put in place. Bug the MTD
   maintainer */
static inline int mtd_fake_writev(struct mtd_info *mtd, const struct kvec *vecs,
				  unsigned long count, loff_t to, size_t *retlen)
{
	unsigned long i;
	size_t totlen = 0, thislen;
	int ret = 0;

	for (i=0; i<count; i++) {
		if (!vecs[i].iov_len)
			continue;
		ret = mtd->write(mtd, to, vecs[i].iov_len, &thislen, vecs[i].iov_base);
		totlen += thislen;
		if (ret || thislen != vecs[i].iov_len)
			break;
		to += vecs[i].iov_len;
	}
	if (retlen)
		*retlen = totlen;
	return ret;
}

int jffs2_flash_direct_writev(struct jffs2_sb_info *c, const struct kvec *vecs,
			      unsigned long count, loff_t to, size_t *retlen)
{
	if (!jffs2_is_writebuffered(c)) {
		if (jffs2_sum_active()) {
			int res;
			res = jffs2_sum_add_kvec(c, vecs, count, (uint32_t) to);
			if (res) {
				return res;
			}
		}
	}

	if (c->mtd->writev)
		return c->mtd->writev(c->mtd, vecs, count, to, retlen);
	else {
		return mtd_fake_writev(c->mtd, vecs, count, to, retlen);
	}
}

int jffs2_flash_direct_write(struct jffs2_sb_info *c, loff_t ofs, size_t len,
			size_t *retlen, const u_char *buf)
{
	int ret;
	ret = c->mtd->write(c->mtd, ofs, len, retlen, buf);

	if (jffs2_sum_active()) {
		struct kvec vecs[1];
		int res;

		vecs[0].iov_base = (unsigned char *) buf;
		vecs[0].iov_len = len;

		res = jffs2_sum_add_kvec(c, vecs, 1, (uint32_t) ofs);
		if (res) {
			return res;
		}
	}
	return ret;
}
