/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/disk.h>
#include <sys/endian.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <assert.h>
#include <libgeom.h>

#include "misc/subr.h"


struct std_metadata {
	char		md_magic[16];
	uint32_t	md_version;
};

static void
std_metadata_decode(const unsigned char *data, struct std_metadata *md)
{

        bcopy(data, md->md_magic, sizeof(md->md_magic));
        md->md_version = le32dec(data + 16);
}

/*
 * Greatest Common Divisor.
 */
static unsigned int
gcd(unsigned int a, unsigned int b)
{
	unsigned int c;

	while (b != 0) {
		c = a;
		a = b;
		b = (c % b);
	}
	return (a);
}

/*
 * Least Common Multiple.
 */
unsigned int
g_lcm(unsigned int a, unsigned int b)
{

	return ((a * b) / gcd(a, b));
}

uint32_t
bitcount32(uint32_t x)
{

	x = (x & 0x55555555) + ((x & 0xaaaaaaaa) >> 1);
	x = (x & 0x33333333) + ((x & 0xcccccccc) >> 2);
	x = (x & 0x0f0f0f0f) + ((x & 0xf0f0f0f0) >> 4);
	x = (x & 0x00ff00ff) + ((x & 0xff00ff00) >> 8);
	x = (x & 0x0000ffff) + ((x & 0xffff0000) >> 16);
	return (x);
}

/*
 * The size of a sector is context specific (i.e. determined by the
 * media). But when users enter a value with a SI unit, they really
 * mean the byte-size or byte-offset and not the size or offset in
 * sectors. We should map the byte-oriented value into a sector-oriented
 * value when we already know the sector size in bytes. At this time
 * we can use g_parse_lba() function. It converts user specified
 * value into sectors with following conditions:
 * o  Sectors size taken as argument from caller.
 * o  When no SI unit is specified the value is in sectors.
 * o  With an SI unit the value is in bytes.
 * o  The 'b' suffix forces byte interpretation and the 's'
 *    suffix forces sector interpretation.
 *
 * Thus:
 * o  2 and 2s mean 2 sectors, and 2b means 2 bytes.
 * o  4k and 4kb mean 4096 bytes, and 4ks means 4096 sectors.
 *
 */
int
g_parse_lba(const char *lbastr, unsigned int sectorsize, off_t *sectors)
{
	off_t number, mult, unit;
	char *s;

	assert(lbastr != NULL);
	assert(sectorsize > 0);
	assert(sectors != NULL);

	number = (off_t)strtoimax(lbastr, &s, 0);
	if (s == lbastr || number < 0)
		return (EINVAL);

	mult = 1;
	unit = sectorsize;
	if (*s == '\0')
		goto done;
	switch (*s) {
	case 'e': case 'E':
		mult *= 1024;
		/* FALLTHROUGH */
	case 'p': case 'P':
		mult *= 1024;
		/* FALLTHROUGH */
	case 't': case 'T':
		mult *= 1024;
		/* FALLTHROUGH */
	case 'g': case 'G':
		mult *= 1024;
		/* FALLTHROUGH */
	case 'm': case 'M':
		mult *= 1024;
		/* FALLTHROUGH */
	case 'k': case 'K':
		mult *= 1024;
		break;
	default:
		goto sfx;
	}
	unit = 1;	/* bytes */
	s++;
	if (*s == '\0')
		goto done;
sfx:
	switch (*s) {
	case 's': case 'S':
		unit = sectorsize;	/* sector */
		break;
	case 'b': case 'B':
		unit = 1;		/* bytes */
		break;
	default:
		return (EINVAL);
	}
	s++;
	if (*s != '\0')
		return (EINVAL);
done:
	if ((OFF_MAX / unit) < mult || (OFF_MAX / mult / unit) < number)
		return (ERANGE);
	number *= mult * unit;
	if (number % sectorsize)
		return (EINVAL);
	number /= sectorsize;
	*sectors = number;
	return (0);
}

off_t
g_get_mediasize(const char *name)
{
	off_t mediasize;
	int fd;

	fd = g_open(name, 0);
	if (fd == -1)
		return (0);
	mediasize = g_mediasize(fd);
	if (mediasize == -1)
		mediasize = 0;
	(void)g_close(fd);
	return (mediasize);
}

