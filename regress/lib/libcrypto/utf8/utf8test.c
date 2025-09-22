/*	$OpenBSD: utf8test.c,v 1.5 2022/11/26 16:08:56 tb Exp $	*/
/*
 * Copyright (c) 2014 Philip Guenther <guenther@openbsd.org>
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

/*
 * A mostly exhaustive test of UTF-8 decoder and encoder
 */

#include <stdio.h>
#include <string.h>
#include <err.h>

#include <openssl/asn1.h>
#include "asn1_local.h"		/* peek into the internals */

#define	UNCHANGED	0xfedcba98

#define ASSERT(x)						\
	do {							\
		if (!(x))					\
			errx(1, "test failed at line %d: %s",	\
			    __LINE__, #x);			\
	} while (0)

int
main(void)
{
	unsigned char testbuf[] = "012345";
	const unsigned char zerobuf[sizeof testbuf] = { 0 };
	unsigned long value;
	unsigned int i, j, k, l;
	int ret;

	/*
	 * First, verify UTF8_getc()
	 */
	value = UNCHANGED;
	ret = UTF8_getc(testbuf, 0, &value);
	ASSERT(ret == 0);
	ASSERT(value == UNCHANGED);

	/* check all valid single-byte chars */
	for (i = 0; i < 0x80; i++) {
		testbuf[0] = i;
		ret = UTF8_getc(testbuf, 1, &value);
		ASSERT(ret == 1);
		ASSERT(value == i);

		ret = UTF8_getc(testbuf, 2, &value);
		ASSERT(ret == 1);
		ASSERT(value == i);
	}

	/*
	 * Verify failure on all invalid initial bytes:
	 *	0x80 - 0xBF	following bytes only
	 *	0xC0 - 0xC1	used to be in non-shortest forms
	 *	0xF5 - 0xFD	used to be initial for 5 and 6 byte sequences
	 *	0xFE - 0xFF	have never been valid in utf-8
	 */
	for (i = 0x80; i < 0xC2; i++) {
		value = UNCHANGED;
		testbuf[0] = i;
		ret = UTF8_getc(testbuf, 1, &value);
		ASSERT(ret == -2);
		ASSERT(value == UNCHANGED);
	}
	for (i = 0xF5; i < 0x100; i++) {
		value = UNCHANGED;
		testbuf[0] = i;
		ret = UTF8_getc(testbuf, 1, &value);
		ASSERT(ret == -2);
		ASSERT(value == UNCHANGED);
	}

	/*
	 * Verify handling of all two-byte sequences
	 */
	for (i = 0xC2; i < 0xE0; i++) {
		testbuf[0] = i;

		for (j = 0; j < 0x100; j++) {
			testbuf[1] = j;

			value = UNCHANGED;
			ret = UTF8_getc(testbuf, 1, &value);
			ASSERT(ret == -1);
			ASSERT(value == UNCHANGED);

			ret = UTF8_getc(testbuf, 2, &value);

			/* outside range of trailing bytes */
			if (j < 0x80 || j > 0xBF) {
				ASSERT(ret == -3);
				ASSERT(value == UNCHANGED);
				continue;
			}

			/* valid */
			ASSERT(ret == 2);
			ASSERT((value & 0x3F) == (j & 0x3F));
			ASSERT(value >> 6 == (i & 0x1F));
		}
	}

	/*
	 * Verify handling of all three-byte sequences
	 */
	for (i = 0xE0; i < 0xF0; i++) {
		testbuf[0] = i;

		for (j = 0; j < 0x100; j++) {
			testbuf[1] = j;

			for (k = 0; k < 0x100; k++) {
				testbuf[2] = k;

				value = UNCHANGED;
				ret = UTF8_getc(testbuf, 2, &value);
				ASSERT(ret == -1);
				ASSERT(value == UNCHANGED);

				ret = UTF8_getc(testbuf, 3, &value);

				/* outside range of trailing bytes */
				if (j < 0x80 || j > 0xBF ||
				    k < 0x80 || k > 0xBF) {
					ASSERT(ret == -3);
					ASSERT(value == UNCHANGED);
					continue;
				}

				/* non-shortest form */
				if (i == 0xE0 && j < 0xA0) {
					ASSERT(ret == -4);
					ASSERT(value == UNCHANGED);
					continue;
				}

				/* surrogate pair code point */
				if (i == 0xED && j > 0x9F) {
					ASSERT(ret == -2);
					ASSERT(value == UNCHANGED);
					continue;
				}

				ASSERT(ret == 3);
				ASSERT((value & 0x3F) == (k & 0x3F));
				ASSERT(((value >> 6) & 0x3F) == (j & 0x3F));
				ASSERT(value >> 12 == (i & 0x0F));
			}
		}
	}

	/*
	 * Verify handling of all four-byte sequences
	 */
	for (i = 0xF0; i < 0xF5; i++) {
		testbuf[0] = i;

		for (j = 0; j < 0x100; j++) {
			testbuf[1] = j;

			for (k = 0; k < 0x100; k++) {
				testbuf[2] = k;

				for (l = 0; l < 0x100; l++) {
					testbuf[3] = l;

					value = UNCHANGED;
					ret = UTF8_getc(testbuf, 3, &value);
					ASSERT(ret == -1);
					ASSERT(value == UNCHANGED);

					ret = UTF8_getc(testbuf, 4, &value);

					/* outside range of trailing bytes */
					if (j < 0x80 || j > 0xBF ||
					    k < 0x80 || k > 0xBF ||
					    l < 0x80 || l > 0xBF) {
						ASSERT(ret == -3);
						ASSERT(value == UNCHANGED);
						continue;
					}

					/* non-shortest form */
					if (i == 0xF0 && j < 0x90) {
						ASSERT(ret == -4);
						ASSERT(value == UNCHANGED);
						continue;
					}

					/* beyond end of UCS range */
					if (i == 0xF4 && j > 0x8F) {
						ASSERT(ret == -2);
						ASSERT(value == UNCHANGED);
						continue;
					}

					ASSERT(ret == 4);
					ASSERT((value & 0x3F) == (l & 0x3F));
					ASSERT(((value >> 6) & 0x3F) ==
							  (k & 0x3F));
					ASSERT(((value >> 12) & 0x3F) ==
							   (j & 0x3F));
					ASSERT(value >> 18 == (i & 0x07));
				}
			}
		}
	}


	/*
	 * Next, verify UTF8_putc()
	 */
	memset(testbuf, 0, sizeof testbuf);

	/* single-byte sequences */
	for (i = 0; i < 0x80; i++) {
		ret = UTF8_putc(NULL, 0, i);
		ASSERT(ret == 1);

		testbuf[0] = 0;
		ret = UTF8_putc(testbuf, 0, i);
		ASSERT(ret == -1);
		ASSERT(memcmp(testbuf, zerobuf, sizeof testbuf) == 0);

		ret = UTF8_putc(testbuf, 1, i);
		ASSERT(ret == 1);
		ASSERT(testbuf[0] == i);
		ASSERT(memcmp(testbuf+1, zerobuf, sizeof(testbuf)-1) == 0);
	}

	/* two-byte sequences */
	for (i = 0x80; i < 0x800; i++) {
		ret = UTF8_putc(NULL, 0, i);
		ASSERT(ret == 2);

		testbuf[0] = testbuf[1] = 0;
		ret = UTF8_putc(testbuf, 1, i);
		ASSERT(ret == -1);
		ASSERT(memcmp(testbuf, zerobuf, sizeof testbuf) == 0);

		ret = UTF8_putc(testbuf, 2, i);
		ASSERT(ret == 2);
		ASSERT(memcmp(testbuf+2, zerobuf, sizeof(testbuf)-2) == 0);
		ret = UTF8_getc(testbuf, 2, &value);
		ASSERT(ret == 2);
		ASSERT(value == i);
	}

	/* three-byte sequences */
	for (i = 0x800; i < 0x10000; i++) {
		if (i >= 0xD800 && i < 0xE000) {
			/* surrogates aren't valid */
			ret = UTF8_putc(NULL, 0, i);
			ASSERT(ret == -2);
			continue;
		}

		ret = UTF8_putc(NULL, 0, i);
		ASSERT(ret == 3);

		testbuf[0] = testbuf[1] = testbuf[2] = 0;
		ret = UTF8_putc(testbuf, 2, i);
		ASSERT(ret == -1);
		ASSERT(memcmp(testbuf, zerobuf, sizeof testbuf) == 0);

		ret = UTF8_putc(testbuf, 3, i);
		ASSERT(ret == 3);
		ASSERT(memcmp(testbuf+3, zerobuf, sizeof(testbuf)-3) == 0);
		ret = UTF8_getc(testbuf, 3, &value);
		ASSERT(ret == 3);
		ASSERT(value == i);
	}

	/* four-byte sequences */
	for (i = 0x10000; i < 0x110000; i++) {
		ret = UTF8_putc(NULL, 0, i);
		ASSERT(ret == 4);

		testbuf[0] = testbuf[1] = testbuf[2] = testbuf[3] = 0;
		ret = UTF8_putc(testbuf, 3, i);
		ASSERT(ret == -1);
		ASSERT(memcmp(testbuf, zerobuf, sizeof testbuf) == 0);

		ret = UTF8_putc(testbuf, 4, i);
		ASSERT(ret == 4);
		ASSERT(memcmp(testbuf+4, zerobuf, sizeof(testbuf)-4) == 0);
		ret = UTF8_getc(testbuf, 4, &value);
		ASSERT(ret == 4);
		ASSERT(value == i);
	}

	/* spot check some larger values to confirm error return */
	for (i = 0x110000; i < 0x110100; i++) {
		ret = UTF8_putc(NULL, 0, i);
		ASSERT(ret == -2);
	}
	for (value = (unsigned long)-1; value > (unsigned long)-256; value--) {
		ret = UTF8_putc(NULL, 0, value);
		ASSERT(ret == -2);
	}

	return 0;
}
