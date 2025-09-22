/*	$OpenBSD: output-csv.c,v 1.16 2025/07/08 14:19:21 job Exp $ */
/*
 * Copyright (c) 2019 Claudio Jeker <claudio@openbsd.org>
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

#include <stdlib.h>

#include "extern.h"

int
output_csv(FILE *out, struct validation_data *vd, struct stats *st)
{
	struct vrp	*v;

	if (fprintf(out, "ASN,IP Prefix,Max Length,Trust Anchor,Expires\n") < 0)
		return -1;

	RB_FOREACH(v, vrp_tree, &vd->vrps) {
		char buf[64];

		ip_addr_print(&v->addr, v->afi, buf, sizeof(buf));

		if (fprintf(out, "AS%u,%s,%u,%s,%lld\n", v->asid, buf,
		    v->maxlength, taldescs[v->talid],
		    (long long)v->expires) < 0)
			return -1;
	}
	return 0;
}
