/*	$OpenBSD: mmap_noreplace.c,v 1.1 2014/06/19 19:34:22 matthew Exp $	*/
/*
 * Copyright (c) 2014 Google Inc.
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

#include <sys/mman.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#define CHECK(a) assert(a)
#define CHECK_EQ(a, b) assert((a) == (b))
#define CHECK_NE(a, b) assert((a) != (b))
#define CHECK_GE(a, b) assert((a) >= (b))

int
ismemset(void *b, int c, size_t n)
{
	unsigned char *p = b;
	size_t i;
	for (i = 0; i < n; i++)
		if (p[i] != c)
			return (0);
	return (1);
}

int
main()
{
	char *p;
	long pagesize;

	pagesize = sysconf(_SC_PAGESIZE);
	CHECK_GE(pagesize, 1);

	/* Allocate three pages of anonymous memory. */
	p = mmap(NULL, 3 * pagesize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
	CHECK_NE(MAP_FAILED, p);

	/* Initialize memory to all 1 bytes. */
	memset(p, 1, 3 * pagesize);
	CHECK(ismemset(p, 1, 3 * pagesize));

	/* Map new anonymous memory over the second page. */
	CHECK_EQ(p + pagesize, mmap(p + pagesize, pagesize,
	    PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0));

	/* Verify the second page is zero'd out, and the others are unscathed. */
	CHECK(ismemset(p, 1, pagesize));
	CHECK(ismemset(p + pagesize, 0, pagesize));
	CHECK(ismemset(p + 2 * pagesize, 1, pagesize));

	/* Re-initialized memory. */
	memset(p, 1, 3 * pagesize);
	CHECK(ismemset(p, 1, 3 * pagesize));

	/* Try to map over second page with __MAP_NOREPLACE; should fail. */
	CHECK_EQ(MAP_FAILED, mmap(p, pagesize, PROT_READ|PROT_WRITE,
	    MAP_PRIVATE|MAP_ANON|MAP_FIXED|__MAP_NOREPLACE, -1, 0));

	/* Verify the pages are still set. */
	CHECK(ismemset(p, 1, 3 * pagesize));

	return (0);
}
