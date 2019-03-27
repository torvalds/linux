/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>
 * All rights reserved.
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

#include <sys/endian.h>

#include <errno.h>
#include <string.h>
#include <strings.h>

#include <hast.h>
#include <lzf.h>
#include <nv.h>
#include <pjdlog.h>

#include "hast_compression.h"

static bool
allzeros(const void *data, size_t size)
{
	const uint64_t *p = data;
	unsigned int i;
	uint64_t v;

	PJDLOG_ASSERT((size % sizeof(*p)) == 0);

	/*
	 * This is the fastest method I found for checking if the given
	 * buffer contain all zeros.
	 * Because inside the loop we don't check at every step, we would
	 * get an answer only after walking through entire buffer.
	 * To return early if the buffer doesn't contain all zeros, we probe
	 * 8 bytes at the beginning, in the middle and at the end of the buffer
	 * first.
	 */

	size >>= 3;	/* divide by 8 */
	if ((p[0] | p[size >> 1] | p[size - 1]) != 0)
		return (false);
	v = 0;
	for (i = 0; i < size; i++)
		v |= *p++;
	return (v == 0);
}

static void *
hast_hole_compress(const unsigned char *data, size_t *sizep)
{
	uint32_t size;
	void *newbuf;

	if (!allzeros(data, *sizep))
		return (NULL);

	newbuf = malloc(sizeof(size));
	if (newbuf == NULL) {
		pjdlog_warning("Unable to compress (no memory: %zu).",
		    (size_t)*sizep);
		return (NULL);
	}
	size = htole32((uint32_t)*sizep);
	bcopy(&size, newbuf, sizeof(size));
	*sizep = sizeof(size);

	return (newbuf);
}

static void *
hast_hole_decompress(const unsigned char *data, size_t *sizep)
{
	uint32_t size;
	void *newbuf;

	if (*sizep != sizeof(size)) {
		pjdlog_error("Unable to decompress (invalid size: %zu).",
		    *sizep);
		return (NULL);
	}

	bcopy(data, &size, sizeof(size));
	size = le32toh(size);

	newbuf = malloc(size);
	if (newbuf == NULL) {
		pjdlog_error("Unable to decompress (no memory: %zu).",
		    (size_t)size);
		return (NULL);
	}
	bzero(newbuf, size);
	*sizep = size;

	return (newbuf);
}

/* Minimum block size to try to compress. */
#define	HAST_LZF_COMPRESS_MIN	1024

static void *
hast_lzf_compress(const unsigned char *data, size_t *sizep)
{
	unsigned char *newbuf;
	uint32_t origsize;
	size_t newsize;

	origsize = *sizep;

	if (origsize <= HAST_LZF_COMPRESS_MIN)
		return (NULL);

	newsize = sizeof(origsize) + origsize - HAST_LZF_COMPRESS_MIN;
	newbuf = malloc(newsize);
	if (newbuf == NULL) {
		pjdlog_warning("Unable to compress (no memory: %zu).",
		    newsize);
		return (NULL);
	}
	newsize = lzf_compress(data, *sizep, newbuf + sizeof(origsize),
	    newsize - sizeof(origsize));
	if (newsize == 0) {
		free(newbuf);
		return (NULL);
	}
	origsize = htole32(origsize);
	bcopy(&origsize, newbuf, sizeof(origsize));

	*sizep = sizeof(origsize) + newsize;
	return (newbuf);
}

static void *
hast_lzf_decompress(const unsigned char *data, size_t *sizep)
{
	unsigned char *newbuf;
	uint32_t origsize;
	size_t newsize;

	PJDLOG_ASSERT(*sizep > sizeof(origsize));

	bcopy(data, &origsize, sizeof(origsize));
	origsize = le32toh(origsize);
	PJDLOG_ASSERT(origsize > HAST_LZF_COMPRESS_MIN);

	newbuf = malloc(origsize);
	if (newbuf == NULL) {
		pjdlog_error("Unable to decompress (no memory: %zu).",
		    (size_t)origsize);
		return (NULL);
	}
	newsize = lzf_decompress(data + sizeof(origsize),
	    *sizep - sizeof(origsize), newbuf, origsize);
	if (newsize == 0) {
		free(newbuf);
		pjdlog_error("Unable to decompress.");
		return (NULL);
	}
	PJDLOG_ASSERT(newsize == origsize);

	*sizep = newsize;
	return (newbuf);
}

const char *
compression_name(int num)
{

	switch (num) {
	case HAST_COMPRESSION_NONE:
		return ("none");
	case HAST_COMPRESSION_HOLE:
		return ("hole");
	case HAST_COMPRESSION_LZF:
		return ("lzf");
	}
	return ("unknown");
}

int
compression_send(const struct hast_resource *res, struct nv *nv, void **datap,
    size_t *sizep, bool *freedatap)
{
	unsigned char *newbuf;
	int compression;
	size_t size;

	size = *sizep;
	compression = res->hr_compression;

	switch (compression) {
	case HAST_COMPRESSION_NONE:
		return (0);
	case HAST_COMPRESSION_HOLE:
		newbuf = hast_hole_compress(*datap, &size);
		break;
	case HAST_COMPRESSION_LZF:
		/* Try 'hole' compression first. */
		newbuf = hast_hole_compress(*datap, &size);
		if (newbuf != NULL)
			compression = HAST_COMPRESSION_HOLE;
		else
			newbuf = hast_lzf_compress(*datap, &size);
		break;
	default:
		PJDLOG_ABORT("Invalid compression: %d.", res->hr_compression);
	}

	if (newbuf == NULL) {
		/* Unable to compress the data. */
		return (0);
	}
	nv_add_string(nv, compression_name(compression), "compression");
	if (nv_error(nv) != 0) {
		free(newbuf);
		errno = nv_error(nv);
		return (-1);
	}
	if (*freedatap)
		free(*datap);
	*freedatap = true;
	*datap = newbuf;
	*sizep = size;

	return (0);
}

int
compression_recv(const struct hast_resource *res __unused, struct nv *nv,
    void **datap, size_t *sizep, bool *freedatap)
{
	unsigned char *newbuf;
	const char *algo;
	size_t size;

	algo = nv_get_string(nv, "compression");
	if (algo == NULL)
		return (0);	/* No compression. */

	newbuf = NULL;
	size = *sizep;

	if (strcmp(algo, "hole") == 0)
		newbuf = hast_hole_decompress(*datap, &size);
	else if (strcmp(algo, "lzf") == 0)
		newbuf = hast_lzf_decompress(*datap, &size);
	else {
		pjdlog_error("Unknown compression algorithm '%s'.", algo);
		return (-1);	/* Unknown compression algorithm. */
	}

	if (newbuf == NULL)
		return (-1);
	if (*freedatap)
		free(*datap);
	*freedatap = true;
	*datap = newbuf;
	*sizep = size;

	return (0);
}
