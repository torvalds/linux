/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: pushpull.h,v 1.10 2004/11/16 20:36:11 dwmw2 Exp $
 *
 */

#ifndef __PUSHPULL_H__
#define __PUSHPULL_H__

#include <linux/errno.h>

struct pushpull {
	unsigned char *buf;
	unsigned int buflen;
	unsigned int ofs;
	unsigned int reserve;
};


static inline void init_pushpull(struct pushpull *pp, char *buf, unsigned buflen, unsigned ofs, unsigned reserve)
{
	pp->buf = buf;
	pp->buflen = buflen;
	pp->ofs = ofs;
	pp->reserve = reserve;
}

static inline int pushbit(struct pushpull *pp, int bit, int use_reserved)
{
	if (pp->ofs >= pp->buflen - (use_reserved?0:pp->reserve)) {
		return -ENOSPC;
	}

	if (bit) {
		pp->buf[pp->ofs >> 3] |= (1<<(7-(pp->ofs &7)));
	}
	else {
		pp->buf[pp->ofs >> 3] &= ~(1<<(7-(pp->ofs &7)));
	}
	pp->ofs++;

	return 0;
}

static inline int pushedbits(struct pushpull *pp)
{
	return pp->ofs;
}

static inline int pullbit(struct pushpull *pp)
{
	int bit;

	bit = (pp->buf[pp->ofs >> 3] >> (7-(pp->ofs & 7))) & 1;

	pp->ofs++;
	return bit;
}

static inline int pulledbits(struct pushpull *pp)
{
	return pp->ofs;
}

#endif /* __PUSHPULL_H__ */
