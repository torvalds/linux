/*	$OpenBSD: ldexp_test.c,v 1.1 2017/10/15 12:15:30 visa Exp $	*/

/*
 * Copyright (c) 2017 Visa Hankala
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

#include <assert.h>
#include <math.h>

int
main(int argc, char *argv[])
{
	double f;

	/*
	 * Test that the result has the correct sign.
	 * Assumes IEEE 754 double-precision arithmetics.
	 */

	assert(ldexp(1.0, -1022) > 0.0); /* IEEE 754 minimum normal positive */
	assert(ldexp(1.0, -1023) > 0.0); /* subnormal positive */
	assert(ldexp(1.0, -1024) > 0.0); /* subnormal positive */
	assert(ldexp(1.0, -1074) > 0.0); /* minimum subnormal positive */
	assert(ldexp(1.0, -1075) >= 0.0); /* zero */
	assert(ldexp(ldexp(1.0, -1022), -53) >= 0.0); /* zero */

	assert(ldexp(1.0, 1023) > 0.0);	/* normal positive */

	f = ldexp(1.0, 1024);		/* infinite positive */
	assert(isinf(f));
	assert(!signbit(f));

	f = ldexp(ldexp(1.0, 1023), 1);	/* infinite positive */
	assert(isinf(f));
	assert(!signbit(f));

	assert(ldexp(-1.0, -1022) < 0.0); /* IEEE 754 maximum normal negative */
	assert(ldexp(-1.0, -1023) < 0.0); /* subnormal negative */
	assert(ldexp(-1.0, -1024) < 0.0); /* subnormal negative */
	assert(ldexp(-1.0, -1074) < 0.0); /* maximum subnormal negative */
	assert(ldexp(-1.0, -1075) <= 0.0); /* zero */
	assert(ldexp(ldexp(-1.0, -1022), -53) <= 0.0); /* zero */

	assert(ldexp(-1.0, 1023) < 0.0); /* normal negative */

	f = ldexp(-1.0, 1024);		/* infinite negative */
	assert(isinf(f));
	assert(signbit(f));

	f = ldexp(ldexp(-1.0, 1023), 1); /* infinite negative */
	assert(isinf(f));
	assert(signbit(f));

	f = ldexp(NAN, 0);
	assert(isnan(f));
	assert(!signbit(f));

	f = ldexp(-NAN, 0);
	assert(isnan(f));
	assert(signbit(f));

	return 0;
}