unsigned int
g_get_sectorsize(const char *name)
{
	ssize_t sectorsize;
	int fd;

	fd = g_open(name, 0);
	if (fd == -1)
		return (0);
	sectorsize = g_sectorsize(fd);
	if (sectorsize == -1)
		sectorsize = 0;
	(void)g_close(fd);
	return ((unsigned int)sectorsize);
}

int
g_metadata_read(const char *name, unsigned char *md, size_t size,
    const char *magic)
{
	struct std_metadata stdmd;
	unsigned char *sector;
	ssize_t sectorsize;
	off_t mediasize;
	int error, fd;

	sector = NULL;
	error = 0;

	fd = g_open(name, 0);
	if (fd == -1)
		return (errno);
	mediasize = g_mediasize(fd);
	if (mediasize == -1) {
		error = errno;
		goto out;
	}
	sectorsize = g_sectorsize(fd);
	if (sectorsize == -1) {
		error = errno;
		goto out;
	}
	assert(sectorsize >= (ssize_t)size);
	sector = malloc(sectorsize);
	if (sector == NULL) {
		error = ENOMEM;
		goto out;
	}
	if (pread(fd, sector, sectorsize, mediasize - sectorsize) !=
	    sectorsize) {
		error = errno;
		goto out;
	}
	if (magic != NULL) {
		std_metadata_decode(sector, &stdmd);
		if (strcmp(stdmd.md_magic, magic) != 0) {
			error = EINVAL;
			goto out;
		}
	}
	bcopy(sector, md, size);
out:
	if (sector != NULL)
		free(sector);
	g_close(fd);
	return (error);
}

/* 
 * Actually write the GEOM label to the provider
 *
 * @param name	GEOM provider's name (ie "ada0")
 * @param md	Pointer to the label data to write
 * @param size	Size of the data pointed to by md
 */
int
g_metadata_store(const char *name, const unsigned char *md, size_t size)
{
	unsigned char *sector;
	ssize_t sectorsize;
	off_t mediasize;
	int error, fd;

	sector = NULL;
	error = 0;

	fd = g_open(name, 1);
	if (fd == -1)
		return (errno);
	mediasize = g_mediasize(fd);
	if (mediasize == -1) {
		error = errno;
		goto out;
	}
	sectorsize = g_sectorsize(fd);
	if (sectorsize == -1) {
		error = errno;
		goto out;
	}
	assert(sectorsize >= (ssize_t)size);
	sector = malloc(sectorsize);
	if (sector == NULL) {
		error = ENOMEM;
		goto out;
	}
	bcopy(md, sector, size);
	bzero(sector + size, sectorsize - size);
	if (pwrite(fd, sector, sectorsize, mediasize - sectorsize) !=
	    sectorsize) {
		error = errno;
		goto out;
	}
	(void)g_flush(fd);
out:
	if (sector != NULL)
		free(sector);
	(void)g_close(fd);
	return (error);
}

int
g_metadata_clear(const char *name, const char *magic)
{
	struct std_metadata md;
	unsigned char *sector;
	ssize_t sectorsize;
	off_t mediasize;
	int error, fd;

	sector = NULL;
	error = 0;

	fd = g_open(name, 1);
	if (fd == -1)
		return (errno);
	mediasize = g_mediasize(fd);
	if (mediasize == 0) {
		error = errno;
		goto out;
	}
	sectorsize = g_sectorsize(fd);
	if (sectorsize <= 0) {
		error = errno;
		goto out;
	}
	sector = malloc(sectorsize);
	if (sector == NULL) {
		error = ENOMEM;
		goto out;
	}
	if (magic != NULL) {
		if (pread(fd, sector, sectorsize, mediasize - sectorsize) !=
		    sectorsize) {
			error = errno;
			goto out;
		}
		std_metadata_decode(sector, &md);
		if (strcmp(md.md_magic, magic) != 0) {
			error = EINVAL;
			goto out;
		}
	}
	bzero(sector, sectorsize);
	if (pwrite(fd, sector, sectorsize, mediasize - sectorsize) !=
	    sectorsize) {
		error = errno;
		goto out;
	}
	(void)g_flush(fd);
out:
	free(sector);
	g_close(fd);
	return (error);
}

