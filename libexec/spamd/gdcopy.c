/*
 * Copyright (c) 2013 Todd C. Miller <millert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <db.h>
#include <string.h>

#include "grey.h"

/* Fill in struct gdata from DBT, converting from obsolete format as needed. */
int
gdcopyin(const void *v, struct gdata *gd)
{
	const DBT *dbd = v;
	int rc = 0;

	if (dbd->size == sizeof(struct gdata)) {
		/* Current grey data format. */
		memcpy(gd, dbd->data, sizeof(struct gdata));
	} else if (dbd->size == sizeof(struct ogdata)) {
		/* Backwards compat for obsolete grey data format. */
		struct ogdata ogd;
		memcpy(&ogd, dbd->data, sizeof(struct ogdata));
		gd->first = ogd.first;
		gd->pass = ogd.pass;
		gd->expire = ogd.expire;
		gd->bcount = ogd.bcount;
		gd->pcount = ogd.pcount;
	} else {
		/* Unsupported grey data format. */
		rc = -1;
	}
	return (rc);
}
