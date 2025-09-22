/*	$OpenBSD: rde_flowspec_test.c,v 1.2 2023/04/18 06:41:00 claudio Exp $ */

/*
 * Copyright (c) 2023 Claudio Jeker <claudio@openbsd.org>
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
#include <stdio.h>

#include "bgpd.h"
#include "rde.h"

const uint8_t ordered0[] = { 0x01, 0x00, 0x02, 0x00, 0x03, 0x80, 0x00 };
const uint8_t ordered1[] = { 0x02, 0x00, 0x01, 0x00, 0x03, 0x80, 0x00 };
const uint8_t ordered2[] = { 0x02, 0x00, 0x03, 0x80, 0x00, 0x01, 0x00 };
const uint8_t ordered3[] = { 0x01, 0x00, 0x01, 0x00, 0x03, 0x80, 0x00 };

const uint8_t minmax0[] = { 0x00, 0x00 };
const uint8_t minmax1[] = { 0x0e, 0x00 };
const uint8_t minmax2[] = { 0xfe, 0x00 };

const uint8_t flow[] = { 0x0d, 0x80, 0x00 };

const uint8_t badand[] = { 0x04, 0xc0, 0x00 };
const uint8_t goodand[] = { 0x04, 0x00, 0x00, 0xc0, 0x00 };

const uint8_t overflow0[] = { 0x04 };
const uint8_t overflow1[] = { 0x04, 0x80 };
const uint8_t overflow2[] = { 0x04, 0x90, 0x00 };
const uint8_t overflow3[] = { 0x04, 0xc0, 0x00, 0x00, 0x00 };
const uint8_t overflow4[] = { 0x04, 0x00, 0x00 };
const uint8_t overflow5[] = { 0x04, 0x00, 0x00, 0x80 };
const uint8_t overflow6[] = { 0x04, 0x10, 0x00, 0x80 };
const uint8_t prefix0[] = { 0x01 };
const uint8_t prefix1[] = { 0x01, 0x07 };
const uint8_t prefix2[] = { 0x01, 0x0a, 0xef };
const uint8_t prefix3[] = { 0x01, 0x11, 0xef, 0x00 };
const uint8_t prefix4[] = { 0x01, 0x21, 0xef, 0x00, 0x00, 0x01, 0x00 };
const uint8_t prefix60[] = { 0x01 };
const uint8_t prefix61[] = { 0x01, 0x07 };
const uint8_t prefix62[] = { 0x01, 0x10, 0x1 };
const uint8_t prefix63[] = { 0x01, 0x10, 0x01, 0x20 };
const uint8_t prefix64[] = { 0x01, 0x81, 0x73, 0x20, 0x01 };
const uint8_t prefix65[] = { 0x01, 0x80, 0x83, 0x20, 0x01 };
const uint8_t prefix66[] = { 0x01, 0x40, 0x40, 0x20, 0x01 };

static void
test_flowspec_valid(void)
{
	/* empty NLRI is invalid */
	if (flowspec_valid(NULL, 0, 0) != -1)
		errx(1, "empty NLRI is not invalid");

	/* ensure that type range is checked */
	if (flowspec_valid(minmax0, sizeof(minmax0), 0) != -1 ||
	    flowspec_valid(minmax1, sizeof(minmax1), 0) != -1 ||
	    flowspec_valid(minmax2, sizeof(minmax2), 0) != -1)
		errx(1, "out of range type is not invalid");

	/* ensure that types are ordered */
	if (flowspec_valid(ordered0, sizeof(ordered0), 0) != 0)
		errx(1, "in order NLRI is not valid");
	if (flowspec_valid(ordered1, sizeof(ordered1), 0) != -1 ||
	    flowspec_valid(ordered2, sizeof(ordered2), 0) != -1 ||
	    flowspec_valid(ordered3, sizeof(ordered3), 0) != -1)
		errx(1, "out of order types are not invalid");

	/* flow is only valid in the IPv6 case */
	if (flowspec_valid(flow, sizeof(flow), 0) != -1)
		errx(1, "FLOW type for IPv4 is not invalid");
	if (flowspec_valid(flow, sizeof(flow), 1) != 0)
		errx(1, "FLOW type for IPv4 is not valid");

	/* first component cannot have and flag set */
	if (flowspec_valid(badand, sizeof(badand), 0) != -1)
		errx(1, "AND in first element is not invalid");
	if (flowspec_valid(goodand, sizeof(goodand), 0) != 0)
		errx(1, "AND in other element is not valid");

	/* various overflows */
	if (flowspec_valid(overflow0, sizeof(overflow0), 0) != -1 ||
	    flowspec_valid(overflow1, sizeof(overflow1), 0) != -1 ||
	    flowspec_valid(overflow2, sizeof(overflow2), 0) != -1 ||
	    flowspec_valid(overflow3, sizeof(overflow3), 0) != -1 ||
	    flowspec_valid(overflow4, sizeof(overflow4), 0) != -1 ||
	    flowspec_valid(overflow5, sizeof(overflow5), 0) != -1 ||
	    flowspec_valid(overflow6, sizeof(overflow6), 0) != -1)
		errx(1, "overflow not detected");

	if (flowspec_valid(prefix0, sizeof(prefix0), 0) != -1 ||
	    flowspec_valid(prefix1, sizeof(prefix1), 0) != -1 ||
	    flowspec_valid(prefix2, sizeof(prefix2), 0) != -1 ||
	    flowspec_valid(prefix3, sizeof(prefix3), 0) != -1 ||
	    flowspec_valid(prefix4, sizeof(prefix4), 0) != -1)
		errx(1, "bad prefix encoding is not invalid");

	if (flowspec_valid(prefix60, sizeof(prefix60), 1) != -1 ||
	    flowspec_valid(prefix61, sizeof(prefix61), 1) != -1 ||
	    flowspec_valid(prefix62, sizeof(prefix62), 1) != -1 ||
	    flowspec_valid(prefix63, sizeof(prefix63), 1) != -1 ||
	    flowspec_valid(prefix64, sizeof(prefix64), 1) != -1 ||
	    flowspec_valid(prefix65, sizeof(prefix65), 1) != -1 ||
	    flowspec_valid(prefix66, sizeof(prefix66), 1) != -1)
		errx(1, "bad IPv6 prefix encoding is not invalid");
}