/*
 * Set an error message, if one does not already exist.
 */
void
gctl_error(struct gctl_req *req, const char *error, ...)
{
	va_list ap;

	if (req != NULL && req->error != NULL)
		return;
	va_start(ap, error);
	if (req != NULL) {
		vasprintf(&req->error, error, ap);
	} else {
		vfprintf(stderr, error, ap);
		fprintf(stderr, "\n");
	}
	va_end(ap);
}

static void *
gctl_get_param(struct gctl_req *req, size_t len, const char *pfmt, va_list ap)
{
	struct gctl_req_arg *argp;
	char param[256];
	unsigned int i;
	void *p;

	vsnprintf(param, sizeof(param), pfmt, ap);
	for (i = 0; i < req->narg; i++) {
		argp = &req->arg[i];
		if (strcmp(param, argp->name))
			continue;
		if (!(argp->flag & GCTL_PARAM_RD))
			continue;
		p = argp->value;
		if (len == 0) {
			/* We are looking for a string. */
			if (argp->len < 1) {
				fprintf(stderr, "No length argument (%s).\n",
				    param);
				abort();
			}
			if (((char *)p)[argp->len - 1] != '\0') {
				fprintf(stderr, "Unterminated argument (%s).\n",
				    param);
				abort();
			}
		} else if ((int)len != argp->len) {
			fprintf(stderr, "Wrong length %s argument.\n", param);
			abort();
		}
		return (p);
	}
	fprintf(stderr, "No such argument (%s).\n", param);
	abort();
}

int
gctl_get_int(struct gctl_req *req, const char *pfmt, ...)
{
	int *p;
	va_list ap;

	va_start(ap, pfmt);
	p = gctl_get_param(req, sizeof(int), pfmt, ap);
	va_end(ap);
	return (*p);
}

intmax_t
gctl_get_intmax(struct gctl_req *req, const char *pfmt, ...)
{
	intmax_t *p;
	va_list ap;

	va_start(ap, pfmt);
	p = gctl_get_param(req, sizeof(intmax_t), pfmt, ap);
	va_end(ap);
	return (*p);
}

const char *
gctl_get_ascii(struct gctl_req *req, const char *pfmt, ...)
{
	const char *p;
	va_list ap;

	va_start(ap, pfmt);
	p = gctl_get_param(req, 0, pfmt, ap);
	va_end(ap);
	return (p);
}

int
gctl_change_param(struct gctl_req *req, const char *name, int len,
    const void *value)
{
	struct gctl_req_arg *ap;
	unsigned int i;

	if (req == NULL || req->error != NULL)
		return (EDOOFUS);
	for (i = 0; i < req->narg; i++) {
		ap = &req->arg[i];
		if (strcmp(ap->name, name) != 0)
			continue;
		ap->value = __DECONST(void *, value);
		if (len >= 0) {
			ap->flag &= ~GCTL_PARAM_ASCII;
			ap->len = len;
		} else if (len < 0) {
			ap->flag |= GCTL_PARAM_ASCII;
			ap->len = strlen(value) + 1;
		}
		return (0);
	}
	return (ENOENT);
}

int
gctl_delete_param(struct gctl_req *req, const char *name)
{
	struct gctl_req_arg *ap;
	unsigned int i;

	if (req == NULL || req->error != NULL)
		return (EDOOFUS);

	i = 0;
	while (i < req->narg) {
		ap = &req->arg[i];
		if (strcmp(ap->name, name) == 0)
			break;
		i++;
	}
	if (i == req->narg)
		return (ENOENT);

	free(ap->name);
	req->narg--;
	while (i < req->narg) {
		req->arg[i] = req->arg[i + 1];
		i++;
	}
	return (0);
}

int
gctl_has_param(struct gctl_req *req, const char *name)
{
	struct gctl_req_arg *ap;
	unsigned int i;

	if (req == NULL || req->error != NULL)
		return (0);

	for (i = 0; i < req->narg; i++) {
		ap = &req->arg[i];
		if (strcmp(ap->name, name) == 0)
			return (1);
	}
	return (0);
}
