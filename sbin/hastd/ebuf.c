/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <strings.h>
#include <unistd.h>

#include <pjdlog.h>

#include "ebuf.h"

#ifndef	PJDLOG_ASSERT
#include <assert.h>
#define	PJDLOG_ASSERT(...)	assert(__VA_ARGS__)
#endif

#define	EBUF_MAGIC	0xeb0f41c
struct ebuf {
	/* Magic to assert the caller uses valid structure. */
	int		 eb_magic;
	/* Address where we did the allocation. */
	unsigned char	*eb_start;
	/* Allocation end address. */
	unsigned char	*eb_end;
	/* Start of real data. */
	unsigned char	*eb_used;
	/* Size of real data. */
	size_t		 eb_size;
};

static int ebuf_head_extend(struct ebuf *eb, size_t size);
static int ebuf_tail_extend(struct ebuf *eb, size_t size);

struct ebuf *
ebuf_alloc(size_t size)
{
	struct ebuf *eb;
	int rerrno;

	eb = malloc(sizeof(*eb));
	if (eb == NULL)
		return (NULL);
	size += PAGE_SIZE;
	eb->eb_start = malloc(size);
	if (eb->eb_start == NULL) {
		rerrno = errno;
		free(eb);
		errno = rerrno;
		return (NULL);
	}
	eb->eb_end = eb->eb_start + size;
	/*
	 * We set start address for real data not at the first entry, because
	 * we want to be able to add data at the front.
	 */
	eb->eb_used = eb->eb_start + PAGE_SIZE / 4;
	eb->eb_size = 0;
	eb->eb_magic = EBUF_MAGIC;

	return (eb);
}

void
ebuf_free(struct ebuf *eb)
{

	PJDLOG_ASSERT(eb != NULL && eb->eb_magic == EBUF_MAGIC);

	eb->eb_magic = 0;

	free(eb->eb_start);
	free(eb);
}

int
ebuf_add_head(struct ebuf *eb, const void *data, size_t size)
{

	PJDLOG_ASSERT(eb != NULL && eb->eb_magic == EBUF_MAGIC);

	if (size > (size_t)(eb->eb_used - eb->eb_start)) {
		/*
		 * We can't add more entries at the front, so we have to extend
		 * our buffer.
		 */
		if (ebuf_head_extend(eb, size) == -1)
			return (-1);
	}
	PJDLOG_ASSERT(size <= (size_t)(eb->eb_used - eb->eb_start));

	eb->eb_size += size;
	eb->eb_used -= size;
	/*
	 * If data is NULL the caller just wants to reserve place.
	 */
	if (data != NULL)
		bcopy(data, eb->eb_used, size);

	return (0);
}

int
ebuf_add_tail(struct ebuf *eb, const void *data, size_t size)
{

	PJDLOG_ASSERT(eb != NULL && eb->eb_magic == EBUF_MAGIC);

	if (size > (size_t)(eb->eb_end - (eb->eb_used + eb->eb_size))) {
		/*
		 * We can't add more entries at the back, so we have to extend
		 * our buffer.
		 */
		if (ebuf_tail_extend(eb, size) == -1)
			return (-1);
	}
	PJDLOG_ASSERT(size <=
	    (size_t)(eb->eb_end - (eb->eb_used + eb->eb_size)));

	/*
	 * If data is NULL the caller just wants to reserve space.
	 */
	if (data != NULL)
		bcopy(data, eb->eb_used + eb->eb_size, size);
	eb->eb_size += size;

	return (0);
}

void
ebuf_del_head(struct ebuf *eb, size_t size)
{

	PJDLOG_ASSERT(eb != NULL && eb->eb_magic == EBUF_MAGIC);
	PJDLOG_ASSERT(size <= eb->eb_size);

	eb->eb_used += size;
	eb->eb_size -= size;
}

void
ebuf_del_tail(struct ebuf *eb, size_t size)
{

	PJDLOG_ASSERT(eb != NULL && eb->eb_magic == EBUF_MAGIC);
	PJDLOG_ASSERT(size <= eb->eb_size);

	eb->eb_size -= size;
}

/*
 * Return pointer to the data and data size.
 */
void *
ebuf_data(struct ebuf *eb, size_t *sizep)
{

	PJDLOG_ASSERT(eb != NULL && eb->eb_magic == EBUF_MAGIC);

	if (sizep != NULL)
		*sizep = eb->eb_size;
	return (eb->eb_size > 0 ? eb->eb_used : NULL);
}

/*
 * Return data size.
 */
size_t
ebuf_size(struct ebuf *eb)
{

	PJDLOG_ASSERT(eb != NULL && eb->eb_magic == EBUF_MAGIC);

	return (eb->eb_size);
}

/*
 * Function adds size + (PAGE_SIZE / 4) bytes at the front of the buffer..
 */
static int
ebuf_head_extend(struct ebuf *eb, size_t size)
{
	unsigned char *newstart, *newused;
	size_t newsize;

	PJDLOG_ASSERT(eb != NULL && eb->eb_magic == EBUF_MAGIC);

	newsize = eb->eb_end - eb->eb_start + (PAGE_SIZE / 4) + size;

	newstart = malloc(newsize);
	if (newstart == NULL)
		return (-1);
	newused =
	    newstart + (PAGE_SIZE / 4) + size + (eb->eb_used - eb->eb_start);

	bcopy(eb->eb_used, newused, eb->eb_size);

	eb->eb_start = newstart;
	eb->eb_used = newused;
	eb->eb_end = newstart + newsize;

	return (0);
}

/*
 * Function adds size + ((3 * PAGE_SIZE) / 4) bytes at the back.
 */
static int
ebuf_tail_extend(struct ebuf *eb, size_t size)
{
	unsigned char *newstart;
	size_t newsize;

	PJDLOG_ASSERT(eb != NULL && eb->eb_magic == EBUF_MAGIC);

	newsize = eb->eb_end - eb->eb_start + size + ((3 * PAGE_SIZE) / 4);

	newstart = realloc(eb->eb_start, newsize);
	if (newstart == NULL)
		return (-1);

	eb->eb_used = newstart + (eb->eb_used - eb->eb_start);
	eb->eb_start = newstart;
	eb->eb_end = newstart + newsize;

	return (0);
}