static int
do_cmp(const uint8_t *a, int alen, const uint8_t *b, int blen, int is_v6)
{
	if (flowspec_cmp(a, alen, b, blen, is_v6) != -1 ||
	    flowspec_cmp(b, blen, a, alen, is_v6) != 1)
		return -1;
	return 0;
}

const uint8_t cmp1[] = { 0x01, 0x00 };
const uint8_t cmp2[] = { 0x02, 0x00 };
const uint8_t cmp3[] = { 0x01, 0x00, 0x2, 0x00 };
const uint8_t cmp4[] = { 0x04, 0x80, 0x2 };
const uint8_t cmp5[] = { 0x04, 0x80, 0x3 };
const uint8_t cmp6[] = { 0x04, 0x00, 0x3, 0x80, 0x02 };

const uint8_t cmp41[] = { 0x01, 24, 192, 168, 16 };
const uint8_t cmp42[] = { 0x01, 24, 192, 168, 32 };
const uint8_t cmp43[] = { 0x01, 24, 192, 168, 42 };
const uint8_t cmp44[] = { 0x01, 20, 192, 168, 32 };

const uint8_t cmp61[] = { 0x01, 48, 0, 0x20, 0x01, 0x0d, 0xb8, 0xc0, 0xfe };
const uint8_t cmp62[] = { 0x01, 48, 8, 0x01, 0x0d, 0xb8, 0xc0, 0xfe };
const uint8_t cmp63[] = { 0x01, 40, 0, 0x20, 0x01, 0x0d, 0xb8, 0xc0 };
const uint8_t cmp64[] = { 0x01, 40, 0, 0x20, 0x01, 0x0d, 0xb8, 0xd0 };

static void
test_flowspec_cmp(void)
{
	if (do_cmp(cmp1, sizeof(cmp1), cmp2, sizeof(cmp2), 0) != 0)
		errx(1, "cmp on type failed");
	if (do_cmp(cmp3, sizeof(cmp3), cmp1, sizeof(cmp1), 0) != 0)
		errx(1, "cmp on more components failed");
	if (do_cmp(cmp4, sizeof(cmp4), cmp5, sizeof(cmp5), 0) != 0)
		errx(1, "cmp on lowest common component failed");
	if (do_cmp(cmp6, sizeof(cmp6), cmp5, sizeof(cmp5), 0) != 0)
		errx(1, "cmp on lowest common component failed");
	if (do_cmp(cmp6, sizeof(cmp6), cmp4, sizeof(cmp4), 0) != 0)
		errx(1, "cmp on lowest common component failed");

	if (do_cmp(cmp41, sizeof(cmp41), cmp42, sizeof(cmp42), 0) != 0)
		errx(1, "cmp 1 on prefix component failed");
	if (do_cmp(cmp41, sizeof(cmp41), cmp43, sizeof(cmp43), 0) != 0)
		errx(1, "cmp 2 on prefix component failed");
	if (do_cmp(cmp41, sizeof(cmp41), cmp44, sizeof(cmp44), 0) != 0)
		errx(1, "cmp 3 on prefix component failed");
	if (do_cmp(cmp42, sizeof(cmp42), cmp43, sizeof(cmp43), 0) != 0)
		errx(1, "cmp 4 on prefix component failed");
	if (do_cmp(cmp42, sizeof(cmp42), cmp44, sizeof(cmp44), 0) != 0)
		errx(1, "cmp 5 on prefix component failed");
	if (do_cmp(cmp43, sizeof(cmp43), cmp44, sizeof(cmp44), 0) != 0)
		errx(1, "cmp 6 on prefix component failed");

	if (do_cmp(cmp61, sizeof(cmp61), cmp62, sizeof(cmp62), 1) != 0)
		errx(1, "cmp 1 on inet6 prefix component failed");
	if (do_cmp(cmp61, sizeof(cmp61), cmp63, sizeof(cmp63), 1) != 0)
		errx(1, "cmp 1 on inet6 prefix component failed");
	if (do_cmp(cmp61, sizeof(cmp61), cmp64, sizeof(cmp64), 1) != 0)
		errx(1, "cmp 1 on inet6 prefix component failed");
	if (do_cmp(cmp63, sizeof(cmp63), cmp64, sizeof(cmp64), 1) != 0)
		errx(1, "cmp 1 on inet6 prefix component failed");
}

int
main(int argc, char **argv)
{
	test_flowspec_valid();
	test_flowspec_cmp();
	printf("OK\n");
	return 0;
}

__dead void
fatalx(const char *emsg, ...)
{
	va_list ap;
	va_start(ap, emsg);
	verrx(2, emsg, ap);
}

