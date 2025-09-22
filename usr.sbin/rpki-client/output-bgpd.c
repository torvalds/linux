/*	$OpenBSD: output-bgpd.c,v 1.35 2025/08/23 09:13:14 job Exp $ */
/*
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
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
output_bgpd(FILE *out, struct validation_data *vd, struct stats *st)
{
	struct vrp	*vrp;
	struct vap	*vap;
	size_t		 i;

	if (outputheader(out, vd, st) < 0)
		return -1;

	if (fprintf(out, "roa-set {\n") < 0)
		return -1;

	RB_FOREACH(vrp, vrp_tree, &vd->vrps) {
		char ipbuf[64], maxlenbuf[100];

		ip_addr_print(&vrp->addr, vrp->afi, ipbuf, sizeof(ipbuf));
		if (vrp->maxlength > vrp->addr.prefixlen) {
			int ret = snprintf(maxlenbuf, sizeof(maxlenbuf),
			    "maxlen %u ", vrp->maxlength);
			if (ret < 0 || (size_t)ret > sizeof(maxlenbuf))
				return -1;
		} else
			maxlenbuf[0] = '\0';
		if (fprintf(out, "\t%s %ssource-as %u expires %lld\n",
		    ipbuf, maxlenbuf, vrp->asid, (long long)vrp->expires) < 0)
			return -1;
	}

	if (fprintf(out, "}\n") < 0)
		return -1;

	if (excludeaspa)
		return 0;

	if (fprintf(out, "\naspa-set {\n") < 0)
		return -1;
	RB_FOREACH(vap, vap_tree, &vd->vaps) {
		if (vap->overflowed)
			continue;
		if (fprintf(out, "\tcustomer-as %d expires %lld "
		    "provider-as { ", vap->custasid,
		    (long long)vap->expires) < 0)
			return -1;
		for (i = 0; i < vap->num_providers; i++) {
			if (fprintf(out, "%u", vap->providers[i]) < 0)
				return -1;
			if (i + 1 < vap->num_providers)
				if (fprintf(out, ", ") < 0)
					return -1;
		}

		if (fprintf(out, " }\n") < 0)
			return -1;
	}
	if (fprintf(out, "}\n") < 0)
		return -1;

	return 0;
}
