/*	$OpenBSD: crypto_test.c,v 1.2 2024/11/08 14:06:34 jsing Exp $	*/
/*
 * Copyright (c) 2024 Joel Sing <jsing@openbsd.org>
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "crypto_internal.h"

static int
test_ct_size_t(void)
{
	size_t a, b, mask;
	uint8_t buf[8];
	int i, j;
	int failed = 1;

	CTASSERT(sizeof(a) <= sizeof(buf));

	for (i = 0; i < 4096; i++) {
		arc4random_buf(buf, sizeof(buf));
		memcpy(&a, buf, sizeof(a));

		if ((a != 0) != crypto_ct_ne_zero(a)) {
			fprintf(stderr, "FAIL: crypto_ct_ne_zero(0x%llx) = %d, "
			    "want %d\n", (unsigned long long)a,
			    crypto_ct_ne_zero(a), a != 0);
			goto failure;
		}
		mask = (a != 0) ? -1 : 0;
		if (mask != crypto_ct_ne_zero_mask(a)) {
			fprintf(stderr, "FAIL: crypto_ct_ne_zero_mask(0x%llx) = "
			    "0x%llx, want 0x%llx\n", (unsigned long long)a,
			    (unsigned long long)crypto_ct_ne_zero_mask(a),
			    (unsigned long long)mask);
			goto failure;
		}
		if ((a == 0) != crypto_ct_eq_zero(a)) {
			fprintf(stderr, "FAIL: crypto_ct_eq_zero(0x%llx) = %d, "
			    "want %d\n", (unsigned long long)a,
			    crypto_ct_ne_zero(a), a != 0);
			goto failure;
		}
		mask = (a == 0) ? -1 : 0;
		if (mask != crypto_ct_eq_zero_mask(a)) {
			fprintf(stderr, "FAIL: crypto_ct_eq_zero_mask(0x%llx) = "
			    "0x%llx, want 0x%llx\n", (unsigned long long)a,
			    (unsigned long long)crypto_ct_ne_zero_mask(a),
			    (unsigned long long)mask);
			goto failure;
		}

		for (j = 0; j < 4096; j++) {
			arc4random_buf(buf, sizeof(buf));
			memcpy(&b, buf, sizeof(b));

			if ((a < b) != crypto_ct_lt(a, b)) {
				fprintf(stderr, "FAIL: crypto_ct_lt(0x%llx, "
				    "0x%llx) = %d, want %d\n",
				    (unsigned long long)a,
				    (unsigned long long)b,
				    crypto_ct_lt(a, b), a < b);
				goto failure;
			}
			mask = (a < b) ? -1 : 0;
			if (mask != crypto_ct_lt_mask(a, b)) {
				fprintf(stderr, "FAIL: crypto_ct_lt_mask(0x%llx, "
				    "0x%llx) = 0x%llx, want 0x%llx\n",
				    (unsigned long long)a,
				    (unsigned long long)b,
				    (unsigned long long)crypto_ct_lt_mask(a, b),
				    (unsigned long long)mask);
				goto failure;
			}
			if ((a > b) != crypto_ct_gt(a, b)) {
				fprintf(stderr, "FAIL: crypto_ct_gt(0x%llx, "
				    "0x%llx) = %d, want %d\n",
				    (unsigned long long)a,
				    (unsigned long long)b,
				    crypto_ct_gt(a, b), a > b);
				goto failure;
			}
			mask = (a > b) ? -1 : 0;
			if (mask != crypto_ct_gt_mask(a, b)) {
				fprintf(stderr, "FAIL: crypto_ct_gt_mask(0x%llx, "
				    "0x%llx) = 0x%llx, want 0x%llx\n",
				    (unsigned long long)a,
				    (unsigned long long)b,
				    (unsigned long long)crypto_ct_gt_mask(a, b),
				    (unsigned long long)mask);
				goto failure;
			}
		}
	}

	failed = 0;

 failure:
	return failed;
}

static int
test_ct_u8(void)
{
	uint8_t a, b, mask;
	int failed = 1;

	a = 0;

	do {
		if ((a != 0) != crypto_ct_ne_zero_u8(a)) {
			fprintf(stderr, "FAIL: crypto_ct_ne_zero_u8(%d) = %d, "
			    "want %d\n", a, crypto_ct_ne_zero_u8(a), a != 0);
			goto failure;
		}
		mask = (a != 0) ? -1 : 0;
		if (mask != crypto_ct_ne_zero_mask_u8(a)) {
			fprintf(stderr, "FAIL: crypto_ct_ne_zero_mask_u8(%d) = %x, "
			    "want %x\n", a, crypto_ct_ne_zero_mask_u8(a), mask);
			goto failure;
		}
		if ((a == 0) != crypto_ct_eq_zero_u8(a)) {
			fprintf(stderr, "FAIL: crypto_ct_eq_zero_u8(%d) = %d, "
			    "want %d\n", a, crypto_ct_ne_zero_u8(a), a != 0);
			goto failure;
		}
		mask = (a == 0) ? -1 : 0;
		if (mask != crypto_ct_eq_zero_mask_u8(a)) {
			fprintf(stderr, "FAIL: crypto_ct_eq_zero_mask_u8(%d) = %x, "
			    "want %x\n", a, crypto_ct_ne_zero_mask_u8(a), mask);
			goto failure;
		}

		b = 0;

		do {
			if ((a != b) != crypto_ct_ne_u8(a, b)) {
				fprintf(stderr, "FAIL: crypto_ct_ne_u8(%d, %d) = %d, "
				    "want %d\n", a, b, crypto_ct_ne_u8(a, b), a != b);
				goto failure;
			}
			mask = (a != b) ? -1 : 0;
			if (mask != crypto_ct_ne_mask_u8(a, b)) {
				fprintf(stderr, "FAIL: crypto_ct_ne_mask_u8(%d, %d) = %x, "
				    "want %x\n", a, b, crypto_ct_ne_mask_u8(a, b), mask);
				goto failure;
			}
			if ((a == b) != crypto_ct_eq_u8(a, b)) {
				fprintf(stderr, "FAIL: crypto_ct_eq_u8(%d, %d) = %d, "
				    "want %d\n", a, b, crypto_ct_eq_u8(a, b), a != b);
				goto failure;
			}
			mask = (a == b) ? -1 : 0;
			if (mask != crypto_ct_eq_mask_u8(a, b)) {
				fprintf(stderr, "FAIL: crypto_ct_eq_mask_u8(%d, %d) = %x, "
				    "want %x\n", a, b, crypto_ct_eq_mask_u8(a, b), mask);
				goto failure;
			}
		} while (++b != 0);
	} while (++a != 0);

	failed = 0;

 failure:
	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= test_ct_size_t();
	failed |= test_ct_u8();

	return failed;
}
