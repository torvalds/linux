/*	$OpenBSD: as.c,v 1.17 2024/11/12 09:23:07 tb Exp $ */
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

#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

/* Parse a uint32_t AS identifier from an ASN1_INTEGER. */
int
as_id_parse(const ASN1_INTEGER *v, uint32_t *out)
{
	uint64_t res = 0;

	if (!ASN1_INTEGER_get_uint64(&res, v))
		return 0;
	if (res > UINT32_MAX)
		return 0;
	*out = res;
	return 1;
}

/*
 * Given a newly-parsed AS number or range "as", make sure that "as" does
 * not overlap with any other numbers or ranges in the "ases" array.
 * This is defined by RFC 3779 section 3.2.3.4.
 * Returns zero on failure, non-zero on success.
 */
int
as_check_overlap(const struct cert_as *as, const char *fn,
    const struct cert_as *ases, size_t num_ases, int quiet)
{
	size_t	 i;

	/* We can have only one inheritance statement. */

	if (num_ases &&
	    (as->type == CERT_AS_INHERIT || ases[0].type == CERT_AS_INHERIT)) {
		if (!quiet) {
			warnx("%s: RFC 3779 section 3.2.3.3: "
			    "cannot have inheritance and multiple ASnum or "
			    "multiple inheritance", fn);
		}
		return 0;
	}

	/* Now check for overlaps between singletons/ranges. */

	for (i = 0; i < num_ases; i++) {
		switch (ases[i].type) {
		case CERT_AS_ID:
			switch (as->type) {
			case CERT_AS_ID:
				if (as->id != ases[i].id)
					continue;
				break;
			case CERT_AS_RANGE:
				if (ases->range.min > ases[i].id ||
				    ases->range.max < ases[i].id)
					continue;
				break;
			default:
				abort();
			}
			break;
		case CERT_AS_RANGE:
			switch (as->type) {
			case CERT_AS_ID:
				if (ases[i].range.min > as->id ||
				    ases[i].range.max < as->id)
					continue;
				break;
			case CERT_AS_RANGE:
				if (as->range.max < ases[i].range.min ||
				    as->range.min > ases[i].range.max)
					continue;
				break;
			default:
				abort();
			}
			break;
		default:
			abort();
		}
		if (!quiet) {
			warnx("%s: RFC 3779 section 3.2.3.4: "
			    "cannot have overlapping ASnum", fn);
		}
		return 0;
	}

	return 1;
}

/*
 * See if a given AS range (which may be the same number, in the case of
 * singleton AS identifiers) is covered by the AS numbers or ranges
 * specified in the "ases" array.
 * Return <0 if there is no cover, 0 if we're inheriting, >0 if there is.
 */
int
as_check_covered(uint32_t min, uint32_t max,
    const struct cert_as *ases, size_t num_ases)
{
	size_t	 i;
	uint32_t amin, amax;

	for (i = 0; i < num_ases; i++) {
		if (ases[i].type == CERT_AS_INHERIT)
			return 0;
		amin = ases[i].type == CERT_AS_RANGE ?
		    ases[i].range.min : ases[i].id;
		amax = ases[i].type == CERT_AS_RANGE ?
		    ases[i].range.max : ases[i].id;
		if (min >= amin && max <= amax)
			return 1;
	}

	return -1;
}

void
as_warn(const char *fn, const char *msg, const struct cert_as *as)
{
	switch (as->type) {
	case CERT_AS_ID:
		warnx("%s: %s: AS %u", fn, msg, as->id);
		break;
	case CERT_AS_RANGE:
		warnx("%s: %s: AS range %u--%u", fn, msg, as->range.min,
		    as->range.max);
		break;
	case CERT_AS_INHERIT:
		warnx("%s: %s: AS (inherit)", fn, msg);
		break;
	default:
		warnx("%s: corrupt cert", fn);
		break;
	}
}
