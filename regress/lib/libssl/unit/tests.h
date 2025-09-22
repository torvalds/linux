/*	$OpenBSD: tests.h,v 1.1 2015/06/27 23:35:52 doug Exp $	*/
/*
 * Copyright (c) 2015 Doug Hogan <doug@openbsd.org>
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

#ifndef LIBRESSL_REGRESS_TESTS_H__
#define LIBRESSL_REGRESS_TESTS_H__ 1

/* Ugly macros that are useful for regression tests. */

#define SKIP(a) do {							\
	printf("Skipping test in %s [%s:%d]\n", __func__, __FILE__,	\
	    __LINE__);							\
} while (0)

#define CHECK(a) do {							\
	if (!(a)) {							\
		printf("Error in %s [%s:%d]\n", __func__, __FILE__,	\
		    __LINE__);						\
		return 0;						\
	}								\
} while (0)

#define CHECK_GOTO(a) do {						\
	if (!(a)) {							\
		printf("Error in %s [%s:%d]\n", __func__, __FILE__,	\
		    __LINE__);						\
		goto err;						\
	}								\
} while (0)

#endif /* LIBRESSL_REGRESS_TESTS_H__ */
